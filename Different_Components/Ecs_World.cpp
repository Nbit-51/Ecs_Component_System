/**
 * @file Ecs_World.cpp
 * @brief ECS World — the central coordinator for the Entity Component System.
 *
 * The World owns all component stores and provides the primary interface
 * for creating entities, attaching/detaching components, and running systems.
 *
 * Compile:
 *   g++ -std=c++20 -O2 -Wall Ecs_World.cpp -o ecs_world && ./ecs_world
 */

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <cassert>
#include <algorithm>
#include <stdexcept>

// ─── Entity ───────────────────────────────────────────────────────────────────

using EntityId = std::uint32_t;
constexpr EntityId NULL_ENTITY = UINT32_MAX;

// ─── Components ───────────────────────────────────────────────────────────────

struct Transform {
    float x{}, y{}, z{};
    float rot_x{}, rot_y{}, rot_z{};
    float scale{1.f};
};

struct Velocity {
    float dx{}, dy{}, dz{};
};

struct Health {
    float hp{100.f};
    float max_hp{100.f};
    bool  alive{true};
};

struct Tag {
    std::string name;
};

// ─── Generic component store (dense unordered_map per component type) ─────────

template<typename T>
struct ComponentStore {
    std::unordered_map<EntityId, T> data;

    void  add(EntityId id, T component)       { data[id] = std::move(component); }
    void  remove(EntityId id)                  { data.erase(id); }
    bool  has(EntityId id) const               { return data.count(id) > 0; }
    T&    get(EntityId id)                     { return data.at(id); }
    const T& get(EntityId id) const            { return data.at(id); }

    // Iterate all entities that own this component
    void each(std::function<void(EntityId, T&)> fn) {
        for (auto& [id, comp] : data) fn(id, comp);
    }
};

// ─── World ───────────────────────────────────────────────────────────────────

class World {
public:
    // ── Entity management ──────────────────────────────────────────────────

    EntityId create(const std::string& tag = "entity") {
        EntityId id;
        if (!free_list_.empty()) {
            id = free_list_.back();
            free_list_.pop_back();
        } else {
            id = next_id_++;
        }
        alive_.insert(id);
        tags_.add(id, Tag{tag});
        return id;
    }

    void destroy(EntityId id) {
        assert_alive(id);
        // Strip all components
        transforms_.remove(id);
        velocities_.remove(id);
        healths_.remove(id);
        tags_.remove(id);
        alive_.erase(id);
        free_list_.push_back(id);   // recycle id
    }

    bool alive(EntityId id) const { return alive_.count(id) > 0; }
    std::size_t entity_count()    const { return alive_.size(); }

    // ── Component accessors ────────────────────────────────────────────────

    // Transform
    void add_transform(EntityId id, Transform t = {})    { assert_alive(id); transforms_.add(id, t); }
    void remove_transform(EntityId id)                    { transforms_.remove(id); }
    bool has_transform(EntityId id)  const                { return transforms_.has(id); }
    Transform& transform(EntityId id)                     { return transforms_.get(id); }

    // Velocity
    void add_velocity(EntityId id, Velocity v = {})      { assert_alive(id); velocities_.add(id, v); }
    void remove_velocity(EntityId id)                     { velocities_.remove(id); }
    bool has_velocity(EntityId id)   const                { return velocities_.has(id); }
    Velocity& velocity(EntityId id)                       { return velocities_.get(id); }

    // Health
    void add_health(EntityId id, Health h = {})          { assert_alive(id); healths_.add(id, h); }
    void remove_health(EntityId id)                       { healths_.remove(id); }
    bool has_health(EntityId id)     const                { return healths_.has(id); }
    Health& health(EntityId id)                           { return healths_.get(id); }

    // Tag (always present)
    const std::string& name(EntityId id) const            { return tags_.get(id).name; }

    // ── View helpers (iterate entities with a given component) ─────────────

    void each_transform(std::function<void(EntityId, Transform&)> fn) {
        transforms_.each(fn);
    }
    void each_velocity(std::function<void(EntityId, Velocity&)> fn) {
        velocities_.each(fn);
    }
    void each_health(std::function<void(EntityId, Health&)> fn) {
        healths_.each(fn);
    }

    // ── Built-in systems ───────────────────────────────────────────────────

    // Move every entity that has both Transform and Velocity
    void system_movement(float dt) {
        velocities_.each([&](EntityId id, Velocity& v) {
            if (!has_transform(id)) return;
            auto& t = transform(id);
            t.x += v.dx * dt;
            t.y += v.dy * dt;
            t.z += v.dz * dt;
        });
    }

