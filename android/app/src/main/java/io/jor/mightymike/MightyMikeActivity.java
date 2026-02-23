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
     * Overrides SDL3's orientation management entirely.
     * SDL3's SDLActivity.setOrientationBis() calls setRequestedOrientation() based on
     * the SDL_HINT_ORIENTATIONS value. We intercept this here and always enforce
     * SENSOR_LANDSCAPE regardless of what SDL calculates, ensuring the app can never
     * enter portrait mode even if SDL's logic produces an unexpected result.
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
