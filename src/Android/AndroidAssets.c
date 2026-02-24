// ANDROID ASSET EXTRACTION FOR MIGHTY MIKE
// Copies game data from APK assets to the app's internal storage.
//
// SDL_EnumerateDirectory uses POSIX opendir() on Android and therefore
// CANNOT enumerate APK asset paths.  Instead we keep a complete, explicit
// list of every game data file.  SDL_IOFromFile() with a relative path
// DOES read from the APK asset bundle on Android, so we use that for the
// actual byte-for-byte copy.

#ifdef __ANDROID__

#include "AndroidAssets.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "MightyMike", __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "MightyMike", __VA_ARGS__)

// Version file: if this file exists and contains our version, skip extraction.
// Bump this string whenever the Data/ directory contents change.
#define EXTRACT_VERSION_FILE  ".extract_version"
#define EXTRACT_VERSION       "3.0.3"

// -------------------------------------------------------------------------
// Complete list of all game data files, relative to the Data/ root.
// These are the exact paths that end up at the APK asset bundle root
// (because build.gradle.kts uses  assets.srcDirs("../../Data")).
// -------------------------------------------------------------------------
static const char *kAllDataFiles[] = {
    "Audio/Bargain/DogRoar.aiff",
    "Audio/Bargain/DoorOpen.aiff",
    "Audio/Bargain/ExitShip.aiff",
    "Audio/Bargain/RobotDanger.aiff",
    "Audio/Bargain/SpaceShip.aiff",
    "Audio/Candy/BunnyHop.aiff",
    "Audio/Candy/CarmelMonster.aiff",
    "Audio/Candy/Hehehe.aiff",
    "Audio/Clown/ClownLaugh.aiff",
    "Audio/Clown/DoorOpen.aiff",
    "Audio/Clown/JackInTheBox.aiff",
    "Audio/Clown/TireSkid.aiff",
    "Audio/Default/BadHit.aiff",
    "Audio/Default/ComeHereRodent.aiff",
    "Audio/Default/DeathScream.aiff",
    "Audio/Default/EatMyDust.aiff",
    "Audio/Default/EnemyExplode.aiff",
    "Audio/Default/FireInTheHole.aiff",
    "Audio/Default/Food.aiff",
    "Audio/Default/FreeDude.aiff",
    "Audio/Default/GetCoin.aiff",
    "Audio/Default/GetKey.aiff",
    "Audio/Default/GetPOW.aiff",
    "Audio/Default/Heart.aiff",
    "Audio/Default/HeatSeekBeew.aiff",
    "Audio/Default/IllSaveYou.aiff",
    "Audio/Default/MachineGun.aiff",
    "Audio/Default/MissleLaunch.aiff",
    "Audio/Default/NoMoreNiceGuy.aiff",
    "Audio/Default/Nuke.aiff",
    "Audio/Default/Ouch.aiff",
    "Audio/Default/Pie.aiff",
    "Audio/Default/PixieDust.aiff",
    "Audio/Default/Pop.aiff",
    "Audio/Default/RadarEnter.aiff",
    "Audio/Default/RifleShot.aiff",
    "Audio/Default/RubberGun.aiff",
    "Audio/Default/SelectChime.aiff",
    "Audio/Default/Splash.aiff",
    "Audio/Default/Squeek.aiff",
    "Audio/Default/SuctionCupPop.aiff",
    "Audio/Default/TakeThat.aiff",
    "Audio/Default/TracerShot.aiff",
    "Audio/Default/WeaponPickup.aiff",
    "Audio/Fairy/DoorOpen.aiff",
    "Audio/Fairy/Frog.aiff",
    "Audio/Fairy/Shriek.aiff",
    "Audio/Fairy/Witch.aiff",
    "Audio/Jurassic/BarneyBounce.aiff",
    "Audio/Jurassic/DinoBoom.aiff",
    "Audio/Jurassic/DoorOpen.aiff",
    "Audio/Jurassic/UngaBunga.aiff",
    "Audio/Music/CandyCaneLane.aiff",
    "Audio/Music/CarShopCartRace.aiff",
    "Audio/Music/ClowningAround.aiff",
    "Audio/Music/FairyTaleTrail.aiff",
    "Audio/Music/GamesGallery.aiff",
    "Audio/Music/IntroToCandyCane.aiff",
    "Audio/Music/IntroToClowning.aiff",
    "Audio/Music/IntroToEnteringWorlds.aiff",
    "Audio/Music/IntroToFairyTale.aiff",
    "Audio/Music/IntroToGamesGallery.aiff",
    "Audio/Music/IntroToPrehistoric.aiff",
    "Audio/Music/LoseGame.aiff",
    "Audio/Music/MainTitleTheme.aiff",
    "Audio/Music/MikeFinishLevel.aiff",
    "Audio/Music/PangeaIntro.aiff",
    "Audio/Music/PrehistoricPlaza.aiff",
    "Audio/Music/WinGame.aiff",
    "Audio/Music/WinGameLoop.aiff",
    "Audio/Music/WinHum.aiff",
    "Images/bargainscene.tga",
    "Images/bonus.tga",
    "Images/border.tga",
    "Images/border2.tga",
    "Images/border832.tga",
    "Images/candyscene.tga",
    "Images/charging.tga",
    "Images/clownscene.tga",
    "Images/credits1.tga",
    "Images/diff.tga",
    "Images/dinoscene.tga",
    "Images/fairyscene.tga",
    "Images/head.tga",
    "Images/legal.tga",
    "Images/lose.tga",
    "Images/overheadmap.tga",
    "Images/overheadmap2.tga",
    "Images/overheadmap3.tga",
    "Images/playerchoose.tga",
    "Images/radarmap.tga",
    "Images/scores.tga",
    "Images/titlepage.tga",
    "Images/titlepagepp.tga",
    "Images/view68k.tga",
    "Images/viewppc.tga",
    "Images/win.tga",
    "Images/winbw.tga",
    "Maps/bargain.map-1",
    "Maps/bargain.map-2",
    "Maps/bargain.map-3",
    "Maps/bargain.tileset",
    "Maps/candy.map-1",
    "Maps/candy.map-2",
    "Maps/candy.map-3",
    "Maps/candy.tileset",
    "Maps/clown.map-1",
    "Maps/clown.map-2",
    "Maps/clown.map-3",
    "Maps/clown.tileset",
    "Maps/fairy.map-1",
    "Maps/fairy.map-2",
    "Maps/fairy.map-3",
    "Maps/fairy.tileset",
    "Maps/jurassic.map-1",
    "Maps/jurassic.map-2",
    "Maps/jurassic.map-3",
    "Maps/jurassic.tileset",
    "Movies/Pangea.spin",
    "Shapes/bargain1.shapes",
    "Shapes/bargain2.shapes",
    "Shapes/bonus.shapes",
    "Shapes/candy1.shapes",
    "Shapes/candy2.shapes",
    "Shapes/clown1.shapes",
    "Shapes/clown2.shapes",
    "Shapes/difficulty.shapes",
    "Shapes/fairy1.shapes",
    "Shapes/fairy2.shapes",
    "Shapes/highscore.shapes",
    "Shapes/infobar.shapes",
    "Shapes/infobar2.shapes",
    "Shapes/jurassic1.shapes",
    "Shapes/jurassic2.shapes",
    "Shapes/main.shapes",
    "Shapes/overheadmap.shapes",
    "Shapes/playerchoose.shapes",
    "Shapes/title.shapes",
    "Shapes/view.shapes",
    "Shapes/weapon.shapes",
    "Shapes/win.shapes",
    "System/Application.rsrc",
    "System/credits.txt",
    "System/gamecontrollerdb.txt",
    "System/win1.txt",
    "System/win2.txt",
    "System/win3.txt",
    NULL
};

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

