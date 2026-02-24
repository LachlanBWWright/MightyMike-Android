package io.jor.mightymike;

import org.libsdl.app.SDLActivity;

/**
 * Mighty Mike Android Activity.
 * Extends SDLActivity to configure SDL3's Android integration.
 * SDL3 renames main() to SDL_main() via SDL_main.h macro, so
 * getMainFunction() must return "SDL_main" (not "main").
 */
public class MightyMikeActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "main"
        };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }
}
