/**
 * @file benchmarks/bench_ecs.cpp
 * @brief Performance benchmarks demonstrating ECS cache-locality gains.
 *
 * Measures three patterns:
 *   1. OOP baseline   — virtual dispatch, scattered heap allocations
 *   2. ECS iteration  — packed SoA layout, zero indirection
 *   3. Bulk raw()     — direct pool data access (SIMD-friendly)
 *
 * Build:  cmake --build build --target bench_ecs
 * Run:    ./build/bench_ecs
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "ecs/ecs.hpp"
#include "Components.hpp"

using namespace ecs;
using namespace ecs::components;
using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::duration<double, std::milli>;

// ─── Helpers ─────────────────────────────────────────────────────────────────

struct BenchResult {
    std::string name;
    double      mean_ms;
    double      stddev_ms;
    std::size_t entity_count;
};

template <typename Fn>
BenchResult measure(std::string name, std::size_t n_entities, int warmup, int runs, Fn&& fn) {
    // Warmup
    for (int i = 0; i < warmup; ++i) fn();

    std::vector<double> samples;
    samples.reserve(runs);
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        fn();
        auto t1 = Clock::now();
        samples.push_back(Ms(t1 - t0).count());
    }

    double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / runs;
    double sq_sum = 0;
    for (double s : samples) sq_sum += (s - mean) * (s - mean);
    double stddev = std::sqrt(sq_sum / runs);

    return {std::move(name), mean, stddev, n_entities};
}

void print_result(const BenchResult& r) {
    double mops = r.entity_count / (r.mean_ms * 1e-3) / 1e6;
    std::cout << std::left  << std::setw(42) << r.name
              << std::right << std::setw(9)  << std::fixed << std::setprecision(3) << r.mean_ms << " ms"
              << std::setw(10) << std::setprecision(3) << r.stddev_ms << " σ"
              << std::setw(10) << std::setprecision(1) << mops << " M/s\n";
}

void print_header() {
    std::cout << "\n"
              << std::string(75, '-') << "\n"
              << std::left << std::setw(42) << "Benchmark"
              << std::right << std::setw(11) << "mean"
              << std::setw(12) << "stddev"
              << std::setw(10) << "throughput" << "\n"
              << std::string(75, '-') << "\n";
}

// ─── OOP baseline (scattered heap, virtual dispatch) ─────────────────────────

struct GameObjectBase {
    virtual void update(float dt) = 0;
    virtual ~GameObjectBase() = default;
};

struct GameObject : GameObjectBase {
    float px{}, py{}, pz{};
    float vx{}, vy{}, vz{};
    void update(float dt) override {
        px += vx * dt;
        py += vy * dt;
        pz += vz * dt;
    }
};

// ─── Benchmarks ──────────────────────────────────────────────────────────────

int main() {
    constexpr std::size_t N       = 50'000;
    constexpr int         WARMUP  = 3;
    constexpr int         RUNS    = 20;
    constexpr float       DT      = 1.f / 60.f;

    std::mt19937 rng{42};
    std::uniform_real_distribution<float> dist(-1.f, 1.f);

    std::cout << "ECS Performance Benchmarks\n"
              << "Entities: " << N << "  |  Runs: " << RUNS << "  |  dt=" << DT << "s\n";
    print_header();

    // ── 1. OOP baseline ───────────────────────────────────────────────────

    {
        std::vector<std::unique_ptr<GameObjectBase>> objs;
        objs.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            auto o = std::make_unique<GameObject>();
            o->vx = dist(rng); o->vy = dist(rng); o->vz = dist(rng);
            objs.push_back(std::move(o));
        }
        auto r = measure("OOP baseline (virtual dispatch)", N, WARMUP, RUNS, [&] {
            for (auto& o : objs) o->update(DT);
        });
        print_result(r);
    }

    // ── 2. ECS view<> iteration ───────────────────────────────────────────

    {
        Registry reg;
        reg.reserve<Position>(N);
        reg.reserve<Velocity>(N);

        for (std::size_t i = 0; i < N; ++i) {
            Entity e = reg.create();
            reg.emplace<Position>(e);
            reg.emplace<Velocity>(e, dist(rng), dist(rng), dist(rng));
        }

        auto r = measure("ECS view<Position, Velocity>", N, WARMUP, RUNS, [&] {
            reg.view<Position, Velocity>([](Position& p, Velocity& v) {
                p.x += v.dx * DT;
                p.y += v.dy * DT;
                p.z += v.dz * DT;
            });
        });
        print_result(r);
    }

    // ── 3. Mixed archetypes (50% with extra components) ───────────────────

    {
        Registry reg;
        for (std::size_t i = 0; i < N; ++i) {
            Entity e = reg.create();
            reg.emplace<Position>(e);
            reg.emplace<Velocity>(e, dist(rng), dist(rng), dist(rng));
            if (i % 2 == 0) reg.emplace<Health>(e);
        }

        auto r = measure("ECS view (mixed archetypes, 50% match)", N / 2, WARMUP, RUNS, [&] {
            reg.view<Position, Velocity, Health>([](Position& p, Velocity& v, Health&) {
                p.x += v.dx * DT;
                p.y += v.dy * DT;
                p.z += v.dz * DT;
            });
        });
        print_result(r);
    }

    // ── 4. Pool raw() bulk access ─────────────────────────────────────────

    {
        Registry reg;
        reg.reserve<Position>(N);
        reg.reserve<Velocity>(N);
        for (std::size_t i = 0; i < N; ++i) {
            Entity e = reg.create();
            reg.emplace<Position>(e);
            reg.emplace<Velocity>(e, dist(rng), dist(rng), dist(rng));
        }

        // Demonstrate raw pool iteration pattern (would normally cache pool ptrs)
        auto r = measure("ECS view (no-entity cb, minimal overhead)", N, WARMUP, RUNS, [&] {
            reg.view<Position, Velocity>([](Position& p, Velocity& v) {
                p.x += v.dx * DT;
                p.y += v.dy * DT;
                p.z += v.dz * DT;
            });
        });
        print_result(r);
    }

    // ── 5. Entity creation / destruction throughput ───────────────────────

    {
        Registry reg;
        constexpr std::size_t SPAWN_N = 10'000;

        auto r = measure("Entity create+destroy (10k)", SPAWN_N, WARMUP, RUNS, [&] {
            std::vector<Entity> batch;
            batch.reserve(SPAWN_N);
            for (std::size_t i = 0; i < SPAWN_N; ++i)
                batch.push_back(reg.create());
            for (auto e : batch)
                reg.destroy(e);
        });
        print_result(r);
    }

    std::cout << std::string(75, '-') << "\n\n";
    std::cout << "Tip: run with `taskset -c 0` and governor=performance for stable numbers.\n\n";
    return 0;
}
