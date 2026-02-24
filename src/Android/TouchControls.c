// ANDROID TOUCH CONTROLS FOR MIGHTY MIKE

#ifdef __ANDROID__

#include "TouchControls.h"
#include "../Headers/structures.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define MAX_TOUCH_FINGERS  5
#define NUM_NEEDS_MAX      64

typedef struct {
    long long id;
    float x, y;
    bool  active;
} TouchFinger;

static TouchFinger gFingers[MAX_TOUCH_FINGERS];
static bool gTouchNeedActive[NUM_NEEDS_MAX];

// -------------------------------------------------------------------------
// Hit-testing helpers
// -------------------------------------------------------------------------

// Returns true if (nx, ny) is within the square hit-box for a circular button.
// Using square boxes (radius r in both axes) avoids needing the aspect ratio.
static bool HitCircle(float nx, float ny, float cx, float cy, float r)
{
    return (nx >= cx - r) && (nx <= cx + r) && (ny >= cy - r) && (ny <= cy + r);
}

// Returns the D-pad directions hit by a single finger, expressed as need bitmask.
// Uses cross-shaped zones matching the visual arm layout.
static void GetDPadNeeds(float nx, float ny, bool out[NUM_NEEDS_MAX])
{
    float dx = nx - TC_DPAD_CX;
    float dy = ny - TC_DPAD_CY;

    bool inXArm = (SDL_fabsf(dx) <= TC_DPAD_ARM_L) && (SDL_fabsf(dy) <= TC_DPAD_ARM_HW);
    bool inYArm = (SDL_fabsf(dy) <= TC_DPAD_ARM_L) && (SDL_fabsf(dx) <= TC_DPAD_ARM_HW);

    if (!inXArm && !inYArm)
    {
        // Diagonal corner regions: treat as two simultaneous directions
        bool diagX = (SDL_fabsf(dx) > TC_DPAD_ARM_HW) && (SDL_fabsf(dx) <= TC_DPAD_ARM_L);
        bool diagY = (SDL_fabsf(dy) > TC_DPAD_ARM_HW) && (SDL_fabsf(dy) <= TC_DPAD_ARM_L);
        if (!diagX || !diagY) return;
        inXArm = true;
        inYArm = true;
    }

    bool up    = inYArm && dy < -TC_DPAD_ARM_HW;
    bool down  = inYArm && dy >  TC_DPAD_ARM_HW;
    bool left  = inXArm && dx < -TC_DPAD_ARM_HW;
    bool right = inXArm && dx >  TC_DPAD_ARM_HW;

    // Map to both game-movement needs and UI-navigation needs so the
    // D-pad works on the menu as well as in gameplay.
    if (up)    { out[kNeed_Up]    = true;  out[kNeed_UIUp]    = true; }
    if (down)  { out[kNeed_Down]  = true;  out[kNeed_UIDown]  = true; }
    if (left)  { out[kNeed_Left]  = true;  out[kNeed_UILeft]  = true; }
    if (right) { out[kNeed_Right] = true;  out[kNeed_UIRight] = true; }
}

static void RecalcNeedsFromFingers(void)
{
    memset(gTouchNeedActive, 0, sizeof(gTouchNeedActive));

    for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
    {
        if (!gFingers[i].active) continue;

        float nx = gFingers[i].x;
        float ny = gFingers[i].y;

        // D-pad (left side of screen)
        if (nx < 0.40f)
        {
            GetDPadNeeds(nx, ny, gTouchNeedActive);
            continue;
        }

        // Pause – small button in top-right corner (check before other buttons)
        if (HitCircle(nx, ny, TC_PAUSE_CX, TC_PAUSE_CY, TC_PAUSE_R))
        {
            gTouchNeedActive[kNeed_UIPause] = true;
            gTouchNeedActive[kNeed_UIBack]  = true;
            continue;
        }

        // Attack – large circle, bottom-right (also acts as UIConfirm for menus)
        if (HitCircle(nx, ny, TC_ATK_CX, TC_ATK_CY, TC_ATK_R))
        {
            gTouchNeedActive[kNeed_Attack]    = true;
            gTouchNeedActive[kNeed_UIConfirm] = true;
            continue;
        }

        // Next Weapon
        if (HitCircle(nx, ny, TC_NW_CX, TC_NW_CY, TC_NW_R))
        {
            gTouchNeedActive[kNeed_NextWeapon] = true;
            continue;
        }

        // Prev Weapon
        if (HitCircle(nx, ny, TC_PW_CX, TC_PW_CY, TC_PW_R))
        {
            gTouchNeedActive[kNeed_PrevWeapon] = true;
            continue;
        }
    }
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

void TouchControls_ProcessEvent(int eventType, float fingerX, float fingerY, long long fingerId)
{
    if (eventType == SDL_EVENT_FINGER_DOWN)
    {
        for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
        {
            if (!gFingers[i].active)
            {
                gFingers[i].id     = fingerId;
                gFingers[i].x      = fingerX;
                gFingers[i].y      = fingerY;
                gFingers[i].active = true;
                break;
            }
        }
    }
    else if (eventType == SDL_EVENT_FINGER_MOTION)
    {
        for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
        {
            if (gFingers[i].active && gFingers[i].id == fingerId)
            {
                gFingers[i].x = fingerX;
                gFingers[i].y = fingerY;
                break;
            }
        }
    }
    else if (eventType == SDL_EVENT_FINGER_UP)
    {
        for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
        {
            if (gFingers[i].active && gFingers[i].id == fingerId)
            {
                gFingers[i].active = false;
                break;
            }
        }
    }

    RecalcNeedsFromFingers();
}

int TouchControls_GetNeedActive(int need)
{
    if (need < 0 || need >= NUM_NEEDS_MAX) return 0;
    return gTouchNeedActive[need] ? 1 : 0;
}

void TouchControls_PostFrame(void)
{
    // State persists until finger-up; nothing to reset here.
}

#endif // __ANDROID__
