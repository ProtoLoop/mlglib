#pragma once

#include "AssertHelper.h"
#include "InplaceString.h"

#include <algorithm>
#include <span>
#include <string_view>

/// Alpha blending mode for materials.
enum class AlphaMode : uint32_t
{
    /// Fully opaque, no transparency.
    Opaque,
    /// Transparency is determined by a mask.
    Mask,
    /// Full alpha blending.
    Blend
};

/// RGB color representation.
template<typename T>
class RgbColor
{
public:
    using ValueType = T;

    static constexpr T kMaxValue = std::is_integral_v<T> ? static_cast<T>(255) : static_cast<T>(1);
    static constexpr T kMinValue = 0;
    static constexpr size_t kHexStringMaxLen = 15; // #RRGGBBAA

    using HexString = InplaceString<kHexStringMaxLen>;

    constexpr RgbColor() = default;

    constexpr RgbColor(const T inR, const T inG, const T inB) noexcept
        : r(inR),
          g(inG),
          b(inB)
    {
    }

    template<typename U>
    constexpr static T Clamp(const U value) noexcept
    {
        const U clampedValue =
            std::clamp(value, static_cast<U>(kMinValue), static_cast<U>(kMaxValue));
        return static_cast<T>(clampedValue);
    }

    ///  Conversion constructor.
    template<typename U>
    constexpr explicit RgbColor(const RgbColor<U>& other) noexcept;

    /// Converts the color to a hexadecimal string representation - #RRGGBBAA
    [[nodiscard]] HexString ToHexString() const;

    constexpr friend bool operator==(const RgbColor& colorA, const RgbColor& colorB) noexcept
    {
        return colorA.r == colorB.r && colorA.g == colorB.g && colorA.b == colorB.b;
    }

    T r{ 0 }, g{ 0 }, b{ 0 };
};

/// RGBA color representation.
template<typename T>
class RgbaColor
{
public:
    using ValueType = T;

    static constexpr T kMaxValue = std::is_integral_v<T> ? static_cast<T>(255) : static_cast<T>(1);
    static constexpr T kMinValue = 0;
    static constexpr size_t kHexStringMaxLen = 15; // #RRGGBBAA

    using HexString = InplaceString<kHexStringMaxLen>;

    constexpr RgbaColor() = default;

    constexpr RgbaColor(const T inR, const T inG, const T inB) noexcept
        : RgbaColor(inR, inG, inB, 1)
    {
    }

    constexpr RgbaColor(const T inR, const T inG, const T inB, const T inA) noexcept
        : r(inR),
          g(inG),
          b(inB),
          a(inA)
    {
    }

    template<typename U>
    constexpr static T Clamp(const U value) noexcept
    {
        const U clampedValue =
            std::clamp(value, static_cast<U>(kMinValue), static_cast<U>(kMaxValue));
        return static_cast<T>(clampedValue);
    }

    ///  Conversion constructor.
    template<typename U>
    constexpr explicit RgbaColor(const RgbaColor<U>& other) noexcept;

    /// Converts the color to a hexadecimal string representation - #RRGGBBAA
    [[nodiscard]] HexString ToHexString() const;

    constexpr friend bool operator==(const RgbaColor& colorA, const RgbaColor& colorB) noexcept
    {
        return colorA.r == colorB.r
            && colorA.g == colorB.g
            && colorA.b == colorB.b
            && colorA.a == colorB.a;
    }

    T r{ 0 }, g{ 0 }, b{ 0 }, a{ 0 };
};

using RgbaColorf = RgbaColor<float>;
using RgbColorf = RgbColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;
using RgbColoru8 = RgbColor<uint8_t>;

/// Specialization for uint8_t with default alpha of 255.
template<>
constexpr RgbaColor<uint8_t>::RgbaColor(
    const uint8_t inR, const uint8_t inG, const uint8_t inB) noexcept
    : RgbaColor<uint8_t>(inR, inG, inB, kMaxValue)
{
}

/// Specialization for converting from float to uint8_t.
template<>
template<>
constexpr RgbColor<uint8_t>::RgbColor(const RgbColor<float>& other) noexcept
    : r(Clamp(other.r * kMaxValue)),
      g(Clamp(other.g * kMaxValue)),
      b(Clamp(other.b * kMaxValue))
{
}

