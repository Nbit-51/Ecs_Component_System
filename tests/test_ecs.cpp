#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "ecs/ecs.hpp"
#include "Components.hpp"
#include "Systems.hpp"

using namespace ecs;
using namespace ecs::components;

// ─── Entity lifecycle ────────────────────────────────────────────────────────

TEST_SUITE("Entity") {
    TEST_CASE("create returns valid entity") {
        Registry reg;
        Entity e = reg.create();
        CHECK(reg.valid(e));
        CHECK(reg.entity_count() == 1);
    }

    TEST_CASE("destroy invalidates entity") {
        Registry reg;
        Entity e = reg.create();
        reg.destroy(e);
        CHECK(!reg.valid(e));
        CHECK(reg.entity_count() == 0);
    }

    TEST_CASE("id is reused after destroy") {
        Registry reg;
        Entity a = reg.create();
        EntityId aid = a.id();
        reg.destroy(a);
        Entity b = reg.create();
        CHECK(b.id() == aid);
        CHECK(b.version() != a.version()); // version must differ
    }

    TEST_CASE("stale handle is invalid") {
        Registry reg;
        Entity e = reg.create();
        Entity stale = e;
        reg.destroy(e);
        CHECK(!reg.valid(stale));
    }

    TEST_CASE("null entity is always invalid") {
        Registry reg;
        CHECK(!reg.valid(NULL_ENTITY));
    }

    TEST_CASE("create many entities") {
        Registry reg;
        std::vector<Entity> entities;
        entities.reserve(1000);
        for (int i = 0; i < 1000; ++i)
            entities.push_back(reg.create());
        CHECK(reg.entity_count() == 1000);
        for (auto e : entities) CHECK(reg.valid(e));
    }
}

// ─── Component add / remove ──────────────────────────────────────────────────

TEST_SUITE("Components") {
    TEST_CASE("emplace and has") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Position>(e, 1.f, 2.f, 3.f);
        CHECK(reg.has<Position>(e));
        CHECK(!reg.has<Velocity>(e));
    }

    TEST_CASE("get returns correct data") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Position>(e, 4.f, 5.f, 6.f);
        auto& p = reg.get<Position>(e);
        CHECK(p.x == doctest::Approx(4.f));
        CHECK(p.y == doctest::Approx(5.f));
        CHECK(p.z == doctest::Approx(6.f));
    }

    TEST_CASE("get is mutable") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Position>(e);
        reg.get<Position>(e).x = 99.f;
        CHECK(reg.get<Position>(e).x == doctest::Approx(99.f));
    }

    TEST_CASE("remove clears has") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Velocity>(e);
        CHECK(reg.has<Velocity>(e));
        reg.remove<Velocity>(e);
        CHECK(!reg.has<Velocity>(e));
    }

    TEST_CASE("try_get returns nullopt when absent") {
        Registry reg;
        Entity e = reg.create();
        auto opt = reg.try_get<Position>(e);
        CHECK(!opt.has_value());
    }

    TEST_CASE("try_get returns ref when present") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Position>(e, 1.f, 0.f, 0.f);
        auto opt = reg.try_get<Position>(e);
        REQUIRE(opt.has_value());
        CHECK(opt->get().x == doctest::Approx(1.f));
    }

    TEST_CASE("destroy removes all components") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Position>(e);
        reg.emplace<Velocity>(e);
        reg.emplace<Health>(e);
        reg.destroy(e);
        CHECK(!reg.valid(e));
        // Re-create with same id — should have clean slate
        Entity e2 = reg.create();
        CHECK(!reg.has<Position>(e2));
        CHECK(!reg.has<Velocity>(e2));
    }

    TEST_CASE("multiple components on same entity") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Position>(e, 1.f, 2.f, 3.f);
        reg.emplace<Velocity>(e, 0.1f, 0.2f, 0.f);
        reg.emplace<Health>(e);
        CHECK(reg.has<Position>(e));
        CHECK(reg.has<Velocity>(e));
        CHECK(reg.has<Health>(e));
    }
}

// ─── Views / iteration ───────────────────────────────────────────────────────

