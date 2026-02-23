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
#else
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
PFNGLGENBUFFERSARBPROC glGenBuffersARB;
PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB;
PFNGLBINDBUFFERARBPROC glBindBufferARB;
PFNGLMAPBUFFERARBPROC glMapBufferARB;
PFNGLBUFFERDATAARBPROC glBufferDataARB;
PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB;
#endif // __ANDROID__

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
#ifndef __ANDROID__
static GLuint gFramePBO = 0;
#endif
static GLint gMaxTextureSize = 0;

const char* gRendererName = "NULL";
Boolean gCanDoHQStretch = true;

#ifdef __ANDROID__
// OpenGL ES 3.0 shader-based rendering state
static GLuint gShaderProgram = 0;
static GLuint gQuadVAO = 0;
static GLuint gQuadVBO = 0;
static GLuint gQuadIBO = 0;
static GLint gUniformMVP = -1;
static GLint gUniformColor = -1;
static GLuint gWhiteTex = 0;           // 1×1 white texture for solid-color drawing
static color_t s_androidFrameBuffer[kFrameTextureWidth * kFrameTextureHeight];

static const char* kVertexShaderSrc =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform mat4 u_mvp;\n"
	"in vec2 a_position;\n"
	"in vec2 a_texcoord;\n"
	"out vec2 v_texcoord;\n"
	"void main() {\n"
	"    gl_Position = u_mvp * vec4(a_position, 0.0, 1.0);\n"
	"    v_texcoord = a_texcoord;\n"
	"}\n";

static const char* kFragmentShaderSrc =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D u_texture;\n"
	"uniform vec4 u_color;\n"
	"in vec2 v_texcoord;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"    fragColor = texture(u_texture, v_texcoord) * u_color;\n"
	"}\n";

