/**
 * @file Structure_of_Array.cpp
 * @brief Structure of Arrays (SoA) memory layout for the ECS system.
 *
 * SoA stores each component field in its own contiguous array,
 * enabling cache-friendly iteration compared to Array of Structures (AoS).
 *
 * Layout example for a Transform component:
 *   AoS: [x,y,z | x,y,z | x,y,z ...]   <- poor cache use when only x is needed
 *   SoA: [x,x,x...] [y,y,y...] [z,z,z...] <- entire x-array fits in cache lines
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <utility>
#include <stdexcept>
#include <iostream>

// ---------------------------------------------------------------------------
// Configuration / aliases
// ---------------------------------------------------------------------------

using EntityID  = uint32_t;
using ComponentIndex = uint32_t;

static constexpr EntityID   INVALID_ENTITY    = UINT32_MAX;
static constexpr std::size_t DEFAULT_CAPACITY  = 64;

// ---------------------------------------------------------------------------
// RawBuffer — a type-erased, heap-allocated, resizable byte buffer.
// One RawBuffer is used per component field inside the SoA pool.
// ---------------------------------------------------------------------------

class RawBuffer {
public:
    RawBuffer() = default;

    explicit RawBuffer(std::size_t element_size, std::size_t capacity = DEFAULT_CAPACITY)
        : m_element_size(element_size),
          m_capacity(capacity),
          m_size(0)
    {
        m_data = ::operator new(m_element_size * m_capacity);
    }

    ~RawBuffer() { ::operator delete(m_data); }

    // Non-copyable
    RawBuffer(const RawBuffer&)            = delete;
    RawBuffer& operator=(const RawBuffer&) = delete;

    // Movable
    RawBuffer(RawBuffer&& other) noexcept
        : m_data(other.m_data),
          m_element_size(other.m_element_size),
          m_capacity(other.m_capacity),
          m_size(other.m_size)
    {
        other.m_data     = nullptr;
        other.m_capacity = 0;
        other.m_size     = 0;
    }

    RawBuffer& operator=(RawBuffer&& other) noexcept {
        if (this != &other) {
            ::operator delete(m_data);
            m_data         = other.m_data;
            m_element_size = other.m_element_size;
            m_capacity     = other.m_capacity;
            m_size         = other.m_size;
            other.m_data   = nullptr;
            other.m_capacity = 0;
            other.m_size     = 0;
        }
        return *this;
    }

    // ---- Accessors ---------------------------------------------------------

    void*       data()              noexcept { return m_data; }
    const void* data()        const noexcept { return m_data; }
    std::size_t size()        const noexcept { return m_size; }
    std::size_t capacity()    const noexcept { return m_capacity; }
    std::size_t element_size()const noexcept { return m_element_size; }

    void* at(std::size_t index) noexcept {
        return static_cast<char*>(m_data) + index * m_element_size;
    }

    const void* at(std::size_t index) const noexcept {
        return static_cast<const char*>(m_data) + index * m_element_size;
    }

    // ---- Mutation ----------------------------------------------------------

    /**
     * @brief Append an element (raw bytes) to the end of the buffer.
     * Grows capacity if needed (2x doubling strategy).
     */
    void push_back(const void* element) {
        if (m_size == m_capacity)
            grow();

        std::memcpy(at(m_size), element, m_element_size);
        ++m_size;
    }

    /**
     * @brief Swap-and-pop removal: copies last element into slot @p index,
     * then decrements size. O(1), does NOT preserve order.
     * @return true if a swap actually occurred (i.e. index was not the last slot).
     */
    bool swap_and_pop(std::size_t index) {
        assert(index < m_size && "RawBuffer::swap_and_pop: index out of range");

        --m_size;
        bool swapped = (index != m_size);

        if (swapped)
            std::memcpy(at(index), at(m_size), m_element_size);

        return swapped;
    }

    /**
     * @brief Overwrite the element at @p index with @p element.
     */
    void set(std::size_t index, const void* element) {
        assert(index < m_size && "RawBuffer::set: index out of range");
        std::memcpy(at(index), element, m_element_size);
    }

    void clear() noexcept { m_size = 0; }

