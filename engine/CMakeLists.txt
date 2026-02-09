if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(rtype_engine_project LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(rtype_engine
    # ECS core
    src/Registry.cpp
    # Components
    src/components/Position.cpp
    src/components/Velocity.cpp
    src/components/Controller.cpp
    src/components/AiController.cpp
    src/components/Enemy.cpp
    src/components/Player.cpp
    src/components/Size.cpp
    src/components/Collided.cpp
    src/components/Music.cpp
    src/components/SoundEffect.cpp
    # Systems
    src/systems/MovementSystem.cpp
    src/systems/PlayerControlSystem.cpp
    src/systems/AiControlSystem.cpp
    src/systems/CollisionSystem.cpp
    # New gameplay systems for server (rt::game API)
    src/Systems.cpp
)

target_include_directories(rtype_engine
    PUBLIC src
    PRIVATE . $<TARGET_PROPERTY:rtype_common,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_features(rtype_engine PUBLIC cxx_std_20)

# Keep the engine standalone; no external project linkage required here
target_link_libraries(rtype_engine
    PUBLIC rtype_common
)