static GLuint GLES_CompileShader(GLenum type, const char* src)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	GLint status = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status)
	{
		GLint logLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
		char* log = logLen > 0 ? (char*)SDL_malloc(logLen) : NULL;
		if (log) glGetShaderInfoLog(shader, logLen, NULL, log);
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader compile error: %s", log ? log : "(no log)");
		SDL_free(log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint GLES_CreateShaderProgram(void)
{
	GLuint vs = GLES_CompileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
	GLuint fs = GLES_CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glBindAttribLocation(prog, 0, "a_position");
	glBindAttribLocation(prog, 1, "a_texcoord");
	glLinkProgram(prog);
	GLint status = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if (!status)
	{
		GLint logLen = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
		char* log = logLen > 0 ? (char*)SDL_malloc(logLen) : NULL;
		if (log) glGetProgramInfoLog(prog, logLen, NULL, log);
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader link error: %s", log ? log : "(no log)");
		SDL_free(log);
		glDeleteProgram(prog);
		prog = 0;
	}
	glDeleteShader(vs);
	glDeleteShader(fs);
	return prog;
}
#endif // __ANDROID__

#ifdef __ANDROID__
#include <math.h>
#include "touchcontrols.h"  // NUM_BUTTONS

// Draw a filled polygon (triangle fan) in screen-pixel coordinates.
// Caller must have already bound white texture, set color uniform, enabled blend.
// segments must be <= 62 (array holds segments+2 vertices, max 64).
static void GLES_DrawFilledCircle(float cx, float cy, float radius, int segments,
                                  float screenW, float screenH)
{
#define GLES_CIRCLE_MAX_SEGMENTS 62
#define GLES_CIRCLE_MAX_VERTS    (GLES_CIRCLE_MAX_SEGMENTS + 2)
	if (segments > GLES_CIRCLE_MAX_SEGMENTS) segments = GLES_CIRCLE_MAX_SEGMENTS;

	// Build triangle fan: center + segments ring vertices + closing vertex
	int nVerts = segments + 2;
	float verts[GLES_CIRCLE_MAX_VERTS][4];  // x, y, u, v

	static const float kTwoPI = 6.2831853f;  // 2 * PI

	// Ortho: NDC x = 2*px/screenW - 1, NDC y = 1 - 2*py/screenH
	verts[0][0] = cx;  verts[0][1] = cy;  verts[0][2] = 0.5f;  verts[0][3] = 0.5f;
	for (int i = 1; i < nVerts; i++)
	{
		float angle = (float)(i - 1) * kTwoPI / (float)segments;
		verts[i][0] = cx + radius * cosf(angle);
		verts[i][1] = cy + radius * sinf(angle);
		verts[i][2] = 0.5f;
		verts[i][3] = 0.5f;
	}
	verts[nVerts - 1][0] = verts[1][0];  // close the fan
	verts[nVerts - 1][1] = verts[1][1];

	float mvp[16] = {
		2.0f/screenW,  0,           0, 0,
		0,            -2.0f/screenH, 0, 0,
		0,             0,           -1, 0,
		-1.0f,         1.0f,         0, 1,
	};
	glUniformMatrix4fv(gUniformMVP, 1, GL_FALSE, mvp);

	glBindVertexArray(gQuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nVerts * 4 * (GLsizei)sizeof(float)), verts, GL_STREAM_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLE_FAN, 0, nVerts);
	glBindVertexArray(0);
#undef GLES_CIRCLE_MAX_SEGMENTS
#undef GLES_CIRCLE_MAX_VERTS
}

// Draw a filled rotated rectangle centered at (cx, cy) with half-extents (hw, hh),
// rotated by `angle` radians.
static void GLES_DrawRect(float cx, float cy, float hw, float hh, float angle,
                          float screenW, float screenH)
{
	float cosA = cosf(angle), sinA = sinf(angle);
	// Four corners of the rotated rectangle (CCW order)
	float verts[4][4] = {
		{ cx + hw*cosA - hh*sinA,  cy + hw*sinA + hh*cosA,  0.5f, 0.5f },
		{ cx - hw*cosA - hh*sinA,  cy - hw*sinA + hh*cosA,  0.5f, 0.5f },
		{ cx - hw*cosA + hh*sinA,  cy - hw*sinA - hh*cosA,  0.5f, 0.5f },
		{ cx + hw*cosA + hh*sinA,  cy + hw*sinA - hh*cosA,  0.5f, 0.5f },
	};
	float mvp[16] = {
		2.0f/screenW,  0,            0, 0,
		0,            -2.0f/screenH, 0, 0,
		0,             0,           -1, 0,
		-1.0f,          1.0f,         0, 1,
	};
	glUniformMatrix4fv(gUniformMVP, 1, GL_FALSE, mvp);
	glBindVertexArray(gQuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, GL_STREAM_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*(GLsizei)sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*(GLsizei)sizeof(float), (void*)(2*sizeof(float)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	// gQuadIBO is bound inside gQuadVAO: indices [0,1,2,0,2,3]
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
	glBindVertexArray(0);
}

// Draw a filled triangle with three screen-space vertices.
static void GLES_DrawTriangle(float x0, float y0, float x1, float y1, float x2, float y2,
                              float screenW, float screenH)
{
	float verts[3][4] = {
		{ x0, y0, 0.5f, 0.5f },
		{ x1, y1, 0.5f, 0.5f },
		{ x2, y2, 0.5f, 0.5f },
	};
	float mvp[16] = {
		2.0f/screenW,  0,            0, 0,
		0,            -2.0f/screenH, 0, 0,
		0,             0,           -1, 0,
		-1.0f,          1.0f,         0, 1,
	};
	glUniformMatrix4fv(gUniformMVP, 1, GL_FALSE, mvp);
	glBindVertexArray(gQuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, GL_STREAM_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*(GLsizei)sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*(GLsizei)sizeof(float), (void*)(2*sizeof(float)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

// Draw a geometric icon inside a button circle.
// btnIdx: 0=Attack, 1=Back, 2=Prev, 3=Next, 4=Pause, 5=Radar, 6=Music
// (cx,cy) = button center, r = button radius
static void GLES_DrawButtonIcon(int btnIdx, float cx, float cy, float r,
                                float screenW, float screenH)
{
	static const float kPiOver4 = 0.7853982f;  // 45 degrees in radians
	glUniform4f(gUniformColor, 1.0f, 1.0f, 1.0f, 0.85f);

	switch (btnIdx)
	{
	case 0: // Attack: plus/cross (sword attack)
		GLES_DrawRect(cx, cy, r*0.17f, r*0.58f, 0.0f,      screenW, screenH);
		GLES_DrawRect(cx, cy, r*0.58f, r*0.17f, 0.0f,      screenW, screenH);
		break;

	case 1: // Back: X shape (cancel)
		GLES_DrawRect(cx, cy, r*0.14f, r*0.55f,  kPiOver4, screenW, screenH);
		GLES_DrawRect(cx, cy, r*0.14f, r*0.55f, -kPiOver4, screenW, screenH);
		break;

	case 2: // PrevWeapon: left-pointing triangle (◄)
		GLES_DrawTriangle(cx - r*0.40f, cy,
		                  cx + r*0.28f, cy - r*0.38f,
		                  cx + r*0.28f, cy + r*0.38f,
		                  screenW, screenH);
		break;

	case 3: // NextWeapon: right-pointing triangle (►)
		GLES_DrawTriangle(cx + r*0.40f, cy,
		                  cx - r*0.28f, cy - r*0.38f,
		                  cx - r*0.28f, cy + r*0.38f,
		                  screenW, screenH);
		break;

	case 4: // Pause: two vertical bars (‖)
		GLES_DrawRect(cx - r*0.18f, cy, r*0.10f, r*0.38f, 0.0f, screenW, screenH);
		GLES_DrawRect(cx + r*0.18f, cy, r*0.10f, r*0.38f, 0.0f, screenW, screenH);
		break;

	case 5: // Radar: outer ring + center dot (radar ping)
		// Outer white circle
		GLES_DrawFilledCircle(cx, cy, r*0.58f, 16, screenW, screenH);
		// Dark overlay to create the ring gap
		glUniform4f(gUniformColor, 0.0f, 0.0f, 0.2f, 0.72f);
		GLES_DrawFilledCircle(cx, cy, r*0.38f, 16, screenW, screenH);
		// White center dot
		glUniform4f(gUniformColor, 1.0f, 1.0f, 1.0f, 0.85f);
		GLES_DrawFilledCircle(cx, cy, r*0.14f, 10, screenW, screenH);
		break;

	case 6: // Music: three horizontal bars (simplified ♪)
		GLES_DrawRect(cx, cy - r*0.22f, r*0.36f, r*0.07f, 0.0f, screenW, screenH);
		GLES_DrawRect(cx, cy,           r*0.36f, r*0.07f, 0.0f, screenW, screenH);
		GLES_DrawRect(cx, cy + r*0.22f, r*0.36f, r*0.07f, 0.0f, screenW, screenH);
		break;

	default:
		break;
	}
}

// Draw touch control overlay (joystick + buttons with icons) on top of the game frame.
// Called from GLRender_Present() after the game framebuffer quad.
// btn[i] = { cx, cy, radius }
void GLRender_DrawTouchControlsOverlay(float screenW, float screenH,
                                       float joyCX, float joyCY, float joyR,
                                       float joyThumbX, float joyThumbY, bool joyActive,
                                       float btn[NUM_BUTTONS][3], bool btnPressed[NUM_BUTTONS])
{
	// Switch to full-window viewport so touch controls appear over the entire screen,
	// including letterbox bars.  Restore the game viewport afterwards.
	glViewport(0, 0, (GLsizei)screenW, (GLsizei)screenH);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindTexture(GL_TEXTURE_2D, gWhiteTex);
	glUseProgram(gShaderProgram);

	// --- Joystick base ring ---
	// Outer white ring
	glUniform4f(gUniformColor, 0.8f, 0.8f, 0.8f, 0.40f);
	GLES_DrawFilledCircle(joyCX, joyCY, joyR, 24, screenW, screenH);
	// Dark fill to make a ring shape
	glUniform4f(gUniformColor, 0.1f, 0.1f, 0.1f, 0.25f);
	GLES_DrawFilledCircle(joyCX, joyCY, joyR * 0.90f, 24, screenW, screenH);

	// --- Joystick thumb indicator (only when finger is active) ---
	if (joyActive)
	{
		// Single semi-transparent circle — one circle only, no stacking artifacts.
		glUniform4f(gUniformColor, 0.7f, 0.7f, 1.0f, 0.80f);
		GLES_DrawFilledCircle(joyThumbX, joyThumbY, joyR * 0.38f, 20, screenW, screenH);
	}

	// --- Action buttons --- //
	// btn[i] = { cx, cy, r }
	// 0=Attack(orange), 1=Back(blue), 2=Prev(green), 3=Next(green),
	// 4=Pause(grey), 5=Radar(yellow), 6=Music(grey)
	static const float kBtnColors[NUM_BUTTONS][4] = {
		{ 0.90f, 0.45f, 0.15f, 0.45f },   // 0 Attack:  orange
		{ 0.25f, 0.55f, 0.90f, 0.40f },   // 1 Back:    blue
		{ 0.35f, 0.85f, 0.45f, 0.40f },   // 2 Prev:    green
		{ 0.35f, 0.85f, 0.45f, 0.40f },   // 3 Next:    green
		{ 0.80f, 0.80f, 0.80f, 0.35f },   // 4 Pause:   grey
		{ 0.90f, 0.80f, 0.20f, 0.40f },   // 5 Radar:   yellow
		{ 0.65f, 0.65f, 0.65f, 0.32f },   // 6 Music:   grey
	};

	for (int i = 0; i < NUM_BUTTONS; i++)
	{
		float r     = btn[i][2];
		float bx    = btn[i][0];
		float by    = btn[i][1];
		float alpha = btnPressed[i] ? 0.85f : kBtnColors[i][3];
		float cr    = kBtnColors[i][0];
		float cg    = kBtnColors[i][1];
		float cb    = kBtnColors[i][2];

		// Outer ring (white outline)
		glUniform4f(gUniformColor, 1.0f, 1.0f, 1.0f, 0.50f);
		GLES_DrawFilledCircle(bx, by, r, 20, screenW, screenH);
		// Fill
		glUniform4f(gUniformColor, cr, cg, cb, alpha);
		GLES_DrawFilledCircle(bx, by, r * 0.88f, 20, screenW, screenH);

		// Icon
		GLES_DrawButtonIcon(i, bx, by, r, screenW, screenH);
	}

	// Restore state
	glUniform4f(gUniformColor, 1.0f, 1.0f, 1.0f, 1.0f);
	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, gFrameTexture);
	// Restore the game (letterboxed) viewport
	extern SDL_Rect GLRender_GetCurrentViewport(void);
	SDL_Rect vp = GLRender_GetCurrentViewport();
	glViewport(vp.x, vp.y, vp.w, vp.h);
}
#endif // __ANDROID__ (GLRender_DrawTouchControlsOverlay)

#if _DEBUG
#define CHECK_GL_ERROR()												\
	do {					 											\
		GLenum err = glGetError();										\
		if (err != GL_NO_ERROR)											\
			DoFatalGLError(err, __func__, __LINE__);					\
	} while(0)

static void DoFatalGLError(GLenum error, const char* file, int line)
{
	static char alertbuf[1024];
	SDL_snprintf(alertbuf, sizeof(alertbuf), "OpenGL error 0x%x\nin %s:%d", error, file, line);
	DoFatalAlert(alertbuf);
}
#else
#define CHECK_GL_ERROR() do {} while(0)
#endif

#ifndef __ANDROID__
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
		return (SDL_Point) { (int)(displayHeight * logicalAR), displayHeight };
	}
	else
	{
		return (SDL_Point) { displayWidth, (int)(displayWidth / logicalAR) };
	}
}

static void GLRender_InitMatrices(void)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, VISIBLE_WIDTH, VISIBLE_HEIGHT, 0, 0, 1000);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}
#else
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
		return (SDL_Point) { (int)(displayHeight * logicalAR), displayHeight };
	}
	else
	{
		return (SDL_Point) { displayWidth, (int)(displayWidth / logicalAR) };
	}
}
#endif // __ANDROID__

