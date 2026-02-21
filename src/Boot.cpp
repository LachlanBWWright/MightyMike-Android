// MIGHTY MIKE ENTRY POINT
// (C) 2025 Iliyas Jorio
// This file is part of Mighty Mike. https://github.com/jorio/mightymike

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Pomme.h"
#include "PommeFiles.h"
#include "PommeInit.h"

extern "C"
{
	#include "externs.h"
	#include "renderdrivers.h"
	#include "framebufferfilter.h"
	#include "version.h"

	SDL_Window* gSDLWindow = nullptr;
	FSSpec gDataSpec;

	void GameMain(void);
}

#ifdef __ANDROID__
#include <filesystem>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

static const char* kAllDataFiles[] = {
	"Shapes/win.shapes",
	"Shapes/bonus.shapes",
	"Shapes/fairy1.shapes",
	"Shapes/view.shapes",
	"Shapes/fairy2.shapes",
	"Shapes/clown2.shapes",
	"Shapes/difficulty.shapes",
	"Shapes/title.shapes",
	"Shapes/bargain2.shapes",
	"Shapes/bargain1.shapes",
	"Shapes/clown1.shapes",
	"Shapes/infobar.shapes",
	"Shapes/jurassic1.shapes",
	"Shapes/jurassic2.shapes",
	"Shapes/main.shapes",
	"Shapes/weapon.shapes",
	"Shapes/overheadmap.shapes",
	"Shapes/infobar2.shapes",
	"Shapes/playerchoose.shapes",
	"Shapes/candy1.shapes",
	"Shapes/highscore.shapes",
	"Shapes/candy2.shapes",
	"Movies/Pangea.spin",
	"Audio/Music/IntroToCandyCane.aiff",
	"Audio/Music/LoseGame.aiff",
	"Audio/Music/IntroToEnteringWorlds.aiff",
	"Audio/Music/CarShopCartRace.aiff",
	"Audio/Music/PangeaIntro.aiff",
	"Audio/Music/PrehistoricPlaza.aiff",
	"Audio/Music/GamesGallery.aiff",
	"Audio/Music/IntroToGamesGallery.aiff",
	"Audio/Music/FairyTaleTrail.aiff",
	"Audio/Music/WinGame.aiff",
	"Audio/Music/WinGameLoop.aiff",
	"Audio/Music/WinHum.aiff",
	"Audio/Music/ClowningAround.aiff",
	"Audio/Music/MikeFinishLevel.aiff",
	"Audio/Music/IntroToFairyTale.aiff",
	"Audio/Music/CandyCaneLane.aiff",
	"Audio/Music/IntroToPrehistoric.aiff",
	"Audio/Music/IntroToClowning.aiff",
	"Audio/Music/MainTitleTheme.aiff",
	"Audio/Jurassic/DinoBoom.aiff",
	"Audio/Jurassic/BarneyBounce.aiff",
	"Audio/Jurassic/UngaBunga.aiff",
	"Audio/Jurassic/DoorOpen.aiff",
	"Audio/Bargain/SpaceShip.aiff",
	"Audio/Bargain/ExitShip.aiff",
	"Audio/Bargain/RobotDanger.aiff",
	"Audio/Bargain/DogRoar.aiff",
	"Audio/Bargain/DoorOpen.aiff",
	"Audio/Candy/CarmelMonster.aiff",
	"Audio/Candy/Hehehe.aiff",
	"Audio/Candy/BunnyHop.aiff",
	"Audio/Default/GetCoin.aiff",
	"Audio/Default/Squeek.aiff",
	"Audio/Default/MachineGun.aiff",
	"Audio/Default/TakeThat.aiff",
	"Audio/Default/Food.aiff",
	"Audio/Default/FreeDude.aiff",
	"Audio/Default/WeaponPickup.aiff",
	"Audio/Default/NoMoreNiceGuy.aiff",
	"Audio/Default/Pie.aiff",
	"Audio/Default/EatMyDust.aiff",
	"Audio/Default/MissleLaunch.aiff",
	"Audio/Default/PixieDust.aiff",
	"Audio/Default/BadHit.aiff",
	"Audio/Default/IllSaveYou.aiff",
	"Audio/Default/DeathScream.aiff",
	"Audio/Default/GetPOW.aiff",
	"Audio/Default/RadarEnter.aiff",
	"Audio/Default/Nuke.aiff",
	"Audio/Default/EnemyExplode.aiff",
	"Audio/Default/Heart.aiff",
	"Audio/Default/TracerShot.aiff",
	"Audio/Default/RubberGun.aiff",
	"Audio/Default/ComeHereRodent.aiff",
	"Audio/Default/HeatSeekBeew.aiff",
	"Audio/Default/Ouch.aiff",
	"Audio/Default/Splash.aiff",
	"Audio/Default/Pop.aiff",
	"Audio/Default/SelectChime.aiff",
	"Audio/Default/RifleShot.aiff",
	"Audio/Default/GetKey.aiff",
	"Audio/Default/FireInTheHole.aiff",
	"Audio/Default/SuctionCupPop.aiff",
	"Audio/Clown/ClownLaugh.aiff",
	"Audio/Clown/TireSkid.aiff",
	"Audio/Clown/JackInTheBox.aiff",
	"Audio/Clown/DoorOpen.aiff",
	"Audio/Fairy/Shriek.aiff",
	"Audio/Fairy/Frog.aiff",
	"Audio/Fairy/DoorOpen.aiff",
	"Audio/Fairy/Witch.aiff",
	"System/win3.txt",
	"System/win1.txt",
	"System/credits.txt",
	"System/win2.txt",
	"System/Application.rsrc",
	"System/gamecontrollerdb.txt",
	"Images/candyscene.tga",
	"Images/fairyscene.tga",
	"Images/border.tga",
	"Images/winbw.tga",
	"Images/border2.tga",
	"Images/clownscene.tga",
	"Images/viewppc.tga",
	"Images/diff.tga",
	"Images/playerchoose.tga",
	"Images/legal.tga",
	"Images/win.tga",
	"Images/view68k.tga",
	"Images/bargainscene.tga",
	"Images/charging.tga",
	"Images/overheadmap3.tga",
	"Images/titlepage.tga",
	"Images/radarmap.tga",
	"Images/border832.tga",
	"Images/bonus.tga",
	"Images/credits1.tga",
	"Images/overheadmap2.tga",
	"Images/overheadmap.tga",
	"Images/titlepagepp.tga",
	"Images/dinoscene.tga",
	"Images/head.tga",
	"Images/lose.tga",
	"Images/scores.tga",
	"Maps/bargain.map-3",
	"Maps/bargain.map-1",
	"Maps/jurassic.tileset",
	"Maps/fairy.map-3",
	"Maps/clown.map-2",
	"Maps/bargain.tileset",
	"Maps/candy.map-1",
	"Maps/jurassic.map-3",
	"Maps/fairy.map-2",
	"Maps/candy.map-3",
	"Maps/jurassic.map-2",
	"Maps/jurassic.map-1",
	"Maps/clown.map-3",
	"Maps/bargain.map-2",
	"Maps/fairy.tileset",
	"Maps/fairy.map-1",
	"Maps/candy.map-2",
	"Maps/clown.map-1",
	"Maps/clown.tileset",
	"Maps/candy.tileset",
	nullptr
};

