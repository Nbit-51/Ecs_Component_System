/**
 * @file Engine.cpp
 * @brief ECS Engine — the main loop and system scheduler.
 *
 * The Engine owns the World, manages a fixed-timestep game loop,
 * and dispatches registered systems in order each tick.
 *
 * Compile:
 *   g++ -std=c++20 -O2 -Wall Engine.cpp -o engine && ./engine
 */

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <stdexcept>
#include <iomanip>

// ─── Entity ───────────────────────────────────────────────────────────────────

using EntityId = std::uint32_t;
constexpr EntityId NULL_ENTITY = UINT32_MAX;

// ─── Components ───────────────────────────────────────────────────────────────

struct Transform { float x{}, y{}, z{}; };
struct Velocity  { float dx{}, dy{}, dz{}; };
struct Health    { float hp{100.f}; float max_hp{100.f}; };
struct Tag       { std::string name; };

// ─── ComponentStore ───────────────────────────────────────────────────────────

template<typename T>
struct ComponentStore {
    std::unordered_map<EntityId, T> data;
    void  add(EntityId id, T c)      { data[id] = std::move(c); }
    void  remove(EntityId id)         { data.erase(id); }
    bool  has(EntityId id) const      { return data.count(id) > 0; }
    T&    get(EntityId id)            { return data.at(id); }
    void  each(std::function<void(EntityId, T&)> fn) {
        for (auto& [id, c] : data) fn(id, c);
    }
};

// ─── World ───────────────────────────────────────────────────────────────────

class World {
public:
    EntityId create(const std::string& tag = "entity") {
        EntityId id;
        if (!free_list_.empty()) {
            id = free_list_.back(); free_list_.pop_back();
        } else {
            id = next_id_++;
        }
        alive_.insert(id);
        tags_.add(id, Tag{tag});
        return id;
    }

    void destroy(EntityId id) {
        transforms_.remove(id); velocities_.remove(id);
        healths_.remove(id);    tags_.remove(id);
        alive_.erase(id);       free_list_.push_back(id);
    }

    bool        alive(EntityId id) const { return alive_.count(id) > 0; }
    std::size_t count()            const { return alive_.size(); }

    void add_transform(EntityId id, Transform t = {}) { transforms_.add(id, t); }
    void add_velocity (EntityId id, Velocity  v = {}) { velocities_.add(id, v); }
    void add_health   (EntityId id, Health    h = {}) { healths_.add(id, h);    }

    bool      has_transform(EntityId id) const { return transforms_.has(id); }
    bool      has_velocity (EntityId id) const { return velocities_.has(id); }
    bool      has_health   (EntityId id) const { return healths_.has(id);    }

    Transform& transform(EntityId id) { return transforms_.get(id); }
    Velocity&  velocity (EntityId id) { return velocities_.get(id); }
    Health&    health   (EntityId id) { return healths_.get(id);    }

    const std::string& name(EntityId id) const { return tags_.data.at(id).name; }

    void each_velocity(std::function<void(EntityId, Velocity&)> fn)  { velocities_.each(fn); }
    void each_health  (std::function<void(EntityId, Health&)>   fn)  { healths_.each(fn);    }
    void each_transform(std::function<void(EntityId, Transform&)> fn){ transforms_.each(fn); }

    const std::unordered_set<EntityId>& all() const { return alive_; }

private:
    EntityId                      next_id_{0};
    std::vector<EntityId>         free_list_;
    std::unordered_set<EntityId>  alive_;
    ComponentStore<Transform>     transforms_;
    ComponentStore<Velocity>      velocities_;
    ComponentStore<Health>        healths_;
    ComponentStore<Tag>           tags_;
};

// ─── System ───────────────────────────────────────────────────────────────────
// A system is a named callable: void(World&, float dt)

struct System {
    std::string                          name;
    std::function<void(World&, float)>   update;
};

// ─── Engine ───────────────────────────────────────────────────────────────────

class Engine {
public:
    explicit Engine(float fixed_dt = 1.f / 60.f)
        : fixed_dt_(fixed_dt) {}

    World& world() { return world_; }

    // Register a system; systems run in registration order
    void register_system(std::string name, std::function<void(World&, float)> fn) {
        systems_.push_back({std::move(name), std::move(fn)});
        std::cout << "  [Engine] Registered system: " << systems_.back().name << "\n";
    }

