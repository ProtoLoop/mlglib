#pragma once

#include "AssertHelper.h"

#include <cstddef>
#include <memory>
#include <new>
#include <span>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

/// A contiguous container whose capacity is chosen either at construction or at compile time and
/// never grows.
///
/// Elements are constructed when inserted.
///
/// Supports types that are neither copyable nor movable (unlike std::vector) through
/// emplace_back().
///
/// Inserting beyond capacity aborts.
///
/// For InplaceVector, the capacity is fixed at compile time.
/// For BoundedVector, the capacity is determined at runtime.
///
/// InplaceVector
/// =============
/// InplaceVector's storage is in-place and its capacity is fixed at compile time.
///
/// For move ctor and move assignment elements are moved individually rather than
/// transferring ownership of storage.
///
/// WARNING: After moving an InplaceVector any pointers to the moved-from elements
/// will not be updated to point to the new location of the elements.
///
/// The moved-from storage is still in place so any pointers to it remain valid, but they point to
/// the old location.
///
/// This can be a problem if elements in the container have pointers to other elements within the
/// same container. Elements in the moved-to container will have pointers that point to the old
/// location of the elements. If the moved-from container is destroyed those pointers will become
/// dangling.
///
/// It is best practice to avoid storing pointers to elements in an InplaceVector.
/// Store indices instead of raw pointers.
///
/// BoundedVector
/// =============
/// BoundedVector's storage is heap-allocated and determined at runtime.
///
/// For move ctor and move assignment the underlying storage is transferred rather than moving
/// individual elements.
///
/// Later operations on the moved-from object will reallocate its storage.
///

namespace Detail
{
template<typename T, size_t N = 0>
class BoundedVectorImpl
{
    static_assert(sizeof(T) >= 1, "Element size must be greater than zero");
    static_assert(sizeof(T) <= std::vector<std::byte>().max_size(), "Element size too large");

    static constexpr size_t kMaxCapacity = std::vector<std::byte>().max_size() / sizeof(T);

    static_assert(N <= kMaxCapacity, "Capacity exceeds maximum allowed size");

    static constexpr bool kIsInPlace = (N > 0);

public:
    using value_type = T;
    using size_type = size_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using difference_type = std::ptrdiff_t;

    // When N==0, the default constructor is deleted - must call the size constructor.
    BoundedVectorImpl() requires(!kIsInPlace) = delete;

    // Default ctor when kIsInPlace is true.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, readability-redundant-parentheses)
    BoundedVectorImpl() requires(kIsInPlace)
        : m_Capacity(N),
          m_Begin(StorageAsT()),
          m_End(StorageAsT())
    {
    }

    // Sized ctor for dynamically sized storage when !kIsInPlace.
    explicit BoundedVectorImpl(const size_type capacity)
        requires(!kIsInPlace)
        : m_Storage(nullptr),
          m_Capacity(capacity),
          m_Begin(StorageAsT()),
          m_End(StorageAsT())
    {
        MLG_ABORTIF(capacity > max_size(), "Size exceeds maximum allowed size");

        // No storage is allocated yet. It will be allocated on demand when elements are added.
    }

    ~BoundedVectorImpl() { Destruct(); }

    // Non-copyable for now.
    BoundedVectorImpl(const BoundedVectorImpl&) = delete;
    BoundedVectorImpl& operator=(const BoundedVectorImpl&) = delete;

