/**
 * @file Systems.cpp
 * @brief ECS Systems — demonstrates various system patterns in an ECS.
 *
 * Shows: Movement, Combat, Rendering, AI, and Cleanup systems
 * all operating on the same entity pool independently.
 *
 * Compile:
 *   g++ -std=c++20 -O2 -Wall Systems.cpp -o systems && ./systems
 */

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>

using EntityId = std::uint32_t;

// ─── Components ───────────────────────────────────────────────────────────────
struct Position  { float x{}, y{}, z{}; };
struct Velocity  { float dx{}, dy{}, dz{}; };
struct Health    { float hp{100.f}; float max_hp{100.f}; };
struct Attack    { float damage{10.f}; float range{2.f}; float cooldown{0.f}; };
struct AI        { float aggro_range{15.f}; EntityId target{UINT32_MAX}; };
struct Renderable{ std::string sprite; bool visible{true}; };
struct Tag       { std::string name; };

// ─── Flat registry ────────────────────────────────────────────────────────────
struct Registry {
    std::unordered_map<EntityId, Position>   positions;
    std::unordered_map<EntityId, Velocity>   velocities;
    std::unordered_map<EntityId, Health>     healths;
    std::unordered_map<EntityId, Attack>     attacks;
    std::unordered_map<EntityId, AI>         ais;
    std::unordered_map<EntityId, Renderable> renderables;
    std::unordered_map<EntityId, Tag>        tags;
    std::vector<EntityId>                    dead;
    EntityId next{0};

    EntityId spawn(std::string name) {
        EntityId id = next++;
        tags[id] = {std::move(name)};
        return id;
    }
    std::string name(EntityId id) {
        auto it = tags.find(id);
        return it != tags.end() ? it->second.name : "?";
    }
};

// ─── Systems ──────────────────────────────────────────────────────────────────

// 1. Movement System
void movement_system(Registry& reg, float dt) {
    for (auto& [id, pos] : reg.positions) {
        auto it = reg.velocities.find(id);
        if (it == reg.velocities.end()) continue;
        pos.x += it->second.dx * dt;
        pos.y += it->second.dy * dt;
        pos.z += it->second.dz * dt;
    }
}

// 2. AI System — finds nearest enemy and sets velocity toward it
void ai_system(Registry& reg) {
    for (auto& [id, ai] : reg.ais) {
        auto pos_it = reg.positions.find(id);
        if (pos_it == reg.positions.end()) continue;

        float best_dist = ai.aggro_range;
        ai.target = UINT32_MAX;

        // Find closest entity with health (potential target)
        for (auto& [tid, th] : reg.healths) {
            if (tid == id) continue;
            auto tpos_it = reg.positions.find(tid);
            if (tpos_it == reg.positions.end()) continue;

            float dx = tpos_it->second.x - pos_it->second.x;
            float dy = tpos_it->second.y - pos_it->second.y;
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist < best_dist) {
                best_dist = dist;
                ai.target = tid;
            }
        }

        // Move toward target
        if (ai.target != UINT32_MAX) {
            auto& tpos = reg.positions[ai.target];
            float dx = tpos.x - pos_it->second.x;
            float dy = tpos.y - pos_it->second.y;
            float dist = std::sqrt(dx*dx + dy*dy);
            if (dist > 0.01f) {
                reg.velocities[id] = {dx/dist * 3.f, dy/dist * 3.f, 0.f};
            }
        }
    }
}

// 3. Combat System — deals damage when in range
void combat_system(Registry& reg, float dt) {
    for (auto& [id, atk] : reg.attacks) {
        atk.cooldown -= dt;
        if (atk.cooldown > 0.f) continue;

        auto ai_it = reg.ais.find(id);
        if (ai_it == reg.ais.end() || ai_it->second.target == UINT32_MAX) continue;

        EntityId target = ai_it->second.target;
        auto pos_it  = reg.positions.find(id);
        auto tpos_it = reg.positions.find(target);
        if (pos_it == reg.positions.end() || tpos_it == reg.positions.end()) continue;

        float dx   = tpos_it->second.x - pos_it->second.x;
        float dy   = tpos_it->second.y - pos_it->second.y;
        float dist = std::sqrt(dx*dx + dy*dy);

        if (dist <= atk.range) {
            auto th = reg.healths.find(target);
            if (th != reg.healths.end()) {
                th->second.hp -= atk.damage;
                std::cout << "  [Combat] " << reg.name(id)
                          << " hits " << reg.name(target)
                          << " for " << atk.damage << " dmg"
                          << " (hp=" << th->second.hp << ")\n";
                atk.cooldown = 1.f;
            }
        }
    }
}

// 4. Death System — marks dead entities for cleanup
void death_system(Registry& reg) {
    for (auto& [id, h] : reg.healths)
        if (h.hp <= 0.f) reg.dead.push_back(id);

    for (EntityId id : reg.dead) {
        std::cout << "  [Death] " << reg.name(id) << " (id=" << id << ") died\n";
        reg.positions.erase(id);
        reg.velocities.erase(id);
        reg.healths.erase(id);
        reg.attacks.erase(id);
        reg.ais.erase(id);
        reg.renderables.erase(id);
    }
    reg.dead.clear();
}

// 5. Render System — prints visible entities
void render_system(const Registry& reg, int frame) {
    std::cout << "  [Render] Frame " << frame << ": ";
    for (auto& [id, r] : reg.renderables) {
        if (!r.visible) continue;
        auto pit = reg.positions.find(id);
        if (pit == reg.positions.end()) continue;
        std::cout << r.sprite << "@("
                  << pit->second.x << "," << pit->second.y << ") ";
    }
    std::cout << "\n";
}

// ─── Demo ─────────────────────────────────────────────────────────────────────
int main() {
    std::cout << "=== ECS Systems Demo ===\n\n";

    Registry reg;

    // Player
    EntityId player = reg.spawn("Player");
    reg.positions[player]   = {0.f, 0.f, 0.f};
    reg.velocities[player]  = {0.f, 0.f, 0.f};
    reg.healths[player]     = {100.f, 100.f};
    reg.renderables[player] = {"P", true};

    // Enemies with AI
    for (int i = 0; i < 3; ++i) {
        EntityId e = reg.spawn("Enemy_" + std::to_string(i));
        reg.positions[e]   = {float(i*5 + 8), float(i*2), 0.f};
        reg.velocities[e]  = {0.f, 0.f, 0.f};
        reg.healths[e]     = {30.f, 30.f};
        reg.attacks[e]     = {15.f, 1.5f, 0.f};
        reg.ais[e]         = {20.f, UINT32_MAX};
        reg.renderables[e] = {"E", true};
    }

    std::cout << "Spawned " << reg.next << " entities\n\n";

    // Run simulation
    constexpr float DT = 0.5f;
    for (int frame = 1; frame <= 12; ++frame) {
        ai_system(reg);
        movement_system(reg, DT);
        combat_system(reg, DT);
        death_system(reg);
        render_system(reg, frame);
        if (reg.positions.empty()) { std::cout << "  All enemies defeated!\n"; break; }
    }

    std::cout << "\n=== Done ===\n";
    return 0;
}
