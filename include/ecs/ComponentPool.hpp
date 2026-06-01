#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

#include "Entity.hpp"

namespace ecs {

// ─── Interface ────────────────────────────────────────────────────────────────

struct IComponentPool {
    virtual ~IComponentPool() = default;
    virtual void erase(EntityId id) = 0;
};

// ─── Packed sparse-set backed pool ───────────────────────────────────────────

/**
 * @brief A cache-friendly component storage for type `C`.
 *
 * Uses a **sparse-set** structure:
 * - `m_sparse[entity_id]` → index into the packed arrays (or INVALID)
 * - `m_dense[]`           → packed entity ids (parallel to m_data)
 * - `m_data[]`            → packed component data (contiguous, cache-hot)
 *
 * This gives O(1) random access AND cache-friendly linear iteration, which
 * is the key insight behind modern ECS performance.
 *
 *   Iteration pattern (inner loop):
 *   for (auto& c : pool.raw()) { /* touches only component data, no indirection *\/ }
 */
template <typename C>
class ComponentPool final : public IComponentPool {
public:
    static constexpr EntityId INVALID = ~EntityId{0};

    ComponentPool() {
        m_sparse.fill(INVALID);
    }

    // ── Mutators ──────────────────────────────────────────────────────────

    template <typename... Args>
    C& emplace(EntityId id, Args&&... args) {
        assert(id < MAX_ENTITIES_INTERNAL);
        assert(m_sparse[id] == INVALID && "Already has component");
        m_sparse[id] = static_cast<EntityId>(m_dense.size());
        m_dense.push_back(id);
        return m_data.emplace_back(std::forward<Args>(args)...);
    }

    void erase(EntityId id) override {
        assert(id < MAX_ENTITIES_INTERNAL);
        const EntityId idx  = m_sparse[id];
        if (idx == INVALID) return;
        const EntityId last = static_cast<EntityId>(m_dense.size() - 1);
        if (idx != last) {
            // Swap-erase to keep arrays dense
            m_dense[idx] = m_dense[last];
            m_data[idx]  = std::move(m_data[last]);
            m_sparse[m_dense[idx]] = idx;
        }
        m_sparse[id] = INVALID;
        m_dense.pop_back();
        m_data.pop_back();
    }

    // ── Accessors ─────────────────────────────────────────────────────────

    [[nodiscard]] C& get(EntityId id) {
        assert(contains(id));
        return m_data[m_sparse[id]];
    }

    [[nodiscard]] const C& get(EntityId id) const {
        assert(contains(id));
        return m_data[m_sparse[id]];
    }

    [[nodiscard]] bool contains(EntityId id) const noexcept {
        return id < MAX_ENTITIES_INTERNAL && m_sparse[id] != INVALID;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] bool        empty() const noexcept { return m_data.empty(); }

    /// Direct access to packed component data — ideal for SIMD / bulk ops.
    [[nodiscard]] std::vector<C>&       raw()       noexcept { return m_data; }
    [[nodiscard]] const std::vector<C>& raw() const noexcept { return m_data; }

    /// Parallel packed entity ids (same order as raw()).
    [[nodiscard]] const std::vector<EntityId>& entities() const noexcept { return m_dense; }

    void reserve(std::size_t n) {
        m_dense.reserve(n);
        m_data.reserve(n);
    }

private:
    static constexpr std::size_t MAX_ENTITIES_INTERNAL = 1 << 16;

    std::array<EntityId, MAX_ENTITIES_INTERNAL> m_sparse;
    std::vector<EntityId>                       m_dense;
    std::vector<C>                              m_data;
};

} // namespace ecs