    // Custom move ctor when !kIsInPlace, because the storage is dynamically allocated.
    BoundedVectorImpl(BoundedVectorImpl&& other) noexcept
        requires(!kIsInPlace)
        : m_Storage(other.m_Storage),
          m_Capacity(other.m_Capacity),
          m_Begin(other.m_Begin),
          m_End(other.m_End)
    {
        // We stole the storage from the other vector.
        // If an attempt is made to add items to the other
        // vector, it will trigger a reallocation via EnsureCapacity().
        other.m_Storage = nullptr;
        other.m_Begin = other.m_End = other.StorageAsT();
    }
    // Custom move assignment when !kIsInPlace, because the storage is dynamically allocated.
    BoundedVectorImpl& operator=(BoundedVectorImpl&& other) noexcept
        requires(!kIsInPlace)
    {
        if(this != &other)
        {
            Destruct();

            m_Storage = other.m_Storage;
            m_Capacity = other.m_Capacity;
            m_Begin = other.m_Begin;
            m_End = other.m_End;

            // We stole the storage from the other vector.
            // If an attempt is made to add items to the other
            // vector, it will trigger a reallocation via EnsureCapacity().
            other.m_Storage = nullptr;
            other.m_Begin = other.m_End = other.StorageAsT();
        }
        return *this;
    }

    // When kIsInPlace, move operations involve moving the elements within the fixed storage rather
    // than transferring ownership of dynamically allocated storage.

    // NOLINTBEGIN(readability-redundant-parentheses)
    BoundedVectorImpl(BoundedVectorImpl&& other) requires(kIsInPlace)
        : m_Capacity(N),
          m_Begin(StorageAsT()),
          m_End(m_Begin + other.size())
    {
        std::uninitialized_move(other.begin(), other.end(), m_Begin);
    }

    // NOLINTNEXTLINE(readability-magic-numbers) - clang-tidy raised a false positive here.
    BoundedVectorImpl& operator=(BoundedVectorImpl&& other) requires(kIsInPlace)
    {
        if(this != &other)
        {
            // Destroy any elements that will not be overwritten by the move.
            for(size_t i = other.size(); i < size(); ++i)
            {
                std::destroy_at(begin() + i);
            }

            // Move the elements that will be overwritten by the move.
            const size_t minSize = std::min(size(), other.size());
            for(size_t i = 0; i < minSize; ++i)
            {
                *(begin() + i) = std::move(*(other.begin() + i));
            }

            // Construct any new elements that exist in the other vector but not in this one.
            for(size_t i = size(); i < other.size(); ++i)
            {
                // Use m_Begin here because begin() returns nullptr when the container is empty.
                std::uninitialized_move(other.begin() + i, other.begin() + i + 1, m_Begin + i);
            }

            m_Begin = StorageAsT();
            m_End = m_Begin + other.size();
        }
        return *this;
    }
    // NOLINTEND(readability-redundant-parentheses)

    constexpr size_type max_size() const noexcept
    {
        return kMaxCapacity;
    }

    void push_back(const value_type& value)
    {
        MLG_ABORTIF(size() >= capacity());
        EnsureCapacity();

        std::construct_at(m_End, value);
        ++m_End;
    }

    void push_back(value_type&& value)
    {
        MLG_ABORTIF(size() >= capacity());
        EnsureCapacity();

        std::construct_at(m_End, std::move(value));
        ++m_End;
    }

    template<typename... Args>
    reference emplace_back(Args&&... args)
    {
        MLG_ABORTIF(size() >= capacity());
        EnsureCapacity();

        T* element = std::construct_at(m_End, std::forward<Args>(args)...);
        ++m_End;
        return *element;
    }

    template<std::ranges::input_range R>
        requires std::convertible_to<std::ranges::range_reference_t<R>, value_type>
    constexpr void append_range(R&& range)
    {
        std::ranges::copy(std::forward<R>(range), std::back_inserter(*this));
    }

    constexpr iterator erase(const_iterator position) { return erase(position, position + 1); }

    constexpr iterator erase(const_iterator first, const_iterator last)
    {
        MLG_ABORTIF(first < cbegin() || last > cend(), "Range out of bounds");
        MLG_ABORTIF(first > last, "Invalid range");

        if(first == last)
        {
            return begin() + (first - cbegin());
        }

        const auto offset = first - cbegin();
        const auto count = last - first;

        iterator destination = begin() + offset;
        iterator newEnd = std::move(begin() + offset + count, end(), destination);

        std::destroy(newEnd, end());
        m_End = newEnd;

        return begin() + offset;
    }

