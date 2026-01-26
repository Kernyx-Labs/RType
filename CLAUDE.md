# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

R-Type is a modern C++20 networked implementation of the classic R-Type shoot-em-up game featuring:
- Custom ECS (Entity-Component-System) game engine
- Multithreaded authoritative server using standalone Asio (UDP + TCP side-channel)
- Graphical client using raylib (OpenGL/X11)
- CMake/Conan 2 build system

## Build Commands

```bash
# Configure (Conan runs automatically)
cmake -B build -S .

# Build both client and server
cmake --build build

# Build specific target
cmake --build build --target r-type_server
cmake --build build --target r-type_client

# Using presets (after initial configure)
cmake --workflow --preset all

# Debug build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Executables output to project root: `./r-type_server`, `./r-type_client`

## Running

```bash
# Server (UDP: 4242, TCP: 4243 by default)
./r-type_server            # default ports
./r-type_server 5000       # UDP: 5000, TCP: 5001

# Client
./r-type_client
```

## Architecture

### Module Structure

- **common/** - Shared binary protocol definitions (`Protocol.hpp`)
- **engine/** - ECS kernel (Registry, ComponentStorage, Systems)
- **server/** - Authoritative UDP server with Asio, game simulation at ~60 Hz
- **client/** - raylib UI client with screen state machine

### Key Architectural Patterns

**Authoritative Server Model**: Server owns all game state. Clients send inputs, server simulates, and broadcasts state snapshots at 60 Hz.

**ECS Engine** (`engine/src/rt/ecs/`):
- `Registry`: Entity-component manager, creates/destroys entities, stores components by type
- `ComponentStorage<T>`: Per-type sparse storage optimized for cache efficiency
- Components are pure data: Position, Velocity, Controller, Player, Enemy, Size, Collided
- Systems iterate matching components: MovementSystem, PlayerControlSystem, AiControlSystem, CollisionSystem

**Network Protocol** (`common/include/common/Protocol.hpp`):
- Binary format with `#pragma pack(push, 1)` for wire payloads
- Little-endian only (no byte swapping)
- Header: size (2 bytes) + type (1 byte) + version (1 byte)
- TCP handshake provides auth token, then UDP for gameplay

**Connection Flow**:
1. TCP: Client connects → Server sends TcpWelcome (UDP port) + StartGame (token)
2. UDP: Client sends Hello with token → Server validates, spawns player, sends HelloAck
3. Loop: Client sends Input → Server broadcasts State

**Client State Machine**: Menu → Multiplayer → Waiting → Gameplay → GameOver → Menu

### Key Entry Points

- Server main: `server/src/main.cpp`
- Client main: `client/src/main.cpp`
- ECS core: `engine/src/Registry.cpp`
- Protocol definitions: `common/include/common/Protocol.hpp`

## CMake Options

- `BUILD_CLIENT` (ON/OFF) - Include client binary
- `BUILD_SERVER` (ON/OFF) - Include server binary
- `CMAKE_BUILD_TYPE` - Release (default), Debug, RelWithDebInfo, MinSizeRel

## Conventions

### Commits

```
state(scope): description
```

States: feat, fix, refactor, docs, style, test, chore, perf, ci, build, revert

Example: `feat(server): add login endpoint`

### Branches

```
Part/State/Description
```

Example: `Server/Feature/Create-login`, `Client/Fix/Crash-on-startup`

## Documentation

- Architecture: `docs/architecture.md`
- Protocol wire format: `docs/protocol.md`
- Engine internals: `docs/devlopper/engine.md`
- Extended docs: `Gitbook/` directory
