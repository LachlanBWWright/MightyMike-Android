// OPENGL RENDERING DRIVER
// (C) 2022 Iliyas Jorio
// This file is part of Mighty Mike. https://github.com/jorio/mightymike
//
// On PowerPC Macs, SDL 2.0.3's 2D renderer doesn't produce very fast results,
// especially if Altivec isn't available (G3 CPUs).
// So, on PPC, we bypass their renderer and use our own.
//
// Newer platforms may also benefit from this renderer
// as it is typically more performant than SDL's default renderer
// (e.g. up to 3x the framerate as SDL's metal renderer on M1 MBA, SDL 2.26)
//
// Additionally, people have reported issues with SDL's renderer with decent
// hardware on Windows (https://github.com/jorio/MightyMike/issues/13).
// The custom GL renderer has solved their issue.

#if GLRENDER

#include <SDL3/SDL.h>
#include "myglobals.h"
#include "externs.h"
#include "misc.h"
#include "renderdrivers.h"
#include "framebufferfilter.h"

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#include <android/log.h>
// GLES3 uses core buffer functions (no ARB suffix)
#define glGenBuffersARB         glGenBuffers
#define glDeleteBuffersARB      glDeleteBuffers
#define glBindBufferARB         glBindBuffer
#define glBufferDataARB         glBufferData
#define glUnmapBufferARB        glUnmapBuffer
#define GL_PIXEL_UNPACK_BUFFER_ARB  GL_PIXEL_UNPACK_BUFFER
#else
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
PFNGLGENBUFFERSARBPROC glGenBuffersARB;
PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB;
PFNGLBINDBUFFERARBPROC glBindBufferARB;
PFNGLMAPBUFFERARBPROC glMapBufferARB;
PFNGLBUFFERDATAARBPROC glBufferDataARB;
PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB;
#endif

// Marginal FPS increase at the cost of 1 frame of latency
#define DEFERRED_TEX_UPDATE 0

// RGB 5-6-5 appears to be the fastest format for streaming textures
// on graphics cards that ship with ancient PPC hardware
#define kFramePixelType GL_UNSIGNED_SHORT_5_6_5

#define kFrameTextureWidth 1024
#define kFrameTextureHeight 512

#if (kFramePixelType == GL_UNSIGNED_SHORT_5_6_5)
	#define kFrameInternalFormat	GL_RGB
	#define kFramePixelFormat		GL_RGB
	#define kFramePixelType			GL_UNSIGNED_SHORT_5_6_5
	#define kFrameBytesPerPixel		2
#elif (kFramePixelType == GL_UNSIGNED_SHORT_5_5_5_1)
	#define kFrameInternalFormat	GL_RGBA
	#define kFramePixelFormat		GL_RGBA
	#define kFramePixelType			GL_UNSIGNED_SHORT_5_5_5_1
	#define kFrameBytesPerPixel		2
#else
	#define kFrameInternalFormat	GL_RGBA
	#define kFramePixelFormat		GL_RGBA
	#define kFramePixelType			GL_UNSIGNED_BYTE
	#define kFrameBytesPerPixel		4
#endif

static SDL_GLContext gGLContext = NULL;
static GLuint gFrameTexture = 0;
static GLuint gFramePBO = 0;
static GLint gMaxTextureSize = 0;

#ifdef __ANDROID__
// On Android we use a plain CPU buffer instead of a PBO to avoid
// driver-specific glMapBufferRange issues on GLES 3.0.
static color_t* gAndroidFrameBuffer = NULL;
static int      gAndroidFrameBufferSize = 0;
#endif

// -------------------------------------------------------------------------
// GL error checking
// -------------------------------------------------------------------------