#ifndef __ANDROID__
#define GL_GET_PROC_ADDRESS(t, proc) \
do { \
    (proc) = (t) SDL_GL_GetProcAddress(#proc); \
    GAME_ASSERT_MESSAGE((proc), "Missing OpenGL procedure " #proc); \
} while(0)
#endif // !__ANDROID__

static void InitTextureAndPBO(int pixelZoom)
{
	glGenTextures(1, &gFrameTexture);
	CHECK_GL_ERROR();

#ifndef __ANDROID__
	glGenBuffersARB(1, &gFramePBO);
	CHECK_GL_ERROR();

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
#endif // !__ANDROID__

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
#endif // !__ANDROID__

#ifdef __ANDROID__
	if (gWhiteTex != 0)      { glDeleteTextures(1, &gWhiteTex); gWhiteTex = 0; }
	if (gQuadVBO != 0) { glDeleteBuffers(1, &gQuadVBO); gQuadVBO = 0; }
	if (gQuadIBO != 0) { glDeleteBuffers(1, &gQuadIBO); gQuadIBO = 0; }
	if (gQuadVAO != 0) { glDeleteVertexArrays(1, &gQuadVAO); gQuadVAO = 0; }
	if (gShaderProgram != 0) { glDeleteProgram(gShaderProgram); gShaderProgram = 0; }
#endif // __ANDROID__
}

void GLRender_Init(void)
{
	SDL_Log("Using special PPC renderer!");

#if FRAMEBUFFER_COLOR_DEPTH == 32
	gRendererName = "fastgl32";
#elif FRAMEBUFFER_COLOR_DEPTH == 16
	gRendererName = "fastgl16";
#else
	gRendererName = "gl??";
#endif

	gGLContext = SDL_GL_CreateContext(gSDLWindow);
	GAME_ASSERT(gGLContext);

	bool didMakeCurrent = SDL_GL_MakeCurrent(gSDLWindow, gGLContext);
	GAME_ASSERT_MESSAGE(didMakeCurrent, SDL_GetError());

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gMaxTextureSize);
	SDL_Log("Max texture size: %d", (int) gMaxTextureSize);

	if (gMaxTextureSize < kFrameTextureWidth)
	{
		char message[128];
		SDL_snprintf(message, sizeof(message), "Your graphics card's max texture size (%d)\nis below the game's requirements (%d).", (int) gMaxTextureSize, kFrameTextureWidth);
		DoAlert(message);
	}

#ifdef __ANDROID__
	// No HQ stretch on Android; GLES3 uses shader-based rendering
	gCanDoHQStretch = false;
#elif OSXPPC
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
#endif // !__ANDROID__

#if !(NOVSYNC)
	SDL_GL_SetSwapInterval(1);
#else
	SDL_GL_SetSwapInterval(0);
#endif

#ifdef __ANDROID__
	// GLES3: create shader program and quad VBO/VAO
	gShaderProgram = GLES_CreateShaderProgram();
	GAME_ASSERT(gShaderProgram);

	gUniformMVP   = glGetUniformLocation(gShaderProgram, "u_mvp");
	gUniformColor = glGetUniformLocation(gShaderProgram, "u_color");
	glUseProgram(gShaderProgram);
	glUniform1i(glGetUniformLocation(gShaderProgram, "u_texture"), 0);
	glUniform4f(gUniformColor, 1.0f, 1.0f, 1.0f, 1.0f);  // default: white, fully opaque

	glGenVertexArrays(1, &gQuadVAO);
	glGenBuffers(1, &gQuadVBO);
	glGenBuffers(1, &gQuadIBO);

	// Pre-upload static indices (quad split into two triangles)
	static const uint16_t kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };
	glBindVertexArray(gQuadVAO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gQuadIBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIndices), kQuadIndices, GL_STATIC_DRAW);
	glBindVertexArray(0);

	// Create 1×1 white texture used for solid-color drawing (touch control overlay)
	{
		static const uint8_t kWhitePixel[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &gWhiteTex);
		glBindTexture(GL_TEXTURE_2D, gWhiteTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kWhitePixel);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
#else
	GLRender_InitMatrices();

	glDisable(GL_FOG);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_LIGHTING);