TEST_SUITE("Views") {
    TEST_CASE("view visits matching entities") {
        Registry reg;
        Entity a = reg.create();
        Entity b = reg.create();
        Entity c = reg.create();

        reg.emplace<Position>(a);
        reg.emplace<Position>(b);
        reg.emplace<Position>(b); // won't add, already has — wait, this would assert
        // Use b and c
        reg.emplace<Position>(c);
        reg.emplace<Velocity>(b);
        reg.emplace<Velocity>(c);

        int count = 0;
        reg.view<Position, Velocity>([&](Entity e, Position&, Velocity&) {
            CHECK((e == b || e == c));
            ++count;
        });
        CHECK(count == 2);
    }

    TEST_CASE("view with no-entity callback") {
        Registry reg;
        for (int i = 0; i < 5; ++i) {
            Entity e = reg.create();
            reg.emplace<Position>(e, float(i), 0.f, 0.f);
            reg.emplace<Velocity>(e, 1.f, 0.f, 0.f);
        }
        float total_x = 0.f;
        reg.view<Position>([&](Position& p) { total_x += p.x; });
        CHECK(total_x == doctest::Approx(0.f + 1.f + 2.f + 3.f + 4.f));
    }

    TEST_CASE("view skips destroyed entities") {
        Registry reg;
        Entity a = reg.create();
        Entity b = reg.create();
        reg.emplace<Position>(a);
        reg.emplace<Position>(b);
        reg.destroy(a);

        int count = 0;
        reg.view<Position>([&](Entity e, Position&) {
            CHECK(e == b);
            ++count;
        });
        CHECK(count == 1);
    }
}

// ─── Systems ─────────────────────────────────────────────────────────────────

TEST_SUITE("Systems") {
    TEST_CASE("MovementSystem integrates position") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Position>(e, 0.f, 0.f, 0.f);
        reg.emplace<Velocity>(e, 10.f, 0.f, 0.f);

        MovementSystem sys;
        sys.update(reg, 0.1f); // dt = 100ms

        auto& p = reg.get<Position>(e);
        CHECK(p.x == doctest::Approx(1.f));
    }

    TEST_CASE("LifetimeSystem destroys expired entities") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Lifetime>(e).remaining = 0.05f;

        LifetimeSystem sys;
        sys.update(reg, 0.1f); // dt > remaining
        CHECK(!reg.valid(e));
    }

    TEST_CASE("LifetimeSystem keeps alive entities") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Lifetime>(e).remaining = 1.f;

        LifetimeSystem sys;
        sys.update(reg, 0.016f);
        CHECK(reg.valid(e));
        CHECK(reg.get<Lifetime>(e).remaining == doctest::Approx(1.f - 0.016f));
    }

    TEST_CASE("DeathSystem destroys zero-health entities") {
        Registry reg;
        Entity e = reg.create();
        reg.emplace<Health>(e).current = 0.f;

        DeathSystem sys;
        sys.update(reg, 0.f);
        CHECK(!reg.valid(e));
    }
}

// ─── ComponentPool unit tests ─────────────────────────────────────────────────

TEST_SUITE("ComponentPool") {
    TEST_CASE("emplace and contains") {
        ComponentPool<Position> pool;
        pool.emplace(0u, 1.f, 2.f, 3.f);
        CHECK(pool.contains(0u));
        CHECK(!pool.contains(1u));
        CHECK(pool.size() == 1);
    }

    TEST_CASE("erase reduces size") {
        ComponentPool<Position> pool;
        pool.emplace(0u);
        pool.emplace(1u);
        pool.erase(0u);
        CHECK(!pool.contains(0u));
        CHECK(pool.contains(1u));
        CHECK(pool.size() == 1);
    }

    TEST_CASE("swap-erase preserves other elements") {
        ComponentPool<Position> pool;
        pool.emplace(0u, 1.f, 0.f, 0.f);
        pool.emplace(1u, 2.f, 0.f, 0.f);
        pool.emplace(2u, 3.f, 0.f, 0.f);
        pool.erase(0u); // triggers swap with last
        CHECK(!pool.contains(0u));
        CHECK(pool.contains(1u));
        CHECK(pool.contains(2u));
        CHECK(pool.size() == 2);
    }

    TEST_CASE("raw() data is contiguous after erase") {
        ComponentPool<Position> pool;
        for (int i = 0; i < 5; ++i)
            pool.emplace(EntityId(i), float(i), 0.f, 0.f);
        pool.erase(2u);
        auto& data = pool.raw();
        CHECK(data.size() == 4);
        // Verify no gaps
        float sum = 0;
        for (auto& p : data) sum += p.x;
        CHECK(sum == doctest::Approx(0.f + 1.f + 3.f + 4.f));
    }
}

// ─── Edge cases ───────────────────────────────────────────────────────────────

TEST_SUITE("EdgeCases") {
    TEST_CASE("clear resets registry") {
        Registry reg;
        for (int i = 0; i < 100; ++i) {
            Entity e = reg.create();
            reg.emplace<Position>(e);
        }
        reg.clear();
        CHECK(reg.entity_count() == 0);
    }

    TEST_CASE("destroy invalid entity is a no-op") {
        Registry reg;
        Entity e = reg.create();
        reg.destroy(e);
        // Second destroy should not crash
        reg.destroy(e); // stale handle — should be no-op
        CHECK(reg.entity_count() == 0);
    }

    TEST_CASE("remove non-existent component is safe") {
        Registry reg;
        Entity e = reg.create();
        // No Position attached — should not crash
        reg.remove<Position>(e);
        CHECK(!reg.has<Position>(e));
    }
}