private:
    void grow() {
        std::size_t new_capacity = m_capacity ? m_capacity * 2 : DEFAULT_CAPACITY;
        void* new_data = ::operator new(m_element_size * new_capacity);
        if (m_data)
            std::memcpy(new_data, m_data, m_element_size * m_size);
        ::operator delete(m_data);
        m_data     = new_data;
        m_capacity = new_capacity;
    }

    void*       m_data         = nullptr;
    std::size_t m_element_size = 0;
    std::size_t m_capacity     = 0;
    std::size_t m_size         = 0;
};

// ---------------------------------------------------------------------------
// SparseIndex — bidirectional mapping: EntityID <-> dense array index.
// Mirrors the logic in Sparse_Set.cpp but scoped for use inside SoA pools.
// ---------------------------------------------------------------------------

class SparseIndex {
public:
    static constexpr std::size_t PAGE_SIZE  = 1024; // entities per sparse page
    static constexpr std::size_t MAX_SPARSE = 256;  // max pages

    SparseIndex() {
        // sparse pages allocated on demand; dense arrays start empty
    }

    // Check membership
    bool contains(EntityID e) const {
        auto [page, offset] = decode(e);
        if (page >= m_sparse.size() || !m_sparse[page])
            return false;
        ComponentIndex idx = m_sparse[page][offset];
        return idx < static_cast<ComponentIndex>(m_dense.size()) && m_dense[idx] == e;
    }

    // Return dense index for entity (asserts it exists)
    ComponentIndex index_of(EntityID e) const {
        assert(contains(e) && "SparseIndex: entity not present");
        auto [page, offset] = decode(e);
        return m_sparse[page][offset];
    }

    /**
     * @brief Register entity → returns the new dense index it occupies.
     * Must be called BEFORE pushing data into the parallel SoA buffers.
     */
    ComponentIndex insert(EntityID e) {
        assert(!contains(e) && "SparseIndex: entity already registered");
        auto [page, offset] = decode(e);

        // Allocate sparse page if absent
        if (page >= m_sparse.size())
            m_sparse.resize(page + 1, nullptr);
        if (!m_sparse[page]) {
            m_sparse[page] = new ComponentIndex[PAGE_SIZE];
            std::memset(m_sparse[page], 0xFF, PAGE_SIZE * sizeof(ComponentIndex));
        }

        ComponentIndex idx = static_cast<ComponentIndex>(m_dense.size());
        m_sparse[page][offset] = idx;
        m_dense.push_back(e);
        return idx;
    }

    /**
     * @brief Remove entity. Performs a swap-and-pop on the dense array.
     * @return dense index that was freed (so callers can mirror the swap in
     *         their SoA buffers).
     */
    ComponentIndex erase(EntityID e) {
        assert(contains(e) && "SparseIndex: entity not present");
        auto [page, offset] = decode(e);
        ComponentIndex idx  = m_sparse[page][offset];

        EntityID last = m_dense.back();
        m_dense[idx]  = last;
        m_dense.pop_back();

        // Update the sparse entry for the entity that was moved
        if (last != e) {
            auto [lpage, loffset] = decode(last);
            m_sparse[lpage][loffset] = idx;
        }

        m_sparse[page][offset] = UINT32_MAX; // invalidate
        return idx;
    }

    std::size_t size() const noexcept { return m_dense.size(); }

    // Iterate over all registered entities (dense order)
    const std::vector<EntityID>& entities() const noexcept { return m_dense; }

    ~SparseIndex() {
        for (auto* page : m_sparse)
            delete[] page;
    }

private:
    std::pair<std::size_t, std::size_t> decode(EntityID e) const {
        return { e / PAGE_SIZE, e % PAGE_SIZE };
    }

    std::vector<ComponentIndex*> m_sparse; // paged sparse array
    std::vector<EntityID>        m_dense;  // dense entity list