template<>
template<>
constexpr RgbaColor<uint8_t>::RgbaColor(const RgbaColor<float>& other) noexcept
    : r(Clamp(other.r * kMaxValue)),
      g(Clamp(other.g * kMaxValue)),
      b(Clamp(other.b * kMaxValue)),
      a(Clamp(other.a * kMaxValue))
{
}

/// Specialization for float with clamping between 0.0 and 1.0.
template<>
constexpr RgbColor<float>::RgbColor(const float inR, const float inG, const float inB) noexcept
    : r(Clamp(inR)),
      g(Clamp(inG)),
      b(Clamp(inB))
{
    MLG_ASSERT(inR >= kMinValue && inR <= kMaxValue);
    MLG_ASSERT(inG >= kMinValue && inG <= kMaxValue);
    MLG_ASSERT(inB >= kMinValue && inB <= kMaxValue);
}

template<>
constexpr RgbaColor<float>::RgbaColor(
    const float inR, const float inG, const float inB, const float inA) noexcept
    : r(Clamp(inR)),
      g(Clamp(inG)),
      b(Clamp(inB)),
      a(Clamp(inA))
{
    MLG_ASSERT(inR >= kMinValue && inR <= kMaxValue);
    MLG_ASSERT(inG >= kMinValue && inG <= kMaxValue);
    MLG_ASSERT(inB >= kMinValue && inB <= kMaxValue);
    MLG_ASSERT(inA >= kMinValue && inA <= kMaxValue);
}

/// Specialization for converting from uint8_t to float.
template<>
template<>
constexpr RgbColor<float>::RgbColor(const RgbColor<uint8_t>& other) noexcept
    : r(Clamp(static_cast<float>(other.r) / RgbaColor<uint8_t>::kMaxValue)),
      g(Clamp(static_cast<float>(other.g) / RgbaColor<uint8_t>::kMaxValue)),
      b(Clamp(static_cast<float>(other.b) / RgbaColor<uint8_t>::kMaxValue))
{
}

template<>
template<>
constexpr RgbaColor<float>::RgbaColor(const RgbaColor<uint8_t>& other) noexcept
    : r(Clamp(static_cast<float>(other.r) / RgbaColor<uint8_t>::kMaxValue)),
      g(Clamp(static_cast<float>(other.g) / RgbaColor<uint8_t>::kMaxValue)),
      b(Clamp(static_cast<float>(other.b) / RgbaColor<uint8_t>::kMaxValue)),
      a(Clamp(static_cast<float>(other.a) / RgbaColor<uint8_t>::kMaxValue))
{
}

template<>
inline RgbColor<uint8_t>::HexString
RgbColor<uint8_t>::ToHexString() const
{
    constexpr char kHexDigits[] = "0123456789ABCDEF";
    constexpr size_t kMask = 0x0F;

    char hexStrBuf[RgbColor<uint8_t>::HexString::kMaxLength + 1];
    size_t offset = 0;
    hexStrBuf[offset++] = '#';
    hexStrBuf[offset++] = kHexDigits[(r >> 4) & kMask];
    hexStrBuf[offset++] = kHexDigits[r & kMask];
    hexStrBuf[offset++] = kHexDigits[(g >> 4) & kMask];
    hexStrBuf[offset++] = kHexDigits[g & kMask];
    hexStrBuf[offset++] = kHexDigits[(b >> 4) & kMask];
    hexStrBuf[offset++] = kHexDigits[b & kMask];
    hexStrBuf[offset] = '\0'; // Null-terminate the string

    return HexString(std::string_view(&hexStrBuf[0], offset));
}

template<>
inline RgbaColor<uint8_t>::HexString
RgbaColor<uint8_t>::ToHexString() const
{
    constexpr char kHexDigits[] = "0123456789ABCDEF";
    constexpr size_t kMask = 0x0F;

    char hexStrBuf[RgbaColor<uint8_t>::HexString::kMaxLength + 1];
    size_t offset = 0;
    hexStrBuf[offset++] = '#';
    hexStrBuf[offset++] = kHexDigits[(r >> 4) & kMask];
    hexStrBuf[offset++] = kHexDigits[r & kMask];
    hexStrBuf[offset++] = kHexDigits[(g >> 4) & kMask];
    hexStrBuf[offset++] = kHexDigits[g & kMask];
    hexStrBuf[offset++] = kHexDigits[(b >> 4) & kMask];
    hexStrBuf[offset++] = kHexDigits[b & kMask];
    hexStrBuf[offset++] = kHexDigits[(a >> 4) & kMask];
    hexStrBuf[offset++] = kHexDigits[a & kMask];
    hexStrBuf[offset] = '\0'; // Null-terminate the string

    return HexString(std::string_view(&hexStrBuf[0], offset));
}

