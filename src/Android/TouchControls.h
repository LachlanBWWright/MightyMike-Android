// ANDROID TOUCH CONTROLS FOR MIGHTY MIKE
// Simple region-based touch controls that inject into the needs system.
//
// Screen layout (landscape):
//   LEFT 40%  : Virtual D-pad (4 regions)
//   RIGHT 60% : Action buttons
//     - lower right        : Attack
//     - upper right        : Next Weapon
//     - lower right-middle : Prev Weapon
//     - upper left         : Pause / back

#pragma once

#ifdef __ANDROID__

#ifdef __cplusplus
extern "C" {
#endif

// Must be called once per frame BEFORE the main needs loop
// to update the virtual button states.
// fingerX, fingerY are in normalised coordinates [0..1].
void TouchControls_ProcessEvent(int eventType, float fingerX, float fingerY, long long fingerId);

// Returns the OR-mask of KEYSTATE_ACTIVE_BIT for needs that are
// currently "pressed" by touch.
int TouchControls_GetNeedActive(int need);

// Must be called once per frame at the END of the needs loop to
// reset any single-frame bits.
void TouchControls_PostFrame(void);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
