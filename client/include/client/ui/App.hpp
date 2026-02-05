#pragma once
#include "Screens.hpp"
#include <raylib.h>

namespace client {
namespace ui {

class App {
public:
    App();
    ~App();
    // Enable autoconnect to multiplayer on startup (optional)
    void setAutoConnect(const std::string& host, const std::string& port, const std::string& name);
    void run();
private:
    void initAudio();
    void updateAudio();
    void cleanupAudio();

    ScreenState _screen;
    MultiplayerForm _form;
    SingleplayerForm _singleForm;
    Screens _screens;
    bool _resizedForGameplay = false;
    bool _autoConnectPending = false;

    // Audio
    Music _backgroundMusic{};
    bool _musicLoaded = false;
};

} // namespace ui
} // namespace client
