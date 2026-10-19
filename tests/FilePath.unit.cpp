#include "FilePath.h"

#include <functional>
#include <gtest/gtest.h>
#include <SDL3/SDL_assert.h>
#include <string>
#include <string_view>

namespace
{
/// Lets failure-path tests check the Result after MLG_CHECKV reports an assertion.
class IgnorePathAssertions final
{
public:
    IgnorePathAssertions()
        : m_PreviousHandler(SDL_GetAssertionHandler(&m_PreviousUserData))
    {
        SDL_SetAssertionHandler(&Ignore, nullptr);
    }

    ~IgnorePathAssertions() { SDL_SetAssertionHandler(m_PreviousHandler, m_PreviousUserData); }

    IgnorePathAssertions(const IgnorePathAssertions&) = delete;
    IgnorePathAssertions& operator=(const IgnorePathAssertions&) = delete;
    IgnorePathAssertions(IgnorePathAssertions&&) = delete;
    IgnorePathAssertions& operator=(IgnorePathAssertions&&) = delete;

private:
    static SDL_AssertState SDLCALL Ignore(const SDL_AssertData*, void*)
    {
        return SDL_ASSERTION_IGNORE;
    }

    void* m_PreviousUserData{ nullptr };
    SDL_AssertionHandler m_PreviousHandler{ nullptr };
};
} // namespace

TEST(RelativeFilePath, AcceptsRelativeFiles)
{
    const auto file = RelativeFilePath::Create("images/texture.png");
    ASSERT_TRUE(file);
    EXPECT_EQ(std::string_view(*file), "images/texture.png");
    EXPECT_STREQ(file->c_str(), "images/texture.png");

    const auto hiddenFile = RelativeFilePath::Create(".env");
    ASSERT_TRUE(hiddenFile);
    EXPECT_EQ(std::string_view(*hiddenFile), ".env");
}

TEST(RelativeFilePath, CopiesNonTerminatedStringView)
{
    constexpr char source[] = "images/brick.pngextra";
    constexpr std::string_view expected = "images/brick.png";

    const auto file = RelativeFilePath::Create(std::string_view(&source[0], expected.size()));
    ASSERT_TRUE(file);
    EXPECT_EQ(std::string_view(*file), expected);
    EXPECT_EQ(std::string_view(file->c_str()), expected);
}

TEST(RelativeFilePath, RejectsInvalidFiles)
{
    const IgnorePathAssertions ignoreAssertions;

    for(const std::string_view path : { "",
            "/foo",
            "foo/",
            "foo//bar",
            ".",
            "..",
            "./foo",
            "foo/./bar",
            "foo/.",
            "../foo",
            "foo/../bar",
            "foo/..",
            "texture..png",
            "..png",
            "a...b",
            "textures/..name",
            "foo\\bar",
            "C:foo" })
    {
        EXPECT_FALSE(RelativeFilePath::Create(path)) << path;
    }

    constexpr char embeddedNull[] = { 'a', '\0', 'b' };
    EXPECT_FALSE(
        RelativeFilePath::Create(std::string_view(&embeddedNull[0], sizeof(embeddedNull))));
}

TEST(DirectoryPath, AcceptsDirectories)
{
    const auto directory = DirectoryPath::Create("assets/textures/");
    ASSERT_TRUE(directory);
    EXPECT_EQ(std::string_view(*directory), "assets/textures/");

    const auto withoutSeparator = DirectoryPath::Create("foo");
    ASSERT_TRUE(withoutSeparator);
    EXPECT_EQ(std::string_view(*withoutSeparator), "foo/");

    EXPECT_EQ(std::string_view(DirectoryPath::GetCurrentDirectory()), "./");
    const auto current = DirectoryPath::Create("./");
    ASSERT_TRUE(current);
    EXPECT_EQ(std::string_view(*current), "./");
}

TEST(DirectoryPath, CopiesNonTerminatedStringView)
{
    char source[] = "assets/texturesEXTRA";
    constexpr std::string_view input = "assets/textures";

    const auto directory = DirectoryPath::Create(std::string_view(&source[0], input.size()));
    ASSERT_TRUE(directory);

    source[0] = 'X';
    EXPECT_EQ(std::string_view(*directory), "assets/textures/");
    EXPECT_STREQ(directory->c_str(), "assets/textures/");
}

