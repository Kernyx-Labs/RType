#include "Screens.hpp"
#include <iostream>
#include <raylib.h>
#include <vector>

namespace client { namespace ui {

void Screens::logMessage(const std::string& msg, const char* level) {
    if (level)
        std::cout << "[" << level << "] " << msg << std::endl;
    else
        std::cout << "[INFO] " << msg << std::endl;
}

void Screens::loadSoundEffects() {
    if (_shootSoundLoaded) return;

    // Try to find the shooting sound effect
    std::vector<std::string> candidates;
    candidates.emplace_back("sound/Blaster-Shot.mp3");
    candidates.emplace_back("client/sound/Blaster-Shot.mp3");
    candidates.emplace_back("../client/sound/Blaster-Shot.mp3");
    candidates.emplace_back("../../client/sound/Blaster-Shot.mp3");

    for (const auto& path : candidates) {
        if (FileExists(path.c_str())) {
            // Load multiple instances of the sound for polyphonic playback
            bool allLoaded = true;
            for (int i = 0; i < MAX_SHOOT_SOUNDS; ++i) {
                _shootSoundPool[i] = LoadSound(path.c_str());
                if (_shootSoundPool[i].frameCount > 0) {
                    SetSoundVolume(_shootSoundPool[i], 0.5f); // 50% volume (increased from 40%)
                } else {
                    allLoaded = false;
                    break;
                }
            }

            if (allLoaded) {
                _shootSoundLoaded = true;
                _nextShootSound = 0;
                logMessage("Shoot sound effect loaded (" + std::to_string(MAX_SHOOT_SOUNDS) + " instances) from: " + path);
                break;
            }
        }
    }
    if (!_shootSoundLoaded) {
        logMessage("Warning: Shoot sound effect not found", "WARN");
    }

    // Try to find the explosion sound effect
    std::vector<std::string> explosionCandidates;
    explosionCandidates.emplace_back("sound/Explosion.mp3");
    explosionCandidates.emplace_back("client/sound/Explosion.mp3");
    explosionCandidates.emplace_back("../client/sound/Explosion.mp3");
    explosionCandidates.emplace_back("../../client/sound/Explosion.mp3");

    for (const auto& path : explosionCandidates) {
        if (FileExists(path.c_str())) {
            // Load multiple instances of the explosion sound for polyphonic playback
            bool allLoaded = true;
            for (int i = 0; i < MAX_EXPLOSION_SOUNDS; ++i) {
                _explosionSoundPool[i] = LoadSound(path.c_str());
                if (_explosionSoundPool[i].frameCount > 0) {
                    SetSoundVolume(_explosionSoundPool[i], 0.5f); // 50% volume
                } else {
                    allLoaded = false;
                    break;
                }
            }

            if (allLoaded) {
                _explosionSoundLoaded = true;
                _nextExplosionSound = 0;
                logMessage("Explosion sound effect loaded (" + std::to_string(MAX_EXPLOSION_SOUNDS) + " instances) from: " + path);
                return;
            }
        }
    }
    if (!_explosionSoundLoaded) {
        logMessage("Warning: Explosion sound effect not found", "WARN");
    }
}

void Screens::unloadSoundEffects() {
    if (_shootSoundLoaded) {
        for (int i = 0; i < MAX_SHOOT_SOUNDS; ++i) {
            UnloadSound(_shootSoundPool[i]);
        }
        _shootSoundLoaded = false;
    }
    if (_explosionSoundLoaded) {
        for (int i = 0; i < MAX_EXPLOSION_SOUNDS; ++i) {
            UnloadSound(_explosionSoundPool[i]);
        }
        _explosionSoundLoaded = false;
    }
}

void Screens::playShootSound() {
    if (_shootSoundLoaded) {
        // Use round-robin to play from the pool
        // This allows multiple sounds to overlap naturally
        PlaySound(_shootSoundPool[_nextShootSound]);
        _nextShootSound = (_nextShootSound + 1) % MAX_SHOOT_SOUNDS;
    }
}

void Screens::playExplosionSound() {
    if (_explosionSoundLoaded) {
        // Use round-robin to play from the pool
        // This allows multiple explosions to overlap naturally
        PlaySound(_explosionSoundPool[_nextExplosionSound]);
        _nextExplosionSound = (_nextExplosionSound + 1) % MAX_EXPLOSION_SOUNDS;
    }
}

} } // namespace client::ui
