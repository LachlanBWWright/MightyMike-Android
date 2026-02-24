// ANDROID TOUCH CONTROLS FOR MIGHTY MIKE

#ifdef __ANDROID__

#include "TouchControls.h"
#include "../Headers/structures.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <string.h>

// -------------------------------------------------------------------------
// Touch zone definitions (normalised coords, landscape orientation)
//
// The screen is divided as follows:
//
//   +------------------+----+----------------+
//   |                  |    |  Next Weapon   |
//   |   D-Pad (left    |    +----------+-----+
//   |   40% width)     | gap| Pause    |     |
//   +--+------+--+     |    +----------+-----+
//   |  | LEFT |  |     |    |   Attack       |
//   +--+------+--+     |    +----------------+
//   |   DOWN     |     |    |  Prev Weapon   |
//   +------------+-----+----+----------------+
//
// -------------------------------------------------------------------------

// Maximum simultaneous fingers tracked
#define MAX_TOUCH_FINGERS  5

typedef struct {
    long long id;       // SDL finger ID (-1 = unused)
    float x, y;         // current normalised position
    bool  active;
} TouchFinger;

static TouchFinger gFingers[MAX_TOUCH_FINGERS];

// Needs currently active via touch (keyed by kNeed_ enum value)
// We store a bool per need; the main input code ORs KEYSTATE_ACTIVE_BIT
// if this is true.
#define NUM_NEEDS_MAX 64
static bool gTouchNeedActive[NUM_NEEDS_MAX];

// -------------------------------------------------------------------------
// Zone helpers
// -------------------------------------------------------------------------

// Returns true if a point is in the left D-pad area
#define DPAD_MAX_X  0.38f

// Returns the need triggered by a D-pad touch, or -1 if none.
// Within the D-pad area, we divide into 4 quadrants:
//   Up:    y < 0.35
//   Down:  y > 0.65
//   Left:  x < DPAD_MAX_X * 0.5 (left half of D-pad area)
//   Right: x > DPAD_MAX_X * 0.5 (right half)
// Corner overlaps are handled by allowing multiple directions at once.
static void GetDPadNeeds(float nx, float ny, bool out[NUM_NEEDS_MAX])
{
    // Normalise x within D-pad area (0..1)
    float dpad_x = nx / DPAD_MAX_X;

    bool left  = dpad_x < 0.35f;
    bool right = dpad_x > 0.65f;
    bool up    = ny < 0.35f;
    bool down  = ny > 0.65f;

    // Centre region → no direction
    if (!left && !right && !up && !down)
    {
        // Middle zone — could be left or right based on x bias
        if (dpad_x < 0.5f) left = true;
        else                right = true;
    }

    if (up)    out[kNeed_Up]    = true;
    if (down)  out[kNeed_Down]  = true;
    if (left)  out[kNeed_Left]  = true;
    if (right) out[kNeed_Right] = true;
}

// Action area (right side of screen, x > 0.42)
static void GetActionNeeds(float nx, float ny, bool out[NUM_NEEDS_MAX])
{
    // Normalise x within action area
    // Top portion (y < 0.33) → Next Weapon + Pause
    // Middle portion (0.33..0.67) → (nothing / optional)
    // Bottom portion (y > 0.67) → Attack + Prev Weapon
    //
    // Horizontally:
    //   Left part of action area → Pause (y<0.5) or Prev Weapon (y>0.5)
    //   Right part → Next Weapon (y<0.5) or Attack (y>0.5)

    float ax = (nx - 0.42f) / (1.0f - 0.42f);  // 0..1 within action area

    if (ny < 0.5f)
    {
        // Upper half of action area
        if (ax < 0.4f)
        {
            // Upper-left action: Pause
            out[kNeed_UIPause] = true;
        }
        else
        {
            // Upper-right action: Next Weapon
            out[kNeed_NextWeapon] = true;
        }
    }
    else
    {
        // Lower half of action area
        if (ax < 0.4f)
        {
            // Lower-left action: Prev Weapon
            out[kNeed_PrevWeapon] = true;
        }
        else
        {
            // Lower-right action: Attack
            out[kNeed_Attack] = true;
        }
    }
}

static void RecalcNeedsFromFingers(void)
{
    memset(gTouchNeedActive, 0, sizeof(gTouchNeedActive));

    for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
    {
        if (!gFingers[i].active) continue;

        float nx = gFingers[i].x;
        float ny = gFingers[i].y;

        if (nx < DPAD_MAX_X)
            GetDPadNeeds(nx, ny, gTouchNeedActive);
        else if (nx > 0.42f)
            GetActionNeeds(nx, ny, gTouchNeedActive);
    }
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

void TouchControls_ProcessEvent(int eventType, float fingerX, float fingerY, long long fingerId)
{
    if (eventType == SDL_EVENT_FINGER_DOWN)
    {
        // Find an empty slot
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
    // Nothing to do — state persists until finger up
}

#endif // __ANDROID__
