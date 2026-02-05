#include "../../include/client/ui/App.hpp"
#include <raylib.h>
#include <cmath>
#include <vector>
#include <string>
using namespace client::ui;

static void drawStarfield(float t) {
    for (int i = 0; i < 300; ++i) {
        float x = std::fmod((i * 73 + t * 60), (float)GetScreenWidth());
        float y = (i * 37) % GetScreenHeight();
        DrawPixel((int)x, (int)y, (i % 7 == 0) ? RAYWHITE : DARKGRAY);
    }
}

App::App() : _screen(ScreenState::Menu) {}

App::~App() {
    cleanupAudio();
}

void App::initAudio() {
    InitAudioDevice();

    if (!IsAudioDeviceReady()) {
        Screens::logMessage("ERROR: Audio device failed to initialize!", "ERROR");
        return;
    }

    Screens::logMessage("Audio device initialized successfully");
    SetMasterVolume(1.0f); // Set master volume to 100%

    // Try multiple paths for the background music
    std::vector<std::string> musicPaths = {
        "sound/Skeleton-Dance.mp3",
        "client/sound/Skeleton-Dance.mp3",
        "../client/sound/Skeleton-Dance.mp3",
        "../../client/sound/Skeleton-Dance.mp3"
    };

    for (const auto& musicPath : musicPaths) {
        Screens::logMessage("Trying music path: " + musicPath);
        if (FileExists(musicPath.c_str())) {
            Screens::logMessage("File found! Loading music stream...");
            _backgroundMusic = LoadMusicStream(musicPath.c_str());

            if (_backgroundMusic.stream.buffer == nullptr) {
                Screens::logMessage("ERROR: Failed to load music stream", "ERROR");
                continue;
            }

            _musicLoaded = true;
            _backgroundMusic.looping = true; // Enable looping
            SetMusicVolume(_backgroundMusic, 0.3f); // Set to 30% volume (reduced from 50%)
            PlayMusicStream(_backgroundMusic);

            Screens::logMessage("✓ Background music loaded and playing from: " + musicPath);
            Screens::logMessage("  Music duration: " + std::to_string(GetMusicTimeLength(_backgroundMusic)) + " seconds");
            return;
        }
    }

    Screens::logMessage("WARNING: Background music file not found (tried multiple paths)", "WARN");
}

void App::updateAudio() {
    if (_musicLoaded) {
        UpdateMusicStream(_backgroundMusic);

        // Check if music has stopped and restart it (loop)
        if (!IsMusicStreamPlaying(_backgroundMusic)) {
            PlayMusicStream(_backgroundMusic);
        }
    }
}

void App::cleanupAudio() {
    if (_musicLoaded) {
        StopMusicStream(_backgroundMusic);
        UnloadMusicStream(_backgroundMusic);
        _musicLoaded = false;
    }
    CloseAudioDevice();
}

void App::setAutoConnect(const std::string& host, const std::string& port, const std::string& name) {
    _form.serverAddress = host;
    _form.serverPort = port;
    _form.username = name;
    _autoConnectPending = true;
    _screen = ScreenState::Multiplayer;
}

void App::run() {
    const int screenWidth = 960;
    const int screenHeight = 540;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "R-Type Client");
    // Prevent ESC from closing the whole window; we handle ESC ourselves
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    // Initialize audio system and load background music
    initAudio();

    _screens.loadBackground();

    // If CLI requested autoconnect, attempt it once after window init
    if (_autoConnectPending) {
        (void)_screens.autoConnect(_screen, _form);
        _autoConnectPending = false;
    }

    float t = 0.f;
    while (!WindowShouldClose() && _screen != ScreenState::Exiting) {
        float dt = GetFrameTime();
        t += dt;

        // Update music stream
        updateAudio();

        if (IsKeyPressed(KEY_ESCAPE)) {
            if (_screen == ScreenState::Menu) {
                _screen = ScreenState::Exiting;
            } else {
                // Leaving current screen back to menu; ensure we leave any active session
                _screens.leaveSession();
                _screen = ScreenState::Menu;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);
        if (_screens.hasBackground()) {
            _screens.drawBackground(dt);
        } else {
            drawStarfield(t);
        }

        switch (_screen) {
            case ScreenState::Menu: _screens.drawMenu(_screen); break;
            case ScreenState::Singleplayer: _screens.drawSingleplayer(_screen, _singleForm); break;
            case ScreenState::Multiplayer: _screens.drawMultiplayer(_screen, _form); break;
            case ScreenState::Waiting: _screens.drawWaiting(_screen); break;
            case ScreenState::Gameplay:
                if (!_resizedForGameplay) {
                    // Slightly increase height to make room for the bottom bar
                    SetWindowSize(screenWidth, (int)(screenHeight * 1.10f));
                    _resizedForGameplay = true;
                }
                _screens.drawGameplay(_screen);
                break;
            case ScreenState::GameOver:
                _screens.drawGameOver(_screen);
                break;
            case ScreenState::Options: _screens.drawOptions(); break;
            case ScreenState::Leaderboard: _screens.drawLeaderboard(); break;
            case ScreenState::NotEnoughPlayers: _screens.drawNotEnoughPlayers(_screen); break;
            case ScreenState::Exiting: break;
        }

        EndDrawing();
    }

    // On exit, ensure we disconnect cleanly if needed
    _screens.leaveSession();
    // Release GPU resources before closing window
    _screens.unloadGraphics();

    CloseWindow();
}
