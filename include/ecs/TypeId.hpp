#pragma once

#include <atomic>
#include <cstddef>

namespace ecs::detail {

/**
 * @brief Compile-time-stable, runtime-assigned component type IDs.
 *
 * Each unique component type T gets a distinct integer ID the first time
 * component_id<T>() is called. IDs are sequential starting at 0 and are
 * stable within a single program run (same link unit).
 *
 * Implementation uses a per-type function-local static to avoid SIOF
 * and to guarantee thread-safe initialisation (C++11 §6.7).
 */

inline std::size_t next_component_id() noexcept {
    static std::atomic<std::size_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

template <typename T>
std::size_t component_id() noexcept {
    static const std::size_t id = next_component_id();
    return id;
}

} // namespace ecs::detail

namespace ecs {

/// Public alias — use this everywhere.
template <typename T>
std::size_t component_id() noexcept {
    return detail::component_id<T>();
}

} // namespace ecs