//	glEnable(GL_COLOR_MATERIAL);
	glDepthMask(false);

	glColor4f(1,1,1,1);
#endif // __ANDROID__

	//glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	CHECK_GL_ERROR();

	InitTextureAndPBO(1);
}

void GLRender_Shutdown(void)
{
	ShutdownRenderThreads();

	DeleteTextureAndPBO();

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

#ifdef __ANDROID__
static SDL_Rect gCurrentViewportRect = {0};

SDL_Rect GLRender_GetCurrentViewport(void)
{
	return gCurrentViewportRect;
}
#endif

void GLRender_PresentFramebuffer(void)
{
	static SDL_Rect previousViewportRect = {0};
#ifndef __ANDROID__
	static int previousEffectiveScalingType = kScaling_Unspecified;
#endif
	static int needClear = 60;

	const int vw = VISIBLE_WIDTH;
	const int vh = VISIBLE_HEIGHT;

	bool didMakeCurrent = SDL_GL_MakeCurrent(gSDLWindow, gGLContext);
	GAME_ASSERT_MESSAGE(didMakeCurrent, SDL_GetError());

	//-------------------------------------------------------------------------
	// Update dimensions

	SDL_Rect viewportRect = GetViewportSize();
    if (!SDL_RectsEqual(&viewportRect, &previousViewportRect))
	{
		previousViewportRect = viewportRect;
		glViewport(viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h);
		needClear = 60;
	}
#ifdef __ANDROID__
	gCurrentViewportRect = viewportRect;
#else
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
	// Update PBO

	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gFramePBO);
	CHECK_GL_ERROR();

	// get new PBO
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
#endif // !__ANDROID__

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

#ifdef __ANDROID__
	//-------------------------------------------------------------------------
	// Android GLES3: direct texture upload + shader-based quad

	ConvertFramebufferMT(s_androidFrameBuffer);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vw, vh, kFramePixelFormat, kFramePixelType, s_androidFrameBuffer);
	CHECK_GL_ERROR();

	const float umax = vw * (1.0f / kFrameTextureWidth);
	const float vmax = vh * (1.0f / kFrameTextureHeight);

	// Quad vertices: (x, y, u, v) - maps [0,vw]x[0,vh] with Y=0 at top
	float verts[4][4] = {
		{ 0.0f,  (float)vh,  0.0f,  vmax },
		{ (float)vw, (float)vh,  umax,  vmax },
		{ (float)vw, 0.0f,        umax,  0.0f },
		{ 0.0f,  0.0f,        0.0f,  0.0f },
	};

	// Orthographic projection: maps [0,vw]x[0,vh] -> NDC, Y-flipped (top=0)
	// Column-major for OpenGL: x_ndc = 2x/vw - 1,  y_ndc = 1 - 2y/vh
	float mvp[16] = {
		2.0f/vw,   0,        0, 0,   // col 0
		0,        -2.0f/vh,  0, 0,   // col 1
		0,         0,       -1, 0,   // col 2
		-1.0f,     1.0f,     0, 1,   // col 3
	};

	glUseProgram(gShaderProgram);
	glUniformMatrix4fv(gUniformMVP, 1, GL_FALSE, mvp);

	glBindVertexArray(gQuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, GL_STREAM_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void*)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
	glBindVertexArray(0);
	CHECK_GL_ERROR();

	// Draw touch control overlay (joystick + buttons) using actual screen pixel dimensions
	glUniform4f(gUniformColor, 1.0f, 1.0f, 1.0f, 1.0f);  // reset color before overlay
	{
		extern void TouchControls_DrawOverlay(void);
		TouchControls_DrawOverlay();
	}
