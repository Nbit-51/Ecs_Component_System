/**
 * @file Archetype.cpp
 * @brief Archetype memory mapping — grouping entities by component signature.
 *
 * An Archetype stores all entities with the SAME set of components together
 * in contiguous arrays. Used by Unity DOTS, Flecs, and Bevy.
 * Compile:
 *   g++ -std=c++20 -O2 -Wall Archetype.cpp -o archetype && ./archetype
 */

#include <algorithm>
#include <bitset>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

using EntityId = std::uint32_t;
using Clock    = std::chrono::high_resolution_clock;
using Ms       = std::chrono::duration<double, std::milli>;

// ─── Components ───────────────────────────────────────────────────────────────
struct Position  { float x{}, y{}, z{}; };
struct Velocity  { float dx{}, dy{}, dz{}; };
struct Health    { float hp{100.f}; };
struct Renderable{ int sprite_id{0}; bool visible{true}; };

// ─── Component type registry ──────────────────────────────────────────────────
static std::size_t s_cid = 0;
template <typename T>
std::size_t cid() { static std::size_t id = s_cid++; return id; }

using Signature = std::bitset<64>;
template <typename... Ts>
Signature make_sig() {
    Signature s;
    (s.set(cid<Ts>()), ...);
    return s;
}

// ─── Generic column storage ───────────────────────────────────────────────────
struct IColumn { virtual ~IColumn() = default; virtual void push_default() = 0; virtual void swap_erase(std::size_t idx) = 0; };

template <typename T>
struct Column : IColumn {
    std::vector<T> data;
    void push_default() override              { data.emplace_back(); }
    void push(T val)                          { data.push_back(std::move(val)); }
    void swap_erase(std::size_t idx) override {
        if (idx + 1 < data.size()) data[idx] = std::move(data.back());
        data.pop_back();
    }
    T& get(std::size_t idx) { return data[idx]; }
};

// ─── Archetype ────────────────────────────────────────────────────────────────
struct Archetype {
    Signature                                              signature;
    std::vector<EntityId>                                  entities;
    std::unordered_map<std::size_t, std::unique_ptr<IColumn>> columns;

    template <typename T>
    Column<T>* col() {
        auto it = columns.find(cid<T>());
        return it != columns.end() ? static_cast<Column<T>*>(it->second.get()) : nullptr;
    }

    template <typename T>
    void add_column() {
        columns[cid<T>()] = std::make_unique<Column<T>>();
    }

    std::size_t size() const { return entities.size(); }

    // Add an entity row (all columns get default values)
    std::size_t push_entity(EntityId id) {
        entities.push_back(id);
        for (auto& [cid, col] : columns) col->push_default();
        return entities.size() - 1;
    }

    // Swap-erase a row
    void erase_row(std::size_t idx) {
        if (idx + 1 < entities.size()) entities[idx] = entities.back();
        entities.pop_back();
        for (auto& [cid, col] : columns) col->swap_erase(idx);
    }
};

// ─── Archetype World ──────────────────────────────────────────────────────────
class ArchetypeWorld {
public:
    // Get or create an archetype for a given signature
    Archetype* get_or_create(Signature sig, std::function<void(Archetype&)> setup = {}) {
        auto it = m_archetypes.find(sig.to_ulong());
        if (it != m_archetypes.end()) return it->second.get();
        auto arch = std::make_unique<Archetype>();
        arch->signature = sig;
        if (setup) setup(*arch);
        auto* ptr = arch.get();
        m_archetypes[sig.to_ulong()] = std::move(arch);
        return ptr;
    }

    // Spawn entity into matching archetype
    template <typename... Ts>
    EntityId spawn() {
        Signature sig = make_sig<Ts...>();
        Archetype* arch = get_or_create(sig, [](Archetype& a) {
            (a.add_column<Ts>(), ...);
        });
        EntityId id = m_next_id++;
        std::size_t row = arch->push_entity(id);
        m_entity_location[id] = {arch, row};
        return id;
    }

    template <typename T>
    T* get(EntityId id) {
        auto it = m_entity_location.find(id);
        if (it == m_entity_location.end()) return nullptr;
        auto* col = it->second.arch->col<T>();
        return col ? &col->get(it->second.row) : nullptr;
    }

