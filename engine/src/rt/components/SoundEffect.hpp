#pragma once
#include <raylib.h>
#include <string>

namespace rt::components {

/// Component for sound effects (short sounds)
struct SoundEffect {
    ::Sound sound{};
    bool loaded = false;
    float volume = 0.7f; // 0.0f to 1.0f

    SoundEffect() = default;

    // No copy, only move
    SoundEffect(const SoundEffect&) = delete;
    SoundEffect& operator=(const SoundEffect&) = delete;

    SoundEffect(SoundEffect&& other) noexcept
        : sound(other.sound)
        , loaded(other.loaded)
        , volume(other.volume) {
        other.loaded = false;
    }

    SoundEffect& operator=(SoundEffect&& other) noexcept {
        if (this != &other) {
            unload();
            sound = other.sound;
            loaded = other.loaded;
            volume = other.volume;
            other.loaded = false;
        }
        return *this;
    }

    void unload() {
        if (loaded) {
            UnloadSound(sound);
            loaded = false;
        }
    }

    ~SoundEffect() {
        unload();
    }
};

} // namespace rt::components
