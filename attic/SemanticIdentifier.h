#pragma once

#include "AssertHelper.h"

#include <cstddef>

/// A helper class for creating strongly-typed identifiers for various semantics, such as
/// models, meshes, materials, etc.
/// @tparam Tag A unique type used to differentiate between different semantics. The Tag type itself
/// is not important, only that it is unique for each semantic.
template<typename Tag, typename ValueType = size_t, ValueType InvalidValue = static_cast<ValueType>(-1)>
class SemanticIdentifier
{
public:
    SemanticIdentifier() = default;
    explicit SemanticIdentifier(const ValueType value)
        : m_Value(value)
    {
        MLG_ASSERT(value != InvalidValue,
            "SemanticIdentifier cannot be created with invalid value");
    }

    ~SemanticIdentifier() = default;
    SemanticIdentifier(const SemanticIdentifier& other) = default;
    SemanticIdentifier& operator=(const SemanticIdentifier& other) = default;

    SemanticIdentifier(SemanticIdentifier&& other) noexcept
        : m_Value(other.m_Value)
    {
        other.m_Value = InvalidValue;
    }

    SemanticIdentifier& operator=(SemanticIdentifier&& other) noexcept
    {
        if(this == &other)
        {
            return *this;
        }
        m_Value = other.m_Value;
        other.m_Value = InvalidValue;
        return *this;
    }

    bool IsValid() const { return m_Value != InvalidValue; }

    explicit operator bool() const { return IsValid(); }

    ValueType GetValue() const
    {
        MLG_ASSERT(IsValid(), "Cannot get value of invalid SemanticIdentifier");
        return m_Value;
    }

    auto operator<=>(const SemanticIdentifier& other) const = default;

private:
    ValueType m_Value{ InvalidValue };
};