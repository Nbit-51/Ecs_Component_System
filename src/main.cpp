/**
 * @file src/main.cpp
 * @brief Minimal game-loop example using the ECS.
 *
 * Demonstrates:
 *   - Entity creation with multiple components
 *   - System execution order
 *   - Frame-by-frame simulation (10 frames shown)
 */

#include <cstdio>
#include <cmath>

#include "ecs/ecs.hpp"
#include "Components.hpp"
#include "Systems.hpp"

using namespace ecs;
using namespace ecs::components;

int main() {
    std::puts("=== ECS System v2.0 — Example Game Loop ===\n");

    Registry reg;

    // ── Spawn a hero ───────────────────────────────────────────────────────
    Entity hero = reg.create();
    reg.emplace<Position>(hero, 0.f, 0.f, 0.f);
    reg.emplace<Velocity>(hero, 5.f, 0.f, 0.f);
    reg.emplace<Health>(hero, 100.f, 100.f);
    reg.emplace<Tag>(hero, "Hero");

    // ── Spawn enemies (with lifetime) ──────────────────────────────────────
    for (int i = 0; i < 3; ++i) {
        Entity enemy = reg.create();
        reg.emplace<Position>(enemy, float(i * 10), 0.f, 0.f);
        reg.emplace<Velocity>(enemy, -2.f, 0.f, 0.f);
        reg.emplace<Health>(enemy, 30.f, 30.f);
        reg.emplace<Lifetime>(enemy, 0.2f + float(i) * 0.05f); // staggered despawn
    }

    std::printf("Spawned hero + 3 enemies. Total entities: %zu\n\n", reg.entity_count());

    // ── Systems ────────────────────────────────────────────────────────────
    MovementSystem movement;
    LifetimeSystem lifetimes;
    DeathSystem    deaths;

    // ── Game loop ─────────────────────────────────────────────────────────
    constexpr float DT = 1.f / 60.f; // 60 fps
    constexpr int   FRAMES = 30;

    for (int frame = 0; frame < FRAMES; ++frame) {
        movement.update(reg, DT);
        lifetimes.update(reg, DT);
        deaths.update(reg, DT);

        if (frame % 5 == 0 && reg.valid(hero)) {
            auto& p = reg.get<Position>(hero);
            std::printf("Frame %3d | entities=%zu | hero pos=(%.2f, %.2f, %.2f)\n",
                frame, reg.entity_count(), p.x, p.y, p.z);
        }
    }

    std::puts("\nSimulation complete.");
    std::printf("Final entity count: %zu\n", reg.entity_count());
    return 0;
}
