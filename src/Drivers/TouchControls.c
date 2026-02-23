// TOUCH INPUT HANDLING
// Isolated joystick and button state management for Android touch controls.
// (C) 2025 Mighty Mike Android Port

#ifdef __ANDROID__

#include <SDL3/SDL.h>
#include <float.h>
#include <math.h>

#include "myglobals.h"
#include "externs.h"
#include "structures.h"
#include "input.h"
#include "touchcontrols.h"

//------------------------------------------------------------
// Button indices
//------------------------------------------------------------
#define BTN_ATTACK   0   // kNeed_Attack / kNeed_UIConfirm
#define BTN_BACK     1   // kNeed_UIBack / kNeed_UIPrev
#define BTN_PREV     2   // kNeed_PrevWeapon
#define BTN_NEXT     3   // kNeed_NextWeapon
#define BTN_PAUSE    4   // kNeed_UIPause
#define BTN_RADAR    5   // kNeed_Radar
#define BTN_MUSIC    6   // kNeed_ToggleMusic
// NUM_BUTTONS = 7, defined in touchcontrols.h

//------------------------------------------------------------
// Layout (fractions of screen dimensions)
//------------------------------------------------------------

// Joystick
#define JOY_RADIUS_FRAC   0.14f   // fraction of screen height
#define JOY_DEFAULT_CX    0.15f   // default ring center X (fraction of width)
#define JOY_DEFAULT_CY    0.72f   // default ring center Y (fraction of height)
#define JOY_DEAD_ZONE     0.08f   // dead zone as fraction of joystick radius
#define JOY_ZONE_FRAC     0.40f   // left N% of screen is the joystick zone

// Button centres and sizes
static const float kBtnCX[NUM_BUTTONS]    = { 0.88f, 0.73f, 0.73f, 0.88f, 0.96f, 0.80f, 0.87f };
static const float kBtnCY[NUM_BUTTONS]    = { 0.72f, 0.72f, 0.88f, 0.88f, 0.08f, 0.55f, 0.08f };
static const float kBtnRadFrac[NUM_BUTTONS] = {
    0.09f, 0.09f, 0.09f, 0.09f,   // Attack, Back, Prev, Next  - full size
    0.055f,                         // Pause                     - small
    0.09f,                          // Radar                     - full size
    0.055f                          // Music                     - small
};
// Hit radius is slightly larger than visual radius for better touch usability.
// 1.35 = 35% larger than the visual radius, balancing accuracy and ease of tapping.
#define BTN_HIT_SCALE  1.35f

//------------------------------------------------------------
// State
//------------------------------------------------------------

#define MAX_TOUCHES   10

// ---- Joystick: completely isolated from button touches ----
// Uses a "floating" joystick: the ring appears where the finger first lands in the
// joystick zone, and the thumb tracks relative to that anchor point.  When the
// finger lifts the ring returns to its default visual position.
typedef struct {
    bool         active;
    SDL_FingerID fid;         // fingerID we are tracking
    float        anchorX;     // where the finger first touched (ring centre)
    float        anchorY;
    float        currentX;    // current finger position
    float        currentY;
} JoystickState;

static JoystickState gJoy;

// ---- Non-joystick touches (used for button hit testing) ----
typedef struct {
    bool         active;
    SDL_FingerID fid;
    float        x, y;        // current position in pixels
} Touch;

static Touch gTouches[MAX_TOUCHES];

// ---- Cached normalised joystick direction (updated each frame in UpdateNeeds) ----
static float gJoyNormDX = 0.0f;
static float gJoyNormDY = 0.0f;

// ---- Cached screen size ----
static int gScreenW = 1;
static int gScreenH = 1;

//------------------------------------------------------------
// Internal helpers
//------------------------------------------------------------

static void RefreshScreen(void)
{
    SDL_GetWindowSizeInPixels(gSDLWindow, &gScreenW, &gScreenH);
    if (gScreenW < 1) gScreenW = 640;
    if (gScreenH < 1) gScreenH = 480;
}

// Is (px,py) in the left-side joystick zone?
static bool InJoystickZone(float px, float py)
{
    (void)py;
    return px < (float)gScreenW * JOY_ZONE_FRAC;
}

// Find the first inactive slot; returns -1 if full.
static int FindFreeTouch(void)
{
    for (int i = 0; i < MAX_TOUCHES; i++)
        if (!gTouches[i].active) return i;
    return -1;
}

// Find a touch by fingerID; returns -1 if not found.
static int FindTouchByFID(SDL_FingerID fid)
{
    for (int i = 0; i < MAX_TOUCHES; i++)
        if (gTouches[i].active && gTouches[i].fid == fid) return i;
    return -1;
}

