/* * Environment_variables.cpp * ECS System — Core type definitions, constants, and environment configuration */
#include <cstdint>
#include <cstddef>
#include <limits>
#include <cassert>
#include <iostream>
#include <string_view>

static constexpr std::size_t CACHE_LINE_SIZE  = 64;
static constexpr std::size_t MAX_ENTITIES     = 1'000'000;
static constexpr std::size_t MAX_COMPONENTS   = 64;
static constexpr std::size_t MAX_ARCHETYPES   = 4096;
static constexpr std::size_t CHUNK_SIZE       = 16384;
static constexpr std::size_t SPARSE_PAGE_SIZE = 4096;

#if defined(__GNUC__) || defined(__clang__)
#  define ECS_LIKELY(x)      __builtin_expect(!!(x), 1)
#  define ECS_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#  define ECS_FORCEINLINE    __attribute__((always_inline)) inline
#else
#  define ECS_LIKELY(x)      (x)
#  define ECS_UNLIKELY(x)    (x)
#  define ECS_FORCEINLINE    inline
#endif

struct EntityID {
    uint32_t index      = 0;
    uint32_t generation = 0;
    bool operator==(const EntityID& o) const noexcept {
        return index == o.index && generation == o.generation;
    }
    bool operator!=(const EntityID& o) const noexcept { return !(*this == o); }
    static constexpr EntityID null() noexcept {
        return { std::numeric_limits<uint32_t>::max(), 0 };
    }
    bool is_null() const noexcept { return *this == null(); }
};
using ComponentID   = uint32_t;
using ArchetypeID   = uint32_t;
using ComponentMask = uint64_t;
static constexpr ComponentID  INVALID_COMPONENT = std::numeric_limits<ComponentID>::max();
static constexpr ArchetypeID  INVALID_ARCHETYPE = std::numeric_limits<ArchetypeID>::max();

namespace detail {
    inline ComponentID& component_counter() {
        static ComponentID counter = 0;
        return counter;
    }
}
template<typename T>
ComponentID component_id() {
    static const ComponentID id = detail::component_counter()++;
    assert(id < MAX_COMPONENTS && "Exceeded MAX_COMPONENTS — raise the limit");
    return id;
}
template<typename T>
ECS_FORCEINLINE ComponentMask component_mask() {
    return ComponentMask(1) << component_id<T>();
}

struct ComponentInfo {
    std::size_t      size      = 0;
    std::size_t      alignment = 0;
    std::string_view name      = {};
    void (*construct)     (void*)        = nullptr;
    void (*destruct)      (void*)        = nullptr;
    void (*move_construct)(void*, void*) = nullptr;
    void (*copy_construct)(void*, void*) = nullptr;
};

template<typename T>
ComponentInfo make_component_info(std::string_view name = "") {
    ComponentInfo ci;
    ci.size      = sizeof(T);
    ci.alignment = alignof(T);
    ci.name      = name;
    ci.construct      = [](void* p)             { new (p) T{}; };
    ci.destruct       = [](void* p)             { static_cast<T*>(p)->~T(); };
    ci.move_construct = [](void* dst, void* src){ new (dst) T{ std::move(*static_cast<T*>(src)) }; };
    ci.copy_construct = [](void* dst, void* src){ new (dst) T{ *static_cast<const T*>(src) }; };
    return ci;
}

struct ComponentRegistry {
    ComponentInfo infos[MAX_COMPONENTS];
    ComponentID   count = 0;
    template<typename T>
    ComponentID register_component(std::string_view name = "") {
        ComponentID id = component_id<T>();
        if (id < count) return id;
        assert(count < MAX_COMPONENTS);
        infos[count++] = make_component_info<T>(name);
        return id;
    }
    const ComponentInfo& get(ComponentID id) const {
        assert(id < count);
        return infos[id];
    }
    static ComponentRegistry& instance() {
        static ComponentRegistry reg;
        return reg;
    }
};
