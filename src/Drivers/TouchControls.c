// TOUCH CONTROLS FOR ANDROID
// (C) 2025 Mighty Mike Android Port
// This file is part of Mighty Mike. https://github.com/jorio/mightymike

#ifdef __ANDROID__

#include <SDL3/SDL.h>
#include <math.h>

#include "myglobals.h"
#include "externs.h"
#include "structures.h"
#include "input.h"
#include "touchcontrols.h"

//------------------------------------------------------------
// Button indices
//------------------------------------------------------------
#define BTN_IDX_ATTACK    0   // kNeed_Attack
#define BTN_IDX_BACK      1   // kNeed_UIBack / kNeed_UIPrev
#define BTN_IDX_PREV      2   // kNeed_PrevWeapon
#define BTN_IDX_NEXT      3   // kNeed_NextWeapon
#define BTN_IDX_PAUSE     4   // kNeed_UIPause
#define BTN_IDX_RADAR     5   // kNeed_Radar
#define BTN_IDX_MUSIC     6   // kNeed_ToggleMusic
// NUM_BUTTONS is defined in touchcontrols.h

//------------------------------------------------------------
// Layout constants (fractions of screen dimensions)
//------------------------------------------------------------

#define MAX_TOUCH_POINTS        10

// Joystick (left side)
#define JOYSTICK_RADIUS_FRAC    0.12f
#define JOYSTICK_CX_FRAC        0.15f
#define JOYSTICK_CY_FRAC        0.75f
#define JOYSTICK_DEAD_ZONE      0.08f   // fraction of joystick radius

// Button radii (fraction of screen height)
#define BTN_RADIUS_FRAC         0.09f   // main action buttons
#define BTN_SMALL_RADIUS_FRAC   0.055f  // small utility buttons (pause, music)

// Button positions (cx, cy as fractions of screen w/h)
#define BTN_A_CX_FRAC       0.88f   // Attack
#define BTN_A_CY_FRAC       0.72f
#define BTN_B_CX_FRAC       0.73f   // Back
#define BTN_B_CY_FRAC       0.72f
#define BTN_PREV_CX_FRAC    0.73f   // PrevWeapon
#define BTN_PREV_CY_FRAC    0.88f
#define BTN_NEXT_CX_FRAC    0.88f   // NextWeapon
#define BTN_NEXT_CY_FRAC    0.88f
#define BTN_PAUSE_CX_FRAC   0.96f   // Pause (small, top-right corner)
#define BTN_PAUSE_CY_FRAC   0.08f
#define BTN_RADAR_CX_FRAC   0.80f   // Radar (top of diamond)
#define BTN_RADAR_CY_FRAC   0.55f
#define BTN_MUSIC_CX_FRAC   0.87f   // Music (small, near Pause)
#define BTN_MUSIC_CY_FRAC   0.08f

//------------------------------------------------------------
// State
//------------------------------------------------------------

typedef struct
{
	SDL_FingerID    fingerID;
	bool            active;
	bool            isJoystick;   // true if this touch started in the joystick zone
	float           startX, startY;
	float           currentX, currentY;
} TouchPoint;

static TouchPoint   gTouchPoints[MAX_TOUCH_POINTS];
static float        gJoystickDX = 0, gJoystickDY = 0;   // normalized -1..1
static bool         gJoystickFingerActive = false;

static bool gBtnStates[NUM_BUTTONS];

//------------------------------------------------------------

static int gScreenW = 0, gScreenH = 0;

static void UpdateScreenDimensions(void)
{
	SDL_GetWindowSizeInPixels(gSDLWindow, &gScreenW, &gScreenH);
	if (gScreenW <= 0) gScreenW = 640;
	if (gScreenH <= 0) gScreenH = 480;
}

static bool IsInJoystickZone(float x, float y)
{
	(void)y;
	UpdateScreenDimensions();
	return x < gScreenW * 0.4f;
}

// Returns true if (x,y) is within radius r of (cx,cy)
static bool IsInCircle(float x, float y, float cx, float cy, float r)
{
	float dx = x - cx, dy = y - cy;
	return dx*dx + dy*dy < r*r;
}

