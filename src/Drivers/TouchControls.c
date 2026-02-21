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
// Layout constants (in display pixels, scaled by screen size)
//------------------------------------------------------------

#define MAX_TOUCH_POINTS		10

// Joystick is on the left side
#define JOYSTICK_RADIUS_FRAC	0.12f   // radius as fraction of screen height
#define JOYSTICK_CX_FRAC		0.15f   // center X as fraction of screen width
#define JOYSTICK_CY_FRAC		0.75f   // center Y as fraction of screen height
#define JOYSTICK_DEAD_ZONE		0.25f   // dead zone fraction of joystick radius

// Right-side buttons
#define BTN_RADIUS_FRAC			0.07f   // radius as fraction of screen height

// Button positions as fractions of screen (x=right, y=top)
// Diamond layout: A=right, B=bottom
#define BTN_A_CX_FRAC		0.90f   // Attack / Confirm
#define BTN_A_CY_FRAC		0.65f
#define BTN_B_CX_FRAC		0.82f   // Back
#define BTN_B_CY_FRAC		0.75f
#define BTN_PREV_CX_FRAC	0.75f   // Previous weapon
#define BTN_PREV_CY_FRAC	0.85f
#define BTN_NEXT_CX_FRAC	0.90f   // Next weapon
#define BTN_NEXT_CY_FRAC	0.85f
#define BTN_PAUSE_CX_FRAC	0.95f   // Pause
#define BTN_PAUSE_CY_FRAC	0.08f

//------------------------------------------------------------
// State
//------------------------------------------------------------

typedef struct
{
	SDL_FingerID    fingerID;
	bool            active;
	float           startX, startY;
	float           currentX, currentY;
} TouchPoint;

static TouchPoint   gTouchPoints[MAX_TOUCH_POINTS];
static float        gJoystickDX = 0, gJoystickDY = 0;   // normalized -1..1

// Which finger is driving the joystick (-1 = none)
static SDL_FingerID gJoystickFinger = 0;
static bool         gJoystickFingerActive = false;

// Button pressed states
static bool gBtnAttack    = false;
static bool gBtnBack      = false;
static bool gBtnPause     = false;
static bool gBtnPrevWeapon = false;
static bool gBtnNextWeapon = false;

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
	(void)y;  // y not needed: joystick zone is the left 40% of screen width
	UpdateScreenDimensions();
	// Left 40% of screen
	return x < gScreenW * 0.4f;
}

static bool IsInButtonA(float x, float y)
{
	UpdateScreenDimensions();
	float cx = gScreenW * BTN_A_CX_FRAC;
	float cy = gScreenH * BTN_A_CY_FRAC;
	float r  = gScreenH * BTN_RADIUS_FRAC * 1.5f;
	float dx = x - cx, dy = y - cy;
	return dx*dx + dy*dy < r*r;
}

static bool IsInButtonB(float x, float y)
{
	UpdateScreenDimensions();
	float cx = gScreenW * BTN_B_CX_FRAC;
	float cy = gScreenH * BTN_B_CY_FRAC;
	float r  = gScreenH * BTN_RADIUS_FRAC * 1.5f;
	float dx = x - cx, dy = y - cy;
	return dx*dx + dy*dy < r*r;
}

static bool IsInButtonPause(float x, float y)
{
	UpdateScreenDimensions();
	float cx = gScreenW * BTN_PAUSE_CX_FRAC;
	float cy = gScreenH * BTN_PAUSE_CY_FRAC;
	float r  = gScreenH * BTN_RADIUS_FRAC * 1.5f;
	float dx = x - cx, dy = y - cy;
	return dx*dx + dy*dy < r*r;
}

static bool IsInButtonPrevWeapon(float x, float y)
{
	UpdateScreenDimensions();
	float cx = gScreenW * BTN_PREV_CX_FRAC;
	float cy = gScreenH * BTN_PREV_CY_FRAC;
	float r  = gScreenH * BTN_RADIUS_FRAC * 1.5f;
	float dx = x - cx, dy = y - cy;
	return dx*dx + dy*dy < r*r;
}

static bool IsInButtonNextWeapon(float x, float y)
{
	UpdateScreenDimensions();
	float cx = gScreenW * BTN_NEXT_CX_FRAC;
	float cy = gScreenH * BTN_NEXT_CY_FRAC;
	float r  = gScreenH * BTN_RADIUS_FRAC * 1.5f;
	float dx = x - cx, dy = y - cy;
	return dx*dx + dy*dy < r*r;
}

static void UpdateButtonStates(void)
{
	gBtnAttack     = false;
	gBtnBack       = false;
	gBtnPause      = false;
	gBtnPrevWeapon = false;
	gBtnNextWeapon = false;

	for (int i = 0; i < MAX_TOUCH_POINTS; i++)
	{
		if (!gTouchPoints[i].active)
			continue;
		float x = gTouchPoints[i].currentX;
		float y = gTouchPoints[i].currentY;
		if (IsInButtonA(x, y))          gBtnAttack = true;
		if (IsInButtonB(x, y))          gBtnBack   = true;
		if (IsInButtonPause(x, y))      gBtnPause  = true;
		if (IsInButtonPrevWeapon(x, y)) gBtnPrevWeapon = true;
		if (IsInButtonNextWeapon(x, y)) gBtnNextWeapon = true;
	}
}