    // Apply gravity (downward acceleration on Y) to all velocity-bearing entities
    void system_gravity(float dt, float g = -9.8f) {
        velocities_.each([&](EntityId /*id*/, Velocity& v) {
            v.dy += g * dt;
        });
    }

    // Kill entities whose HP has dropped to zero
    void system_death() {
        std::vector<EntityId> to_kill;
        healths_.each([&](EntityId id, Health& h) {
            if (h.hp <= 0.f) { h.alive = false; to_kill.push_back(id); }
        });
        for (EntityId id : to_kill) {
            std::cout << "  [World] Entity " << id
                      << " (" << name(id) << ") died.\n";
            destroy(id);
        }
    }

    // Simple renderer: print position of every entity with a Transform
    void system_render(int frame) const {
        std::cout << "  Frame " << frame << " — " << entity_count() << " entities:\n";
        // Const-safe iteration: iterate underlying map directly
        for (auto& [id, t] : transforms_.data) {
            std::cout << "    [" << id << "] " << tags_.data.at(id).name
                      << "  pos=(" << t.x << "," << t.y << "," << t.z << ")\n";
        }
    }

    // Dump all component counts (diagnostic)
    void debug_dump() const {
        std::cout << "  World dump:\n"
                  << "    alive entities : " << alive_.size()    << "\n"
                  << "    transforms     : " << transforms_.data.size() << "\n"
                  << "    velocities     : " << velocities_.data.size() << "\n"
                  << "    healths        : " << healths_.data.size()    << "\n";
    }

private:
    EntityId                      next_id_{0};
    std::vector<EntityId>         free_list_;
    std::unordered_set<EntityId>  alive_;

    ComponentStore<Transform>  transforms_;
    ComponentStore<Velocity>   velocities_;
    ComponentStore<Health>     healths_;
    ComponentStore<Tag>        tags_;

    void assert_alive(EntityId id) const {
        if (!alive_.count(id))
            throw std::runtime_error("Entity " + std::to_string(id) + " is not alive");
    }
};

// ─── Demo ─────────────────────────────────────────────────────────────────────

int main() {
    World world;

    std::cout << "=== ECS World Demo ===\n\n";

    // ── Spawn entities ────────────────────────────────────────────────────
    EntityId player = world.create("Player");
    world.add_transform(player, {0.f, 10.f, 0.f});
    world.add_velocity(player,  {1.f, 0.f,  0.f});
    world.add_health(player,    {100.f, 100.f});

    EntityId enemy1 = world.create("Enemy_0");
    world.add_transform(enemy1, {5.f, 10.f, 0.f});
    world.add_velocity(enemy1,  {-0.5f, 0.f, 0.f});
    world.add_health(enemy1,    {30.f, 30.f});

    EntityId enemy2 = world.create("Enemy_1");
    world.add_transform(enemy2, {-5.f, 10.f, 0.f});
    world.add_velocity(enemy2,  {0.5f, 0.f, 0.f});
    world.add_health(enemy2,    {20.f, 20.f});

    // A static prop (Transform only — no velocity, no health)
    EntityId crate = world.create("Crate");
    world.add_transform(crate, {0.f, 0.f, 3.f});

    std::cout << "Spawned " << world.entity_count() << " entities.\n\n";
    world.debug_dump();
    std::cout << "\n";

    // ── Simulation loop ───────────────────────────────────────────────────
    constexpr float DT = 0.5f;
    constexpr int   FRAMES = 6;

    for (int frame = 1; frame <= FRAMES; ++frame) {
        std::cout << "--- Frame " << frame << " ---\n";

        // Deal damage to enemies each frame so they die mid-run
        if (world.alive(enemy1)) world.health(enemy1).hp -= 12.f;
        if (world.alive(enemy2)) world.health(enemy2).hp -= 11.f;

        world.system_gravity(DT);
        world.system_movement(DT);
        world.system_death();
        world.system_render(frame);
        std::cout << "\n";
    }

    // ── Entity recycling demo ─────────────────────────────────────────────
    std::cout << "--- Recycling crate id, spawning NPC ---\n";
    world.destroy(crate);
    EntityId npc = world.create("NPC");           // should reuse crate's id
    world.add_transform(npc, {2.f, 0.f, 0.f});
    std::cout << "  Crate was id=" << crate
              << ", NPC got id=" << npc << "\n\n";

    world.debug_dump();
    std::cout << "\n=== Done ===\n";
    return 0;
}
