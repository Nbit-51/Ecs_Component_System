#pragma once

#include "ecs/Registry.hpp"
#include "Components.hpp"

namespace ecs {

// ─── System interface ─────────────────────────────────────────────────────────

/**
 * @brief Optional base class for systems.
 *
 * Systems are plain functions / lambdas in ECS — this class is a
 * convenience base for systems that need update() called each frame.
 */
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void update(Registry& reg, float dt) = 0;
};

// ─── Built-in systems ─────────────────────────────────────────────────────────

/**
 * @brief Integrates velocity into position each frame.
 *
 *   p += v * dt
 *
 * Cache-friendly: iterates two contiguous pools simultaneously.
 */
class MovementSystem final : public ISystem {
public:
    void update(Registry& reg, float dt) override {
        using namespace components;
        reg.view<Position, Velocity>([dt](Position& p, Velocity& v) {
            p.x += v.dx * dt;
            p.y += v.dy * dt;
            p.z += v.dz * dt;
        });
    }
};

/**
 * @brief Ticks down Lifetime components; destroys entities at zero.
 */
class LifetimeSystem final : public ISystem {
public:
    void update(Registry& reg, float dt) override {
        using namespace components;
        std::vector<Entity> to_kill;

        reg.view<Lifetime>([&](Entity e, Lifetime& lt) {
            lt.remaining -= dt;
            if (lt.remaining <= 0.f)
                to_kill.push_back(e);
        });

        for (auto e : to_kill)
            reg.destroy(e);
    }
};

/**
 * @brief Zero out velocities of dead entities (Health::dead()).
 */
class DeathSystem final : public ISystem {
public:
    void update(Registry& reg, float /*dt*/) override {
        using namespace components;
        std::vector<Entity> to_kill;

        reg.view<Health>([&](Entity e, Health& h) {
            if (h.dead()) to_kill.push_back(e);
        });

        for (auto e : to_kill) {
            if (reg.has<Velocity>(e))
                reg.get<Velocity>(e) = {};
            reg.destroy(e);
        }
    }
};

} // namespace ecs