#ifdef __ANDROID__
// On Android, always check GL errors (even in release) since silent crashes
// are the primary debugging mechanism.  Log to both SDL and Android logcat.
static void DoGLError(GLenum error, const char* func, int line)
{
	const char* errStr = "unknown";
	switch (error)
	{
		case GL_INVALID_ENUM:                  errStr = "GL_INVALID_ENUM"; break;
		case GL_INVALID_VALUE:                 errStr = "GL_INVALID_VALUE"; break;
		case GL_INVALID_OPERATION:             errStr = "GL_INVALID_OPERATION"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
		case GL_OUT_OF_MEMORY:                 errStr = "GL_OUT_OF_MEMORY"; break;
	}
	__android_log_print(ANDROID_LOG_ERROR, "MightyMike",
		"GL error %s (0x%x) in %s:%d", errStr, error, func, line);
	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
		"GL error %s (0x%x) in %s:%d", errStr, error, func, line);
}
#define CHECK_GL_ERROR()												\
	do {																\
		GLenum err = glGetError();										\
		if (err != GL_NO_ERROR)											\
			DoGLError(err, __func__, __LINE__);							\
	} while(0)
#elif _DEBUG
#define CHECK_GL_ERROR()												\
	do {					 											\
		GLenum err = glGetError();										\
		if (err != GL_NO_ERROR)											\
			DoFatalGLError(err, __func__, __LINE__);					\
	} while(0)

static void DoFatalGLError(GLenum error, const char* func, int line)
{
	static char alertbuf[1024];
	SDL_snprintf(alertbuf, sizeof(alertbuf), "OpenGL error 0x%x\nin %s:%d", error, func, line);
	DoFatalAlert(alertbuf);
}
#else
#define CHECK_GL_ERROR() do {} while(0)
#endif

#ifdef __ANDROID__
// GLES 3.0 shader-based quad rendering
static GLuint gQuadProgram = 0;
static GLuint gQuadVAO = 0;
static GLuint gQuadVBO = 0;
static GLint  gQuadTexLoc = -1;

static const char *kQuadVS =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 a_position;\n"
    "in vec2 a_texcoord;\n"
    "out vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    v_texcoord = a_texcoord;\n"
    "}\n";

static const char *kQuadFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "in vec2 v_texcoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(u_texture, v_texcoord);\n"
    "}\n";

