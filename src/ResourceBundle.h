#pragma once

#include "AssertHelper.h"
#include "BoundingVolumes.h"
#include "Color.h"
#include "CoopTask.h"
#include "FileFetcher.h"
#include "PhysicsTypes.h"
#include "Result.h"
#include "SceneTypes.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

/**
Format contract:
Byte order:          little-endian
Integers:            fixed-width std::uintN_t / std::intN_t
Floats:              IEEE-754 binary32/binary64
Pointers:            prohibited
size_t/ptrdiff_t:    prohibited
bool:                prohibited in persistent structs
Enums:               explicit underlying type
References:          uint32 offsets or indices
Object placement:    explicitly aligned
Struct requirements: standard-layout + trivially-copyable
Layout:              sizeof/offsetof compile-time verified
*/

struct SceneDef;

static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::uint64_t) == 8); // NOLINT(readability-magic-numbers)
static_assert(sizeof(float) == 4);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(sizeof(std::int32_t) == 4);
static_assert(sizeof(std::int64_t) == 8); // NOLINT(readability-magic-numbers)
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_unsigned_v<VertexIndex>);
static_assert(std::numeric_limits<VertexIndex>::max()
    <= std::numeric_limits<uint32_t>::max()); // NOLINT(misc-redundant-expression)

/// Concept for types that can be safely read from and written to binary streams.
template<class T>
concept BinaryStruct = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

// Compile-time assertions for struct offsets and sizes that will print
// a compiler error that includes the actual offset or size.

template<size_t Expected, size_t Actual>
struct mlg_assert_offset
{
    static_assert(Actual == Expected, "offset mismatch");
};

template<typename T, size_t Expected, size_t Actual = sizeof(T)>
struct mlg_assert_size
{
    static_assert(Actual == Expected, "size mismatch");
};

#define MLG_ASSERT_OFFSET(struct_name, member, offset)                                             \
    static_assert(offsetof(struct_name, member) == (offset), "offset mismatch");

#define MLG_ASSERT_SIZE(struct_name, size)                                                         \
    static_assert(sizeof(struct_name) == (size), "size mismatch");

// NOLINTBEGIN(bugprone-macro-parentheses)
#define MLG_COUNT_FIELD(type, name, ...) +1
#define MLG_SIZE_FIELD(type, name, ...) +sizeof(type)
// NOLINTEND(bugprone-macro-parentheses)

#define MLG_DECLARE_FIELD(type, name, ...) type name __VA_OPT__(= __VA_ARGS__);

#define MLG_FIELD_COUNT(fields) (0 fields(MLG_COUNT_FIELD))

#define MLG_FIELD_SIZE_SUM(fields) (0 fields(MLG_SIZE_FIELD))

#define MLG_ASSERT_NO_PADDING(type, fields)                                                        \
    static_assert(sizeof(type) == MLG_FIELD_SIZE_SUM(fields))

#define MLG_ASSERT_FIELD_COUNT(fields, count) static_assert(MLG_FIELD_COUNT(fields) == (count))

struct StringResource;
struct TextureResource;
struct MaterialResource;
struct MeshResource;
struct ModelResource;
struct ModelInstanceResource;
struct ColliderResource;
struct RigidBodyResource;
struct CameraResource;
struct PointLightResource;
struct DirectionalLightResource;
struct SceneNodeResource;

class ResourceBundleBuilder;

/// Owns a scene's geometry, materials, texture paths, nodes, and physics data in a single buffer.
///
/// Build it with ResourceBundleBuilder::Build(). Accessors return read-only views of the data.
///
/// The builder stores nodes in breadth-first order. Roots come first, and each
/// parent appears before its children. A node's direct children occupy the
/// range given by FirstChildIndex and ChildCount.
///
/// Model instances follow node order. Rigid bodies follow root node order.
/// Each model's meshes and each rigid body's colliders occupy a contiguous range
/// given by its first index and count.
///
/// Resource indices and ranges can be used without bounds checks.
/// Returned spans and string views remain valid until the bundle's memory is freed.
class ResourceBundle final
{
    static constexpr uint32_t kMagicRaw0 = (static_cast<uint32_t>('M') << 24)
        | (static_cast<uint32_t>('L') << 16)
        | (static_cast<uint32_t>('G') << 8)
        | (static_cast<uint32_t>(' '));

