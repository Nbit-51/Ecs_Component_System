#pragma once

#include <cstdint>
#include <functional>
#include <ostream>

namespace ecs {

using EntityId      = std::uint32_t;
using EntityVersion = std::uint32_t;

/**
 * @brief A versioned entity handle.
 *
 * Combines a dense index (id) with a generation counter (version).
 * Stale handles from destroyed entities are detected by comparing
 * the stored version against the registry's version array — O(1), no search.
 *
 *   Entity layout (64 bits):
 *   ┌────────────────────────┬────────────────────────┐
 *   │  version  (32 bits)    │    id      (32 bits)   │
 *   └────────────────────────┴────────────────────────┘
 */
class Entity {
public:
    static constexpr EntityId      INVALID_ID      = ~EntityId{0};
    static constexpr EntityVersion INVALID_VERSION = ~EntityVersion{0};

    constexpr Entity() noexcept : m_id(INVALID_ID), m_version(INVALID_VERSION) {}
    constexpr Entity(EntityId id, EntityVersion ver) noexcept
        : m_id(id), m_version(ver) {}

    [[nodiscard]] constexpr EntityId      id()      const noexcept { return m_id; }
    [[nodiscard]] constexpr EntityVersion version() const noexcept { return m_version; }

    [[nodiscard]] constexpr bool operator==(const Entity&) const noexcept = default;
    [[nodiscard]] constexpr bool operator!=(const Entity&) const noexcept = default;

    [[nodiscard]] constexpr bool is_null() const noexcept {
        return m_id == INVALID_ID;
    }

    friend std::ostream& operator<<(std::ostream& os, const Entity& e) {
        return os << "Entity{id=" << e.m_id << ", v=" << e.m_version << "}";
    }

private:
    EntityId      m_id;
    EntityVersion m_version;
};

inline constexpr Entity NULL_ENTITY{};

} // namespace ecs

// std::hash specialisation so Entity works in unordered containers
template <>
struct std::hash<ecs::Entity> {
    std::size_t operator()(ecs::Entity e) const noexcept {
        std::size_t h1 = std::hash<ecs::EntityId>{}(e.id());
        std::size_t h2 = std::hash<ecs::EntityVersion>{}(e.version());
        return h1 ^ (h2 << 32u);
    }
};