// Find nearest active touch by position (positional fallback for ID mismatch).
static int FindNearestTouch(float px, float py)
{
    float best = FLT_MAX;
    int   idx  = -1;
    for (int i = 0; i < MAX_TOUCHES; i++) {
        if (!gTouches[i].active) continue;
        float dx = gTouches[i].x - px, dy = gTouches[i].y - py;
        float d  = dx*dx + dy*dy;
        if (d < best) { best = d; idx = i; }
    }
    return idx;
}

//------------------------------------------------------------
// Public API
//------------------------------------------------------------

void TouchControls_Init(void)
{
    SDL_memset(&gJoy, 0, sizeof(gJoy));
    SDL_memset(gTouches, 0, sizeof(gTouches));
    gJoyNormDX = 0.0f;
    gJoyNormDY = 0.0f;
}

void TouchControls_HandleEvent(const SDL_Event* ev)
{
    RefreshScreen();
    float px  = ev->tfinger.x * (float)gScreenW;
    float py  = ev->tfinger.y * (float)gScreenH;
    SDL_FingerID fid = ev->tfinger.fingerID;

    switch (ev->type)
    {
    // ------------------------------------------------------------------
    case SDL_EVENT_FINGER_DOWN:
    {
        if (InJoystickZone(px, py) && !gJoy.active)
        {
            // Claim as the joystick touch
            gJoy.active   = true;
            gJoy.fid      = fid;
            gJoy.anchorX  = px;
            gJoy.anchorY  = py;
            gJoy.currentX = px;
            gJoy.currentY = py;
        }
        else
        {
            // Regular button touch - store in free slot
            int slot = FindFreeTouch();
            if (slot >= 0)
            {
                gTouches[slot].active = true;
                gTouches[slot].fid    = fid;
                gTouches[slot].x      = px;
                gTouches[slot].y      = py;
            }
        }
        break;
    }

    // ------------------------------------------------------------------
    case SDL_EVENT_FINGER_MOTION:
    {
        if (gJoy.active && gJoy.fid == fid)
        {
            gJoy.currentX = px;
            gJoy.currentY = py;
        }
        else
        {
            int slot = FindTouchByFID(fid);
            if (slot >= 0)
            {
                gTouches[slot].x = px;
                gTouches[slot].y = py;
            }
        }
        break;
    }

    // ------------------------------------------------------------------
    case SDL_EVENT_FINGER_UP:
    {
        // ---- Try to release joystick ----
        if (gJoy.active && gJoy.fid == fid)
        {
            gJoy.active = false;
            break;
        }

        // ---- Try to release a button touch by fingerID ----
        {
            int slot = FindTouchByFID(fid);
            if (slot >= 0)
            {
                gTouches[slot].active = false;
                break;
            }
        }

        // ---- Positional fallback (fingerID mismatch - Android quirk) ----
        // Decide whether the lifted finger was the joystick or a button touch by
        // checking which active touch is closest to the reported lift position.
        {
            float joyDist = FLT_MAX;
            if (gJoy.active)
            {
                float dx = gJoy.currentX - px, dy = gJoy.currentY - py;
                joyDist = dx*dx + dy*dy;
            }

            int   nearBtn  = FindNearestTouch(px, py);
            float btnDist  = FLT_MAX;
            if (nearBtn >= 0)
            {
                float dx = gTouches[nearBtn].x - px, dy = gTouches[nearBtn].y - py;
                btnDist = dx*dx + dy*dy;
            }

            if (gJoy.active && joyDist <= btnDist)
                gJoy.active = false;
            else if (nearBtn >= 0)
                gTouches[nearBtn].active = false;
        }
        break;
    }
    } // switch
}

void TouchControls_UpdateNeeds(void)
{
    // Recompute normalised joystick direction every frame so IsPressed queries are cheap.
    gJoyNormDX = 0.0f;
    gJoyNormDY = 0.0f;

    if (gJoy.active)
    {
        RefreshScreen();
        float r  = (float)gScreenH * JOY_RADIUS_FRAC;
        float dx = gJoy.currentX - gJoy.anchorX;
        float dy = gJoy.currentY - gJoy.anchorY;
        float dist = sqrtf(dx*dx + dy*dy);

        if (dist >= r * JOY_DEAD_ZONE)
        {
            // Clamp displacement to the joystick radius while preserving direction.
            // If dist <= r: normalizer = r, so output = dx/r (proportional within radius).
            // If dist > r:  normalizer = dist, so output = dx/dist (unit vector, capped at 1.0).
            float normalizer = dist > r ? dist : r;
            gJoyNormDX = dx / normalizer;
            gJoyNormDY = dy / normalizer;
        }
    }
}

