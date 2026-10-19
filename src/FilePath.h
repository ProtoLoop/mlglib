#pragma once

#include "InplaceString.h"
#include "Result.h"

#include <cstddef>
#include <format>
#include <functional>
#include <string_view>

class DirectoryPath;
class FilePath;

/// A validated file path relative to the application's working directory.
/// Create rejects a "." path component and ".." anywhere in the path.
/// It also rejects empty paths, leading or trailing slashes, consecutive slashes,
/// backslashes, colons, embedded nulls, and paths longer than kMaxLength.
/// Valid examples: "foo/bar/baz.bin", ".gitignore".
/// Invalid examples: "./foo/bar/baz.bin", "../foo", "foo/./bar", "foo//bar", "foo/".
class RelativeFilePath final
{
    static constexpr size_t kMaxLength = 127;

public:
    using StringStorageType = InplaceString<kMaxLength>;

    RelativeFilePath() = delete;

    /// Creates a RelativeFilePath from a string view, validating its format and length.
    /// Upon success the result is guaranteed to be a valid RelativeFilePath.
    static Result<RelativeFilePath> Create(std::string_view path);

    const char* c_str() const noexcept { return m_Value.c_str(); }

    size_t size() const noexcept { return m_Value.size(); }

    Result<RelativeFilePath> ReplaceExtension(const std::string_view newExtension) const;

    friend bool operator==(const RelativeFilePath&, const RelativeFilePath&) = default;

    friend Result<FilePath> Join(const DirectoryPath& directory, const RelativeFilePath& file);

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const noexcept { return m_Value; }

    size_t GetHashCode() const noexcept { return m_Value.GetHashCode(); }

private:
    template<size_t N>
        requires(N <= StringStorageType::kMaxLength)
    constexpr explicit RelativeFilePath(InplaceString<N> value)
        : m_Value(std::move(value))
    {
    }

    StringStorageType m_Value;
};

/// A validated directory prefix relative to the application's working directory.
/// Create accepts paths with or without a trailing slash and stores a trailing slash.
/// The exact path "./" represents the current directory.
/// Other invalid inputs follow the RelativeFilePath rules, except that one trailing
/// slash is allowed. The stored path, including that slash, must fit kMaxLength.
/// Valid examples: "foo/bar", "foo/bar/", "./".
/// Invalid examples: "./foo/bar", "foo/./bar", "foo//bar", "/foo/bar", "foo/../bar".
class DirectoryPath final
{
public:

    using StringStorageType = RelativeFilePath::StringStorageType;

    DirectoryPath() = delete;

    /// Creates a DirectoryPath from a string view, validating its format and length.
    /// Upon success the result is guaranteed to be a valid DirectoryPath.
    static Result<DirectoryPath> Create(std::string_view path);

    /// Returns the parent directory of the given path.
    /// Upon success the result is guaranteed to be a valid DirectoryPath.
    static Result<DirectoryPath> GetParentPath(const std::string_view path);

    /// Returns the current working directory of the application.
    static DirectoryPath GetCurrentDirectory();

    const char* c_str() const noexcept { return m_Value.c_str(); }

    size_t size() const noexcept { return m_Value.size(); }

    /// Joins a directory prefix and file path without allocating.
    /// Returns failure if their combined length exceeds FilePath::kMaxLength.
    Result<FilePath> Join(const RelativeFilePath& file) const;

    /// Joins a directory prefix and string_view file path without allocating.
    /// Returns failure if their combined length exceeds FilePath::kMaxLength.
    Result<FilePath> Join(const std::string_view file) const;

    friend bool operator==(const DirectoryPath&, const DirectoryPath&) = default;

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const noexcept { return m_Value; }

    size_t GetHashCode() const noexcept { return m_Value.GetHashCode(); }

private:
    template<size_t N>
        requires(N <= StringStorageType::kMaxLength)
    constexpr explicit DirectoryPath(InplaceString<N> value)
        : m_Value(std::move(value))
    {
    }

    StringStorageType m_Value;
};

// A validated file path - the result of joining a DirectorPath and a RelativeFilePath.
// A FilePath can't be further joined with another path.
class FilePath final
{
    static constexpr size_t kMaxLength = RelativeFilePath::StringStorageType::kMaxLength
        + DirectoryPath::StringStorageType::kMaxLength
        + 1; // +1 for the directory separator

public:

    using StringStorageType = InplaceString<kMaxLength>;

    FilePath() = delete;

    const char* c_str() const noexcept { return m_Value.c_str(); }

    size_t size() const noexcept { return m_Value.size(); }

    /// Returns the stem (filename without the directory path or extension) of the file path.
    std::string_view GetStem() const noexcept;

    /// Returns the extension of the file path, including the leading dot if present.
    std::string_view GetExtension() const noexcept;

    /// Returns the parent directory of the file path, or the current directory if none exists.
    std::string_view GetParentPath() const noexcept;

    /// Returns the filename component of the file path, excluding the directory path.
    std::string_view GetFilename() const noexcept;

    friend bool operator==(const FilePath&, const FilePath&) = default;

    /// Joins a directory prefix and file path without allocating.
    /// Upon success the result is guaranteed to be a valid FilePath.
    friend Result<FilePath> Join(const DirectoryPath& directory, const RelativeFilePath& file);

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const noexcept { return m_Value; }

    size_t GetHashCode() const noexcept { return m_Value.GetHashCode(); }

private:
    template<size_t N>
        requires(N <= StringStorageType::kMaxLength)
    constexpr explicit FilePath(InplaceString<N> value)
        : m_Value(std::move(value))
    {
    }

    StringStorageType m_Value;
};

/// Joins a directory prefix and file path without allocating.
/// Returns failure if their combined length exceeds FilePath::kMaxLength.
Result<FilePath> Join(const DirectoryPath& directory, const RelativeFilePath& file);

/// format_as is used by fmt::format.
inline std::string_view
format_as(const RelativeFilePath& path) noexcept
{
    return std::string_view(path);
}

inline std::string_view
format_as(const FilePath& path) noexcept
{
    return std::string_view(path);
}

inline std::string_view
format_as(const DirectoryPath& path) noexcept
{
    return std::string_view(path);
}

/// Formatter specialization for FilePath to be used with std::format.
template<>
struct std::formatter<RelativeFilePath> : std::formatter<std::string_view>
{
    auto format(const RelativeFilePath& path, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(path), context);
    }
};

/// Formatter specialization for FilePath to be used with std::format.
template<>
struct std::formatter<FilePath> : std::formatter<std::string_view>
{
    auto format(const FilePath& path, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(path), context);
    }
};

/// Formatter specialization for DirectoryPath to be used with std::format.
template<>
struct std::formatter<DirectoryPath> : std::formatter<std::string_view>
{
    auto format(const DirectoryPath& path, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(path), context);
    }
};

/// Hasher specialization for FilePath to be used in unordered containers.
template<>
struct std::hash<RelativeFilePath>
{
    size_t operator()(const RelativeFilePath& path) const noexcept { return path.GetHashCode(); }
};

/// Hasher specialization for FilePath to be used in unordered containers.
template<>
struct std::hash<FilePath>
{
    size_t operator()(const FilePath& path) const noexcept { return path.GetHashCode(); }
};

/// Hasher specialization for DirectoryPath to be used in unordered containers.
template<>
struct std::hash<DirectoryPath>
{
    size_t operator()(const DirectoryPath& path) const noexcept { return path.GetHashCode(); }
};