    static constexpr uint32_t kMagicRaw1 = (static_cast<uint32_t>('S') << 24)
        | (static_cast<uint32_t>('C') << 16)
        | (static_cast<uint32_t>('E') << 8)
        | (static_cast<uint32_t>('N'));

public:
    class LoadTask;

    /// Offset type used for resource bundle offsets.  32-bit to maintain browser compatibility.
    using OffsetType = uint32_t;
    using IndexType = uint32_t;

    static_assert(std::numeric_limits<OffsetType>::max() <= std::numeric_limits<size_t>::max());
    static_assert(std::numeric_limits<IndexType>::max() <= std::numeric_limits<size_t>::max());

    static constexpr uint32_t kMagic0 =
        std::endian::native == std::endian::big ? kMagicRaw0 : std::byteswap(kMagicRaw0);
    static constexpr uint32_t kMagic1 =
        std::endian::native == std::endian::big ? kMagicRaw1 : std::byteswap(kMagicRaw1);

    static constexpr uint32_t kVersion = 0x00000001;

    static constexpr OffsetType kInvalidOffset = std::numeric_limits<OffsetType>::max();
    static constexpr IndexType kInvalidIndex = std::numeric_limits<IndexType>::max();
    static constexpr size_t kMaxCount = std::numeric_limits<IndexType>::max();

    // Maximum allowed size for a resource bundle.
    static constexpr size_t kMaxBundleSize =
        std::min(static_cast<size_t>(std::numeric_limits<OffsetType>::max()),
            std::vector<std::byte>().max_size());

    static constexpr size_t kMaxOffset = kMaxBundleSize - 1;

    static Result<std::unique_ptr<LoadTask>> Load(const FilePath& filePath,
        FileFetcher& fileFetcher);

#define RESOURCE_BUNDLE_HEADER_FIELDS(X)                                                           \
    X(IndexType, Checksum, 0)                                                                      \
    X(IndexType, Magic0, kMagic0)                                                                  \
    X(IndexType, Magic1, kMagic1)                                                                  \
    X(IndexType, Version, kVersion)                                                                \
    X(OffsetType, TotalSize, 0)                                                                    \
    X(OffsetType, CharsOffset, kInvalidOffset)                                                     \
    X(OffsetType, StringsOffset, kInvalidOffset)                                                   \
    X(OffsetType, TexturesOffset, kInvalidOffset)                                                  \
    X(OffsetType, MaterialsOffset, kInvalidOffset)                                                 \
    X(OffsetType, VerticesOffset, kInvalidOffset)                                                  \
    X(OffsetType, IndicesOffset, kInvalidOffset)                                                   \
    X(OffsetType, MeshesOffset, kInvalidOffset)                                                    \
    X(OffsetType, ModelsOffset, kInvalidOffset)                                                    \
    X(OffsetType, ModelInstancesOffset, kInvalidOffset)                                            \
    X(OffsetType, CollidersOffset, kInvalidOffset)                                                 \
    X(OffsetType, RigidBodiesOffset, kInvalidOffset)                                               \
    X(OffsetType, CamerasOffset, kInvalidOffset)                                                   \
    X(OffsetType, PointLightsOffset, kInvalidOffset)                                               \
    X(OffsetType, DirectionalLightsOffset, kInvalidOffset)                                         \
    X(OffsetType, NodesOffset, kInvalidOffset)                                                     \
    X(IndexType, CharsLength, 0)                                                                   \
    X(IndexType, StringCount, 0)                                                                   \
    X(IndexType, TextureCount, 0)                                                                  \
    X(IndexType, MaterialCount, 0)                                                                 \
    X(IndexType, VertexCount, 0)                                                                   \
    X(IndexType, IndexCount, 0)                                                                    \
    X(IndexType, MeshCount, 0)                                                                     \
    X(IndexType, ModelCount, 0)                                                                    \
    X(IndexType, ModelInstanceCount, 0)                                                            \
    X(IndexType, ColliderCount, 0)                                                                 \
    X(IndexType, RigidBodyCount, 0)                                                                \
    X(IndexType, CameraCount, 0)                                                                   \
    X(IndexType, PointLightCount, 0)                                                               \
    X(IndexType, DirectionalLightCount, 0)                                                         \
    X(IndexType, NodeCount, 0)