template<>
inline RgbColor<float>::HexString
RgbColor<float>::ToHexString() const
{
    return RgbColor<uint8_t>(*this).ToHexString();
}

template<>
inline RgbaColor<float>::HexString
RgbaColor<float>::ToHexString() const
{
    return RgbaColor<uint8_t>(*this).ToHexString();
}

/// User-defined literal to convert a hex color code to an RGBA color.
constexpr RgbaColor<uint8_t>
operator""_rgba(const char* str, const size_t len)
{
    auto from_hex = [](char c) -> uint8_t
    {
        constexpr char kLowerHexOffset = 'a' - 10;
        constexpr char kUpperHexOffset = 'A' - 10;

        if(c >= '0' && c <= '9')
        {
            return static_cast<uint8_t>(c - '0');
        }
        if(c >= 'a' && c <= 'f')
        {
            return static_cast<uint8_t>(c - kLowerHexOffset);
        }
        if(c >= 'A' && c <= 'F')
        {
            return static_cast<uint8_t>(c - kUpperHexOffset);
        }
        return 0;
    };

    const std::span strSpan(str, len);
    const size_t offset = (len > 0 && strSpan[0] == '#') ? 1 : 0;
    const size_t digits = len - offset;

    MLG_ASSERT(digits == 3 || digits == 6 || digits == 8, "Invalid RGBA color code length");

    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    if(digits == 3)
    {
        // Shorthand RGB (e.g., #F0A), expand to full form which is #FF00AA
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 1]) << 4) | from_hex(strSpan[offset + 1])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 2]) << 4) | from_hex(strSpan[offset + 2])));
    }
    if(digits == 6)
    {
        // Full RGB (e.g., #FF00AA)
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset + 1])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 2]) << 4) | from_hex(strSpan[offset + 3])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 4]) << 4) | from_hex(strSpan[offset + 5])));
    }
    if(digits == 8)
    {
        // Full RGBA (e.g., #FF00AAFF)
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset + 1])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 2]) << 4) | from_hex(strSpan[offset + 3])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 4]) << 4) | from_hex(strSpan[offset + 5])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 6]) << 4) | from_hex(strSpan[offset + 7])));
    }
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

    return RgbaColor<uint8_t>(); // default
}


/// User-defined literal to convert a hex color code to an RGBA color.
constexpr RgbColor<uint8_t>
operator""_rgb(const char* str, const size_t len)
{
    auto from_hex = [](char c) -> uint8_t
    {
        constexpr char kLowerHexOffset = 'a' - 10;
        constexpr char kUpperHexOffset = 'A' - 10;

        if(c >= '0' && c <= '9')
        {
            return static_cast<uint8_t>(c - '0');
        }
        if(c >= 'a' && c <= 'f')
        {
            return static_cast<uint8_t>(c - kLowerHexOffset);
        }
        if(c >= 'A' && c <= 'F')
        {
            return static_cast<uint8_t>(c - kUpperHexOffset);
        }
        return 0;
    };

    const std::span strSpan(str, len);
    const size_t offset = (len > 0 && strSpan[0] == '#') ? 1 : 0;
    const size_t digits = len - offset;

    MLG_ASSERT(digits == 3 || digits == 6, "Invalid RGB color code length");

    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    if(digits == 3)
    {
        // Shorthand RGB (e.g., #F0A), expand to full form which is #FF00AA
        return RgbColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 1]) << 4) | from_hex(strSpan[offset + 1])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 2]) << 4) | from_hex(strSpan[offset + 2])));
    }
    if(digits == 6)
    {
        // Full RGB (e.g., #FF00AA)
        return RgbColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset + 1])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 2]) << 4) | from_hex(strSpan[offset + 3])),
            static_cast<uint8_t>(
                (from_hex(strSpan[offset + 4]) << 4) | from_hex(strSpan[offset + 5])));
    }
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

    return RgbColor<uint8_t>(); // default
}
