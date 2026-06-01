/**
 * @file Sparse_Set.cpp
 * @brief Sparse-Set memory mapping — the data structure powering modern ECS.
 *
 * A sparse-set gives you:
 *   - O(1) insert, erase, lookup
 *   - Cache-friendly linear iteration (packed dense array)
 *   - No wasted memory for missing components
 *
 * This file benchmarks sparse-set vs std::unordered_map for ECS workloads.
 *
 * Compile:
 *   g++ -std=c++20 -O2 -Wall Sparse_Set.cpp -o sparse_set && ./sparse_set
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

using EntityId = std::uint32_t;
using Clock    = std::chrono::high_resolution_clock;
using Ms       = std::chrono::duration<double, std::milli>;

static constexpr EntityId INVALID    = ~EntityId{0};
static constexpr std::size_t MAX_ENT = 1 << 16;

// ─── Sparse Set ───────────────────────────────────────────────────────────────
/**
 * Classic sparse-set layout:
 *
 *   sparse[entity_id] → index into dense/data arrays (or INVALID)
 *   dense[index]      → entity_id  (for reverse lookup / swap-erase)
 *   data[index]       → component  (contiguous, cache-hot on iteration)
 *
 *   Insert:  sparse[id] = size; dense.push(id); data.push(val)   — O(1)
 *   Erase:   swap last → hole, pop back                          — O(1)
 *   Lookup:  data[sparse[id]]                                    — O(1)
 *   Iterate: for (auto& c : data) { ... }                        — O(n), cache-friendly
 */
template <typename T>
class SparseSet {
public:
    SparseSet() { m_sparse.fill(INVALID); }

    void insert(EntityId id, T val = {}) {
        assert(id < MAX_ENT && m_sparse[id] == INVALID);
        m_sparse[id] = static_cast<EntityId>(m_dense.size());
        m_dense.push_back(id);
        m_data.push_back(std::move(val));
    }

    void erase(EntityId id) {
        if (!contains(id)) return;
        EntityId idx  = m_sparse[id];
        EntityId last = static_cast<EntityId>(m_dense.size() - 1);
        if (idx != last) {
            // Swap with last element to keep arrays dense
            m_dense[idx]              = m_dense[last];
            m_data[idx]               = std::move(m_data[last]);
            m_sparse[m_dense[idx]]    = idx;
        }
        m_sparse[id] = INVALID;
        m_dense.pop_back();
        m_data.pop_back();
    }

    T& get(EntityId id) {
        assert(contains(id));
        return m_data[m_sparse[id]];
    }

    bool contains(EntityId id) const noexcept {
        return id < MAX_ENT && m_sparse[id] != INVALID;
    }

    std::size_t size()  const noexcept { return m_data.size(); }
    bool        empty() const noexcept { return m_data.empty(); }

    // Direct packed data access — no indirection, SIMD-friendly
    std::vector<T>&       data()       noexcept { return m_data; }
    const std::vector<T>& data() const noexcept { return m_data; }
    const std::vector<EntityId>& entities() const noexcept { return m_dense; }

    void reserve(std::size_t n) {
        m_dense.reserve(n);
        m_data.reserve(n);
    }

private:
    std::array<EntityId, MAX_ENT> m_sparse;
    std::vector<EntityId>         m_dense;
    std::vector<T>                m_data;
};

// ─── Component ────────────────────────────────────────────────────────────────
struct Position { float x{}, y{}, z{}; };

// ─── Benchmark helpers ────────────────────────────────────────────────────────
template <typename Fn>
double bench_ms(int runs, Fn&& fn) {
    for (int i = 0; i < 3; ++i) fn(); // warmup
    double total = 0;
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        fn();
        total += Ms(Clock::now() - t0).count();
    }
    return total / runs;
}

