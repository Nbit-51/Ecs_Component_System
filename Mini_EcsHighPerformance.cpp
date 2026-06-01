#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>

// --- Basic Types ---
using Entity = uint32_t;
const Entity MAX_ENTITIES = 100000;

// --- Components ---
struct Position { float x, y; };
struct Velocity { float dx, dy; };

// --- The Sparse Set Container ---
// This is the "Engine Room" of a high-performance ECS
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void entity_destroyed(Entity e) = 0;
};

template<typename T>
class ComponentPool : public IComponentPool {
public:
    ComponentPool() {
        sparse.assign(MAX_ENTITIES, -1);
    }

    void assign(Entity e, T component) {
        if (sparse[e] != -1) return; // Already has component

        // Map entity to the end of the dense array
        sparse[e] = static_cast<int>(dense_components.size());
        dense_entities.push_back(e);
        dense_components.push_back(component);
    }

    void entity_destroyed(Entity e) override {
        if (sparse[e] == -1) return;

        // The "Swap and Pop" trick:
        // Move the last element into the hole of the deleted element
        // to keep the dense array packed.
        size_t index_to_remove = sparse[e];
        size_t last_index = dense_components.size() - 1;
        Entity last_entity = dense_entities[last_index];

        dense_components[index_to_remove] = dense_components[last_index];
        dense_entities[index_to_remove] = last_entity;

        // Update the sparse map for the moved entity
        sparse[last_entity] = index_to_remove;
        sparse[e] = -1;

        dense_components.pop_back();
        dense_entities.pop_back();
    }

    T& get(Entity e) { return dense_components[sparse[e]]; }
    
    // Systems use these for blazing fast iteration
    std::vector<T>& data() { return dense_components; }
    std::vector<Entity>& entities() { return dense_entities; }

private:
    std::vector<int> sparse;           // Map: Entity ID -> Index in Dense Array
    std::vector<Entity> dense_entities; // Packed Entity IDs
    std::vector<T> dense_components;    // Packed Components (Cache Friendly)
};

// --- The Registry (The "World") ---
class Registry {
public:
    Entity create_entity() {
        return next_entity++;
    }

    template<typename T>
    void add_component(Entity e, T component) {
        get_pool<T>()->assign(e, component);
    }

    template<typename T>
    ComponentPool<T>* get_pool() {
        auto type_hash = typeid(T).hash_code();
        if (pools.find(type_hash) == pools.end()) {
            pools[type_hash] = std::make_unique<ComponentPool<T>>();
        }
        return static_cast<ComponentPool<T>*>(pools[type_hash].get());
    }

private:
    Entity next_entity = 0;
    std::unordered_map<size_t, std::unique_ptr<IComponentPool>> pools;
};

// --- The System ---
void MovementSystem(Registry& reg, float dt) {
    auto pos_pool = reg.get_pool<Position>();
    auto vel_pool = reg.get_pool<Velocity>();

    // High Performance: We iterate over the packed arrays
    auto& positions = pos_pool->data();
    auto& entities = pos_pool->entities();

    for (size_t i = 0; i < entities.size(); ++i) {
        Entity e = entities[i];
        // In a real production ECS, you would use a 'View' to 
        // ensure the entity has both components before this step.
        auto& pos = positions[i];
        auto& vel = vel_pool->get(e);

        pos.x += vel.dx * dt;
        pos.y += vel.dy * dt;
    }
}

int main() {
    Registry world;

    // Create 10,000 entities
    for (int i = 0; i < 10000; ++i) {
        Entity e = world.create_entity();
        world.add_component(e, Position{ (float)i, (float)i });
        world.add_component(e, Velocity{ 1.0f, 1.0f });
    }

    std::cout << "Running Movement System on 10,000 entities...\n";
    MovementSystem(world, 0.016f);
    
    auto& p = world.get_pool<Position>()->get(0);
    std::cout << "Entity 0 new position: " << p.x << ", " << p.y << "\n";

    return 0;
}
