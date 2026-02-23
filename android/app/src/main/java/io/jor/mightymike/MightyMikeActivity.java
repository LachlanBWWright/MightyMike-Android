package io.jor.mightymike;

import android.content.pm.ActivityInfo;
import android.os.Bundle;
import org.libsdl.app.SDLActivity;

public class MightyMikeActivity extends SDLActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    /**
     * Intercepts SDL3's orientation management to enforce landscape mode unconditionally.
     * SDL3's SDLActivity calls this from native code (JNI) when creating the window.
     * Without this override, SDL3 may calculate and set an unexpected orientation even
     * when SDL_HINT_ORIENTATIONS is set correctly, because the hint is read asynchronously
     * after the Java activity lifecycle has already started.
     *
     * @param w        window width (unused - we always enforce landscape)
     * @param h        window height (unused - we always enforce landscape)
     * @param resizable whether the window is resizable (unused)
     * @param hint     the SDL_HINT_ORIENTATIONS value (unused - we always enforce landscape)
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected String getMainFunction() {
        return "SDL_main";
    }
}