int main() {
    std::cout << "=== Sparse-Set Memory Mapping Demo ===\n\n";

    constexpr std::size_t N    = 50'000;
    constexpr int         RUNS = 20;

    std::mt19937 rng{42};
    std::vector<EntityId> ids(N);
    std::iota(ids.begin(), ids.end(), 0u);

    // ── 1. Correctness demo ───────────────────────────────────────────────
    std::cout << "--- Correctness ---\n";
    {
        SparseSet<Position> ss;
        ss.insert(0u, {1.f, 2.f, 3.f});
        ss.insert(5u, {4.f, 5.f, 6.f});
        ss.insert(99u,{7.f, 8.f, 9.f});

        std::cout << "  Size after 3 inserts: " << ss.size() << "\n";
        std::cout << "  contains(5): "  << ss.contains(5u)  << "\n";
        std::cout << "  contains(10): " << ss.contains(10u) << "\n";
        std::cout << "  get(5).x = "    << ss.get(5u).x     << "\n";

        ss.erase(5u);
        std::cout << "  After erase(5), size: " << ss.size() << "\n";
        std::cout << "  contains(5) after erase: " << ss.contains(5u) << "\n";

        // Modify via packed data
        for (auto& p : ss.data()) p.x *= 10.f;
        std::cout << "  get(0).x after *=10: " << ss.get(0u).x << "\n";
        std::cout << "  get(99).x after *=10: " << ss.get(99u).x << "\n";
    }

    // ── 2. Insert benchmark ───────────────────────────────────────────────
    std::cout << "\n--- Benchmark: Insert " << N << " elements ---\n";
    {
        double ss_ms = bench_ms(RUNS, [&] {
            SparseSet<Position> ss;
            ss.reserve(N);
            for (EntityId id : ids) ss.insert(id, {float(id), 0.f, 0.f});
            for (EntityId id : ids) ss.erase(id);
        });

        double map_ms = bench_ms(RUNS, [&] {
            std::unordered_map<EntityId, Position> m;
            m.reserve(N);
            for (EntityId id : ids) m[id] = {float(id), 0.f, 0.f};
            for (EntityId id : ids) m.erase(id);
        });

        std::cout << "  SparseSet insert+erase: " << ss_ms  << " ms\n";
        std::cout << "  unordered_map insert+erase: " << map_ms << " ms\n";
        std::cout << "  Speedup: " << map_ms / ss_ms << "x\n";
    }

    // ── 3. Iteration benchmark ────────────────────────────────────────────
    std::cout << "\n--- Benchmark: Iterate " << N << " elements ---\n";
    {
        SparseSet<Position> ss;
        ss.reserve(N);
        for (EntityId id : ids) ss.insert(id, {float(id), 0.f, 0.f});

        std::unordered_map<EntityId, Position> umap;
        umap.reserve(N);
        for (EntityId id : ids) umap[id] = {float(id), 0.f, 0.f};

        volatile float sink = 0.f;

        double ss_ms = bench_ms(RUNS, [&] {
            for (auto& p : ss.data()) sink += p.x;
        });

        double map_ms = bench_ms(RUNS, [&] {
            for (auto& [k, p] : umap) sink += p.x;
        });

        std::cout << "  SparseSet iteration:     " << ss_ms  << " ms\n";
        std::cout << "  unordered_map iteration: " << map_ms << " ms\n";
        std::cout << "  Speedup: " << map_ms / ss_ms << "x\n";
        (void)sink;
    }

    // ── 4. Random access benchmark ────────────────────────────────────────
    std::cout << "\n--- Benchmark: Random lookup " << N << " elements ---\n";
    {
        SparseSet<Position> ss;
        ss.reserve(N);
        for (EntityId id : ids) ss.insert(id, {float(id), 0.f, 0.f});

        std::unordered_map<EntityId, Position> umap;
        umap.reserve(N);
        for (EntityId id : ids) umap[id] = {float(id), 0.f, 0.f};

        std::vector<EntityId> shuffled = ids;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        volatile float sink = 0.f;

        double ss_ms = bench_ms(RUNS, [&] {
            for (EntityId id : shuffled) sink += ss.get(id).x;
        });
        double map_ms = bench_ms(RUNS, [&] {
            for (EntityId id : shuffled) sink += umap[id].x;
        });

        std::cout << "  SparseSet random lookup:     " << ss_ms  << " ms\n";
        std::cout << "  unordered_map random lookup: " << map_ms << " ms\n";
        std::cout << "  Speedup: " << map_ms / ss_ms << "x\n";
        (void)sink;
    }

    std::cout << "\n=== Done ===\n";
    return 0;
}
