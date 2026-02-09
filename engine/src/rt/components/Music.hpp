#pragma once
#include <raylib.h>
#include <string>

namespace rt::components {

/// Component for background music that loops
struct BackgroundMusic {
    ::Music music{};
    bool loaded = false;
    bool playing = false;
    float volume = 0.5f; // 0.0f to 1.0f

    BackgroundMusic() = default;

    // No copy, only move
    BackgroundMusic(const BackgroundMusic&) = delete;
    BackgroundMusic& operator=(const BackgroundMusic&) = delete;

    BackgroundMusic(BackgroundMusic&& other) noexcept
        : music(other.music)
        , loaded(other.loaded)
        , playing(other.playing)
        , volume(other.volume) {
        other.loaded = false;
        other.playing = false;
    }

    BackgroundMusic& operator=(BackgroundMusic&& other) noexcept {
        if (this != &other) {
            unload();
            music = other.music;
            loaded = other.loaded;
            playing = other.playing;
            volume = other.volume;
            other.loaded = false;
            other.playing = false;
        }
        return *this;
    }

    void unload() {
        if (loaded) {
            if (playing) {
                StopMusicStream(music);
                playing = false;
            }
            UnloadMusicStream(music);
            loaded = false;
        }
    }

    ~BackgroundMusic() {
        unload();
    }
};

} // namespace rt::components

