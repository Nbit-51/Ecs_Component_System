#include <iostream>
#include <chrono>
#include <random>
#include "components.h"
#include "ecs_world.h"
#include "systems.h"
#include "oop_world.h"

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(
        high_resolution_clock::now().time_since_epoch()).count();
}

void run_ecs_demo() {
    std::cout << "\n--- ECS DEMO ---\n";
    World world;

    EntityID warrior = world.create_entity();
    world.add_position (warrior, {100.0f, 200.0f, 0.0f});
    world.add_velocity (warrior, {0.0f, 0.0f, 0.0f});
    world.add_health   (warrior, {100.0f, 100.0f, 2.0f, true});
    world.add_ai       (warrior, {AIState::Patrol, -1, 150.0f, 20.0f, 0.0f});
    world.add_render   (warrior, {1, 1.0f, 0.0f, 0.2f,0.4f,1.0f,1.0f, true, 1});
    world.add_collision(warrior, {CollisionShape::Circle, 12.0f});
    std::cout << "[ECS] Created Warrior (ID: " << warrior << ")\n";

    EntityID arrow = world.create_entity();
    world.add_position(arrow, {100.0f, 200.0f, 0.0f});
    world.add_velocity(arrow, {80.0f, 40.0f, 0.0f});
    world.add_physics (arrow, {0.1f, 9.81f, 0.01f});
    world.add_render  (arrow, {5, 0.5f, 0.3f, 1.0f,0.8f,0.2f,1.0f, true, 2});
    std::cout << "[ECS] Created Arrow (ID: " << arrow << ")\n";

    EntityID tree = world.create_entity();
    world.add_position (tree, {400.0f, 300.0f, 0.0f});
    world.add_health   (tree, {50.0f, 50.0f, 0.0f, true});
    world.add_render   (tree, {3, 2.0f, 0.0f, 0.2f,0.8f,0.2f,1.0f, true, 0});
    world.add_collision(tree, {CollisionShape::Circle, 15.0f});
    std::cout << "[ECS] Created Tree (ID: " << tree << ")\n";

    EntityID boss = world.create_entity();
    world.add_position (boss, {250.0f, 250.0f, 0.0f});
    world.add_velocity (boss, {0.0f, 0.0f, 0.0f});
    world.add_health   (boss, {500.0f, 500.0f, 5.0f, true});
    world.add_ai       (boss, {AIState::Patrol, warrior, 200.0f, 30.0f, 0.0f});
    world.add_collision(boss, {CollisionShape::Circle, 30.0f});
    world.add_render   (boss, {10, 2.5f, 0.0f, 1.0f,0.1f,0.1f,1.0f, true, 3});
    world.add_sound    (boss, {5, 1.0f, 1.0f, true, true});
    world.add_physics  (boss, {5.0f, 9.81f, 0.05f});
    std::cout << "[ECS] Created Boss (ID: " << boss << ")\n";

    world.add_poison(warrior, {10.0f, 1.0f, 5.0f, 0.0f});
    world.add_poison(boss,    {5.0f,  2.0f, 10.0f, 0.0f});

    std::cout << "[ECS] Total entities: " << world.entity_count() << "\n";
    
    GameLoop loop;
    loop.run(world, 3);

    world.destroy_entity(arrow);
    std::cout << "[ECS] Arrow destroyed. Remaining: " << world.entity_count() << "\n";
}

void run_oop_demo() {
    std::cout << "\n--- OOP DEMO ---\n";
    OOPWorld world;

    auto* warrior = world.create<Warrior>(100.0f, 200.0f, 100.0f);
    auto* arrow   = world.create<Arrow>  (100.0f, 200.0f, 80.0f, 40.0f, 15.0f);
    auto* tree    = world.create<Tree>   (400.0f, 300.0f, 50.0f);
    auto* boss    = world.create<Boss>   (250.0f, 250.0f);

    std::cout << "[OOP] Created: Warrior, Arrow, Tree, Boss\n";
    warrior->apply_poison(10.0f, 5.0f);
    
    std::cout << "[OOP] Total objects: " << world.count() << "\n";

    float dt = 0.016f;
    for (int f = 0; f < 3; ++f) {
        std::cout << "Frame " << f+1 << " update...\n";
        world.update(dt);
        world.render();
    }
}

void run_benchmark(int N = 100000) {
    std::cout << "\n--- BENCHMARK (N=" << N << ") ---\n";
    std::mt19937 rng(42);
    auto rf = [&](float lo, float hi){
        return lo + (hi-lo)*float(rng())/float(rng.max());
    };

    {
        OOPWorld world;
        for (int i = 0; i < N; ++i) {
            world.create<Warrior>(rf(0,1920), rf(0,1080), 100.0f);
        }

        double t0 = now_ms();
        float dt = 0.016f;
        for (int f = 0; f < 10; ++f) world.update(dt);
        double oop_ms = now_ms() - t0;

        std::cout << "[OOP] Execution: " << oop_ms << " ms (" << oop_ms/10.0 << " ms/avg)\n";
    }

    {
        World world;
        MovementSystem  mov_sys;
        AISystem        ai_sys;
        HealthRegenSystem hp_sys;

        for (int i = 0; i < N; ++i) {
            EntityID id = world.create_entity();
            world.add_position(id, {rf(0,1920), rf(0,1080), 0.0f});
            world.add_velocity(id, {rf(-5,5), rf(-5,5), 0.0f});
            world.add_health  (id, {100.0f, 100.0f, 1.0f, true});
            world.add_ai      (id, {AIState::Patrol, -1, 150.0f, 20.0f, 0.0f});
        }

        double t0 = now_ms();
        float dt = 0.016f;
        for (int f = 0; f < 10; ++f) {
            mov_sys.update(world, dt);
            ai_sys.update(world, dt);
            hp_sys.update(world, dt);
        }
        double ecs_ms = now_ms() - t0;

        std::cout << "[ECS] Execution: " << ecs_ms << " ms (" << ecs_ms/10.0 << " ms/avg)\n";
        std::cout << "[ECS] Dense components: " << world.positions().size() << "\n";
    }
}

int main() {
    std::cout << "ECS vs OOP Performance Test\n";
    run_ecs_demo();
    run_oop_demo();
    run_benchmark(10000);  
    return 0;
}
