#pragma once
#include <array>
#include <brtoy/brtoy.h>

namespace brtoy {

template <typename T, size_t N> class StackVector {
  public:
    using value_type = T;
    using pointer_type = T *;
    static constexpr size_t Capacity = N;
    using array_type = std::array<value_type, Capacity>;

    struct iterator {
        array_type *m_ary;
        size_t m_cur;

        std::strong_ordering operator<=>(const iterator &) const = default;
        iterator &operator++() {
            ++m_cur;
            return *this;
        }
        const T &operator*() const { return (*m_ary)[m_cur]; }
    };

    StackVector() = default;
    StackVector(size_t count) : m_count(count) {}

    StackVector(StackVector &&other) : m_count(other.m_count), m_array(std::move(other.m_array)) {
        other.m_count = 0;
    }

    StackVector &operator=(StackVector &&other) {
        this->m_count = other.m_count;
        this->m_array = std::move(other.m_array);
        other.m_count = 0;
        return *this;
    }

    constexpr size_t capacity() const { return Capacity; }

    size_t size() const { return m_count; }

    bool empty() const { return m_count == 0; }

    const value_type &operator[](size_t i) const { return m_array[i]; }

    void push_back(T &&elem) {
        if (m_count < Capacity)
            m_array[m_count++] = std::move(elem);
    }

    void push_back(const value_type &elem) {
        if (m_count < Capacity)
            m_array[m_count++] = std::move(elem);
    }

    void clear() {
        m_array.fill({});
        m_count = 0;
    }

    iterator begin() { return iterator{&m_array, 0}; }

    iterator end() { return iterator{&m_array, Capacity}; }

    pointer_type data() { return m_array.data(); }

  private:
    size_t m_count = 0;
    array_type m_array = {};
};

} // namespace brtoy
