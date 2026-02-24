// ANDROID TOUCH CONTROLS FOR MIGHTY MIKE
// Screen layout (landscape):
//   Bottom-left : Virtual D-pad (circular base with 4 directional arms)
//   Bottom-right: Action buttons (Attack large, Weapon-cycle small)
//   Top-right   : Pause button (small)

#pragma once

#ifdef __ANDROID__

// -------------------------------------------------------------------------
// Shared position constants (normalised screen coords [0..1], landscape).
// Both hit-testing (TouchControls.c) and visual rendering (GLRender.c)
// use these so the button visuals always match the touch zones.
// -------------------------------------------------------------------------

// D-pad – cross-shaped hit zones centred at (TC_DPAD_CX, TC_DPAD_CY).
// Each arm extends TC_DPAD_ARM_L from the centre; arm half-width is TC_DPAD_ARM_HW.
#define TC_DPAD_CX       0.15f
#define TC_DPAD_CY       0.68f
#define TC_DPAD_ARM_HW   0.055f   // half-width of each arm
#define TC_DPAD_ARM_L    0.145f   // length of each arm from centre

// Attack button – large circle, bottom-right.
#define TC_ATK_CX   0.82f
#define TC_ATK_CY   0.74f
#define TC_ATK_R    0.09f

// Next Weapon – medium circle, upper-right.
#define TC_NW_CX    0.73f
#define TC_NW_CY    0.52f
#define TC_NW_R     0.08f

// Prev Weapon – medium circle, to the right of NextWeapon.
#define TC_PW_CX    0.88f
#define TC_PW_CY    0.52f
#define TC_PW_R     0.08f

// Pause – small circle, top-right corner.
#define TC_PAUSE_CX  0.93f
#define TC_PAUSE_CY  0.08f
#define TC_PAUSE_R   0.065f

#ifdef __cplusplus
extern "C" {
#endif

// Process a single SDL finger event.  fingerX/Y are normalised [0..1].
void TouchControls_ProcessEvent(int eventType, float fingerX, float fingerY, long long fingerId);

// Returns non-zero if the given kNeed_* is currently active via touch.
int TouchControls_GetNeedActive(int need);

// Call once per frame at the END of the needs loop.
void TouchControls_PostFrame(void);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