// Create every directory component of a file path.
static void MakeDirsFor(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

// Copy one file from the APK asset bundle to the filesystem.
// assetPath  - relative to the APK asset root (no leading /)
// destPath   - absolute filesystem destination
static bool ExtractOneFile(const char *assetPath, const char *destPath)
{
    // SDL_IOFromFile with a relative path opens directly from the APK assets
    // on Android (uses AAssetManager internally).
    SDL_IOStream *src = SDL_IOFromFile(assetPath, "rb");
    if (!src)
    {
        LOGE("Cannot open asset %s: %s", assetPath, SDL_GetError());
        return false;
    }

    MakeDirsFor(destPath);

    FILE *dst = fopen(destPath, "wb");
    if (!dst)
    {
        SDL_CloseIO(src);
        LOGE("Cannot create %s: %s", destPath, strerror(errno));
        return false;
    }

    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = SDL_ReadIO(src, buf, sizeof(buf))) > 0)
    {
        if (fwrite(buf, 1, n, dst) != n)
        {
            LOGE("Write error for %s", destPath);
            ok = false;
            break;
        }
    }

    fclose(dst);
    SDL_CloseIO(src);
    return ok;
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

bool Android_ExtractAssets(const char *destDir)
{
    // Check if already extracted with the current version.
    // The version file is only written after a complete successful extraction,
    // so a partial extraction (e.g. after a crash mid-way) will be retried.
    char versionFile[1024];
    snprintf(versionFile, sizeof(versionFile), "%s/%s", destDir, EXTRACT_VERSION_FILE);

    FILE *vf = fopen(versionFile, "r");
    if (vf)
    {
        char ver[64] = "";
        if (fgets(ver, sizeof(ver), vf))
        {
            size_t len = strlen(ver);
            while (len > 0 && (ver[len-1] == '\n' || ver[len-1] == '\r'))
                ver[--len] = '\0';

            if (strcmp(ver, EXTRACT_VERSION) == 0)
            {
                fclose(vf);
                LOGI("Assets already extracted (version %s)", EXTRACT_VERSION);
                return true;
            }
        }
        fclose(vf);
    }

    LOGI("Extracting game assets to %s ...", destDir);
    mkdir(destDir, 0755);

    int totalFiles = 0;
    int failedFiles = 0;

    for (int i = 0; kAllDataFiles[i] != NULL; i++)
    {
        char destPath[1024];
        snprintf(destPath, sizeof(destPath), "%s/%s", destDir, kAllDataFiles[i]);

        if (!ExtractOneFile(kAllDataFiles[i], destPath))
        {
            LOGE("Failed to extract %s", kAllDataFiles[i]);
            failedFiles++;
        }

        totalFiles++;
    }

    if (failedFiles > 0)
    {
        LOGE("Asset extraction: %d/%d files failed", failedFiles, totalFiles);
        return false;
    }

    // Write version stamp only after all files succeeded.
    vf = fopen(versionFile, "w");
    if (vf)
    {
        fputs(EXTRACT_VERSION "\n", vf);
        fclose(vf);
    }

    LOGI("Asset extraction complete: %d files extracted", totalFiles);
    return true;
}

#endif // __ANDROID__
