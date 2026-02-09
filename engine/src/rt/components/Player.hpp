#pragma once
#include <cstdint>

namespace rt::components {
struct Player {}; // Marker

struct ShipType {
  std::uint8_t value; // 0..4
};
} // namespace rt::components
