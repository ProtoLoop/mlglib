#pragma once

#include <limits>

namespace safeint
{

template<typename T>
    requires(!std::numeric_limits<T>::is_signed)
bool
AddWillOverflow(const T x, const T y, const T Max)
{
    static_assert(std::numeric_limits<T>::is_integer, "T must be an integer type");
    return (x > Max) || (y > Max) || (x > (Max - y));
}

template<typename T, T Max = std::numeric_limits<T>::max()>
    requires(!std::numeric_limits<T>::is_signed)
bool
AddWillOverflow(const T x, const T y)
{
    return AddWillOverflow(x, y, Max);
}

template<typename T>
    requires(!std::numeric_limits<T>::is_signed)
bool
MulWillOverflow(const T x, const T y, const T Max)
{
    static_assert(std::numeric_limits<T>::is_integer, "T must be an integer type");
    return (y != 0) && (x > (Max / y));
}

template<typename T, T Max = std::numeric_limits<T>::max()>
    requires(!std::numeric_limits<T>::is_signed)
bool
MulWillOverflow(const T x, const T y)
{
    return MulWillOverflow(x, y, Max);
}

} // namespace safeint