    struct Header
    {
        RESOURCE_BUNDLE_HEADER_FIELDS(MLG_DECLARE_FIELD)
    };
    static_assert(BinaryStruct<Header>);
    MLG_ASSERT_FIELD_COUNT(RESOURCE_BUNDLE_HEADER_FIELDS, 35);
    MLG_ASSERT_NO_PADDING(Header, RESOURCE_BUNDLE_HEADER_FIELDS);
    MLG_ASSERT_OFFSET(Header, Checksum, 0)
    MLG_ASSERT_OFFSET(Header, Magic0, 4)
    MLG_ASSERT_OFFSET(Header, Magic1, 8)
    MLG_ASSERT_OFFSET(Header, Version, 12)
    MLG_ASSERT_OFFSET(Header, TotalSize, 16)
    MLG_ASSERT_OFFSET(Header, CharsOffset, 20)
    MLG_ASSERT_OFFSET(Header, StringsOffset, 24)
    MLG_ASSERT_OFFSET(Header, TexturesOffset, 28)
    MLG_ASSERT_OFFSET(Header, MaterialsOffset, 32)
    MLG_ASSERT_OFFSET(Header, VerticesOffset, 36)
    MLG_ASSERT_OFFSET(Header, IndicesOffset, 40)
    MLG_ASSERT_OFFSET(Header, MeshesOffset, 44)
    MLG_ASSERT_OFFSET(Header, ModelsOffset, 48)
    MLG_ASSERT_OFFSET(Header, ModelInstancesOffset, 52)
    MLG_ASSERT_OFFSET(Header, CollidersOffset, 56)
    MLG_ASSERT_OFFSET(Header, RigidBodiesOffset, 60)
    MLG_ASSERT_OFFSET(Header, CamerasOffset, 64)
    MLG_ASSERT_OFFSET(Header, PointLightsOffset, 68)
    MLG_ASSERT_OFFSET(Header, DirectionalLightsOffset, 72)
    MLG_ASSERT_OFFSET(Header, NodesOffset, 76)
    MLG_ASSERT_OFFSET(Header, CharsLength, 80)
    MLG_ASSERT_OFFSET(Header, StringCount, 84)
    MLG_ASSERT_OFFSET(Header, TextureCount, 88)
    MLG_ASSERT_OFFSET(Header, MaterialCount, 92)
    MLG_ASSERT_OFFSET(Header, VertexCount, 96)
    MLG_ASSERT_OFFSET(Header, IndexCount, 100)
    MLG_ASSERT_OFFSET(Header, MeshCount, 104)
    MLG_ASSERT_OFFSET(Header, ModelCount, 108)
    MLG_ASSERT_OFFSET(Header, ModelInstanceCount, 112)
    MLG_ASSERT_OFFSET(Header, ColliderCount, 116)
    MLG_ASSERT_OFFSET(Header, RigidBodyCount, 120)
    MLG_ASSERT_OFFSET(Header, CameraCount, 124)
    MLG_ASSERT_OFFSET(Header, PointLightCount, 128)
    MLG_ASSERT_OFFSET(Header, DirectionalLightCount, 132)
    MLG_ASSERT_OFFSET(Header, NodeCount, 136)
    MLG_ASSERT_SIZE(Header, 140)

    ResourceBundle() = default;
    ~ResourceBundle() = default;
    ResourceBundle(const ResourceBundle&) = delete;
    ResourceBundle& operator=(const ResourceBundle&) = delete;
    ResourceBundle(ResourceBundle&&) = default;
    ResourceBundle& operator=(ResourceBundle&&) = default;

    std::span<const std::byte> GetBuffer() const;

    /// Clears the resource bundle, releasing its internal buffer.
    /// Invalidates all previously returned spans and string views.
    void Clear() { std::vector<std::byte>().swap(m_Buffer); }

    /// Returns a span of resources of the specified type.
    /// E.g. GetResources<TextureResource>() returns a span of all texture resources.
    template<typename RsrcType>
    std::span<const RsrcType> GetResources() const;

    /// Returns a span of meshes associated with the model resource.
    std::span<const MeshResource> GetMeshes(const ModelResource& modelRsrc) const;

    /// Returns a span of colliders associated with the rigid body resource.
    std::span<const ColliderResource> GetColliders(const RigidBodyResource& rigidBodyRsrc) const;

    /// Returns the string view corresponding to the string resource.
    std::string_view GetStringView(const StringResource& stringResource) const;

private:
    friend class ResourceBundleBuilder;
    friend LoadTask;

    explicit ResourceBundle(std::vector<std::byte>&& buffer);

    const Header* GetHeader() const;