    size_type size() const noexcept { return static_cast<size_type>(m_End - m_Begin); }

    bool empty() const noexcept { return size() == 0; }

    size_type capacity() const noexcept { return m_Capacity; }

    reference operator[](size_type index) noexcept
    {
        MLG_ABORTIF(index >= size(), "Index out of bounds");
        return *(begin() + index);
    }

    const_reference operator[](size_type index) const noexcept
    {
        MLG_ABORTIF(index >= size(), "Index out of bounds");
        return *(begin() + index);
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::span<T>() noexcept { return { begin(), end() }; }
    
    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::span<const T>() const noexcept { return { begin(), end() }; }

    iterator begin() noexcept { return m_End > m_Begin ? m_Begin : nullptr; }

    const_iterator begin() const noexcept { return m_End > m_Begin ? m_Begin : nullptr; }

    iterator end() noexcept { return m_End > m_Begin ? m_End : nullptr; }

    const_iterator end() const noexcept { return m_End > m_Begin ? m_End : nullptr; }

    const_iterator cbegin() const noexcept { return begin(); }

    const_iterator cend() const noexcept { return end(); }

    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }

    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

    value_type* data() noexcept { return begin(); }

    const value_type* data() const noexcept { return begin(); }

    value_type& front() noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *begin();
    }

    const value_type& front() const noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *begin();
    }

    value_type& back() noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *(end() - 1);
    }

    const value_type& back() const noexcept
    {
        MLG_ABORTIF(empty(), "Vector is empty");
        return *(end() - 1);
    }

private:
    /// Ensures that the vector has enough capacity to store elements.
    /// Allocates dynamic storage if !kIsInPlace and capacity is greater than 0.
    void EnsureCapacity()
    {
        if constexpr(!kIsInPlace)
        {
            if(!m_Storage && capacity() > 0)
            {
                void* p = ::operator new(capacity() * sizeof(T), std::align_val_t{ alignof(T) });

                m_Storage = static_cast<std::byte*>(p);

                m_Begin = m_End = StorageAsT();
            }
        }
    }

    /// Destructs all elements.
    /// Releases dynamic storage if !kIsInPlace.
    /// Call by dtor and move assignment operator.
    void Destruct()
    {
        std::destroy(begin(), end());

        if constexpr(!kIsInPlace)
        {
            ::operator delete(m_Storage, std::align_val_t{ alignof(T) });
            m_Storage = nullptr;
        }

        m_Begin = m_End = StorageAsT();
    }

    constexpr T* StorageAsT() noexcept
    {
        void* p = static_cast<void*>(m_Storage);
        return static_cast<T*>(p);
    }
    constexpr const T* StorageAsT() const noexcept
    {
        const void* p = static_cast<const void*>(m_Storage);
        return static_cast<const T*>(p);
    }

    // Storage for the elements. Dynamically allocated if !kIsInPlace, otherwise fixed-size array.
    using Storage =
        std::conditional_t<!kIsInPlace, std::byte*, std::byte[!kIsInPlace ? 1 : N * sizeof(T)]>;

    alignas(kIsInPlace ? alignof(T) : alignof(std::byte*)) Storage m_Storage;

    size_type m_Capacity{ N };
    iterator m_Begin{ nullptr };
    iterator m_End{ nullptr };
};
} // namespace Detail

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

/// BoundedVector is a dynamically sized vector with heap-allocated storage.
template<typename T>
using BoundedVector = Detail::BoundedVectorImpl<T, 0>;

/// InplaceVector is a fixed-size vector with in-place storage.
template<typename T, size_t N>
    requires(N > 0)
using InplaceVector = Detail::BoundedVectorImpl<T, N>;