static void UpdateJoystick(void)
{
	if (!gJoystickFingerActive)
	{
		gJoystickDX = 0;
		gJoystickDY = 0;
		return;
	}

	UpdateScreenDimensions();
	float radius = gScreenH * JOYSTICK_RADIUS_FRAC;

	// Find the active joystick finger
	for (int i = 0; i < MAX_TOUCH_POINTS; i++)
	{
		if (!gTouchPoints[i].active || gTouchPoints[i].fingerID != gJoystickFinger)
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
		return;
	}

	// Finger not found (shouldn't happen)
	gJoystickDX = 0;
	gJoystickDY = 0;
}

//------------------------------------------------------------
// Public API
//------------------------------------------------------------

void TouchControls_Init(void)
{
	SDL_memset(gTouchPoints, 0, sizeof(gTouchPoints));
	gJoystickFingerActive = false;
	gJoystickDX = gJoystickDY = 0;
	gBtnAttack = gBtnBack = gBtnPause = gBtnPrevWeapon = gBtnNextWeapon = false;
}

void TouchControls_HandleEvent(const SDL_Event* event)
{
	switch (event->type)
	{
	case SDL_EVENT_FINGER_DOWN:
	{
		// Convert normalized SDL finger coords to pixels
		UpdateScreenDimensions();
		float px = event->tfinger.x * gScreenW;
		float py = event->tfinger.y * gScreenH;

		// Find a free slot
		for (int i = 0; i < MAX_TOUCH_POINTS; i++)
		{
			if (!gTouchPoints[i].active)
			{
				gTouchPoints[i].active   = true;
				gTouchPoints[i].fingerID = event->tfinger.fingerID;
				gTouchPoints[i].startX   = px;
				gTouchPoints[i].startY   = py;
				gTouchPoints[i].currentX = px;
				gTouchPoints[i].currentY = py;

				if (IsInJoystickZone(px, py) && !gJoystickFingerActive)
				{
					gJoystickFinger       = event->tfinger.fingerID;
					gJoystickFingerActive = true;
				}
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

		for (int i = 0; i < MAX_TOUCH_POINTS; i++)
		{
			if (gTouchPoints[i].active && gTouchPoints[i].fingerID == event->tfinger.fingerID)
			{
				gTouchPoints[i].currentX = px;
				gTouchPoints[i].currentY = py;
				gTouchPoints[i].active = false;

				if (gJoystickFingerActive && gJoystickFinger == event->tfinger.fingerID)
				{
					gJoystickFingerActive = false;
				}
				break;
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
		return gBtnAttack;

	case kNeed_UIBack:
	case kNeed_UIPrev:
		return gBtnBack;

	case kNeed_UIPause:
		return gBtnPause;

	case kNeed_PrevWeapon:
		return gBtnPrevWeapon;

	case kNeed_NextWeapon:
		return gBtnNextWeapon;

	default:
		return false;
	}
}

extern void GLRender_DrawTouchControlsOverlay(float screenW, float screenH,
                                               float joyCX, float joyCY, float joyR,
                                               float joyThumbX, float joyThumbY, bool joyActive,
                                               float btn[5][2], float btnR, bool btnPressed[5]);

void TouchControls_DrawOverlay(void)
{
	UpdateScreenDimensions();
	float sw = (float)gScreenW;
	float sh = (float)gScreenH;

	float joyR  = sh * JOYSTICK_RADIUS_FRAC;
	float joyCX = sw * JOYSTICK_CX_FRAC;
	float joyCY = sh * JOYSTICK_CY_FRAC;

	// Compute thumb position
	float thumbX = joyCX + gJoystickDX * joyR;
	float thumbY = joyCY + gJoystickDY * joyR;

	float btnR = sh * BTN_RADIUS_FRAC;
	float btn[5][2] = {
		{ sw * BTN_A_CX_FRAC,    sh * BTN_A_CY_FRAC    },
		{ sw * BTN_B_CX_FRAC,    sh * BTN_B_CY_FRAC    },
		{ sw * BTN_PREV_CX_FRAC, sh * BTN_PREV_CY_FRAC },
		{ sw * BTN_NEXT_CX_FRAC, sh * BTN_NEXT_CY_FRAC },
		{ sw * BTN_PAUSE_CX_FRAC,sh * BTN_PAUSE_CY_FRAC},
	};
	bool pressed[5] = { gBtnAttack, gBtnBack, gBtnPrevWeapon, gBtnNextWeapon, gBtnPause };

	GLRender_DrawTouchControlsOverlay(sw, sh,
		joyCX, joyCY, joyR,
		thumbX, thumbY, gJoystickFingerActive,
		btn, btnR, pressed);
}

#endif // __ANDROID__