#else
	//-------------------------------------------------------------------------
	// Desktop: PBO-based texture upload + fixed-function quad

#if !DEFERRED_TEX_UPDATE
	// Update the texture
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, zvw, zvh, kFramePixelFormat, kFramePixelType, NULL);
	CHECK_GL_ERROR();
#endif

	const float umax = vw * (1.0f / kFrameTextureWidth);
	const float vmax = vh * (1.0f / kFrameTextureHeight);

	GLRender_InitMatrices();

	glBegin(GL_QUADS);
	glTexCoord2f(   0, vmax); glVertex3f( 0, vh, 0);
	glTexCoord2f(umax, vmax); glVertex3f(vw, vh, 0);
	glTexCoord2f(umax,    0); glVertex3f(vw,  0, 0);
	glTexCoord2f(   0,    0); glVertex3f( 0,  0, 0);
	glEnd();
	CHECK_GL_ERROR();
#endif // __ANDROID__

	SDL_GL_SwapWindow(gSDLWindow);

#if !defined(__ANDROID__) && DEFERRED_TEX_UPDATE
	//-------------------------------------------------------------------------
	// Update texture

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, zvw, zvh, kFramePixelFormat, kFramePixelType, NULL);
	CHECK_GL_ERROR();
#endif
}

#endif // GLRENDER

