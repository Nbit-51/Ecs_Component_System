#pragma once
#include <vector>
#include <memory>
#include <string>
#include <iostream>
#include <cmath>
#include <algorithm>
class GameObject {
public:
    int   id;
    float x, y, z;
    bool  alive = true;
    explicit GameObject(int id, float x=0, float y=0, float z=0)
        : id(id), x(x), y(y), z(z) {}
    virtual ~GameObject() = default;
    virtual void update(float dt) = 0;
    virtual void render() const   = 0;
    virtual std::string type() const = 0;
};
class Arrow : public GameObject {
public:
    float vx, vy, vz;
    float damage;
    int   sprite_id;
    Arrow(int id, float x, float y, float vx, float vy, float dmg)
        : GameObject(id, x, y, 0), vx(vx), vy(vy), vz(0),
          damage(dmg), sprite_id(5) {}
    void update(float dt) override {
        vy -= 9.81f * dt;
        x += vx * dt;
        y += vy * dt;
        if (y < 0) alive = false;
    }
    void render() const override {
        std::cout << "[Render] Arrow #" << id
                  << " at (" << x << "," << y << ")\n";
    }
    std::string type() const override { return "Arrow"; }
};
class Tree : public GameObject {
public:
    float hp, max_hp;
    float radius;
    int   sprite_id;
    Tree(int id, float x, float y, float hp=50)
        : GameObject(id, x, y, 0), hp(hp), max_hp(hp),
          radius(15.0f), sprite_id(3) {}
    void update(float dt) override {
        if (hp <= 0) alive = false;
    }
    void render() const override {
        std::cout << "[Render] Tree #" << id
                  << " at (" << x << "," << y
                  << ") HP:" << hp << "\n";
    }
    void take_damage(float dmg) { hp -= dmg; }
    std::string type() const override { return "Tree"; }
};
class Warrior : public GameObject {
public:
    float vx, vy;
    float hp, max_hp;
    float regen;
    float radius;
    int   sprite_id;
    enum class State { Patrol, Chase, Attack, Flee };
    State ai_state    = State::Patrol;
    int   target_id   = -1;
    float patrol_angle= 0.0f;
    float detect_range= 150.0f;
    int   sound_id    = 2;
    float volume      = 1.0f;
    bool  is_poisoned      = false;
    float poison_damage    = 0.0f;
    float poison_duration  = 0.0f;
    float poison_timer     = 0.0f;
    Warrior(int id, float x, float y, float hp=100)
        : GameObject(id, x, y, 0), vx(0), vy(0),
          hp(hp), max_hp(hp), regen(1.0f), radius(12.0f),
          sprite_id(1) {}
    void update(float dt) override {
        update_ai(dt);
        x += vx * dt;
        y += vy * dt;
        hp = std::min(hp + regen * dt, max_hp);
        if (is_poisoned) {
            poison_timer -= dt;
            poison_duration -= dt;
            if (poison_timer <= 0) {
                hp -= poison_damage;
                poison_timer = 1.0f;
            }
            if (poison_duration <= 0) is_poisoned = false;
        }
        if (hp <= 0) { alive = false; ai_state = State::Flee; }
    }
    void update_ai(float dt) {
        switch (ai_state) {
            case State::Patrol:
                patrol_angle += 0.5f * dt;
                vx = std::cos(patrol_angle) * 30.0f;
                vy = std::sin(patrol_angle) * 30.0f;
                break;
            case State::Chase:
                break;
            case State::Attack:
                std::cout << "[OOP] Warrior #" << id << " attacks!\n";
                break;
            case State::Flee:
                vx = -vx * 2.0f;
                vy = -vy * 2.0f;
                break;
        }
    }
    void render() const override {
        std::cout << "[Render] Warrior #" << id
                  << " at (" << x << "," << y
                  << ") HP:" << hp
                  << " State:" << static_cast<int>(ai_state) << "\n";
    }
    void apply_poison(float dmg, float dur) {
        is_poisoned    = true;
        poison_damage  = dmg;
        poison_duration= dur;
        poison_timer   = 1.0f;
    }
    std::string type() const override { return "Warrior"; }
};
class Boss : public GameObject {
public:
    float vx, vy;
    float hp, max_hp;
    float regen;
    float radius;
    int   sprite_id;
    int   sound_id;
    float volume;
    float patrol_angle = 0.0f;
    enum class State { Patrol, Chase, Attack, Flee };
    State ai_state = State::Patrol;
    bool  is_poisoned    = false;
    float poison_damage  = 0.0f;
    float poison_duration= 0.0f;
    float poison_timer   = 0.0f;
    Boss(int id, float x, float y)
        : GameObject(id, x, y, 0), vx(0), vy(0),
          hp(500), max_hp(500), regen(5.0f),
          radius(30.0f), sprite_id(10), sound_id(5), volume(1.0f) {}
    void update(float dt) override {
        patrol_angle += 0.3f * dt;
        vx = std::cos(patrol_angle) * 20.0f;
        vy = std::sin(patrol_angle) * 20.0f;
        x += vx * dt; y += vy * dt;
        hp = std::min(hp + regen * dt, max_hp);
        if (hp <= 0) alive = false;
    }
    void render() const override {
        std::cout << "[Render] Boss #" << id
                  << " at (" << x << "," << y
                  << ") HP:" << hp << "\n";
    }
    std::string type() const override { return "Boss"; }
};
class OOPWorld {
public:
    using GameObjectPtr = std::unique_ptr<GameObject>;
    template<typename T, typename... Args>
    T* create(Args&&... args) {
        auto obj = std::make_unique<T>(next_id_++, std::forward<Args>(args)...);
        T* raw = obj.get();
        objects_.push_back(std::move(obj));
        return raw;
    }
    void update(float dt) {
        for (auto& obj : objects_) {
            if (obj->alive) obj->update(dt);
        }
        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(),
                [](const GameObjectPtr& o){ return !o->alive; }),
            objects_.end());
    }
    void render() const {
        for (const auto& obj : objects_) {
            if (obj->alive) obj->render();
        }
    }
    size_t count() const { return objects_.size(); }
private:
    int next_id_ = 0;
    std::vector<GameObjectPtr> objects_;
};