static GLuint CompileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static void InitQuadShader(void)
{
    SDL_Log("InitQuadShader: compiling shaders");
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kQuadVS);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kQuadFS);
    if (!vs || !fs)
    {
        DoFatalAlert("GLES 3.0 shader compilation failed! Check logcat for details.");
        return;
    }

    gQuadProgram = glCreateProgram();
    glAttachShader(gQuadProgram, vs);
    glAttachShader(gQuadProgram, fs);
    glBindAttribLocation(gQuadProgram, 0, "a_position");
    glBindAttribLocation(gQuadProgram, 1, "a_texcoord");
    glLinkProgram(gQuadProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(gQuadProgram, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(gQuadProgram, sizeof(log), NULL, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader link error: %s", log);
        __android_log_print(ANDROID_LOG_ERROR, "MightyMike", "Shader link error: %s", log);
        glDeleteProgram(gQuadProgram);
        gQuadProgram = 0;
        DoFatalAlert("GLES 3.0 shader link failed! Check logcat for details.");
        return;
    }

    SDL_Log("InitQuadShader: program linked (id=%u)", gQuadProgram);
    gQuadTexLoc = glGetUniformLocation(gQuadProgram, "u_texture");

    // VAO + VBO for the fullscreen quad (positions + texcoords interleaved)
    glGenVertexArrays(1, &gQuadVAO);
    glGenBuffers(1, &gQuadVBO);
    glBindVertexArray(gQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);

    // Positions in NDC; texcoords match the texture content area.
    // Will be updated each frame when we know umax/vmax.
    // For now allocate the buffer; content is set in GLRender_PresentFramebuffer.
    float placeholder[24] = {0};
    glBufferData(GL_ARRAY_BUFFER, sizeof(placeholder), placeholder, GL_DYNAMIC_DRAW);

    // a_position: xy at offset 0, stride 16
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // a_texcoord: uv at offset 8, stride 16
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();
    SDL_Log("InitQuadShader: VAO/VBO set up");
}

static void DrawQuadGLES(float umax, float vmax)
{
    // Upload updated quad vertices (NDC positions + texture coordinates)
    // Quad covers NDC [-1,1] x [-1,1]; texture Y is flipped (0 at top).
    float verts[24] = {
        // x      y     u      v
        -1.0f, -1.0f,  0.0f, vmax,   // bottom-left
         1.0f, -1.0f, umax, vmax,   // bottom-right
        -1.0f,  1.0f,  0.0f,  0.0f,  // top-left
         1.0f,  1.0f, umax,  0.0f,   // top-right
    };
    glBindVertexArray(gQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    CHECK_GL_ERROR();

    glUseProgram(gQuadProgram);
    glUniform1i(gQuadTexLoc, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK_GL_ERROR();

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// -------------------------------------------------------------------------
// Touch controls overlay renderer (GLES 3.0)
// Draws semi-transparent button zones on top of the game framebuffer.
// -------------------------------------------------------------------------

#include "../Android/TouchControls.h"
#include "../Headers/structures.h"

static GLuint gOverlayProgram = 0;
static GLuint gOverlayVAO = 0;
static GLuint gOverlayVBO = 0;
static GLint  gOverlayColorLoc = -1;

static const char *kOverlayVS =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *kOverlayFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = u_color;\n"
    "}\n";

static void InitOverlayShader(void)
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kOverlayVS);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kOverlayFS);
    if (!vs || !fs)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Overlay shader compile failed");
        return;
    }

    gOverlayProgram = glCreateProgram();
    glAttachShader(gOverlayProgram, vs);
    glAttachShader(gOverlayProgram, fs);
    glBindAttribLocation(gOverlayProgram, 0, "a_pos");
    glLinkProgram(gOverlayProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(gOverlayProgram, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[256];
        glGetProgramInfoLog(gOverlayProgram, sizeof(log), NULL, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Overlay shader link error: %s", log);
        glDeleteProgram(gOverlayProgram);
        gOverlayProgram = 0;
        return;
    }

    gOverlayColorLoc = glGetUniformLocation(gOverlayProgram, "u_color");

    glGenVertexArrays(1, &gOverlayVAO);
    glGenBuffers(1, &gOverlayVBO);
    glBindVertexArray(gOverlayVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gOverlayVBO);
    float placeholder[8] = {0};
    glBufferData(GL_ARRAY_BUFFER, sizeof(placeholder), placeholder, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Draw a filled rectangle.
// nx0,ny0 = normalised top-left (0=left/top, 1=right/bottom).
// nx1,ny1 = normalised bottom-right.
static void DrawOverlayRect(float nx0, float ny0, float nx1, float ny1,
                             float r, float g, float b, float a)
{
    if (!gOverlayProgram) return;
    // Convert to NDC: x = 2*nx-1, y = 1-2*ny (GL Y-up)
    float x0 = 2.0f*nx0 - 1.0f, y0 = 1.0f - 2.0f*ny1;  // bottom-left NDC
    float x1 = 2.0f*nx1 - 1.0f, y1 = 1.0f - 2.0f*ny0;  // top-right NDC
    float verts[8] = {
        x0, y0,   // bottom-left
        x1, y0,   // bottom-right
        x0, y1,   // top-left
        x1, y1,   // top-right
    };
    glBindBuffer(GL_ARRAY_BUFFER, gOverlayVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUniform4f(gOverlayColorLoc, r, g, b, a);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// Draws all virtual button zones.
// Call after the game framebuffer quad and before SDL_GL_SwapWindow.
static void DrawTouchOverlay(void)
{
    if (!gOverlayProgram || !gOverlayVAO) return;

    glUseProgram(gOverlayProgram);
    glBindVertexArray(gOverlayVAO);

    // Enable alpha blending for the overlay
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Alpha values: idle = 0.18, active (touched) = 0.45
    #define IDLE_A   0.18f
    #define ACTIVE_A 0.45f
    #define BTN_ALPHA(need) (TouchControls_GetNeedActive(need) ? ACTIVE_A : IDLE_A)

    // ----------------------------------------------------------------
    // D-pad buttons (left 38% of screen)
    // Zones match TouchControls.c: dpad_x = nx/0.38
    //   Up:    dpad_x 0.35..0.65, ny < 0.35
    //   Down:  dpad_x 0.35..0.65, ny > 0.65
    //   Left:  dpad_x < 0.35
    //   Right: dpad_x > 0.65
    // ----------------------------------------------------------------
    // Up arrow (center third of D-pad, top of screen)
    DrawOverlayRect(0.09f, 0.04f, 0.29f, 0.35f,  0.3f, 0.5f, 1.0f, BTN_ALPHA(kNeed_Up));
    // Down arrow
    DrawOverlayRect(0.09f, 0.65f, 0.29f, 0.96f,  0.3f, 0.5f, 1.0f, BTN_ALPHA(kNeed_Down));
    // Left arrow
    DrawOverlayRect(0.01f, 0.25f, 0.13f, 0.75f,  0.3f, 0.5f, 1.0f, BTN_ALPHA(kNeed_Left));
    // Right arrow
    DrawOverlayRect(0.25f, 0.25f, 0.37f, 0.75f,  0.3f, 0.5f, 1.0f, BTN_ALPHA(kNeed_Right));

    // ----------------------------------------------------------------
    // Action buttons (right side, nx > 0.42)
    // ax = (nx - 0.42) / 0.58
    //   ny < 0.5: Pause (ax < 0.4) or NextWeapon (ax >= 0.4)
    //   ny >= 0.5: PrevWeapon (ax < 0.4) or Attack (ax >= 0.4)
    // ax=0.4 → nx = 0.42 + 0.4*0.58 = 0.652
    // ----------------------------------------------------------------
    // Pause (upper-left action)
    DrawOverlayRect(0.44f, 0.04f, 0.63f, 0.46f,  1.0f, 0.9f, 0.2f, BTN_ALPHA(kNeed_UIPause));
    // Next Weapon (upper-right action)
    DrawOverlayRect(0.67f, 0.04f, 0.98f, 0.46f,  0.2f, 0.9f, 0.3f, BTN_ALPHA(kNeed_NextWeapon));
    // Prev Weapon (lower-left action)
    DrawOverlayRect(0.44f, 0.54f, 0.63f, 0.96f,  0.2f, 0.9f, 0.3f, BTN_ALPHA(kNeed_PrevWeapon));
    // Attack (lower-right action)
    DrawOverlayRect(0.67f, 0.54f, 0.98f, 0.96f,  1.0f, 0.2f, 0.2f, BTN_ALPHA(kNeed_Attack));

    glDisable(GL_BLEND);

    glBindVertexArray(0);
    glUseProgram(0);

    CHECK_GL_ERROR();
}

#endif // __ANDROID__

const char* gRendererName = "NULL";
Boolean gCanDoHQStretch = true;

SDL_Point FitRectKeepAR(
		int logicalWidth,
		int logicalHeight,
		int displayWidth,
		int displayHeight)
{
	float displayAR = (float)displayWidth / (float)displayHeight;
	float logicalAR = (float)logicalWidth / (float)logicalHeight;

	if (displayAR >= logicalAR)
	{
		return (SDL_Point) { displayHeight * logicalAR, displayHeight };
	}
	else
	{
		return (SDL_Point) { displayWidth, displayWidth / logicalAR };
	}
}

static void GLRender_InitMatrices(void)
{
#ifndef __ANDROID__
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, VISIBLE_WIDTH, VISIBLE_HEIGHT, 0, 0, 1000);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
#endif
}

#define GL_GET_PROC_ADDRESS(t, proc) \
do { \
    (proc) = (t) SDL_GL_GetProcAddress(#proc); \
    GAME_ASSERT_MESSAGE((proc), "Missing OpenGL procedure " #proc); \
} while(0)

static void InitTextureAndPBO(int pixelZoom)
{
	glGenTextures(1, &gFrameTexture);
	CHECK_GL_ERROR();

#ifndef __ANDROID__
	glGenBuffersARB(1, &gFramePBO);
	CHECK_GL_ERROR();
#endif

#if 0
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gFramePBO);
	// perhaps we don't need to do this everytime?
	glBufferDataARB(
		GL_PIXEL_UNPACK_BUFFER_ARB,
		kFrameTextureWidth * kFrameTextureHeight * kFrameBytesPerPixel * (pixelZoom*pixelZoom),
		NULL,
		GL_STREAM_DRAW);
	CHECK_GL_ERROR();
#endif

	glBindTexture(GL_TEXTURE_2D, gFrameTexture);
	CHECK_GL_ERROR();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(
			GL_TEXTURE_2D,
			0,
			kFrameInternalFormat,
			kFrameTextureWidth * pixelZoom,
			kFrameTextureHeight * pixelZoom,
			0,
			kFramePixelFormat,
			kFramePixelType,
			NULL // need initial call with NULL so glTexSubImage2D works later on
	);
	CHECK_GL_ERROR();

#ifdef __ANDROID__
	// Android uses a plain CPU buffer (no PBO) for texture upload
	int bufferSize = kFrameTextureWidth * pixelZoom * kFrameTextureHeight * pixelZoom * kFrameBytesPerPixel;
	if (bufferSize != gAndroidFrameBufferSize)
	{
		SDL_free(gAndroidFrameBuffer);
		gAndroidFrameBuffer = (color_t*) SDL_malloc(bufferSize);
		gAndroidFrameBufferSize = bufferSize;
		SDL_Log("GLRender: allocated %d-byte frame buffer (zoom %d)", bufferSize, pixelZoom);
	}
#endif
}

static void DeleteTextureAndPBO(void)
{
	if (gFrameTexture != 0)
	{
		glDeleteTextures(1, &gFrameTexture);
		gFrameTexture = 0;
	}

#ifndef __ANDROID__
	if (gFramePBO != 0)
	{
		glDeleteBuffersARB(1, &gFramePBO);
		gFramePBO = 0;
	}
#endif

#ifdef __ANDROID__
	SDL_free(gAndroidFrameBuffer);
	gAndroidFrameBuffer = NULL;
	gAndroidFrameBufferSize = 0;
#endif
}

void GLRender_Init(void)
{
	SDL_Log("GLRender_Init: starting");

#if FRAMEBUFFER_COLOR_DEPTH == 32
	gRendererName = "fastgl32";
#elif FRAMEBUFFER_COLOR_DEPTH == 16
	gRendererName = "fastgl16";
#else
	gRendererName = "gl??";
#endif

	gGLContext = SDL_GL_CreateContext(gSDLWindow);
	if (!gGLContext)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GL CreateContext FAILED: %s", SDL_GetError());
		GAME_ASSERT(gGLContext);
	}
	SDL_Log("GLRender_Init: GL context created");

	bool didMakeCurrent = SDL_GL_MakeCurrent(gSDLWindow, gGLContext);
	GAME_ASSERT_MESSAGE(didMakeCurrent, SDL_GetError());
	SDL_Log("GLRender_Init: GL context made current");

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gMaxTextureSize);
	SDL_Log("GLRender_Init: Max texture size: %d", (int) gMaxTextureSize);

#ifdef __ANDROID__
	SDL_Log("GLRender_Init: GL_VENDOR   = %s", glGetString(GL_VENDOR));
	SDL_Log("GLRender_Init: GL_RENDERER = %s", glGetString(GL_RENDERER));
	SDL_Log("GLRender_Init: GL_VERSION  = %s", glGetString(GL_VERSION));
#endif

	if (gMaxTextureSize < kFrameTextureWidth)
	{
		char message[128];
		SDL_snprintf(message, sizeof(message), "Your graphics card's max texture size (%d)\nis below the game's requirements (%d).", (int) gMaxTextureSize, kFrameTextureWidth);
		DoAlert(message);
	}

#if OSXPPC
	gCanDoHQStretch = false;
#else
	gCanDoHQStretch = gMaxTextureSize >= 2*kFrameTextureWidth;
#endif

#ifndef __ANDROID__
	GL_GET_PROC_ADDRESS(PFNGLGENBUFFERSARBPROC, glGenBuffersARB);
	GL_GET_PROC_ADDRESS(PFNGLDELETEBUFFERSARBPROC, glDeleteBuffersARB);
	GL_GET_PROC_ADDRESS(PFNGLBINDBUFFERARBPROC, glBindBufferARB);
	GL_GET_PROC_ADDRESS(PFNGLUNMAPBUFFERPROC, glUnmapBufferARB);
	GL_GET_PROC_ADDRESS(PFNGLMAPBUFFERARBPROC, glMapBufferARB);
	GL_GET_PROC_ADDRESS(PFNGLBUFFERDATAARBPROC, glBufferDataARB);
#endif

#if !(NOVSYNC)
	SDL_GL_SetSwapInterval(1);
#else
	SDL_GL_SetSwapInterval(0);
#endif

	GLRender_InitMatrices();

#ifndef __ANDROID__
	glDisable(GL_FOG);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_LIGHTING);
#endif
	glEnable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDepthMask(false);

#ifndef __ANDROID__
	glColor4f(1,1,1,1);
#endif

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	CHECK_GL_ERROR();

#ifdef __ANDROID__
	InitQuadShader();
	SDL_Log("GLRender_Init: quad shader ready");
	InitOverlayShader();
	SDL_Log("GLRender_Init: overlay shader ready");
#endif

	InitTextureAndPBO(1);
	SDL_Log("GLRender_Init: texture and buffer ready");
}

void GLRender_Shutdown(void)
{
	ShutdownRenderThreads();

	DeleteTextureAndPBO();

#ifdef __ANDROID__
	if (gQuadVBO) { glDeleteBuffers(1, &gQuadVBO); gQuadVBO = 0; }
	if (gQuadVAO) { glDeleteVertexArrays(1, &gQuadVAO); gQuadVAO = 0; }
	if (gQuadProgram) { glDeleteProgram(gQuadProgram); gQuadProgram = 0; }
	if (gOverlayVBO) { glDeleteBuffers(1, &gOverlayVBO); gOverlayVBO = 0; }
	if (gOverlayVAO) { glDeleteVertexArrays(1, &gOverlayVAO); gOverlayVAO = 0; }
	if (gOverlayProgram) { glDeleteProgram(gOverlayProgram); gOverlayProgram = 0; }
#endif

	if (gGLContext)
	{
        SDL_GL_DestroyContext(gGLContext);
		gGLContext = NULL;
	}
}

static SDL_Rect GetViewportSize(void)
{
	const int vw = VISIBLE_WIDTH;
	const int vh = VISIBLE_HEIGHT;

	int dw = 0;
	int dh = 0;
//	SDL_GL_GetDrawableSize(gSDLWindow, &dw, &dh);	// DON'T use SDL_GetWindowSize as it returns fake scaled pixels in HiDPI displays
	SDL_GetWindowSizeInPixels(gSDLWindow, &dw, &dh);

	SDL_Point size;
	if (gEffectiveScalingType == kScaling_PixelPerfect)
	{
		int zoom = GetMaxIntegerZoom(dw, dh);
		size = (SDL_Point) { zoom*vw, zoom*vh };
	}
	else
	{
		size = FitRectKeepAR(vw, vh, dw, dh);
	}

	return (SDL_Rect)
	{
		.x = (dw-size.x) / 2,
		.y = (dh-size.y) / 2,
		.w = size.x,
		.h = size.y
	};
}

void GLRender_PresentFramebuffer(void)
{
	static SDL_Rect previousViewportRect = {0};
	static int previousEffectiveScalingType = kScaling_Unspecified;
	static int needClear = 60;

	const int vw = VISIBLE_WIDTH;
	const int vh = VISIBLE_HEIGHT;

	bool didMakeCurrent = SDL_GL_MakeCurrent(gSDLWindow, gGLContext);
#ifdef __ANDROID__
	if (!didMakeCurrent)
	{
		// On Android the EGL surface may be momentarily unavailable
		// (e.g. when the activity is partially obscured). Skip this frame.
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"SDL_GL_MakeCurrent failed: %s -- skipping frame", SDL_GetError());
		return;
	}
#else
	GAME_ASSERT_MESSAGE(didMakeCurrent, SDL_GetError());
#endif

	//-------------------------------------------------------------------------
	// Update dimensions

	SDL_Rect viewportRect = GetViewportSize();
    if (!SDL_RectsEqual(&viewportRect, &previousViewportRect))
	{
		previousViewportRect = viewportRect;
		glViewport(viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
		needClear = 60;
	}

	bool isHQ = gEffectiveScalingType == kScaling_HQStretch;
	bool wasHQ = previousEffectiveScalingType == kScaling_HQStretch;
	if (wasHQ ^ isHQ)
	{
		DeleteTextureAndPBO();
		InitTextureAndPBO(isHQ? 2: 1);
	}
	previousEffectiveScalingType = gEffectiveScalingType;

	int zvw = (isHQ ? 2 : 1) * vw;
	int zvh = (isHQ ? 2 : 1) * vh;

	//-------------------------------------------------------------------------
	// Update frame data

#ifdef __ANDROID__
	// Android: use a plain CPU buffer instead of a PBO.
	// PBO + glMapBufferRange has driver-specific issues on some GLES 3.0 devices.
	{
		int numBytes = zvw * zvh * kFrameBytesPerPixel;
		if (!gAndroidFrameBuffer || numBytes > gAndroidFrameBufferSize)
		{
			// Buffer needs (re)allocation
			SDL_free(gAndroidFrameBuffer);
			gAndroidFrameBuffer = (color_t*) SDL_malloc(numBytes);
			gAndroidFrameBufferSize = numBytes;
		}
		GAME_ASSERT(gAndroidFrameBuffer);
		ConvertFramebufferMT(gAndroidFrameBuffer);
	}
#else
	// Desktop: use PBO for streaming
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gFramePBO);
	CHECK_GL_ERROR();

	// Orphan old PBO and allocate new one
	int numBytes = zvw * zvh * kFrameBytesPerPixel;
	glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, numBytes, NULL, GL_STREAM_DRAW);
	CHECK_GL_ERROR();

	void* mappedBuffer = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY);
	CHECK_GL_ERROR();
	GAME_ASSERT(mappedBuffer);

	// now write data into the buffer, possibly in another thread
	ConvertFramebufferMT(mappedBuffer);

	glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
	CHECK_GL_ERROR();
#endif

	//-------------------------------------------------------------------------
	// Draw the quad

	// On a Mini G4, NOT clearing the screen increases the framerate by 8%
	// so don't do it unless the viewport rectangle has recently changed.
	if (needClear > 0)
	{
		needClear--;
		glClearColor(0,0,0,1);
#if _DEBUG
		if (needClear > 4)
			glClearColor(0,0,1,1);
#endif
		glClear(GL_COLOR_BUFFER_BIT);
	}

	glBindTexture(GL_TEXTURE_2D, gFrameTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
					gEffectiveScalingType == kScaling_PixelPerfect ? GL_NEAREST : GL_LINEAR);

#if !DEFERRED_TEX_UPDATE
	// Update the texture
#ifdef __ANDROID__
	// With a CPU buffer (no PBO), pass the buffer pointer directly.
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, zvw, zvh, kFramePixelFormat, kFramePixelType, gAndroidFrameBuffer);
#else
	// With PBO bound, NULL means offset 0 into the PBO.
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, zvw, zvh, kFramePixelFormat, kFramePixelType, NULL);
#endif
	CHECK_GL_ERROR();
#endif

	const float umax = vw * (1.0f / kFrameTextureWidth);
	const float vmax = vh * (1.0f / kFrameTextureHeight);

#ifdef __ANDROID__
	DrawQuadGLES(umax, vmax);
	DrawTouchOverlay();
#else
	GLRender_InitMatrices();

	glBegin(GL_QUADS);
	glTexCoord2f(   0, vmax); glVertex3f( 0, vh, 0);
	glTexCoord2f(umax, vmax); glVertex3f(vw, vh, 0);
	glTexCoord2f(umax,    0); glVertex3f(vw,  0, 0);
	glTexCoord2f(   0,    0); glVertex3f( 0,  0, 0);
	glEnd();
	CHECK_GL_ERROR();
#endif

	SDL_GL_SwapWindow(gSDLWindow);

#if DEFERRED_TEX_UPDATE
	//-------------------------------------------------------------------------
	// Update texture

#ifdef __ANDROID__
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, zvw, zvh, kFramePixelFormat, kFramePixelType, gAndroidFrameBuffer);
#else
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, zvw, zvh, kFramePixelFormat, kFramePixelType, NULL);
#endif
	CHECK_GL_ERROR();
#endif
}

#endif // GLRENDER

