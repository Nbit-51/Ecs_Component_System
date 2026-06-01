#pragma once

#include <string>

namespace ecs::components {

// ── Spatial ────────────────────────────────────────────────────────────────

struct Position {
    float x{0.f}, y{0.f}, z{0.f};
};

struct Velocity {
    float dx{0.f}, dy{0.f}, dz{0.f};
};

struct Rotation {
    float pitch{0.f}, yaw{0.f}, roll{0.f};
};

struct Scale {
    float x{1.f}, y{1.f}, z{1.f};
};

// ── Physics ────────────────────────────────────────────────────────────────

struct RigidBody {
    float mass{1.f};
    float restitution{0.5f};
    bool  is_kinematic{false};
};

struct AABB {
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
};

// ── Gameplay ───────────────────────────────────────────────────────────────

struct Health {
    float current{100.f};
    float maximum{100.f};

    [[nodiscard]] float pct()  const noexcept { return current / maximum; }
    [[nodiscard]] bool  dead() const noexcept { return current <= 0.f; }
};

struct Tag {
    std::string name;
};

struct Lifetime {
    float remaining{1.f}; // seconds
};

} // namespace ecs::components