    template<typename T>
    std::span<const T> GetSpan(const OffsetType byteOffset, const IndexType itemCount) const
    {
        if(!MLG_VERIFY(byteOffset <= m_Buffer.size(), "byteOffset is out of ranger")
            || itemCount == 0)
        {
            return std::span<T>();
        }

        const size_t capacity = m_Buffer.size() - byteOffset;
        const size_t maxItems = capacity / sizeof(T);

        if(!MLG_VERIFY(itemCount <= maxItems, "Item count is out of range"))
        {
            return std::span<T>();
        }

        const std::span s(m_Buffer);
        const void* p = s.subspan(static_cast<size_t>(byteOffset)).data();
        return std::span<const T>(static_cast<const T*>(p), itemCount);
    }

    static bool Validate(const std::span<const std::byte>& buffer);

    std::vector<std::byte> m_Buffer;
};

class ResourceBundleBuilder final
{
public:
    ResourceBundleBuilder() = default;
    ~ResourceBundleBuilder() = default;
    ResourceBundleBuilder(const ResourceBundleBuilder&) = delete;
    ResourceBundleBuilder& operator=(const ResourceBundleBuilder&) = delete;
    ResourceBundleBuilder(ResourceBundleBuilder&&) = default;
    ResourceBundleBuilder& operator=(ResourceBundleBuilder&&) = default;

    struct Options
    {
        /// Flags from TextureResourceEncoding indicating which encoding variants are available.
        uint32_t TextureEncodings{ 0 };
    };

    Result<ResourceBundle> Build(const SceneDef& sceneDef);

    Result<ResourceBundle> Build(const SceneDef& sceneDef, const Options& options);

private:
    ResourceBundle::Header* GetHeader()
    {
        if(!MLG_VERIFY(m_Buffer.size() >= sizeof(ResourceBundle::Header), "Buffer is empty"))
        {
            return nullptr;
        }
        void* p = m_Buffer.data();
        return static_cast<ResourceBundle::Header*>(p);
    }

    void AppendHeader(const ResourceBundle::OffsetType totalSize);

    /// Appends a span of items of type T to the resource bundle.
    /// Updates the corresponding header fields for offset and length.
    /// Ensures that the header fields are properly initialized and not already set.
    template<typename RsrcType>
    Result<> AppendResources(const std::span<const RsrcType> items);

    std::vector<std::byte> m_Buffer;
};

class ResourceBundle::LoadTask : public ICoopTask<>
{
    friend class ResourceBundle;

public:
    LoadTask() = delete;
    ~LoadTask() override = default; // The base class will verify completion
    LoadTask(const LoadTask&) = delete;
    LoadTask& operator=(const LoadTask&) = delete;
    LoadTask(LoadTask&&) = delete;
    LoadTask& operator=(LoadTask&&) = delete;

    Result<ResourceBundle> Take();

private:
    friend class ResourceBundle;

    LoadTask(const FilePath& filePath, FileFetcher& fileFetcher);

    Result<> OnStart() override;

    void OnUpdate() override;

    FilePath m_FilePath;
    FileFetcher* m_FileFetcher{ nullptr };
    FetchRequestId m_FetchRequestId;
    Result<ResourceBundle> m_Result;
};

/// StringResource

#define STRING_RESOURCE_FIELDS(X)                                                                  \
    X(ResourceBundle::IndexType, CharIndex, 0)                                                     \
    X(ResourceBundle::IndexType, Length, 0)

struct StringResource final
{
    STRING_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<StringResource>);