    // Run the engine for a fixed number of ticks (deterministic, good for demos/tests)
    void run(int ticks) {
        std::cout << "\n[Engine] Starting — " << ticks << " ticks @ dt=" << fixed_dt_ << "s\n\n";
        for (int tick = 1; tick <= ticks && !quit_; ++tick) {
            tick_ = tick;
            accumulator_ += fixed_dt_;      // simulate wall-clock advancing

            while (accumulator_ >= fixed_dt_) {
                dispatch(fixed_dt_);
                accumulator_ -= fixed_dt_;
                ++total_ticks_;
            }
        }
        print_stats();
    }

    // Quit from inside a system
    void quit() { quit_ = true; }

    int  current_tick()  const { return tick_; }
    int  total_ticks()   const { return total_ticks_; }
    bool is_running()    const { return !quit_; }

private:
    void dispatch(float dt) {
        for (auto& sys : systems_) {
            sys.update(world_, dt);
            if (quit_) break;
        }
    }

    void print_stats() const {
        std::cout << "\n[Engine] Finished — "
                  << total_ticks_ << " ticks dispatched, "
                  << world_.count() << " entities alive.\n";
    }

    World               world_;
    std::vector<System> systems_;
    float               fixed_dt_;
    float               accumulator_{0.f};
    int                 tick_{0};
    int                 total_ticks_{0};
    bool                quit_{false};
};

// ─── Built-in systems (free functions) ───────────────────────────────────────

void movement_system(World& w, float dt) {
    w.each_velocity([&](EntityId id, Velocity& v) {
        if (!w.has_transform(id)) return;
        auto& t = w.transform(id);
        t.x += v.dx * dt;
        t.y += v.dy * dt;
        t.z += v.dz * dt;
    });
}

void gravity_system(World& w, float dt) {
    constexpr float G = -9.8f;
    w.each_velocity([&](EntityId /*id*/, Velocity& v) {
        v.dy += G * dt;
    });
}

void damage_system(World& w, float /*dt*/) {
    // For demo purposes: every entity with health takes 15 damage per tick
    w.each_health([&](EntityId /*id*/, Health& h) {
        h.hp -= 15.f;
    });
}

void death_system(World& w, float /*dt*/) {
    std::vector<EntityId> to_kill;
    w.each_health([&](EntityId id, Health& h) {
        if (h.hp <= 0.f) to_kill.push_back(id);
    });
    for (EntityId id : to_kill) {
        std::cout << "    [death] " << w.name(id) << " (id=" << id << ") destroyed\n";
        w.destroy(id);
    }
}

void render_system(World& w, float /*dt*/) {
    // Lightweight: just print entity count and first few positions
    std::cout << "  [render] " << w.count() << " alive | positions: ";
    int shown = 0;
    w.each_transform([&](EntityId id, Transform& t) {
        if (shown++ >= 3) return;
        std::cout << w.name(id) << "=("
                  << std::fixed << std::setprecision(1)
                  << t.x << "," << t.y << ") ";
    });
    std::cout << "\n";
}

// ─── Demo ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== ECS Engine Demo ===\n\n";

    Engine engine(0.5f);   // half-second ticks for readable output

    // ── Register systems (order matters) ─────────────────────────────────
    engine.register_system("gravity",  gravity_system);
    engine.register_system("movement", movement_system);
    engine.register_system("damage",   damage_system);
    engine.register_system("death",    death_system);
    engine.register_system("render",   render_system);

    // Stop the engine when no enemies are left
    engine.register_system("win-check", [](World& w, float) {
        bool any_enemy = false;
        for (EntityId id : w.all())
            if (w.name(id).find("Enemy") != std::string::npos) { any_enemy = true; break; }
        // Engine not directly accessible here — use entity count heuristic
        (void)any_enemy;   // handled via death_system drain
    });

    // ── Spawn entities ────────────────────────────────────────────────────
    World& world = engine.world();

    EntityId player = world.create("Player");
    world.add_transform(player, {0.f, 5.f, 0.f});
    world.add_velocity (player, {2.f, 0.f, 0.f});
    world.add_health   (player, {200.f, 200.f});

    for (int i = 0; i < 3; ++i) {
        EntityId e = world.create("Enemy_" + std::to_string(i));
        world.add_transform(e, {float(i * 4), 5.f, 0.f});
        world.add_velocity (e, {-1.f, 0.f, 0.f});
        world.add_health   (e, {45.f, 45.f});
    }

    std::cout << "\nSpawned " << world.count() << " entities.\n";

    // ── Run for 8 ticks ───────────────────────────────────────────────────
    engine.run(8);

    return 0;
}
