#define MLG_LOGGER_NAME "PATH"

#include "FilePath.h"

#include <utility>

namespace
{
bool
HasValidComponents(const std::string_view path, const size_t maxLength)
{
    if(path.empty()
        || path.size() > maxLength
        || path.front() == '/'
        || path.back() == '/'
        || path.find_first_of("\\:") != std::string_view::npos
        || path.contains('\0')
        || path.contains(".."))
    {
        return false;
    }

    size_t start = 0;
    while(start < path.size())
    {
        const size_t end = path.find('/', start);
        const std::string_view component = path.substr(start, end - start);
        if(component.empty() || component == ".")
        {
            return false;
        }

        if(end == std::string_view::npos)
        {
            return true;
        }
        start = end + 1;
    }

    return true;
}

/// Returns the directory containing the final path component.
/// The input is a validated relative file or directory path.
///
/// Examples:
/// "assets/textures/brick.png" returns "assets/textures".
/// "assets/textures/" returns "assets".
/// "brick.png" returns an empty view.
constexpr std::string_view
GetParent(const std::string_view path) noexcept
{
    const std::size_t searchPos = path.ends_with('/') ? path.size() - 2 : std::string_view::npos;
    const std::size_t separator = path.find_last_of('/', searchPos);
    return separator == std::string_view::npos ? std::string_view{} : path.substr(0, separator);
}

/// Returns the final path component, including its extension.
/// The input is a validated file path.
///
/// Example:
/// "assets/textures/brick.png" returns "brick.png".
constexpr std::string_view
GetFileName(std::string_view path) noexcept
{
    const std::size_t separator = path.find_last_of('/');

    if(separator == std::string_view::npos)
    {
        return path;
    }

    return path.substr(separator + 1);
}

/// Returns the final extension of a validated filename, including its leading dot.
/// Returns an empty view when the filename has no extension.
/// A filename such as ".gitignore" is treated as having no extension.
///
/// Example:
/// "archive.tar.gz" returns ".gz".
constexpr std::string_view
GetExtension(const std::string_view fileName) noexcept
{
    const std::size_t dot = fileName.find_last_of('.');
    if(dot == std::string_view::npos || dot == 0)
    {
        return {};
    }

    return fileName.substr(dot);
}

/// Returns the filename without its final extension.
/// Directory components are not included.
///
/// Example:
/// "assets/archive.tar.gz" returns "archive.tar".
constexpr std::string_view
GetStem(std::string_view path) noexcept
{
    const std::string_view fileName = GetFileName(path);
    const std::string_view extension = GetExtension(fileName);

    return fileName.substr(0, fileName.size() - extension.size());
}

constexpr std::string_view
RemoveExtension(const std::string_view path) noexcept
{
    const std::string_view filename = GetFileName(path);
    const std::string_view extension = GetExtension(filename);
    return path.substr(0, path.size() - extension.size());
}
} // namespace

/// RelativeFilePath

Result<RelativeFilePath>
RelativeFilePath::Create(const std::string_view path)
{
    MLG_CHECKV(HasValidComponents(path, StringStorageType::kMaxLength),
        "Invalid file path: {}",
        path);

    return RelativeFilePath(StringStorageType(path));
}

Result<RelativeFilePath>
RelativeFilePath::ReplaceExtension(const std::string_view newExtension) const
{
    const std::string_view noDotExtension =
        newExtension.starts_with('.') ? newExtension.substr(1) : newExtension;
    const std::string_view selfWithoutExtension = RemoveExtension(m_Value);

    MLG_CHECK(selfWithoutExtension.size() + noDotExtension.size() + 1
            <= StringStorageType::kMaxLength,
        "Resulting file path is too long: {}.{}",
        selfWithoutExtension,
        noDotExtension);

    const StringStorageType resultPath =
        StringStorageType::Format("{}.{}", selfWithoutExtension, noDotExtension);

    return RelativeFilePath::Create(resultPath);
}

/// DirectoryPath

Result<DirectoryPath>
DirectoryPath::Create(const std::string_view path)
{
    if(path == "./")
    {
        return Current();
    }

    MLG_CHECKV(!path.empty(), "Directory path must not be empty");

    if(path.back() == '/')
    {
        MLG_CHECKV(path.size() <= StringStorageType::kMaxLength,
            "Directory path is too long: {}",
            path);
        MLG_CHECKV(
            HasValidComponents(path.substr(0, path.size() - 1), StringStorageType::kMaxLength),
            "Invalid directory path: {}",
            path);

        return DirectoryPath(StringStorageType(path));
    }

    // +1 for the trailing '/'
    MLG_CHECKV(path.size() + 1 <= StringStorageType::kMaxLength,
        "Directory path is too long: {}",
        path);
    MLG_CHECKV(HasValidComponents(path, StringStorageType::kMaxLength),
        "Invalid directory path: {}",
        path);

    // Add the trailing '/' to the directory path
    MLG_CHECKV(path.size() + 1 <= StringStorageType::kMaxLength,
        "Directory path is too long: {}",
        path);

    auto fixedPath = StringStorageType::Format("{}/", path);

    return DirectoryPath(std::move(fixedPath));
}

Result<DirectoryPath>
DirectoryPath::ParentPath(const std::string_view path)
{
    if(path == "./")
    {
        return Current();
    }

    // HasValidComponents fails for paths ending with a '/'
    const std::string_view withoutSlash =
        path.ends_with('/') ? path.substr(0, path.size() - 1) : path;

    MLG_CHECKV(HasValidComponents(withoutSlash, FilePath::StringStorageType::kMaxLength),
        "Invalid file path: {}",
        path);

    const std::string_view parentDir = GetParent(withoutSlash);

    if(parentDir.empty())
    {
        return DirectoryPath::Current();
    }

    return DirectoryPath::Create(parentDir);
}

DirectoryPath
DirectoryPath::Current()
{
    static const DirectoryPath current(StringStorageType("./"));

    return current;
}

Result<FilePath>
DirectoryPath::Join(const RelativeFilePath& file) const
{
    return ::Join(*this, file);
}

Result<FilePath>
DirectoryPath::Join(const std::string_view file) const
{
    auto relativeFile = RelativeFilePath::Create(file);
    MLG_CHECKV(relativeFile, "Failed to create relative file path: {}", file);
    return Join(*relativeFile);
}

/// FilePath

std::string_view
FilePath::GetStem() const noexcept
{
    return ::GetStem(m_Value);
}

std::string_view
FilePath::GetExtension() const noexcept
{
    return ::GetExtension(m_Value);
}

Result<FilePath>
Join(const DirectoryPath& directory, const RelativeFilePath& file)
{
    const std::string_view directoryView = std::string_view(directory);
    const std::string_view fileView = std::string_view(file);

    if(directoryView == "./")
    {
        return FilePath(file.m_Value);
    }

    MLG_CHECKV(directoryView.size() <= FilePath::StringStorageType::kMaxLength - fileView.size(),
        "Joined file path is too long: {}{}",
        directoryView,
        fileView);

    FilePath::StringStorageType joinedPath =
        FilePath::StringStorageType::Format("{}{}", directoryView, fileView);

    return FilePath(std::move(joinedPath));
}