TEST(DirectoryPath, ParentPathOfNestedFile)
{
    const auto nestedParent = DirectoryPath::GetParentPath("assets/textures/brick.png");
    ASSERT_TRUE(nestedParent);
    EXPECT_EQ(std::string_view(*nestedParent), "assets/textures/");

    const auto parent = DirectoryPath::GetParentPath("assets/brick.png");
    ASSERT_TRUE(parent);
    EXPECT_EQ(std::string_view(*parent), "assets/");
}

TEST(DirectoryPath, ParentPathOfNestedDirectory)
{
    const auto parent = DirectoryPath::GetParentPath("assets/textures/");
    ASSERT_TRUE(parent);
    EXPECT_EQ(std::string_view(*parent), "assets/");
}

TEST(DirectoryPath, ParentPathOfFileInCurrentDirectory)
{
    const IgnorePathAssertions ignoreAssertions;

    const auto parent = DirectoryPath::GetParentPath("brick.png");
    ASSERT_TRUE(parent);
    EXPECT_EQ(*parent, DirectoryPath::GetCurrentDirectory());
}

TEST(DirectoryPath, RejectsInvalidDirectories)
{
    const IgnorePathAssertions ignoreAssertions;

    for(const std::string_view path : { "",
            "/",
            "/foo/",
            "foo//",
            "foo//bar/",
            "foo/./",
            "foo/../",
            "textures/..name/",
            "../foo/",
            "foo\\bar/",
            "C:foo/",
            "C:/foo/" })
    {
        EXPECT_FALSE(DirectoryPath::Create(path)) << path;
    }

    constexpr char embeddedNull[] = { 'a', '\0', 'b', '/' };
    EXPECT_FALSE(DirectoryPath::Create(std::string_view(&embeddedNull[0], sizeof(embeddedNull))));
}

TEST(FilePath, JoinsDirectoriesAndFiles)
{
    const auto directory = DirectoryPath::Create("assets/textures/");
    const auto file = RelativeFilePath::Create("brick.png");
    ASSERT_TRUE(directory);
    ASSERT_TRUE(file);

    const auto joined = Join(*directory, *file);
    ASSERT_TRUE(joined);
    EXPECT_EQ(std::string_view(*joined), "assets/textures/brick.png");
    EXPECT_STREQ(joined->c_str(), "assets/textures/brick.png");
    EXPECT_EQ(joined->GetStem(), "brick");

    const auto typedMemberJoin = directory->Join(*file);
    const auto viewMemberJoin = directory->Join(std::string_view(*file));
    ASSERT_TRUE(typedMemberJoin);
    ASSERT_TRUE(viewMemberJoin);
    EXPECT_EQ(*joined, *typedMemberJoin);
    EXPECT_EQ(*joined, *viewMemberJoin);
    EXPECT_EQ(std::hash<FilePath>{}(*joined), std::hash<FilePath>{}(*typedMemberJoin));
    EXPECT_EQ(std::hash<FilePath>{}(*joined), std::hash<FilePath>{}(*viewMemberJoin));

    const auto nestedFile = RelativeFilePath::Create("icons/a.png");
    ASSERT_TRUE(nestedFile);
    const auto nestedJoin = Join(*directory, *nestedFile);
    ASSERT_TRUE(nestedJoin);
    EXPECT_EQ(std::string_view(*nestedJoin), "assets/textures/icons/a.png");

    const auto fromCurrent = Join(DirectoryPath::GetCurrentDirectory(), *file);
    ASSERT_TRUE(fromCurrent);
    EXPECT_EQ(std::string_view(*fromCurrent), "brick.png");
    EXPECT_STREQ(fromCurrent->c_str(), "brick.png");
}