MLG_ASSERT_FIELD_COUNT(STRING_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(StringResource, STRING_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(StringResource, CharIndex, 0)
MLG_ASSERT_OFFSET(StringResource, Length, 4)
MLG_ASSERT_SIZE(StringResource, 8)

/// TextureResource

/// Flags indicating which encoding variants are available, in addition to the original format.
enum TextureResourceEncoding : uint32_t // NOLINT(cppcoreguidelines-use-enum-class)
{
    None = 0x00,             // No encoding variants available
    BC7 = 0x01,              // Block Compression encoding variant
    ASTC = 0x02,             // Adaptive Scalable Texture Compression encoding variant
    RGBA = 0x04,             // Uncompressed RGBA KTX2 variant
    All = BC7 | ASTC | RGBA, // All encoding variants available
};
#define TEXTURE_RESOURCE_FIELDS(X)                                                                 \
    X(StringResource, TexturePath)                                                                 \
    X(uint32_t, Encodings, 0)

struct TextureResource final
{
    TEXTURE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)

    bool HasBC7Encoding() const { return (Encodings & TextureResourceEncoding::BC7) != 0; }

    bool HasASTCEncoding() const { return (Encodings & TextureResourceEncoding::ASTC) != 0; }

    bool HasRGBAEncoding() const { return (Encodings & TextureResourceEncoding::RGBA) != 0; }
};
static_assert(BinaryStruct<TextureResource>);
MLG_ASSERT_FIELD_COUNT(TEXTURE_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(TextureResource, TEXTURE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(TextureResource, TexturePath, 0)
MLG_ASSERT_OFFSET(TextureResource, Encodings, 8)
MLG_ASSERT_SIZE(TextureResource, 12)

/// MaterialResource

#define MATERIAL_RESOURCE_FIELDS(X)                                                                \
    X(ResourceBundle::IndexType, BaseTextureIndex, ResourceBundle::kInvalidIndex)                  \
    X(RgbaColorf, Color, { 1, 0, 1, 1 })                                                           \
    X(AlphaMode, AlphaMode, AlphaMode::Opaque)                                                     \
    X(float, Metalness, 0)                                                                         \
    X(float, Roughness, 0)

struct MaterialResource final
{
    MATERIAL_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<MaterialResource>);
MLG_ASSERT_FIELD_COUNT(MATERIAL_RESOURCE_FIELDS, 5);
MLG_ASSERT_NO_PADDING(MaterialResource, MATERIAL_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(MaterialResource, BaseTextureIndex, 0)
MLG_ASSERT_OFFSET(MaterialResource, Color, 4)
MLG_ASSERT_OFFSET(MaterialResource, AlphaMode, 20)
MLG_ASSERT_OFFSET(MaterialResource, Metalness, 24)
MLG_ASSERT_OFFSET(MaterialResource, Roughness, 28)
MLG_ASSERT_SIZE(MaterialResource, 32)

/// MeshResource

#define MESH_RESOURCE_FIELDS(X)                                                                    \
    X(ResourceBundle::IndexType, IndexCount, 0)                                                    \
    X(ResourceBundle::IndexType, FirstIndex, 0)                                                    \
    X(ResourceBundle::IndexType, BaseVertex, 0)                                                    \
    X(ResourceBundle::IndexType, MaterialIndex, ResourceBundle::kInvalidIndex)                     \
    X(BoundingBox, BoundingBox)

// clang-tidy false positive
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct MeshResource final
{
    MESH_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<MeshResource>);
MLG_ASSERT_FIELD_COUNT(MESH_RESOURCE_FIELDS, 5);
MLG_ASSERT_NO_PADDING(MeshResource, MESH_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(MeshResource, IndexCount, 0)
MLG_ASSERT_OFFSET(MeshResource, FirstIndex, 4)
MLG_ASSERT_OFFSET(MeshResource, BaseVertex, 8)
MLG_ASSERT_OFFSET(MeshResource, MaterialIndex, 12)
MLG_ASSERT_OFFSET(MeshResource, BoundingBox, 16)
MLG_ASSERT_SIZE(MeshResource, 40)

/// ModelResource

#define MODEL_RESOURCE_FIELDS(X)                                                                   \
    X(ResourceBundle::IndexType, FirstMeshIndex, 0)                                                \
    X(ResourceBundle::IndexType, MeshCount, 0)                                                     \
    X(BoundingBox, BoundingBox)

// clang-tidy false positive
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct ModelResource final
{
    MODEL_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<ModelResource>);
MLG_ASSERT_FIELD_COUNT(MODEL_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(ModelResource, MODEL_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ModelResource, FirstMeshIndex, 0)
MLG_ASSERT_OFFSET(ModelResource, MeshCount, 4)
MLG_ASSERT_OFFSET(ModelResource, BoundingBox, 8)
MLG_ASSERT_SIZE(ModelResource, 32)

/// ModelInstanceResource

#define MODEL_INSTANCE_RESOURCE_FIELDS(X)                                                          \
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)                         \
    X(ResourceBundle::IndexType, ModelIndex, ResourceBundle::kInvalidIndex)

struct ModelInstanceResource final
{
    MODEL_INSTANCE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<ModelInstanceResource>);
MLG_ASSERT_FIELD_COUNT(MODEL_INSTANCE_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(ModelInstanceResource, MODEL_INSTANCE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ModelInstanceResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(ModelInstanceResource, ModelIndex, 4)
MLG_ASSERT_SIZE(ModelInstanceResource, 8)

/// ColliderResource

#define SPHERE_RESOURCE_FIELDS(X)                                                                  \
    X(float, Radius)                                                                               \
    X(Vec3f, Center)

#define BOX_RESOURCE_FIELDS(X)                                                                     \
    X(Vec3f, HalfExtents)                                                                          \
    X(Vec3f, Center)

#define CAPSULE_RESOURCE_FIELDS(X)                                                                 \
    X(float, Radius)                                                                               \
    X(float, HalfHeight)                                                                           \
    X(Vec3f, Center)

#define COLLIDER_RESOURCE_FIELDS(X)                                                                \
    X(CollisionType, CollisionType)                                                                \
    X(ColliderShapeType, ShapeType)                                                                \
    X(ColliderResource::Shape, Shape)

struct ColliderResource final
{
    struct Sphere final
    {
        SPHERE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
    };

    struct Box final
    {
        BOX_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
    };

    struct Capsule final
    {
        CAPSULE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
    };

    union Shape
    {
        Shape() = delete;
        Shape(const Sphere& sphere) // NOLINT(google-explicit-constructor)
            : Sphere(sphere)
        {
        }
        Shape(const Box& box) // NOLINT(google-explicit-constructor)
            : Box(box)
        {
        }
        Shape(const Capsule& capsule) // NOLINT(google-explicit-constructor)
            : Capsule(capsule)
        {
        }

        Sphere Sphere;
        Box Box;
        Capsule Capsule;
    };

    COLLIDER_RESOURCE_FIELDS(MLG_DECLARE_FIELD)

    const Sphere& GetSphere() const
    {
        MLG_ASSERT(ShapeType == ColliderShapeType::Sphere);
        return Shape.Sphere; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const Box& GetBox() const
    {
        MLG_ASSERT(ShapeType == ColliderShapeType::Box);
        return Shape.Box; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const Capsule& GetCapsule() const
    {
        MLG_ASSERT(ShapeType == ColliderShapeType::Capsule);
        return Shape.Capsule; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
};
static_assert(BinaryStruct<ColliderResource::Sphere>);
MLG_ASSERT_FIELD_COUNT(SPHERE_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(ColliderResource::Sphere, SPHERE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource::Sphere, Radius, 0)
MLG_ASSERT_OFFSET(ColliderResource::Sphere, Center, 4)
MLG_ASSERT_SIZE(ColliderResource::Sphere, 16)

static_assert(BinaryStruct<ColliderResource::Box>);
MLG_ASSERT_FIELD_COUNT(BOX_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(ColliderResource::Box, BOX_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource::Box, HalfExtents, 0)
MLG_ASSERT_OFFSET(ColliderResource::Box, Center, 12)
MLG_ASSERT_SIZE(ColliderResource::Box, 24)

static_assert(BinaryStruct<ColliderResource::Capsule>);
MLG_ASSERT_FIELD_COUNT(CAPSULE_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(ColliderResource::Capsule, CAPSULE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource::Capsule, Radius, 0)
MLG_ASSERT_OFFSET(ColliderResource::Capsule, HalfHeight, 4)
MLG_ASSERT_OFFSET(ColliderResource::Capsule, Center, 8)
MLG_ASSERT_SIZE(ColliderResource::Capsule, 20)

static_assert(BinaryStruct<ColliderResource>);
MLG_ASSERT_FIELD_COUNT(COLLIDER_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(ColliderResource, COLLIDER_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource, CollisionType, 0)
MLG_ASSERT_OFFSET(ColliderResource, ShapeType, 4)
MLG_ASSERT_OFFSET(ColliderResource, Shape, 8)
MLG_ASSERT_SIZE(ColliderResource, 32)

/// RigidBodyResource

#define RIGID_BODY_RESOURCE_FIELDS(X)                                                              \
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)                         \
    X(float, Mass, 0.0f)                                                                           \
    X(MotionType, MotionType)                                                                      \
    X(ResourceBundle::IndexType, FirstColliderIndex, 0)                                            \
    X(ResourceBundle::IndexType, ColliderCount, 0)

// clang-tidy false positive
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct RigidBodyResource final
{
    RIGID_BODY_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<RigidBodyResource>);
MLG_ASSERT_FIELD_COUNT(RIGID_BODY_RESOURCE_FIELDS, 5);
MLG_ASSERT_NO_PADDING(RigidBodyResource, RIGID_BODY_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(RigidBodyResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(RigidBodyResource, Mass, 4)
MLG_ASSERT_OFFSET(RigidBodyResource, MotionType, 8)
MLG_ASSERT_OFFSET(RigidBodyResource, FirstColliderIndex, 12)
MLG_ASSERT_OFFSET(RigidBodyResource, ColliderCount, 16)
MLG_ASSERT_SIZE(RigidBodyResource, 20)

/// CameraResource

#define CAMERA_RESOURCE_FIELDS(X)                                                                  \
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)

struct CameraResource final
{
    CAMERA_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<CameraResource>);
MLG_ASSERT_FIELD_COUNT(CAMERA_RESOURCE_FIELDS, 1);
MLG_ASSERT_NO_PADDING(CameraResource, CAMERA_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(CameraResource, NodeIndex, 0)
MLG_ASSERT_SIZE(CameraResource, 4)

/// PointLightResource

#define POINT_LIGHT_RESOURCE_FIELDS(X)                                                             \
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)                         \
    X(Vec3f, Position, Vec3f{ 0.0f, 0.0f, 0.0f })                                                  \
    X(RgbaColorf, Color, RgbaColorf{ 1.0f, 1.0f, 1.0f, 1.0f })

struct PointLightResource final
{
    POINT_LIGHT_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<PointLightResource>);
MLG_ASSERT_FIELD_COUNT(POINT_LIGHT_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(PointLightResource, POINT_LIGHT_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(PointLightResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(PointLightResource, Position, 4)
MLG_ASSERT_OFFSET(PointLightResource, Color, 16)
MLG_ASSERT_SIZE(PointLightResource, 32)

/// DirectionalLightResource

#define DIRECTIONAL_LIGHT_RESOURCE_FIELDS(X)                                                       \
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)                         \
    X(Vec3f, Direction, Vec3f{ 0.0f, -1.0f, 0.0f })                                                \
    X(RgbaColorf, Color, RgbaColorf{ 1.0f, 1.0f, 1.0f, 1.0f })

struct DirectionalLightResource final
{
    DIRECTIONAL_LIGHT_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<DirectionalLightResource>);
MLG_ASSERT_FIELD_COUNT(DIRECTIONAL_LIGHT_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(DirectionalLightResource, DIRECTIONAL_LIGHT_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(DirectionalLightResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(DirectionalLightResource, Direction, 4)
MLG_ASSERT_OFFSET(DirectionalLightResource, Color, 16)
MLG_ASSERT_SIZE(DirectionalLightResource, 32)

/// SceneNodeResource

#define SCENE_NODE_RESOURCE_FIELDS(X)                                                              \
    X(StringResource, Name)                                                                        \
    X(ResourceBundle::IndexType, ParentIndex, ResourceBundle::kInvalidIndex)                       \
    X(ResourceBundle::IndexType, FirstChildIndex, ResourceBundle::kInvalidIndex)                   \
    X(ResourceBundle::IndexType, ChildCount, 0)                                                    \
    X(Vec3f, LocalPos, Vec3f{ 0.0f, 0.0f, 0.0f })                                                  \
    X(Vec4f, LocalRot, Vec4f{ 0.0f, 0.0f, 0.0f, 1.0f })                                            \
    X(Vec3f, LocalScale, Vec3f{ 1.0f, 1.0f, 1.0f })

struct SceneNodeResource final
{
    SCENE_NODE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<SceneNodeResource>);
MLG_ASSERT_FIELD_COUNT(SCENE_NODE_RESOURCE_FIELDS, 7);
MLG_ASSERT_NO_PADDING(SceneNodeResource, SCENE_NODE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(SceneNodeResource, Name, 0)
MLG_ASSERT_OFFSET(SceneNodeResource, ParentIndex, 8)
MLG_ASSERT_OFFSET(SceneNodeResource, FirstChildIndex, 12)
MLG_ASSERT_OFFSET(SceneNodeResource, ChildCount, 16)
MLG_ASSERT_OFFSET(SceneNodeResource, LocalPos, 20)
MLG_ASSERT_OFFSET(SceneNodeResource, LocalRot, 32)
MLG_ASSERT_OFFSET(SceneNodeResource, LocalScale, 48)
MLG_ASSERT_SIZE(SceneNodeResource, 60)