static void MakeDir(const std::string& path)
{
	mkdir(path.c_str(), 0755);
}

static bool ExtractGameDataIfNeeded(const char* destBase)
{
	std::string versionStampPath = std::string(destBase) + "/.data_version";
	std::string dataDir = std::string(destBase) + "/Data";

	// Check if data already extracted with current version
	{
		SDL_IOStream* stamp = SDL_IOFromFile(versionStampPath.c_str(), "r");
		if (stamp)
		{
			char buf[64] = {};
			SDL_ReadIO(stamp, buf, sizeof(buf) - 1);
			SDL_CloseIO(stamp);
			if (strcmp(buf, GAME_VERSION) == 0)
			{
				SDL_Log("Game data already extracted (version %s)", GAME_VERSION);
				return true;
			}
		}
	}

	SDL_Log("Extracting game data to %s ...", dataDir.c_str());

	// Create top-level Data directory and subdirectories
	MakeDir(dataDir);
	MakeDir(dataDir + "/Shapes");
	MakeDir(dataDir + "/Movies");
	MakeDir(dataDir + "/Audio");
	MakeDir(dataDir + "/Audio/Music");
	MakeDir(dataDir + "/Audio/Jurassic");
	MakeDir(dataDir + "/Audio/Bargain");
	MakeDir(dataDir + "/Audio/Candy");
	MakeDir(dataDir + "/Audio/Default");
	MakeDir(dataDir + "/Audio/Clown");
	MakeDir(dataDir + "/Audio/Fairy");
	MakeDir(dataDir + "/System");
	MakeDir(dataDir + "/Images");
	MakeDir(dataDir + "/Maps");

	static const size_t kCopyBufSize = 65536;
	char* copyBuf = (char*)SDL_malloc(kCopyBufSize);
	if (!copyBuf)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate copy buffer");
		return false;
	}

	for (int i = 0; kAllDataFiles[i] != nullptr; i++)
	{
		const char* relPath = kAllDataFiles[i];
		std::string destPath = dataDir + "/" + relPath;

		// Open source from APK assets (relative path reads from APK on Android)
		SDL_IOStream* src = SDL_IOFromFile(relPath, "rb");
		if (!src)
		{
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open asset: %s", relPath);
			SDL_free(copyBuf);
			return false;
		}

		FILE* dst = fopen(destPath.c_str(), "wb");
		if (!dst)
		{
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create file: %s", destPath.c_str());
			SDL_CloseIO(src);
			SDL_free(copyBuf);
			return false;
		}

		Sint64 nRead;
		while ((nRead = SDL_ReadIO(src, copyBuf, kCopyBufSize)) > 0)
		{
			fwrite(copyBuf, 1, (size_t)nRead, dst);
		}

		fclose(dst);
		SDL_CloseIO(src);
	}

	SDL_free(copyBuf);

	// Write version stamp only after all files succeeded
	{
		SDL_IOStream* stamp = SDL_IOFromFile(versionStampPath.c_str(), "w");
		if (stamp)
		{
			SDL_WriteIO(stamp, GAME_VERSION, strlen(GAME_VERSION));
			SDL_CloseIO(stamp);
		}
	}

	SDL_Log("Game data extraction complete.");
	return true;
}
#endif // __ANDROID__

