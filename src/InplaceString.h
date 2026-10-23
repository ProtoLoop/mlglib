#pragma once

#include "AssertHelper.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <spdlog/fmt/fmt.h>
#include <string_view>

struct InplaceStringHelper
{
    /// The default length for InplaceString if no template argument is provided.
    static constexpr size_t kDefaultLength = 255;
};

/// A fixed-size string class that stores a string of up to N characters and a null terminator.
/// Storage for the string is in-place within the object itself.
/// The null terminator will add one extra character to the storage requirement.
/// So InplaceString<255> requires 256 characters of storage.
template<size_t N = InplaceStringHelper::kDefaultLength>
class InplaceString
{
    template<size_t M>
    friend class InplaceString;

    /// The size of the storage buffer, including the null terminator.
    static constexpr size_t kStorageSize = N + 1;

    static_assert(kStorageSize > 0, "Size must be greater than 0");

public:
    /// The maximum number of characters that can be stored, excluding the null terminator.
    static constexpr size_t kMaxLength = N;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    constexpr InplaceString()
    {
        m_Chars[0] = '\0';
        m_Hash = Hash(*this); // NOLINT(cppcoreguidelines-prefer-member-initializer)
    }

    template<size_t M>
        requires(M <= N)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    constexpr explicit InplaceString(InplaceString<M> value)
        : m_Length(value.size())
    {
        for(size_t i = 0; i < m_Length; ++i)
        {
            m_Chars[i] = value.m_Chars[i];
        }
        m_Chars[m_Length] = '\0';

        m_Hash = Hash(*this);
    }

    template<size_t M>
        requires(M <= N)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    constexpr explicit InplaceString(const char (&value)[M])
        : m_Chars{}
    {
        for(size_t i = 0; i < M && '\0' != value[i]; ++i)
        {
            m_Chars[i] = value[i];
            ++m_Length;
        }
        m_Chars[m_Length] = '\0';

        m_Hash = Hash(*this);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    constexpr explicit InplaceString(const std::string_view str)
        : m_Length((str.size() <= kMaxLength) ? str.size() : kMaxLength)
    {
        MLG_ASSERT(str.size() <= kMaxLength);

        for(size_t i = 0; i < m_Length; ++i)
        {
            m_Chars[i] = str[i];
        }
        m_Chars[m_Length] = '\0';

        m_Hash = Hash(*this);
    }

    InplaceString& operator=(const std::string_view str)
    {
        MLG_ASSERT(str.size() <= kMaxLength);

        m_Length = (str.size() <= kMaxLength) ? str.size() : kMaxLength;

        for(size_t i = 0; i < m_Length; ++i)
        {
            m_Chars[i] = str[i];
        }
        m_Chars[m_Length] = '\0';

        m_Hash = Hash(*this);

        return *this;
    }

    constexpr size_t size() const { return m_Length; }

    constexpr bool empty() const { return m_Length == 0; }

    constexpr const char* c_str() const { return &m_Chars[0]; }

    constexpr const char* data() const { return &m_Chars[0]; }

    constexpr size_t GetHashCode() const { return m_Hash; }

    /// Formats a message into an InplaceString.
    /// If the formatted message exceeds the buffer size, it will be truncated and an
    /// ellipsis will be appended.
    template<typename... Args>
    static InplaceString Format(fmt::format_string<Args...> fmtStr, Args&&... args)
    {
        InplaceString fs;
        auto result =
            fmt::format_to_n(&fs.m_Chars[0], kMaxLength, fmtStr, std::forward<Args>(args)...);

        if(result.size > kMaxLength && kMaxLength >= 3)
        {
            fs.m_Chars[kMaxLength - 3] = '.';
            fs.m_Chars[kMaxLength - 2] = '.';
            fs.m_Chars[kMaxLength - 1] = '.';
        }

        const size_t formattedSize = std::min(result.size, kMaxLength);
        fs.m_Length = formattedSize;
        fs.m_Chars[formattedSize] = '\0';
        fs.m_Hash = Hash(fs);

        return fs;
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr operator std::string_view() const { return std::string_view(&m_Chars[0], m_Length); }

    friend auto operator<=>(const InplaceString& a, const InplaceString& b)
    {
        return std::string_view(a) <=> std::string_view(b);
    }

    friend bool operator==(const InplaceString& a, const InplaceString& b)
    {
        if(a.GetHashCode() == b.GetHashCode())
        {
            return std::string_view(a) == std::string_view(b);
        }

        return false;
    }

    friend bool operator!=(const InplaceString& a, const InplaceString& b) { return !(a == b); }
    
    static constexpr size_t Hash(const std::string_view str) noexcept
    {
        constexpr uint64_t kOffsetBasis = 0xCBF29CE484222325;
        constexpr uint64_t kPrime = 0x100000001B3;

        uint64_t hash = kOffsetBasis;
        for(const char ch : str)
        {
            hash ^= static_cast<unsigned char>(ch);
            hash *= kPrime;
        }
        return static_cast<size_t>(hash);
    }

private:

    char m_Chars[kStorageSize];
    size_t m_Length{ 0 };
    size_t m_Hash{ 0 };
};

/// format_as is used by fmt::format for InplaceString.
template<size_t N>
inline std::string_view
format_as(const InplaceString<N>& str) noexcept
{
    return std::string_view(str);
}

/// Formatter specialization for InplaceString to be used with std::format.
template<size_t N>
struct std::formatter<InplaceString<N>> : std::formatter<std::string_view>
{
    auto format(const InplaceString<N>& str, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(str), context);
    }
};

/// Hash specialization for InplaceString to be used in unordered containers.
template<size_t N>
struct std::hash<InplaceString<N>>
{
    size_t operator()(const InplaceString<N>& str) const noexcept { return str.GetHashCode(); }
};