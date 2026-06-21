#pragma once
/**
 * @file flat_unordered_map.hpp
 * @brief Project-local flat open-addressing hash containers.
 *
 * A compact robin-hood hash table for CriCodecs hot metadata paths where node
 * allocation and pointer chasing are more expensive than reference stability.
 * The API intentionally mirrors the small subset of `std::unordered_map` /
 * `std::unordered_set` used by this project. Iterators, references, and
 * pointers are invalidated by insertions that rehash and by erase operations.
 *
 * This is not a drop-in replacement for `std::unordered_map` when stable
 * references or exact standard bucket semantics are required.
 */

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cricodecs::util {

namespace detail {

[[nodiscard]] constexpr size_t mix_hash(size_t value) noexcept {
    if constexpr (sizeof(size_t) >= 8) {
        uint64_t x = static_cast<uint64_t>(value);
        x ^= x >> 30u;
        x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27u;
        x *= 0x94d049bb133111ebull;
        x ^= x >> 31u;
        return static_cast<size_t>(x);
    } else {
        uint32_t x = static_cast<uint32_t>(value);
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return static_cast<size_t>(x);
    }
}

[[nodiscard]] constexpr size_t next_power_of_two_at_least(size_t value) noexcept {
    if (value <= 8u) {
        return 8u;
    }
    return std::bit_ceil(value);
}

template <typename Value>
struct flat_hash_bucket {
    std::optional<Value> value;
    size_t distance = 0;

    [[nodiscard]] bool occupied() const noexcept { return value.has_value(); }
};

template <typename Value, typename Key, typename KeyOfValue, typename Hash, typename KeyEqual>
class flat_hash_table {
public:
    using key_type = Key;
    using value_type = Value;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

private:
    using bucket_type = flat_hash_bucket<value_type>;
    static constexpr size_type npos = static_cast<size_type>(-1);

    template <bool IsConst>
    class basic_iterator {
        friend class flat_hash_table;
        using table_type = std::conditional_t<IsConst, const flat_hash_table, flat_hash_table>;
        using bucket_pointer = std::conditional_t<IsConst, const bucket_type*, bucket_type*>;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = flat_hash_table::value_type;
        using difference_type = flat_hash_table::difference_type;
        using reference = std::conditional_t<IsConst, const value_type&, value_type&>;
        using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;

        basic_iterator() = default;

        template <bool OtherConst>
            requires IsConst && (!OtherConst)
        basic_iterator(const basic_iterator<OtherConst>& other) noexcept
            : m_table(other.m_table), m_index(other.m_index) {}

        [[nodiscard]] reference operator*() const noexcept { return *bucket()->value; }
        [[nodiscard]] pointer operator->() const noexcept { return std::addressof(*bucket()->value); }

        basic_iterator& operator++() noexcept {
            ++m_index;
            skip_empty();
            return *this;
        }

        basic_iterator operator++(int) noexcept {
            basic_iterator copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(const basic_iterator& lhs, const basic_iterator& rhs) noexcept {
            return lhs.m_table == rhs.m_table && lhs.m_index == rhs.m_index;
        }

        friend bool operator!=(const basic_iterator& lhs, const basic_iterator& rhs) noexcept {
            return !(lhs == rhs);
        }

    private:
        basic_iterator(table_type* table, size_type index) noexcept
            : m_table(table), m_index(index) {
            skip_empty();
        }

        [[nodiscard]] bucket_pointer bucket() const noexcept {
            return m_table->m_buckets.data() + static_cast<difference_type>(m_index);
        }

        void skip_empty() noexcept {
            if (m_table == nullptr) {
                return;
            }
            while (m_index < m_table->m_buckets.size() && !m_table->m_buckets[m_index].occupied()) {
                ++m_index;
            }
        }

        table_type* m_table = nullptr;
        size_type m_index = 0;
    };

public:
    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

    flat_hash_table() = default;

    explicit flat_hash_table(size_type bucket_count, const Hash& hash = Hash{}, const KeyEqual& equal = KeyEqual{})
        : m_hash(hash), m_equal(equal) {
        rehash(bucket_count);
    }

    template <std::input_iterator InputIt>
    flat_hash_table(InputIt first, InputIt last, size_type bucket_count = 0, const Hash& hash = Hash{}, const KeyEqual& equal = KeyEqual{})
        : flat_hash_table(bucket_count, hash, equal) {
        insert(first, last);
    }

    flat_hash_table(std::initializer_list<value_type> values, size_type bucket_count = 0, const Hash& hash = Hash{}, const KeyEqual& equal = KeyEqual{})
        : flat_hash_table(std::max(bucket_count, values.size()), hash, equal) {
        insert(values.begin(), values.end());
    }

    [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
    [[nodiscard]] iterator end() noexcept { return iterator(this, m_buckets.size()); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, m_buckets.size()); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] size_type size() const noexcept { return m_size; }
    [[nodiscard]] size_type bucket_count() const noexcept { return m_buckets.size(); }
    [[nodiscard]] float max_load_factor() const noexcept { return m_max_load_factor; }

    void max_load_factor(float value) noexcept {
        if (value > 0.1f && value < 0.95f) {
            m_max_load_factor = value;
        }
    }

    [[nodiscard]] float load_factor() const noexcept {
        return m_buckets.empty() ? 0.0f : static_cast<float>(m_size) / static_cast<float>(m_buckets.size());
    }

    void clear() noexcept {
        for (auto& bucket : m_buckets) {
            bucket.value.reset();
            bucket.distance = 0;
        }
        m_size = 0;
    }

    void reserve(size_type count) {
        const size_type required = buckets_for_capacity(count);
        if (required > m_buckets.size()) {
            rehash(required);
        }
    }

    void rehash(size_type bucket_count) {
        bucket_count = next_power_of_two_at_least(bucket_count);
        if (bucket_count == m_buckets.size()) {
            return;
        }

        std::vector<bucket_type> old_buckets = std::move(m_buckets);
        m_buckets.assign(bucket_count, bucket_type{});
        m_size = 0;
        for (auto& bucket : old_buckets) {
            if (bucket.occupied()) {
                (void)insert_value(std::move(*bucket.value));
            }
        }
    }

    [[nodiscard]] hasher hash_function() const { return m_hash; }
    [[nodiscard]] key_equal key_eq() const { return m_equal; }

    template <typename LookupKey>
    [[nodiscard]] iterator find(const LookupKey& key) {
        const size_type index = find_index(key);
        return index == npos ? end() : iterator(this, index);
    }

    template <typename LookupKey>
    [[nodiscard]] const_iterator find(const LookupKey& key) const {
        const size_type index = find_index(key);
        return index == npos ? end() : const_iterator(this, index);
    }

    template <typename LookupKey>
    [[nodiscard]] bool contains(const LookupKey& key) const {
        return find_index(key) != npos;
    }

    template <typename LookupKey>
    [[nodiscard]] size_type count(const LookupKey& key) const {
        return contains(key) ? 1u : 0u;
    }

    std::pair<iterator, bool> insert(const value_type& value) {
        return insert_value(value);
    }

    std::pair<iterator, bool> insert(value_type&& value) {
        return insert_value(std::move(value));
    }

    template <std::input_iterator InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            (void)insert(*first);
        }
    }

    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        return insert_value(value_type(std::forward<Args>(args)...));
    }

    template <typename LookupKey>
    size_type erase(const LookupKey& key) {
        const size_type index = find_index(key);
        if (index == npos) {
            return 0;
        }
        erase_at(index);
        return 1;
    }

    iterator erase(const_iterator pos) {
        if (pos == cend()) {
            return end();
        }
        const size_type index = pos.m_index;
        erase_at(index);
        return iterator(this, index);
    }

    void swap(flat_hash_table& other) noexcept(
        std::is_nothrow_swappable_v<Hash> && std::is_nothrow_swappable_v<KeyEqual>
    ) {
        using std::swap;
        swap(m_buckets, other.m_buckets);
        swap(m_size, other.m_size);
        swap(m_hash, other.m_hash);
        swap(m_equal, other.m_equal);
        swap(m_max_load_factor, other.m_max_load_factor);
    }

private:
    template <typename LookupKey>
    [[nodiscard]] size_type bucket_for(const LookupKey& key) const {
        return mixed_hash(key) & (m_buckets.size() - 1u);
    }

    template <typename LookupKey>
    [[nodiscard]] size_type mixed_hash(const LookupKey& key) const {
        return mix_hash(static_cast<size_t>(std::invoke(m_hash, key)));
    }

    [[nodiscard]] size_type buckets_for_capacity(size_type count) const noexcept {
        const auto desired = static_cast<size_type>(static_cast<double>(count) / static_cast<double>(m_max_load_factor)) + 1u;
        return next_power_of_two_at_least(desired);
    }

    [[nodiscard]] bool should_grow(size_type next_size) const noexcept {
        return m_buckets.empty() || static_cast<double>(next_size) > static_cast<double>(m_buckets.size()) * m_max_load_factor;
    }

    template <typename LookupKey>
    [[nodiscard]] size_type find_index(const LookupKey& key) const {
        if (m_buckets.empty()) {
            return npos;
        }

        size_type index = bucket_for(key);
        size_type distance = 0;
        while (true) {
            const auto& bucket = m_buckets[index];
            if (!bucket.occupied() || bucket.distance < distance) {
                return npos;
            }
            if (std::invoke(m_equal, KeyOfValue{}(*bucket.value), key)) {
                return index;
            }
            index = (index + 1u) & (m_buckets.size() - 1u);
            ++distance;
        }
    }

    template <typename InsertValue>
    std::pair<iterator, bool> insert_value(InsertValue&& inserted_value) {
        if (should_grow(m_size + 1u)) {
            rehash(buckets_for_capacity(m_size + 1u));
        }

        key_type inserted_key(KeyOfValue{}(inserted_value));
        value_type incoming(std::forward<InsertValue>(inserted_value));
        size_type index = bucket_for(KeyOfValue{}(incoming));
        size_type distance = 0;

        while (true) {
            auto& bucket = m_buckets[index];
            if (!bucket.occupied()) {
                bucket.value.emplace(std::move(incoming));
                bucket.distance = distance;
                ++m_size;
                return {iterator(this, find_index(inserted_key)), true};
            }

            if (std::invoke(m_equal, KeyOfValue{}(*bucket.value), KeyOfValue{}(incoming))) {
                return {iterator(this, index), false};
            }

            if (bucket.distance < distance) {
                using std::swap;
                swap(bucket.distance, distance);
                swap(*bucket.value, incoming);
            }

            index = (index + 1u) & (m_buckets.size() - 1u);
            ++distance;
        }
    }

    void erase_at(size_type index) {
        m_buckets[index].value.reset();
        m_buckets[index].distance = 0;
        --m_size;

        size_type next = (index + 1u) & (m_buckets.size() - 1u);
        while (m_buckets[next].occupied() && m_buckets[next].distance > 0u) {
            m_buckets[index].value.emplace(std::move(*m_buckets[next].value));
            m_buckets[index].distance = m_buckets[next].distance - 1u;
            m_buckets[next].value.reset();
            m_buckets[next].distance = 0;
            index = next;
            next = (next + 1u) & (m_buckets.size() - 1u);
        }
    }

    std::vector<bucket_type> m_buckets;
    size_type m_size = 0;
    [[no_unique_address]] Hash m_hash{};
    [[no_unique_address]] KeyEqual m_equal{};
    float m_max_load_factor = 0.80f;
};