    // Bring in vector for SparseIndex internal use
    // (project headers should already include <vector>; repeated here for clarity)
    #include <vector> // NOLINT — intentional local include
};

// ---------------------------------------------------------------------------
// SoAPool<Fields...> — a variadic Structure of Arrays pool.
//
// Each template parameter represents one field type. Fields are stored in
// separate contiguous arrays that grow/shrink together.
//
// Usage example (Transform with position x, y, z and a dirty flag):
//
//   SoAPool<float, float, float, bool> transform_pool;
//   transform_pool.insert(entity_id, 1.0f, 2.0f, 3.0f, false);
//   auto [x, y, z, dirty] = transform_pool.get(entity_id);
//   transform_pool.set<0>(entity_id, 5.0f);  // update x only
//   transform_pool.remove(entity_id);
// ---------------------------------------------------------------------------

template<typename... Fields>
class SoAPool {
    static constexpr std::size_t NUM_FIELDS = sizeof...(Fields);

    // Tuple of RawBuffer, one per field type
    using BufferTuple = std::tuple<RawBuffer*...>;

    // Helper: array of element sizes (one per field)
    static constexpr std::size_t field_sizes[NUM_FIELDS] = { sizeof(Fields)... };

public:
    SoAPool() {
        init_buffers(std::index_sequence_for<Fields...>{});
    }

    ~SoAPool() {
        destroy_buffers(std::index_sequence_for<Fields...>{});
    }

    // Non-copyable, movable
    SoAPool(const SoAPool&)            = delete;
    SoAPool& operator=(const SoAPool&) = delete;

    // ---- Core API ----------------------------------------------------------

    /**
     * @brief Add a new entity with initial field values.
     * @param entity  The EntityID to register.
     * @param values  One value per field, in order.
     */
    void insert(EntityID entity, const Fields&... values) {
        m_index.insert(entity);

        std::size_t i = 0;
        // Fold over each field buffer and push the corresponding value
        (push_field<Fields>(i++, values), ...);
    }

    /**
     * @brief Remove an entity. Performs O(1) swap-and-pop.
     */
    void remove(EntityID entity) {
        ComponentIndex idx = m_index.erase(entity);
        swap_and_pop_all(idx, std::index_sequence_for<Fields...>{});
    }

    /**
     * @brief Returns true if the pool contains the entity.
     */
    bool contains(EntityID entity) const {
        return m_index.contains(entity);
    }

    /**
     * @brief Get a tuple of references to all fields for an entity.
     *
     * Example:
     *   auto [x, y, z] = pool.get(eid);
     */
    std::tuple<Fields&...> get(EntityID entity) {
        ComponentIndex idx = m_index.index_of(entity);
        return get_all(idx, std::index_sequence_for<Fields...>{});
    }

    std::tuple<const Fields&...> get(EntityID entity) const {
        ComponentIndex idx = m_index.index_of(entity);
        return get_all_const(idx, std::index_sequence_for<Fields...>{});
    }

    /**
     * @brief Get a reference to a single field by compile-time index.
     *
     * Example:
     *   float& x = pool.get<0>(eid);
     */
    template<std::size_t FieldIndex>
    auto& get(EntityID entity) {
        using FieldType = std::tuple_element_t<FieldIndex, std::tuple<Fields...>>;
        ComponentIndex idx = m_index.index_of(entity);
        return *static_cast<FieldType*>(buffer_at(FieldIndex, idx));
    }

    template<std::size_t FieldIndex>
    const auto& get(EntityID entity) const {
        using FieldType = std::tuple_element_t<FieldIndex, std::tuple<Fields...>>;
        ComponentIndex idx = m_index.index_of(entity);
        return *static_cast<const FieldType*>(buffer_at_const(FieldIndex, idx));
    }

