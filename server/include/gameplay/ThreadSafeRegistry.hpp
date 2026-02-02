#pragma once

#include "rt/ecs/Registry.hpp"
#include <functional>
#include <mutex>

namespace rtype::server::gameplay {

/**
 * @brief Thread-safe wrapper around the ECS Registry.
 *
 * This class ensures that all accesses to the registry are synchronized via a
 * mutex. Direct access to the underlying registry is restricted to prevent race
 * conditions between the ASIO thread (networking) and the Game Loop thread.
 */
class ThreadSafeRegistry {
public:
  ThreadSafeRegistry() = default;
  ~ThreadSafeRegistry() = default;

  // Delete copy/move to prevent accidental slicing or race conditions on the
  // mutex
  ThreadSafeRegistry(const ThreadSafeRegistry &) = delete;
  ThreadSafeRegistry &operator=(const ThreadSafeRegistry &) = delete;
  ThreadSafeRegistry(ThreadSafeRegistry &&) = delete;
  ThreadSafeRegistry &operator=(ThreadSafeRegistry &&) = delete;

  /**
   * @brief Execute a function with exclusive access to the registry.
   *
   * @tparam F Function type (callable)
   * @param func logic to execute, receiving ecs::Registry& as argument
   * @return The return value of func
   */
  template <typename F> auto withLock(F &&func) {
    std::lock_guard<std::mutex> lock(mutex_);
    return func(registry_);
  }

private:
  rt::ecs::Registry registry_;
  std::mutex mutex_;
};

} // namespace rtype::server::gameplay