static void UpdateButtonStates(void)
{
	for (int i = 0; i < NUM_BUTTONS; i++)
		gBtnStates[i] = false;

	UpdateScreenDimensions();
	float sw = (float)gScreenW, sh = (float)gScreenH;
	float mainR  = sh * BTN_RADIUS_FRAC * 1.5f;
	float smallR = sh * BTN_SMALL_RADIUS_FRAC * 1.5f;

	float btnCX[NUM_BUTTONS] = {
		sw * BTN_A_CX_FRAC,
		sw * BTN_B_CX_FRAC,
		sw * BTN_PREV_CX_FRAC,
		sw * BTN_NEXT_CX_FRAC,
		sw * BTN_PAUSE_CX_FRAC,
		sw * BTN_RADAR_CX_FRAC,
		sw * BTN_MUSIC_CX_FRAC,
	};
	float btnCY[NUM_BUTTONS] = {
		sh * BTN_A_CY_FRAC,
		sh * BTN_B_CY_FRAC,
		sh * BTN_PREV_CY_FRAC,
		sh * BTN_NEXT_CY_FRAC,
		sh * BTN_PAUSE_CY_FRAC,
		sh * BTN_RADAR_CY_FRAC,
		sh * BTN_MUSIC_CY_FRAC,
	};
	float btnR[NUM_BUTTONS] = {
		mainR, mainR, mainR, mainR, smallR, mainR, smallR,
	};

	for (int i = 0; i < MAX_TOUCH_POINTS; i++)
	{
		if (!gTouchPoints[i].active)
			continue;
		float x = gTouchPoints[i].currentX;
		float y = gTouchPoints[i].currentY;
		for (int b = 0; b < NUM_BUTTONS; b++)
		{
			if (IsInCircle(x, y, btnCX[b], btnCY[b], btnR[b]))
				gBtnStates[b] = true;
		}
	}
}

// Derive joystick state entirely from gTouchPoints[].isJoystick slots.
// This avoids all fingerID-mismatch issues: once a slot is marked isJoystick,
// the joystick tracks that slot until it goes inactive, regardless of fingerID.
static void UpdateJoystick(void)
{
	UpdateScreenDimensions();
	float radius = gScreenH * JOYSTICK_RADIUS_FRAC;

	for (int i = 0; i < MAX_TOUCH_POINTS; i++)
	{
		if (!gTouchPoints[i].active || !gTouchPoints[i].isJoystick)
			continue;

		float dx = gTouchPoints[i].currentX - gTouchPoints[i].startX;
		float dy = gTouchPoints[i].currentY - gTouchPoints[i].startY;
		float dist = sqrtf(dx*dx + dy*dy);

		if (dist < radius * JOYSTICK_DEAD_ZONE)
		{
			gJoystickDX = 0;
			gJoystickDY = 0;
		}
		else
		{
			float normalizer = (dist > radius) ? dist : radius;
			gJoystickDX = dx / normalizer;
			gJoystickDY = dy / normalizer;
		}
		gJoystickFingerActive = true;
		return;
	}

	// No active joystick touch found
	gJoystickFingerActive = false;
	gJoystickDX = 0;
	gJoystickDY = 0;
}

//------------------------------------------------------------
// Public API
//------------------------------------------------------------

void TouchControls_Init(void)
{
	SDL_memset(gTouchPoints, 0, sizeof(gTouchPoints));
	SDL_memset(gBtnStates, 0, sizeof(gBtnStates));
	gJoystickFingerActive = false;
	gJoystickDX = gJoystickDY = 0;
}

void TouchControls_HandleEvent(const SDL_Event* event)
{
	switch (event->type)
	{
	case SDL_EVENT_FINGER_DOWN:
	{
		UpdateScreenDimensions();
		float px = event->tfinger.x * gScreenW;
		float py = event->tfinger.y * gScreenH;

		// Check whether a joystick slot is already active
		bool hasJoystick = false;
		for (int j = 0; j < MAX_TOUCH_POINTS; j++)
			if (gTouchPoints[j].active && gTouchPoints[j].isJoystick)
				{ hasJoystick = true; break; }

		for (int i = 0; i < MAX_TOUCH_POINTS; i++)
		{
			if (!gTouchPoints[i].active)
			{
				gTouchPoints[i].active    = true;
				gTouchPoints[i].fingerID  = event->tfinger.fingerID;
				gTouchPoints[i].isJoystick = IsInJoystickZone(px, py) && !hasJoystick;
				gTouchPoints[i].startX    = px;
				gTouchPoints[i].startY    = py;
				gTouchPoints[i].currentX  = px;
				gTouchPoints[i].currentY  = py;
				break;
			}
		}
		break;
	}

	case SDL_EVENT_FINGER_MOTION:
	{
		UpdateScreenDimensions();
		float px = event->tfinger.x * gScreenW;
		float py = event->tfinger.y * gScreenH;

		for (int i = 0; i < MAX_TOUCH_POINTS; i++)
		{
			if (gTouchPoints[i].active && gTouchPoints[i].fingerID == event->tfinger.fingerID)
			{
				gTouchPoints[i].currentX = px;
				gTouchPoints[i].currentY = py;
				break;
			}
		}
		break;
	}

	case SDL_EVENT_FINGER_UP:
	{
		UpdateScreenDimensions();
		float px = event->tfinger.x * gScreenW;
		float py = event->tfinger.y * gScreenH;

		// Primary: match by fingerID (the normal path)
		bool found = false;
		for (int i = 0; i < MAX_TOUCH_POINTS; i++)
		{
			if (gTouchPoints[i].active && gTouchPoints[i].fingerID == event->tfinger.fingerID)
			{
				gTouchPoints[i].active     = false;
				gTouchPoints[i].isJoystick = false;
				found = true;
				break;
			}
		}

		// Fallback: some Android versions report a different fingerID in FINGER_UP
		// (the "zombie touch" bug).  Clear the nearest active slot so the array
		// never fills up with phantom touches.
		if (!found)
		{
			float minDist = 1e12f;
			int   bestIdx = -1;
			for (int i = 0; i < MAX_TOUCH_POINTS; i++)
			{
				if (!gTouchPoints[i].active) continue;
				float dx = gTouchPoints[i].currentX - px;
				float dy = gTouchPoints[i].currentY - py;
				float d2 = dx*dx + dy*dy;
				if (d2 < minDist) { minDist = d2; bestIdx = i; }
			}
			if (bestIdx >= 0)
			{
				gTouchPoints[bestIdx].active     = false;
				gTouchPoints[bestIdx].isJoystick = false;
			}
		}
		break;
	}
	}
}