    // Iterate all archetypes that match a query signature
    template <typename... Ts, typename Fn>
    void query(Fn&& fn) {
        Signature required = make_sig<Ts...>();
        for (auto& [key, arch] : m_archetypes) {
            if ((arch->signature & required) != required) continue;
            for (std::size_t i = 0; i < arch->size(); ++i) {
                auto* cols = std::make_tuple(arch->col<Ts>()...);
                if ((std::get<Column<Ts>*>(cols) && ...))
                    fn(arch->entities[i], std::get<Column<Ts>*>(cols)->get(i)...);
            }
        }
    }

    std::size_t entity_count() const { return m_entity_location.size(); }
    std::size_t archetype_count() const { return m_archetypes.size(); }

private:
    struct Location { Archetype* arch; std::size_t row; };
    std::unordered_map<unsigned long, std::unique_ptr<Archetype>> m_archetypes;
    std::unordered_map<EntityId, Location>                        m_entity_location;
    EntityId                                                       m_next_id{0};
};

// ─── Demo ─────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== Archetype Memory Mapping Demo ===\n\n";

    ArchetypeWorld world;

    // Archetype 1: Position + Velocity + Health (dynamic enemies)
    for (int i = 0; i < 5; ++i) {
        EntityId e = world.spawn<Position, Velocity, Health>();
        world.get<Position>(e)->x = float(i * 3);
        world.get<Velocity>(e)->dx = 1.f;
        world.get<Health>(e)->hp = 50.f;
    }

    // Archetype 2: Position + Health (static turrets — no velocity)
    for (int i = 0; i < 3; ++i) {
        EntityId e = world.spawn<Position, Health>();
        world.get<Position>(e)->x = float(i * 10 + 50);
        world.get<Health>(e)->hp = 200.f;
    }

    // Archetype 3: Position + Velocity + Renderable (particles)
    for (int i = 0; i < 8; ++i) {
        EntityId e = world.spawn<Position, Velocity, Renderable>();
        world.get<Position>(e)->y = float(i);
        world.get<Velocity>(e)->dy = -0.5f;
        world.get<Renderable>(e)->sprite_id = i % 4;
    }

    std::cout << "Entities: "   << world.entity_count()   << "\n";
    std::cout << "Archetypes: " << world.archetype_count() << "\n\n";

    // Query 1: All entities with Position + Velocity (enemies + particles)
    int move_count = 0;
    world.query<Position, Velocity>([&](EntityId, Position& p, Velocity& v) {
        p.x += v.dx;
        p.y += v.dy;
        ++move_count;
    });
    std::cout << "Movement query matched: " << move_count << " entities\n";

    // Query 2: All entities with Health (enemies + turrets)
    float total_hp = 0.f;
    world.query<Health>([&](EntityId, Health& h) { total_hp += h.hp; });
    std::cout << "Total HP across all archetypes: " << total_hp << "\n";

    // Query 3: Only entities with Position + Velocity + Health
    int pvh_count = 0;
    world.query<Position, Velocity, Health>([&](EntityId id, Position& p, Velocity&, Health& h) {
        std::cout << "  Entity " << id << " pos=(" << p.x << "," << p.y << ") hp=" << h.hp << "\n";
        ++pvh_count;
    });
    std::cout << "PVH archetype count: " << pvh_count << "\n";

    // Benchmark: iteration over single archetype vs query
    std::cout << "\n--- Benchmark: 50k entities, 20 runs ---\n";
    constexpr std::size_t BN = 50'000;
    ArchetypeWorld bworld;
    for (std::size_t i = 0; i < BN; ++i) {
        EntityId e = bworld.spawn<Position, Velocity>();
        bworld.get<Position>(e)->x = float(i);
        bworld.get<Velocity>(e)->dx = 0.1f;
    }

    volatile float sink = 0.f;
    double total = 0.0;
    for (int r = 0; r < 20; ++r) {
        auto t0 = Clock::now();
        bworld.query<Position, Velocity>([&](EntityId, Position& p, Velocity& v) {
            p.x += v.dx; sink += p.x;
        });
        total += Ms(Clock::now() - t0).count();
    }
    std::cout << "  Archetype query avg: " << total / 20.0 << " ms\n";
    (void)sink;

    std::cout << "\n=== Done ===\n";
    return 0;
}