bool TouchControls_IsPressed(int needID)
{
    // --- Directional / joystick needs ---
    switch (needID)
    {
    case kNeed_Up:    case kNeed_UIUp:    return gJoyNormDY < -0.3f;
    case kNeed_Down:  case kNeed_UIDown:  return gJoyNormDY >  0.3f;
    case kNeed_Left:  case kNeed_UILeft:  return gJoyNormDX < -0.3f;
    case kNeed_Right: case kNeed_UIRight: return gJoyNormDX >  0.3f;
    default: break;
    }

    // --- Map needID to button index ---
    int btnIdx = -1;
    switch (needID)
    {
    case kNeed_Attack:      case kNeed_UIConfirm: case kNeed_UINext: btnIdx = BTN_ATTACK; break;
    case kNeed_UIBack:      case kNeed_UIPrev:                       btnIdx = BTN_BACK;   break;
    case kNeed_PrevWeapon:                                           btnIdx = BTN_PREV;   break;
    case kNeed_NextWeapon:                                           btnIdx = BTN_NEXT;   break;
    case kNeed_UIPause:                                              btnIdx = BTN_PAUSE;  break;
    case kNeed_Radar:                                                btnIdx = BTN_RADAR;  break;
    case kNeed_ToggleMusic:                                          btnIdx = BTN_MUSIC;  break;
    default: return false;
    }

    // Check all active non-joystick touches against this button
    RefreshScreen();
    float cx = (float)gScreenW * kBtnCX[btnIdx];
    float cy = (float)gScreenH * kBtnCY[btnIdx];
    float r  = (float)gScreenH * kBtnRadFrac[btnIdx] * BTN_HIT_SCALE;

    for (int i = 0; i < MAX_TOUCHES; i++)
    {
        if (!gTouches[i].active) continue;
        float dx = gTouches[i].x - cx, dy = gTouches[i].y - cy;
        if (dx*dx + dy*dy < r*r) return true;
    }
    return false;
}

//------------------------------------------------------------
// Overlay drawing
//------------------------------------------------------------

extern void GLRender_DrawTouchControlsOverlay(float screenW, float screenH,
                                               float joyCX, float joyCY, float joyR,
                                               float joyThumbX, float joyThumbY, bool joyActive,
                                               float btn[NUM_BUTTONS][3], bool btnPressed[NUM_BUTTONS]);

void TouchControls_DrawOverlay(void)
{
    RefreshScreen();
    float sw = (float)gScreenW;
    float sh = (float)gScreenH;

    // ---- Joystick ring centre and thumb position ----
    // When inactive: draw ring at the static default position (visual hint to the player).
    // When active:   draw ring at the finger's anchor point (floating joystick).
    float joyR   = sh * JOY_RADIUS_FRAC;
    float ringCX = gJoy.active ? gJoy.anchorX  : sw * JOY_DEFAULT_CX;
    float ringCY = gJoy.active ? gJoy.anchorY  : sh * JOY_DEFAULT_CY;

    float thumbX = ringCX;
    float thumbY = ringCY;
    if (gJoy.active)
    {
        float dx   = gJoy.currentX - gJoy.anchorX;
        float dy   = gJoy.currentY - gJoy.anchorY;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist > joyR)
        {
            dx = dx / dist * joyR;
            dy = dy / dist * joyR;
        }
        thumbX = gJoy.anchorX + dx;
        thumbY = gJoy.anchorY + dy;
    }

    // ---- Button layout and pressed state ----
    float btn[NUM_BUTTONS][3];
    bool  btnPressed[NUM_BUTTONS];

    for (int i = 0; i < NUM_BUTTONS; i++)
    {
        btn[i][0]    = sw * kBtnCX[i];
        btn[i][1]    = sh * kBtnCY[i];
        btn[i][2]    = sh * kBtnRadFrac[i];
        btnPressed[i] = false;
    }

    // Mark buttons as pressed if any touch is inside their hit area
    for (int t = 0; t < MAX_TOUCHES; t++)
    {
        if (!gTouches[t].active) continue;
        for (int b = 0; b < NUM_BUTTONS; b++)
        {
            if (!btnPressed[b])
            {
                float dx = gTouches[t].x - btn[b][0];
                float dy = gTouches[t].y - btn[b][1];
                float rHit = btn[b][2] * BTN_HIT_SCALE;
                if (dx*dx + dy*dy < rHit*rHit)
                    btnPressed[b] = true;
            }
        }
    }

    GLRender_DrawTouchControlsOverlay(sw, sh,
        ringCX, ringCY, joyR,
        thumbX, thumbY, gJoy.active,
        btn, btnPressed);
}

#endif // __ANDROID__