template <typename Key>
struct set_key_of {
    [[nodiscard]] constexpr const Key& operator()(const Key& value) const noexcept { return value; }
};

template <typename Key, typename Mapped>
struct map_key_of {
    [[nodiscard]] constexpr const Key& operator()(const std::pair<Key, Mapped>& value) const noexcept {
        return value.first;
    }
};

} // namespace detail

struct transparent_string_hash {
    using is_transparent = void;

    [[nodiscard]] static constexpr size_t hash_bytes(std::string_view value) noexcept {
        if constexpr (sizeof(size_t) >= 8) {
            uint64_t hash = 14695981039346656037ull;
            for (const unsigned char byte : value) {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            return static_cast<size_t>(hash);
        } else {
            uint32_t hash = 2166136261u;
            for (const unsigned char byte : value) {
                hash ^= byte;
                hash *= 16777619u;
            }
            return static_cast<size_t>(hash);
        }
    }

    [[nodiscard]] static constexpr size_t c_string_length(const char* value) noexcept {
        size_t length = 0;
        while (value[length] != '\0') {
            ++length;
        }
        return length;
    }

    [[nodiscard]] constexpr size_t operator()(std::string_view value) const noexcept {
        return hash_bytes(value);
    }

    [[nodiscard]] constexpr size_t operator()(const std::string& value) const noexcept {
        return hash_bytes(std::string_view(value));
    }

    [[nodiscard]] constexpr size_t operator()(const char* value) const noexcept {
        return hash_bytes(std::string_view(value, c_string_length(value)));
    }
};

template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class flat_unordered_set : public detail::flat_hash_table<Key, Key, detail::set_key_of<Key>, Hash, KeyEqual> {
    using table_type = detail::flat_hash_table<Key, Key, detail::set_key_of<Key>, Hash, KeyEqual>;

public:
    using table_type::table_type;
};

template <typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class flat_unordered_map : public detail::flat_hash_table<std::pair<Key, T>, Key, detail::map_key_of<Key, T>, Hash, KeyEqual> {
    using table_type = detail::flat_hash_table<std::pair<Key, T>, Key, detail::map_key_of<Key, T>, Hash, KeyEqual>;

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = typename table_type::value_type;
    using iterator = typename table_type::iterator;
    using const_iterator = typename table_type::const_iterator;
    using table_type::table_type;
    using table_type::emplace;

    T& operator[](const Key& key) {
        return emplace(key, T{}).first->second;
    }

    T& operator[](Key&& key) {
        return emplace(std::move(key), T{}).first->second;
    }

    [[nodiscard]] T& at(const Key& key) {
        auto found = this->find(key);
        if (found == this->end()) {
            throw std::out_of_range("flat_unordered_map::at key not found");
        }
        return found->second;
    }

    [[nodiscard]] const T& at(const Key& key) const {
        auto found = this->find(key);
        if (found == this->end()) {
            throw std::out_of_range("flat_unordered_map::at key not found");
        }
        return found->second;
    }

    template <typename M>
    std::pair<iterator, bool> insert_or_assign(const Key& key, M&& value) {
        auto found = this->find(key);
        if (found != this->end()) {
            found->second = std::forward<M>(value);
            return {found, false};
        }
        auto [it, inserted] = emplace(key, std::forward<M>(value));
        return {it, inserted};
    }

    template <typename M>
    std::pair<iterator, bool> insert_or_assign(Key&& key, M&& value) {
        auto found = this->find(key);
        if (found != this->end()) {
            found->second = std::forward<M>(value);
            return {found, false};
        }
        auto [it, inserted] = emplace(std::move(key), std::forward<M>(value));
        return {it, inserted};
    }
};

template <typename Key, typename Hash, typename KeyEqual>
void swap(flat_unordered_set<Key, Hash, KeyEqual>& lhs, flat_unordered_set<Key, Hash, KeyEqual>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

template <typename Key, typename T, typename Hash, typename KeyEqual>
void swap(flat_unordered_map<Key, T, Hash, KeyEqual>& lhs, flat_unordered_map<Key, T, Hash, KeyEqual>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

} // namespace cricodecs::util
