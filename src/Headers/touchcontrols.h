#pragma once

#ifdef __ANDROID__

#include <SDL3/SDL.h>
#include <stdbool.h>

void TouchControls_Init(void);
void TouchControls_HandleEvent(const SDL_Event* event);
void TouchControls_UpdateNeeds(void);
bool TouchControls_IsPressed(int needID);

#endif // __ANDROID__