void TouchControls_UpdateNeeds(void)
{
	UpdateJoystick();
	UpdateButtonStates();
}

bool TouchControls_IsPressed(int needID)
{
	switch (needID)
	{
	case kNeed_Up:
	case kNeed_UIUp:
		return gJoystickDY < -0.3f;

	case kNeed_Down:
	case kNeed_UIDown:
		return gJoystickDY > 0.3f;

	case kNeed_Left:
	case kNeed_UILeft:
		return gJoystickDX < -0.3f;

	case kNeed_Right:
	case kNeed_UIRight:
		return gJoystickDX > 0.3f;

	case kNeed_Attack:
	case kNeed_UIConfirm:
	case kNeed_UINext:
		return gBtnStates[BTN_IDX_ATTACK];

	case kNeed_UIBack:
	case kNeed_UIPrev:
		return gBtnStates[BTN_IDX_BACK];

	case kNeed_UIPause:
		return gBtnStates[BTN_IDX_PAUSE];

	case kNeed_PrevWeapon:
		return gBtnStates[BTN_IDX_PREV];

	case kNeed_NextWeapon:
		return gBtnStates[BTN_IDX_NEXT];

	case kNeed_Radar:
		return gBtnStates[BTN_IDX_RADAR];

	case kNeed_ToggleMusic:
		return gBtnStates[BTN_IDX_MUSIC];

	default:
		return false;
	}
}

extern void GLRender_DrawTouchControlsOverlay(float screenW, float screenH,
                                               float joyCX, float joyCY, float joyR,
                                               float joyThumbX, float joyThumbY, bool joyActive,
                                               float btn[NUM_BUTTONS][3], bool btnPressed[NUM_BUTTONS]);

void TouchControls_DrawOverlay(void)
{
	UpdateScreenDimensions();
	float sw = (float)gScreenW;
	float sh = (float)gScreenH;

	float joyR  = sh * JOYSTICK_RADIUS_FRAC;
	float joyCX = sw * JOYSTICK_CX_FRAC;
	float joyCY = sh * JOYSTICK_CY_FRAC;

	// Clamp thumb offset to joystick radius so it never leaves the ring visually
	float thumbDX = gJoystickDX * joyR;
	float thumbDY = gJoystickDY * joyR;
	float thumbDist = sqrtf(thumbDX*thumbDX + thumbDY*thumbDY);
	if (thumbDist > joyR)
	{
		thumbDX = thumbDX / thumbDist * joyR;
		thumbDY = thumbDY / thumbDist * joyR;
	}
	float thumbX = joyCX + thumbDX;
	float thumbY = joyCY + thumbDY;

	float mainR  = sh * BTN_RADIUS_FRAC;
	float smallR = sh * BTN_SMALL_RADIUS_FRAC;

	// btn[i] = { cx, cy, radius }
	float btn[NUM_BUTTONS][3] = {
		{ sw * BTN_A_CX_FRAC,     sh * BTN_A_CY_FRAC,     mainR  },   // Attack
		{ sw * BTN_B_CX_FRAC,     sh * BTN_B_CY_FRAC,     mainR  },   // Back
		{ sw * BTN_PREV_CX_FRAC,  sh * BTN_PREV_CY_FRAC,  mainR  },   // PrevWeapon
		{ sw * BTN_NEXT_CX_FRAC,  sh * BTN_NEXT_CY_FRAC,  mainR  },   // NextWeapon
		{ sw * BTN_PAUSE_CX_FRAC, sh * BTN_PAUSE_CY_FRAC, smallR },   // Pause
		{ sw * BTN_RADAR_CX_FRAC, sh * BTN_RADAR_CY_FRAC, mainR  },   // Radar
		{ sw * BTN_MUSIC_CX_FRAC, sh * BTN_MUSIC_CY_FRAC, smallR },   // Music
	};

	GLRender_DrawTouchControlsOverlay(sw, sh,
		joyCX, joyCY, joyR,
		thumbX, thumbY, gJoystickFingerActive,
		btn, gBtnStates);
}

#endif // __ANDROID__