TEST(FilePath, GetsStemForExtensionlessHiddenAndCompoundNames)
{
    const DirectoryPath current = DirectoryPath::GetCurrentDirectory();

    const auto extensionless = current.Join("README");
    ASSERT_TRUE(extensionless);
    EXPECT_EQ(extensionless->GetStem(), "README");

    const auto hidden = current.Join(".env");
    ASSERT_TRUE(hidden);
    EXPECT_EQ(hidden->GetStem(), ".env");

    const auto compound = current.Join("archive.tar.gz");
    ASSERT_TRUE(compound);
    EXPECT_EQ(compound->GetStem(), "archive.tar");
}

TEST(FilePath, RejectsInvalidStringViewJoin)
{
    const IgnorePathAssertions ignoreAssertions;
    const auto directory = DirectoryPath::Create("assets/");
    ASSERT_TRUE(directory);

    EXPECT_FALSE(directory->Join("../texture.png"));
    EXPECT_FALSE(directory->Join("/texture.png"));
}

TEST(RelativeFilePath, EnforcesCapacity)
{
    const IgnorePathAssertions ignoreAssertions;
    const std::string longestFile(RelativeFilePath::StringStorageType::kMaxLength, 'a');
    const auto longestFilePath = RelativeFilePath::Create(longestFile);
    ASSERT_TRUE(longestFilePath);
    EXPECT_EQ(longestFilePath->size(), RelativeFilePath::StringStorageType::kMaxLength);
    EXPECT_FALSE(RelativeFilePath::Create(longestFile + 'a'));
}

TEST(DirectoryPath, EnforcesCapacity)
{
    const IgnorePathAssertions ignoreAssertions;
    constexpr std::string_view separator = "/";
    const std::string longestDirectory(RelativeFilePath::StringStorageType::kMaxLength - separator.size(), 'a');
    const auto directory = DirectoryPath::Create(longestDirectory + '/');
    ASSERT_TRUE(directory);
    EXPECT_EQ(directory->size(), RelativeFilePath::StringStorageType::kMaxLength);
    const auto normalizedDirectory = DirectoryPath::Create(longestDirectory);
    ASSERT_TRUE(normalizedDirectory);
    EXPECT_EQ(*directory, *normalizedDirectory);
    EXPECT_FALSE(DirectoryPath::Create(longestDirectory + "a/"));
    EXPECT_FALSE(DirectoryPath::Create(longestDirectory + 'a'));
}

TEST(DirectoryPath, NormalizationPreservesEqualityAndHash)
{
    const auto normalized = DirectoryPath::Create("assets");
    const auto terminated = DirectoryPath::Create("assets/");
    ASSERT_TRUE(normalized);
    ASSERT_TRUE(terminated);

    EXPECT_EQ(*normalized, *terminated);
    EXPECT_EQ(std::hash<DirectoryPath>{}(*normalized), std::hash<DirectoryPath>{}(*terminated));
}

TEST(FilePath, JoinsAtMaximumInputLengths)
{
    constexpr std::string_view separator = "/";
    const std::string directoryName(RelativeFilePath::StringStorageType::kMaxLength - separator.size(), 'd');
    const std::string fileName(RelativeFilePath::StringStorageType::kMaxLength, 'f');
    const auto directory = DirectoryPath::Create(directoryName + '/');
    const auto file = RelativeFilePath::Create(fileName);
    ASSERT_TRUE(directory);
    ASSERT_TRUE(file);

    const auto joined = Join(*directory, *file);
    ASSERT_TRUE(joined);
    const std::string expected = directoryName + '/' + fileName;
    EXPECT_EQ(joined->size(), directory->size() + file->size());
    EXPECT_EQ(joined->size(), RelativeFilePath::StringStorageType::kMaxLength + RelativeFilePath::StringStorageType::kMaxLength);
    EXPECT_LE(joined->size(), FilePath::StringStorageType::kMaxLength);
    EXPECT_EQ(std::string_view(*joined), std::string_view(expected));
    EXPECT_STREQ(joined->c_str(), expected.c_str());

    const auto fromCurrent = Join(DirectoryPath::GetCurrentDirectory(), *file);
    ASSERT_TRUE(fromCurrent);
    EXPECT_EQ(std::string_view(*fromCurrent), std::string_view(fileName));
}