    /**
     * @brief Overwrite a single field for an entity.
     *
     * Example:
     *   pool.set<0>(eid, 99.0f);
     */
    template<std::size_t FieldIndex>
    void set(EntityID entity, const std::tuple_element_t<FieldIndex, std::tuple<Fields...>>& value) {
        ComponentIndex idx = m_index.index_of(entity);
        get_buffer(FieldIndex)->set(idx, &value);
    }

    /**
     * @brief Number of entities currently stored.
     */
    std::size_t size() const noexcept { return m_index.size(); }

    /**
     * @brief Direct pointer to field N's contiguous array — for SIMD / bulk ops.
     *
     * Example:
     *   float* xs = pool.raw<0>();
     *   // SIMD: process all x-values in one loop with no stride
     */
    template<std::size_t FieldIndex>
    auto* raw() {
        using FieldType = std::tuple_element_t<FieldIndex, std::tuple<Fields...>>;
        return static_cast<FieldType*>(get_buffer(FieldIndex)->data());
    }

    template<std::size_t FieldIndex>
    const auto* raw() const {
        using FieldType = std::tuple_element_t<FieldIndex, std::tuple<Fields...>>;
        return static_cast<const FieldType*>(get_buffer(FieldIndex)->data());
    }

    /**
     * @brief Iterate over all entities with a callback receiving field refs.
     *
     * Example:
     *   pool.each([](EntityID e, float& x, float& y, float& z) {
     *       x += 1.0f;
     *   });
     */
    template<typename Func>
    void each(Func&& func) {
        const auto& entities = m_index.entities();
        for (std::size_t i = 0; i < entities.size(); ++i)
            invoke_func(func, entities[i], i, std::index_sequence_for<Fields...>{});
    }

    /**
     * @brief Clear all entities from the pool.
     */
    void clear() {
        // Erase all entities one-by-one to keep the sparse index consistent
        auto entities_copy = m_index.entities(); // copy since we mutate
        for (EntityID e : entities_copy)
            remove(e);
    }

private:
    // ---- Buffer management -------------------------------------------------

    RawBuffer* m_buffers[NUM_FIELDS]{};
    SparseIndex m_index;

    template<std::size_t... Is>
    void init_buffers(std::index_sequence<Is...>) {
        ((m_buffers[Is] = new RawBuffer(field_sizes[Is])), ...);
    }

    template<std::size_t... Is>
    void destroy_buffers(std::index_sequence<Is...>) {
        ((delete m_buffers[Is]), ...);
    }

    RawBuffer* get_buffer(std::size_t field) {
        assert(field < NUM_FIELDS);
        return m_buffers[field];
    }

    const RawBuffer* get_buffer(std::size_t field) const {
        assert(field < NUM_FIELDS);
        return m_buffers[field];
    }

    void* buffer_at(std::size_t field, std::size_t index) {
        return get_buffer(field)->at(index);
    }

    const void* buffer_at_const(std::size_t field, std::size_t index) const {
        return get_buffer(field)->at(index);
    }

    // Push one field value into its buffer
    template<typename T>
    void push_field(std::size_t field_index, const T& value) {
        get_buffer(field_index)->push_back(&value);
    }

    // Swap-and-pop across all field buffers
    template<std::size_t... Is>
    void swap_and_pop_all(ComponentIndex idx, std::index_sequence<Is...>) {
        (m_buffers[Is]->swap_and_pop(idx), ...);
    }

    // Build tuple of references for get()
    template<std::size_t... Is>
    std::tuple<Fields&...> get_all(ComponentIndex idx, std::index_sequence<Is...>) {
        return { *static_cast<std::tuple_element_t<Is, std::tuple<Fields...>>*>(
                     m_buffers[Is]->at(idx))... };
    }

    template<std::size_t... Is>
    std::tuple<const Fields&...> get_all_const(ComponentIndex idx, std::index_sequence<Is...>) const {
        return { *static_cast<const std::tuple_element_t<Is, std::tuple<Fields...>>*>(
                     m_buffers[Is]->at(idx))... };
    }

