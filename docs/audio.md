# Audio System Documentation

## Overview

The R-Type client now features a comprehensive audio system with background music and sound effects.

## Features

### Background Music
- **File**: `client/sound/Skeleton-Dance.mp3`
- **Behavior**: Automatically plays when the game launches and loops continuously
- **Volume**: Set to 50% by default (increased for better audibility)
- **Implementation**: Managed by the `App` class using Raylib's music streaming API

### Sound Effects

#### Shoot Sound
- **File**: `client/sound/Blaster-Shot.mp3`
- **Behavior**: Plays when the player fires weapons
- **Volume**: Set to 50% by default (increased for better audibility)
- **Polyphonic Playback**: Uses a pool of 8 sound instances to allow overlapping
- **Implementation**: Round-robin playback from sound pool for realistic rapid-fire
- **Throttling**: Limited firing rate (singleplayer: 0.18s interval, multiplayer: 0.15s throttle)

## Components

### Engine Components (ECS)

Two new audio components have been added to the engine:

1. **`rt::components::BackgroundMusic`** (`engine/src/rt/components/Music.hpp`)
   - Wraps Raylib's `Music` type with RAII semantics
   - Handles loading, playing, and unloading music streams
   - Supports volume control (0.0 to 1.0)
   - Move-only semantics for safe resource management

2. **`rt::components::SoundEffect`** (`engine/src/rt/components/SoundEffect.hpp`)
   - Wraps Raylib's `Sound` type with RAII semantics
   - Handles loading and unloading sound effects
   - Supports volume control (0.0 to 1.0)
   - Move-only semantics for safe resource management

### Client Implementation

The audio system is integrated into the client UI layer:

- **App class** (`client/include/client/ui/App.hpp`):
  - Initializes the audio device on startup
  - Loads and plays background music
  - Updates music stream every frame
  - Cleans up audio resources on shutdown

- **Screens class** (`client/include/client/ui/Screens.hpp`):
  - Manages sound effects loading/unloading
  - Provides `playShootSound()` method for triggering effects
  - Automatically loads sounds when entering gameplay modes

## Usage

### Adding New Music

1. Place your music file in `client/sound/`
2. Modify `App::initAudio()` to load your music file
3. Adjust volume as needed using `SetMusicVolume()`

### Adding New Sound Effects

1. Place your sound file in `client/sound/`
2. Add a new `Sound` member to the `Screens` class
3. Load it in `Screens::loadSoundEffects()`
4. Unload it in `Screens::unloadSoundEffects()`
5. Create a play method (e.g., `playExplosionSound()`)
6. Call the method where the sound should trigger

## File Structure

```
RType/
├── client/
│   └── sound/
│       ├── Skeleton-Dance.mp3    # Background music
│       └── Blaster-Shot.mp3      # Shooting sound effect
├── engine/
│   └── src/
│       ├── rt/
│       │   └── components/
│       │       ├── Music.hpp          # Background music component
│       │       └── SoundEffect.hpp    # Sound effect component
│       └── components/
│           ├── Music.cpp
│           └── SoundEffect.cpp
```

## Technical Notes

- Audio initialization uses Raylib's `InitAudioDevice()`
- Music streams are updated each frame via `UpdateMusicStream()`
- All audio resources are properly cleaned up before closing the window
- **Polyphonic sound effects**: Each sound effect uses a pool of 8 instances
  - Round-robin playback allows natural overlapping
  - Multiple shots can be heard simultaneously
  - No interruption of ongoing sounds
- The system gracefully handles missing audio files with warning messages
- Memory-efficient: Sound instances are loaded once and reused

## Future Enhancements

Potential improvements for the audio system:

- Volume controls in the options menu
- Music toggle on/off
- Multiple music tracks for different game states
- More sound effects (explosions, collisions, power-ups, etc.)
- 3D positional audio for multiplayer
- Audio mixing and fading between tracks
