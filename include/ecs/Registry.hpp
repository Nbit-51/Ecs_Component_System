#pragma once

#include <array>
#include <bitset>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "ComponentPool.hpp"
#include "Entity.hpp"
#include "TypeId.hpp"

namespace ecs {

/// Maximum number of component types supported.
inline constexpr std::size_t MAX_COMPONENTS = 64;
/// Maximum number of entities alive at once.
inline constexpr std::size_t MAX_ENTITIES = 1 << 16; // 65536

using ComponentMask = std::bitset<MAX_COMPONENTS>;

// ─── Concepts ────────────────────────────────────────────────────────────────

template <typename T>
concept Component = std::is_default_constructible_v<T> && std::is_move_constructible_v<T>;

template <typename... Ts>
concept ComponentPack = (Component<Ts> && ...);

// ─── Registry ────────────────────────────────────────────────────────────────

/**
 * @brief Central ECS registry.
 *
 * Manages entity lifecycle and component storage using a cache-friendly
 * packed-array (SoA) layout. All component pools are contiguous in memory,
 * enabling tight iteration loops with minimal cache misses.
 *
 * ### Design goals
 * - **O(1)** entity creation / destruction
 * - **O(1)** component add / remove / get
 * - **Linear** iteration over matching archetypes (no indirection)
 * - Zero heap allocation after initial pool reservation
 */
class Registry {
public:
    Registry() {
        // Pre-generate entity IDs so creation is O(1)
        m_free_list.reserve(MAX_ENTITIES);
        for (EntityId i = MAX_ENTITIES - 1; i > 0; --i)
            m_free_list.push_back(i);
        m_free_list.push_back(0);

        m_versions.fill(0);
        m_masks.fill(ComponentMask{});
        m_alive.fill(false);
    }

    // ── Entity lifecycle ──────────────────────────────────────────────────

    [[nodiscard]] Entity create() {
        assert(!m_free_list.empty() && "Entity limit reached");
        EntityId id = m_free_list.back();
        m_free_list.pop_back();
        m_alive[id] = true;
        ++m_entity_count;
        return Entity{id, m_versions[id]};
    }

    void destroy(Entity e) {
        if (!valid(e)) return;
        // Remove all components
        for (std::size_t cid = 0; cid < MAX_COMPONENTS; ++cid) {
            if (m_masks[e.id()].test(cid) && m_pools[cid])
                m_pools[cid]->erase(e.id());
        }
        m_masks[e.id()].reset();
        m_alive[e.id()] = false;
        ++m_versions[e.id()]; // invalidate outstanding handles
        m_free_list.push_back(e.id());
        --m_entity_count;
    }

    [[nodiscard]] bool valid(Entity e) const noexcept {
        return e.id() < MAX_ENTITIES &&
               m_alive[e.id()] &&
               m_versions[e.id()] == e.version();
    }

    [[nodiscard]] std::size_t entity_count() const noexcept { return m_entity_count; }

    // ── Component management ──────────────────────────────────────────────

    template <Component C, typename... Args>
    C& emplace(Entity e, Args&&... args) {
        assert(valid(e));
        const std::size_t cid = component_id<C>();
        assert(!m_masks[e.id()].test(cid) && "Component already attached");
        pool_of<C>().emplace(e.id(), std::forward<Args>(args)...);
        m_masks[e.id()].set(cid);
        return pool_of<C>().get(e.id());
    }

    template <Component C>
    void remove(Entity e) {
        assert(valid(e));
        const std::size_t cid = component_id<C>();
        if (!m_masks[e.id()].test(cid)) return;
        pool_of<C>().erase(e.id());
        m_masks[e.id()].reset(cid);
    }

    template <Component C>
    [[nodiscard]] bool has(Entity e) const noexcept {
        return valid(e) && m_masks[e.id()].test(component_id<C>());
    }

    template <Component C>
    [[nodiscard]] C& get(Entity e) {
        assert(has<C>(e));
        return pool_of<C>().get(e.id());
    }

    template <Component C>
    [[nodiscard]] const C& get(Entity e) const {
        assert(has<C>(e));
        return pool_of<C>().get(e.id());
    }

    template <Component C>
    [[nodiscard]] std::optional<std::reference_wrapper<C>> try_get(Entity e) {
        if (!has<C>(e)) return std::nullopt;
        return std::ref(get<C>(e));
    }

    // ── View / iteration ──────────────────────────────────────────────────

    /**
     * @brief Iterate over all entities that possess every listed component.
     *
     * The callback receives (Entity, C0&, C1&, ...) — unpacked and ready.
     * Uses a bitset membership check, so filtering is a single AND + test.
     *
     * @example
     * registry.view<Position, Velocity>([](Entity e, Position& p, Velocity& v) {
     *     p.x += v.dx;
     *     p.y += v.dy;
     * });
     */
    template <Component... Cs, std::invocable<Entity, Cs&...> Fn>
    void view(Fn&& fn) {
        ComponentMask required;
        (required.set(component_id<Cs>()), ...);

        for (EntityId id = 0; id < MAX_ENTITIES; ++id) {
            if (!m_alive[id]) continue;
            if ((m_masks[id] & required) == required) {
                Entity e{id, m_versions[id]};
                std::invoke(fn, e, pool_of<Cs>().get(id)...);
            }
        }
    }

    /**
     * @brief Same as view<> but the callback only takes components (no Entity).
     */
    template <Component... Cs, std::invocable<Cs&...> Fn>
    void view(Fn&& fn) {
        ComponentMask required;
        (required.set(component_id<Cs>()), ...);

        for (EntityId id = 0; id < MAX_ENTITIES; ++id) {
            if (!m_alive[id]) continue;
            if ((m_masks[id] & required) == required)
                std::invoke(fn, pool_of<Cs>().get(id)...);
        }
    }

    /// Reserve pool capacity up-front to avoid reallocations.
    template <Component C>
    void reserve(std::size_t n) {
        pool_of<C>().reserve(n);
    }

    /// Clear all entities and components (keeps pool allocations).
    void clear() {
        for (EntityId id = 0; id < MAX_ENTITIES; ++id) {
            if (m_alive[id]) destroy(Entity{id, m_versions[id]});
        }
    }

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    template <Component C>
    [[nodiscard]] ComponentPool<C>& pool_of() {
        const std::size_t cid = component_id<C>();
        if (!m_pools[cid])
            m_pools[cid] = std::make_unique<ComponentPool<C>>();
        return static_cast<ComponentPool<C>&>(*m_pools[cid]);
    }

    template <Component C>
    [[nodiscard]] const ComponentPool<C>& pool_of() const {
        const std::size_t cid = component_id<C>();
        assert(m_pools[cid] && "Pool not initialised");
        return static_cast<const ComponentPool<C>&>(*m_pools[cid]);
    }

    // ── Data ──────────────────────────────────────────────────────────────

    std::array<std::unique_ptr<IComponentPool>, MAX_COMPONENTS> m_pools{};
    std::array<ComponentMask, MAX_ENTITIES>                     m_masks{};
    std::array<EntityVersion, MAX_ENTITIES>                     m_versions{};
    std::array<bool, MAX_ENTITIES>                              m_alive{};
    std::vector<EntityId>                                       m_free_list;
    std::size_t                                                 m_entity_count{0};
};

} // namespace ecs
