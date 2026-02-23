package io.jor.mightymike;

import android.content.pm.ActivityInfo;
import android.os.Bundle;
import org.libsdl.app.SDLActivity;

public class MightyMikeActivity extends SDLActivity {
    private void forceLandscape() {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        forceLandscape();
    }

    @Override
    protected void onStart() {
        super.onStart();
        forceLandscape();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Re-apply after SDL's SDLActivity.onResume() may have reset the orientation.
        forceLandscape();
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