    // Invoke callback for each()
    template<typename Func, std::size_t... Is>
    void invoke_func(Func&& func, EntityID e, std::size_t idx, std::index_sequence<Is...>) {
        func(e,
             *static_cast<std::tuple_element_t<Is, std::tuple<Fields...>>*>(
                 m_buffers[Is]->at(idx))...);
    }
};

// ---------------------------------------------------------------------------
// Example component definitions (mirroring ECS_System conventions)
// ---------------------------------------------------------------------------

/// Transform: world-space position + uniform scale
struct TransformSoA {
    SoAPool<float, float, float, float> pool; // x, y, z, scale

    void add(EntityID e, float x, float y, float z, float scale = 1.0f) {
        pool.insert(e, x, y, z, scale);
    }
    void remove(EntityID e) { pool.remove(e); }

    // Bulk-translate all entities (cache-friendly: each field array accessed sequentially)
    void translate_all(float dx, float dy, float dz) {
        float* xs = pool.raw<0>();
        float* ys = pool.raw<1>();
        float* zs = pool.raw<2>();
        for (std::size_t i = 0, n = pool.size(); i < n; ++i) {
            xs[i] += dx;
            ys[i] += dy;
            zs[i] += dz;
        }
    }
};

/// Velocity: linear velocity in 3D
struct VelocitySoA {
    SoAPool<float, float, float> pool; // vx, vy, vz

    void add(EntityID e, float vx, float vy, float vz) {
        pool.insert(e, vx, vy, vz);
    }
    void remove(EntityID e) { pool.remove(e); }
};

// ---------------------------------------------------------------------------
// Minimal smoke-test (remove or guard with #ifdef SOA_TEST in production)
// ---------------------------------------------------------------------------

#ifdef SOA_TEST

#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== SoA ECS Smoke Test ===\n";

    // --- RawBuffer ---
    {
        RawBuffer buf(sizeof(float));
        float values[] = {1.0f, 2.0f, 3.0f};
        buf.push_back(&values[0]);
        buf.push_back(&values[1]);
        buf.push_back(&values[2]);
        assert(buf.size() == 3);
        assert(*static_cast<float*>(buf.at(1)) == 2.0f);

        buf.swap_and_pop(0);            // removes index 0, moves 3.0f there
        assert(buf.size() == 2);
        assert(*static_cast<float*>(buf.at(0)) == 3.0f);
        std::cout << "[PASS] RawBuffer\n";
    }

    // --- SoAPool ---
    {
        SoAPool<float, float, float> pos;
        pos.insert(1u, 1.0f, 2.0f, 3.0f);
        pos.insert(2u, 4.0f, 5.0f, 6.0f);
        pos.insert(3u, 7.0f, 8.0f, 9.0f);
        assert(pos.size() == 3);

        auto [x, y, z] = pos.get(2u);
        assert(x == 4.0f && y == 5.0f && z == 6.0f);

        pos.set<0>(2u, 99.0f);
        assert(pos.get<0>(2u) == 99.0f);

        pos.remove(1u);
        assert(pos.size() == 2);
        assert(!pos.contains(1u));
        assert(pos.contains(2u));
        assert(pos.contains(3u));

        // each() should visit exactly the 2 remaining entities
        int count = 0;
        pos.each([&](EntityID, float&, float&, float&) { ++count; });
        assert(count == 2);

        std::cout << "[PASS] SoAPool\n";
    }

    // --- TransformSoA ---
    {
        TransformSoA xfm;
        xfm.add(10u, 0.0f, 0.0f, 0.0f);
        xfm.add(11u, 1.0f, 1.0f, 1.0f);
        xfm.translate_all(2.0f, 0.0f, 0.0f);

        assert(xfm.pool.get<0>(10u) == 2.0f);
        assert(xfm.pool.get<0>(11u) == 3.0f);
        std::cout << "[PASS] TransformSoA::translate_all\n";
    }

    std::cout << "=== All tests passed ===\n";
    return 0;
}

#endif // SOA_TEST