static fs::path FindGameData(const char* executablePath)
{
	fs::path dataPath;

#ifdef __ANDROID__
	(void)executablePath;
	const char* internalPath = SDL_GetAndroidInternalStoragePath();
	if (!internalPath)
		throw std::runtime_error("Couldn't get Android internal storage path.");

	if (!ExtractGameDataIfNeeded(internalPath))
		throw std::runtime_error("Failed to extract game data from APK.");

	dataPath = std::filesystem::path(internalPath) / "Data";

	gDataSpec = Pomme::Files::HostPathToFSSpec(dataPath / "System");

	auto applicationSpec = Pomme::Files::HostPathToFSSpec(dataPath / "System" / "Application");
	short resFileRefNum = FSpOpenResFile(&applicationSpec, fsRdPerm);
	if (resFileRefNum == -1)
		throw std::runtime_error("Couldn't open Application resource file.");

	UseResFile(resFileRefNum);
	return dataPath;
#else
	int attemptNum = 0;

#if !(__APPLE__)
	attemptNum++;		// skip macOS special case #0
#endif

	if (!executablePath)
		attemptNum = 2;

tryAgain:
	switch (attemptNum)
	{
		case 0:			// special case for macOS app bundles
			dataPath = executablePath;
			dataPath = dataPath.parent_path().parent_path() / "Resources";
			break;

		case 1:
			dataPath = executablePath;
			dataPath = dataPath.parent_path() / "Data";
			break;

		case 2:
			dataPath = "Data";
			break;

		default:
			throw std::runtime_error("Couldn't find the Data folder.");
	}

	attemptNum++;

	dataPath = dataPath.lexically_normal();

	// Set data spec -- Lets the game know where to find its asset files
	gDataSpec = Pomme::Files::HostPathToFSSpec(dataPath / "System");

	// Use application resource file
	auto applicationSpec = Pomme::Files::HostPathToFSSpec(dataPath / "System" / "Application");
	short resFileRefNum = FSpOpenResFile(&applicationSpec, fsRdPerm);

	if (resFileRefNum == -1)
	{
		goto tryAgain;
	}

	UseResFile(resFileRefNum);

	return dataPath;
#endif // __ANDROID__
}

static void Boot(int argc, char** argv)
{
	const char* executablePath = argc > 0 ? argv[0] : NULL;

	SDL_SetAppMetadata(GAME_FULL_NAME, GAME_VERSION, GAME_IDENTIFIER);
#if _DEBUG
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
#else
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
#endif

#ifdef __ANDROID__
	// Set HOME so Pomme can find preference files
	const char* internalPath = SDL_GetAndroidInternalStoragePath();
	if (internalPath && !getenv("HOME"))
	{
		setenv("HOME", internalPath, 1);
	}
	// Create ~/.config directory before Pomme initializes
	if (internalPath)
	{
		std::error_code ec;
		std::filesystem::create_directories(std::string(internalPath) + "/.config", ec);
		if (ec)
		{
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Couldn't create config dir: %s", ec.message().c_str());
		}
	}
#endif // __ANDROID__

	// Start our "machine"
	Pomme::Init();

	// Initialize SDL video subsystem
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		throw std::runtime_error("Couldn't initialize SDL video subsystem.");
	}

#if GLRENDER
	#ifdef __ANDROID__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	#endif
#endif // GLRENDER

	// Create window
	int windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if GLRENDER
	windowFlags |= SDL_WINDOW_OPENGL;
#endif
	gSDLWindow = SDL_CreateWindow(GAME_FULL_NAME " " GAME_VERSION, VISIBLE_WIDTH, VISIBLE_HEIGHT, windowFlags);
	if (!gSDLWindow)
		throw std::runtime_error("Couldn't create SDL window.");

#if GLRENDER
	GLRender_Init();
#else
	if (!SDLRender_Init())
		throw std::runtime_error("Couldn't create SDL renderer.");
#endif // GLRENDER

	// Find path to game data folder
	fs::path dataPath = FindGameData(executablePath);

	// Init joystick subsystem
	{
		SDL_Init(SDL_INIT_GAMEPAD);
		auto gamecontrollerdbPath8 = (dataPath / "System" / "gamecontrollerdb.txt").u8string();
		if (-1 == SDL_AddGamepadMappingsFromFile((const char*)gamecontrollerdbPath8.c_str()))
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, GAME_FULL_NAME, "Couldn't load gamecontrollerdb.txt!", gSDLWindow);
		}
	}
}

static void Shutdown()
{
	// Always restore the user's mouse acceleration before exiting.
	// SetMacLinearMouse(false);

	Pomme::Shutdown();

	if (gSDLWindow)
	{
		SDL_DestroyWindow(gSDLWindow);
		gSDLWindow = NULL;
	}

	SDL_Quit();
}

int main(int argc, char** argv)
{
	bool success = true;
	std::string uncaught = "";

	try
	{
		Boot(argc, argv);
		GameMain();
	}
	catch (Pomme::QuitRequest&)
	{
		// no-op, the game may throw this exception to shut us down cleanly
	}
#if !(_DEBUG) || defined(__ANDROID__)
	// In release builds (and always on Android), catch anything that might be thrown
	// so we can show an error dialog to the user.
	catch (std::exception& ex)		// Last-resort catch
	{
		success = false;
		uncaught = ex.what();
	}
	catch (...)						// Last-resort catch
	{
		success = false;
		uncaught = "unknown";
	}
#endif

	Shutdown();

	if (!success)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Uncaught exception: %s", uncaught.c_str());
		SDL_ShowSimpleMessageBox(0, GAME_FULL_NAME, uncaught.c_str(), nullptr);
	}

	return success ? 0 : 1;
}
