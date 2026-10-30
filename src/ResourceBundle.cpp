#include "ResourceBundle.h"

#include "Defer.h"
#include "Result.h"
#include "SafeInteger.h"
#include "SceneTypes.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <SDL3/SDL_stdinc.h>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

template<>
constexpr bool
std::less<MaterialDef>::operator()(const MaterialDef& lhs, const MaterialDef& rhs) const
{
    if(lhs.BaseTexturePath != rhs.BaseTexturePath)
    {
        return lhs.BaseTexturePath < rhs.BaseTexturePath;
    }

    // Use strong_order for floating point values ensure consistent ordering of NaNs.

    if(auto cmp = std::strong_order(lhs.Color.r, rhs.Color.r); cmp != 0)
    {
        return cmp < 0;
    }

    if(auto cmp = std::strong_order(lhs.Color.g, rhs.Color.g); cmp != 0)
    {
        return cmp < 0;
    }

    if(auto cmp = std::strong_order(lhs.Color.b, rhs.Color.b); cmp != 0)
    {
        return cmp < 0;
    }

    if(auto cmp = std::strong_order(lhs.Color.a, rhs.Color.a); cmp != 0)
    {
        return cmp < 0;
    }

    if(auto cmp = std::strong_order(lhs.AlphaMode, rhs.AlphaMode); cmp != 0)
    {
        return cmp < 0;
    }

    if(auto cmp = std::strong_order(lhs.Metalness, rhs.Metalness); cmp != 0)
    {
        return cmp < 0;
    }

    if(auto cmp = std::strong_order(lhs.Roughness, rhs.Roughness); cmp != 0)
    {
        return cmp < 0;
    }

    return false;
}

namespace
{

using NodeDefPointer = std::variant<const RootNodeDef*, const ChildNodeDef*>;
using IndexType = ResourceBundle::IndexType;
using OffsetType = ResourceBundle::OffsetType;
using Header = ResourceBundle::Header;

constexpr IndexType kInvalidIndex = ResourceBundle::kInvalidIndex;
#ifndef NDEBUG
constexpr OffsetType kInvalidOffset = ResourceBundle::kInvalidOffset;
#endif
constexpr size_t kMaxBundleSize = ResourceBundle::kMaxBundleSize;

template<typename T>
constexpr size_t kMaxVectorSize = std::min( //
    {
        ResourceBundle::kMaxCount,
        std::vector<T>().max_size(),
        static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
    });

struct FlatNodeDef
{
    NodeDefPointer NodeDefPtr;
    IndexType ParentIndex;
    IndexType FirstChildIndex;
};

/// Used when constructing a std::map to keep track of the original indices of the values.
template<typename T>
struct IndexEntry
{
    T Value;
    size_t Index;
};

/// Used when flattening a node hierarchy into a breadth-first ordered vector of FlatNodeDef
/// objects.
struct PendingNode
{
    NodeDefPointer Node;
    IndexType ParentIndex;
};

constexpr size_t kMaxCharCount = kMaxVectorSize<char>;
constexpr size_t kMaxVertexCount = kMaxVectorSize<Vertex>;
constexpr size_t kMaxIndexCount = kMaxVectorSize<VertexIndex>;
constexpr size_t kMaxMeshCount = std::min( //
    {
        kMaxVectorSize<MeshResource>,
        kMaxVectorSize<MeshDef>,
        static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
    });

using MaterialLookup = std::map<MaterialDef, IndexEntry<MaterialResource>>;
using TextureLookup = std::map<std::string_view, IndexEntry<TextureResource>>;
using StringLookup = std::map<std::string_view, StringResource>;
using ModelLookup = std::map<std::string_view, IndexEntry<ModelResource>>;

/// Converts a std::map to a std::vector by extracting the values.
template<typename K, typename V>
std::vector<V>
MapToVector(const std::map<K, V>& map)
{
    std::vector<V> vector;
    vector.reserve(map.size());
    for(const auto& [key, value] : map)
    {
        vector.push_back(value);
    }
    return vector;
}

/// Converts a std::map to a std::vector by extracting the values from IndexEntry objects.
template<typename K, typename V>
std::vector<V>
MapToVector(const std::map<K, IndexEntry<V>>& map)
{
    std::vector<V> vector;
    vector.reserve(map.size());
    for(const auto& [key, value] : map)
    {
        vector.push_back(value.Value);
    }
    return vector;
}

/// Calculates the padding needed to align the given size for type T.
template<typename T>
constexpr size_t
Pad(const size_t size)
{
    return (alignof(T) - (size % alignof(T))) % alignof(T);
}

/// Pads the given buffer to ensure it is properly aligned for type T.
/// The buffer is extended with zero bytes as needed.
template<typename T>
void
AppendPad(std::vector<std::byte>& buffer)
{
    const size_t pad = Pad<T>(buffer.size());
    buffer.insert(buffer.end(), pad, std::byte{ 0 });
}

/// Flattens a hierarchy of nodes into a breadth-first ordered vector of FlatNodeDef objects.
Result<std::vector<FlatNodeDef>>
FlattenNodesBreadthFirst(const std::span<const RootNodeDef> rootNodeDefs)
{
    std::vector<FlatNodeDef> flatNodes;
    std::deque<PendingNode> pendingNodes;

    static constexpr size_t kMaxSceneNodeCount = std::min( //
        {
            kMaxVectorSize<FlatNodeDef>,
            flatNodes.max_size(),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    MLG_CHECK(
        !safeint::AddWillOverflow(pendingNodes.size(), rootNodeDefs.size(), kMaxSceneNodeCount),
        "Too many nodes defined");

    // Queue root nodes for processing.
    for(const RootNodeDef& rootNodeDef : rootNodeDefs)
    {
        pendingNodes.emplace_back(&rootNodeDef, kInvalidIndex);
    }

    while(!pendingNodes.empty())
    {
        const PendingNode pendingNode = pendingNodes.front();
        pendingNodes.pop_front();
        const size_t curNodeIndex = flatNodes.size();

        const FlatNodeDef flatNodeDef //
            {
                .NodeDefPtr = pendingNode.Node,
                .ParentIndex = pendingNode.ParentIndex,
                .FirstChildIndex = kInvalidIndex,
            };

        MLG_CHECK(flatNodes.size() < kMaxSceneNodeCount, "Too many nodes defined");

        flatNodes.push_back(flatNodeDef);

        // Queue child nodes for processing.
        const auto visitResult = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                for(const ChildNodeDef& childNodeDef : nodeDef->Children)
                {
                    MLG_CHECK(pendingNodes.size() < kMaxSceneNodeCount, "Too many nodes defined");

                    const PendingNode pendingChildNode //
                        {
                            .Node = &childNodeDef,
                            .ParentIndex = static_cast<IndexType>(curNodeIndex),
                        };

                    pendingNodes.push_back(pendingChildNode);
                }

                return Result<>::Ok;
            },
            pendingNode.Node);

        MLG_CHECK(visitResult);
    }

    // Populate the FirstChildIndex for each parent node.
    IndexType parentIndex = kInvalidIndex;

    for(size_t nodeIndex = 0; nodeIndex < flatNodes.size(); ++nodeIndex)
    {
        const FlatNodeDef& flatNode = flatNodes[nodeIndex];
        if(flatNode.ParentIndex != parentIndex)
        {
            parentIndex = flatNode.ParentIndex;
            FlatNodeDef& parentNode = flatNodes[parentIndex];
            parentNode.FirstChildIndex = static_cast<IndexType>(nodeIndex);
        }
    }

    return flatNodes;
}

Result<std::vector<const MeshDef*>>
CollectMeshDefs(const std::span<const ModelDef> modelDefs)
{
    size_t count = 0;
    for(const ModelDef& modelDef : modelDefs)
    {
        const size_t meshCount = modelDef.MeshDefs.size();
        MLG_CHECKV(meshCount > 0, "Model has no meshes: {}", modelDef.Name);
        MLG_CHECKV(!safeint::AddWillOverflow(count, meshCount, kMaxMeshCount),
            "Mesh count overflow: {}",
            modelDef.Name);

        count += meshCount;
    }

    std::vector<const MeshDef*> meshDefs;
    meshDefs.reserve(count);

    for(const ModelDef& modelDef : modelDefs)
    {
        for(const MeshDef& meshDef : modelDef.MeshDefs)
        {
            meshDefs.push_back(&meshDef);
        }
    }

    return meshDefs;
}

/// Adds a string to the string resource map and character buffer.
Result<StringResource>
AddString(StringLookup& stringResourceMap, std::vector<char>& chars, const std::string_view str)
{
    MLG_CHECKV(!str.empty(), "String is empty");

    MLG_CHECKV(!stringResourceMap.contains(str), "String already exists: {}", str);

    const size_t strLen = str.length();
    const size_t charIndex = chars.size();

    const StringResource sr = StringResource //
        {
            .CharIndex = static_cast<IndexType>(charIndex),
            .Length = static_cast<IndexType>(strLen),
        };

    MLG_CHECKV(!safeint::AddWillOverflow(charIndex, strLen, kMaxCharCount),
        "Character buffer overflow: {}",
        str);

    chars.append_range(str);

    static const size_t kMaxStringCount = std::min( //
        {
            StringLookup().max_size(),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    MLG_CHECKV(stringResourceMap.size() < kMaxStringCount, "String count out of range: {}", str);
    stringResourceMap.emplace(str, sr);

    return sr;
}

Result<StringLookup>
CollectStrings(const std::span<const FlatNodeDef> flatNodeDefs,
    const std::span<const MeshDef* const> meshDefs,
    std::vector<char>& chars)
{
    StringLookup stringResourceMap;

    chars.clear();

    // Collect unique strings from node names.
    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const Result<> result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                if(!stringResourceMap.contains(nodeDef->Name))
                {
                    MLG_CHECK(AddString(stringResourceMap, chars, nodeDef->Name));
                }
                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    // Collect unique strings from texture paths.
    for(const MeshDef* meshDef : meshDefs)
    {
        if(!meshDef->MaterialDef.BaseTexturePath.empty()
            && !stringResourceMap.contains(meshDef->MaterialDef.BaseTexturePath))
        {
            MLG_CHECK(AddString(stringResourceMap, chars, meshDef->MaterialDef.BaseTexturePath));
        }
    }

    return stringResourceMap;
}

Result<TextureLookup>
CollectTextures(const std::span<const MeshDef* const> meshDefs,
    const StringLookup& stringIndexMap,
    const ResourceBundleBuilder::Options& options)
{
    MLG_CHECKV(0 == (options.TextureEncodings & ~TextureResourceEncoding::All),
        "Invalid texture encodings specified: {:#08b}",
        options.TextureEncodings);

    static const size_t kMaxTextureCount = std::min( //
        {
            TextureLookup().max_size(),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    TextureLookup textureIndexMap;

    for(const MeshDef* meshDef : meshDefs)
    {
        const std::string_view texPath = meshDef->MaterialDef.BaseTexturePath;
        if(texPath.empty())
        {
            continue;
        }

        if(textureIndexMap.contains(texPath))
        {
            continue;
        }

        const auto it = stringIndexMap.find(texPath);

        // The string resource map was built from the texture paths so the
        // lookup should succeed 100%.
        MLG_ASSERT(it != stringIndexMap.end());

        const auto& stringRsrc = it->second;

        const TextureResource texRsrc //
            {
                .TexturePath = stringRsrc,
                .Encodings = options.TextureEncodings,
            };

        MLG_CHECKV(textureIndexMap.size() < kMaxTextureCount, "Texture index out of range");

        // Index will be filled later
        textureIndexMap.emplace(texPath, texRsrc);
    }

    size_t index = 0;
    for(auto& [texPath, entry] : textureIndexMap)
    {
        // Fill in the index.
        entry.Index = index++;
    }

    return textureIndexMap;
}

Result<MaterialLookup>
CollectMaterials(const std::span<const MeshDef* const> meshDefs,
    const TextureLookup& textureIndexMap)
{
    MaterialLookup materialIndexMap;

    static const size_t kMaxMaterialCount = std::min( //
        {
            MaterialLookup().max_size(),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    for(const MeshDef* meshDef : meshDefs)
    {
        if(!materialIndexMap.contains(meshDef->MaterialDef))
        {
            const MaterialDef& materialDef = meshDef->MaterialDef;

            IndexType baseTextureIndex = kInvalidIndex;

            const std::string_view texPath = materialDef.BaseTexturePath;

            if(!texPath.empty())
            {
                auto it = textureIndexMap.find(texPath);
                MLG_CHECKV(it != textureIndexMap.end(), "Texture not found: {}", texPath);
                const IndexEntry<TextureResource>& entry = it->second;
                baseTextureIndex = static_cast<IndexType>(entry.Index);
            }

            const MaterialResource materialResource //
                {
                    .BaseTextureIndex = baseTextureIndex,
                    .Color = materialDef.Color,
                    .AlphaMode = materialDef.AlphaMode,
                    .Metalness = materialDef.Metalness,
                    .Roughness = materialDef.Roughness,
                };

            MLG_CHECKV(materialIndexMap.size() < kMaxMaterialCount, "Material index out of range");

            // Index will be filled later
            materialIndexMap.emplace(meshDef->MaterialDef, materialResource);
        }
    }

    size_t index = 0;
    for(auto& [mtlDef, entry] : materialIndexMap)
    {
        // Fill in the index.
        entry.Index = index++;
    }

    return materialIndexMap;
}

Result<std::vector<Vertex>>
CollectVertices(const std::span<const MeshDef* const> meshDefs)
{
    using VertexVector = std::vector<Vertex>;

    size_t count = 0;
    for(const MeshDef* meshDef : meshDefs)
    {
        const size_t vtxCount = meshDef->Vertices.size();
        MLG_CHECKV(vtxCount > 0, "Vertex count must be greater than zero");
        MLG_CHECKV(!safeint::AddWillOverflow(count, vtxCount, kMaxVertexCount),
            "Vertex count overflow");
        count += vtxCount;
    }

    VertexVector vertices;
    vertices.reserve(count);

    for(const MeshDef* meshDef : meshDefs)
    {
        vertices.append_range(meshDef->Vertices);
    }

    return vertices;
}

Result<std::vector<VertexIndex>>
CollectIndices(const std::span<const MeshDef* const> meshDefs)
{
    size_t count = 0;
    for(const MeshDef* meshDef : meshDefs)
    {
        const size_t idxCount = meshDef->Indices.size();
        MLG_CHECKV(idxCount >= 3, "Index count must be at least three");
        MLG_CHECKV(!safeint::AddWillOverflow(count, idxCount, kMaxIndexCount),
            "Index count overflow");
        count += idxCount;
    }

    std::vector<VertexIndex> indices;
    indices.reserve(count);

    for(const MeshDef* meshDef : meshDefs)
    {
        for(const VertexIndex index : meshDef->Indices)
        {
            MLG_CHECKV(index < meshDef->Vertices.size(), "Invalid vertex index");
        }

        indices.append_range(meshDef->Indices);
    }

    return indices;
}

Result<std::vector<MeshResource>>
CollectMeshes(const std::span<const MeshDef* const> meshDefs,
    const MaterialLookup& materialIndexMap)
{
    MLG_CHECKV(meshDefs.size() <= kMaxMeshCount, "Mesh count out of range");

    std::vector<MeshResource> meshes;
    meshes.reserve(meshDefs.size());

    size_t indexIndex = 0;
    size_t vertexIndex = 0;

    for(const MeshDef* meshDef : meshDefs)
    {
        const BoundingBox boundingBox =
            BoundingBox::FromVertices(meshDef->Vertices, meshDef->Indices);

        const auto it = materialIndexMap.find(meshDef->MaterialDef);
        MLG_CHECKV(it != materialIndexMap.end(), "Material not found");
        const IndexEntry<MaterialResource>& entry = it->second;

        // At this point vertices/indices/materials have been confirmed to not
        // exceed the max count so no checking is needed.
        const size_t indexCount = meshDef->Indices.size();
        const size_t vertexCount = meshDef->Vertices.size();
        const size_t materialIndex = entry.Index;

        const MeshResource mesh //
            {
                .IndexCount = static_cast<IndexType>(indexCount),
                .FirstIndex = static_cast<IndexType>(indexIndex),
                .BaseVertex = static_cast<IndexType>(vertexIndex),
                .MaterialIndex = static_cast<IndexType>(materialIndex),
                .BoundingBox = boundingBox,
            };
        meshes.push_back(mesh);

        MLG_CHECKV(!safeint::AddWillOverflow(indexIndex, indexCount, kMaxIndexCount),
            "Index count overflow");
        MLG_CHECKV(!safeint::AddWillOverflow(vertexIndex, vertexCount, kMaxVertexCount),
            "Vertex count overflow");

        indexIndex += indexCount;
        vertexIndex += vertexCount;
    }

    return meshes;
}

Result<ModelLookup>
CollectModels(const std::span<const ModelDef> modelDefs, const std::span<const MeshResource> meshes)
{
    ModelLookup modelIndexMap;

    static const size_t kMaxModelCount = std::min( //
        {
            ModelLookup().max_size(),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    size_t meshIndex = 0;

    // Meshes are collected sequentially for each model.
    // I.e. meshes for model N+1 immediately follow those for model N.

    for(const ModelDef& modelDef : modelDefs)
    {
        // At this point mesh counts have been confirmed to not
        // exceed the max, so no checking is needed.
        const size_t meshCount = modelDef.MeshDefs.size();

        MLG_CHECKV(meshCount > 0, "Model has no meshes: {}", modelDef.Name);

        const std::span meshSpan = meshes.subspan(meshIndex, meshCount);

        BoundingBox boundingBox = meshSpan[0].BoundingBox;
        for(const MeshResource& mesh : meshSpan.subspan(1))
        {
            boundingBox += mesh.BoundingBox;
        }

        const std::string_view modelName = modelDef.Name;

        MLG_CHECKV(!modelName.empty(), "Model name is empty");

        MLG_CHECKV(!modelIndexMap.contains(modelName), "Duplicate model name: {}", modelName);

        const ModelResource model //
            {
                .FirstMeshIndex = static_cast<IndexType>(meshIndex),
                .MeshCount = static_cast<IndexType>(meshCount),
                .BoundingBox = boundingBox,
            };

        MLG_CHECKV(modelIndexMap.size() < kMaxModelCount, "Model index out of range");

        // Index will be filled later
        modelIndexMap.emplace(modelName, model);

        meshIndex += meshCount;
    }

    size_t index = 0;
    for(auto& [modelName, entry] : modelIndexMap)
    {
        // Fill in the index.
        entry.Index = index++;
    }

    return modelIndexMap;
}

Result<std::vector<ModelInstanceResource>>
CollectModelInstances(const std::span<const FlatNodeDef> flatNodeDefs,
    const ModelLookup& modelIndexMap)
{
    std::vector<ModelInstanceResource> modelInstances;

    static constexpr size_t kMaxModelInstanceCount = std::min( //
        {
            static_cast<size_t>(kMaxVectorSize<ModelInstanceResource>),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    size_t count = 0;
    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const bool hasModel =
            std::visit([&](const auto* nodeDef) { return static_cast<bool>(nodeDef->Model); },
                flatNodeDef.NodeDefPtr);
        if(hasModel)
        {
            MLG_CHECKV(!safeint::AddWillOverflow(count, 1uz, kMaxModelInstanceCount),
                "Model instance count overflow");
            ++count;
        }
    }

    modelInstances.reserve(count);

    for(size_t nodeIndex = 0; nodeIndex < flatNodeDefs.size(); ++nodeIndex)
    {
        const FlatNodeDef& flatNodeDef = flatNodeDefs[nodeIndex];

        const auto result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                const std::optional<ModelRef>& optModel = nodeDef->Model;

                if(optModel)
                {
                    const ModelRef& modelRef = *optModel;
                    auto it = modelIndexMap.find(modelRef.Name);
                    MLG_CHECKV(it != modelIndexMap.end(), "Model not found: {}", modelRef.Name);
                    const IndexEntry<ModelResource>& modelEntry = it->second;

                    const size_t modelIdx = modelEntry.Index;

                    const ModelInstanceResource modelInstance //
                        {
                            .NodeIndex = static_cast<IndexType>(nodeIndex),
                            .ModelIndex = static_cast<IndexType>(modelIdx),
                        };

                    modelInstances.push_back(modelInstance);
                }

                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    return modelInstances;
}

ColliderResource
CreateCollider(const ColliderDef& colliderDef)
{
    switch(colliderDef.Shape.GetType())
    {
        case ColliderShapeType::Sphere:
        {
            const SphereDef& sphereDef = colliderDef.Shape.GetSphere();
            return ColliderResource //
                {
                    .CollisionType = colliderDef.CollisionType,
                    .ShapeType = colliderDef.Shape.GetType(),
                    .Shape =
                        ColliderResource::Sphere //
                    {
                        .Radius = sphereDef.Radius,
                        .Center = sphereDef.Center,
                    },
                };
        }
        case ColliderShapeType::Box:
        {
            const BoxDef& boxDef = colliderDef.Shape.GetBox();
            return ColliderResource //
                {
                    .CollisionType = colliderDef.CollisionType,
                    .ShapeType = colliderDef.Shape.GetType(),
                    .Shape =
                        ColliderResource::Box //
                    {
                        .HalfExtents = boxDef.HalfExtents,
                        .Center = boxDef.Center,
                    },
                };
        }
        case ColliderShapeType::Capsule:
        {
            const CapsuleDef& capsuleDef = colliderDef.Shape.GetCapsule();
            return ColliderResource //
                {
                    .CollisionType = colliderDef.CollisionType,
                    .ShapeType = colliderDef.Shape.GetType(),
                    .Shape =
                        ColliderResource::Capsule //
                    {
                        .Radius = capsuleDef.Radius,
                        .HalfHeight = capsuleDef.HalfHeight,
                        .Center = capsuleDef.Center,
                    },
                };
        }
        default:
            MLG_ABORT("Unsupported collider shape type");
    }
}

Result<std::vector<ColliderResource>>
CollectColliders(const std::span<const RootNodeDef> nodeDefs)
{
    std::vector<ColliderResource> colliders;

    static const size_t kMaxColliderCount = std::min( //
        {
            static_cast<size_t>(kMaxVectorSize<ColliderResource>),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    size_t count = 0;
    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        if(nodeDef.Body)
        {
            const size_t colliderCount = nodeDef.Body->Colliders.size();
            MLG_CHECKV(!safeint::AddWillOverflow(count, colliderCount, kMaxColliderCount),
                "Collider count overflow");
            count += colliderCount;
        }
    }

    colliders.reserve(count);

    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        const std::optional<RigidBodyDef> body = nodeDef.Body;
        if(!body)
        {
            continue;
        }

        MLG_CHECKV(!body->Colliders.empty(), "Rigid body has no colliders");

        for(const ColliderDef& colliderDef : body->Colliders)
        {
            colliders.push_back(CreateCollider(colliderDef));
        }
    }

    return colliders;
}

Result<std::vector<RigidBodyResource>>
CollectRigidBodies(const std::span<const RootNodeDef> nodeDefs)
{
    std::vector<RigidBodyResource> rigidBodies;

    static const size_t kMaxRigidBodyCount = std::min( //
        {
            static_cast<size_t>(kMaxVectorSize<RigidBodyResource>),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    size_t count = 0;
    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        if(nodeDef.Body)
        {
            MLG_CHECKV(!safeint::AddWillOverflow(count, 1uz, kMaxRigidBodyCount),
                "Rigid body count overflow");
            ++count;
        }
    }

    rigidBodies.reserve(count);

    size_t colliderIndex = 0;

    for(size_t nodeIndex = 0; nodeIndex < nodeDefs.size(); ++nodeIndex)
    {
        const RootNodeDef& nodeDef = nodeDefs[nodeIndex];

        const std::optional<RigidBodyDef> body = nodeDef.Body;
        if(!body)
        {
            continue;
        }

        const size_t colliderCount = body->Colliders.size();

        MLG_CHECKV(colliderCount > 0, "Rigid body has no colliders");

        const RigidBodyResource rigidBody //
            {
                .NodeIndex = static_cast<IndexType>(nodeIndex),
                .Mass = body->Mass.Value(),
                .MotionType = body->MotionType,
                .FirstColliderIndex = static_cast<IndexType>(colliderIndex),
                .ColliderCount = static_cast<IndexType>(colliderCount),
            };

        rigidBodies.push_back(rigidBody);

        colliderIndex += colliderCount;
    }

    return rigidBodies;
}

Result<std::vector<CameraResource>>
CollectCameras(const std::span<const FlatNodeDef> flatNodeDefs)
{
    std::vector<CameraResource> cameras;

    static const size_t kMaxCameraCount = std::min( //
        {
            static_cast<size_t>(kMaxVectorSize<CameraResource>),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    size_t count = 0;
    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const bool hasCamera =
            std::visit([&](const auto* nodeDef) { return static_cast<bool>(nodeDef->Camera); },
                flatNodeDef.NodeDefPtr);

        if(hasCamera)
        {
            MLG_CHECKV(!safeint::AddWillOverflow(count, 1uz, kMaxCameraCount),
                "Camera count overflow");
            ++count;
        }
    }

    cameras.reserve(count);

    for(size_t nodeIndex = 0; nodeIndex < flatNodeDefs.size(); ++nodeIndex)
    {
        const FlatNodeDef& flatNodeDef = flatNodeDefs[nodeIndex];

        const auto result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                const std::optional<CameraDef>& optCamera = nodeDef->Camera;

                if(optCamera)
                {
                    const CameraResource cameraRsrc //
                        {
                            .NodeIndex = static_cast<IndexType>(nodeIndex),
                        };

                    cameras.push_back(cameraRsrc);
                }

                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    return cameras;
}

PointLightResource
MakeLightResource(const PointLightDef& pointLightDef, const size_t nodeIndex)
{
    return PointLightResource //
        {
            .NodeIndex = static_cast<IndexType>(nodeIndex),
            .Position = pointLightDef.Position,
            .Color = pointLightDef.Color,
        };
}

DirectionalLightResource
MakeLightResource(const DirectionalLightDef& directionalLightDef, const size_t nodeIndex)
{
    return DirectionalLightResource //
        {
            .NodeIndex = static_cast<IndexType>(nodeIndex),
            .Direction = directionalLightDef.Direction,
            .Color = directionalLightDef.Color,
        };
}

template<typename DefType, typename ResourceType>
Result<std::vector<ResourceType>>
CollectLights(const std::span<const FlatNodeDef> flatNodeDefs)
{
    std::vector<ResourceType> lights;

    static const size_t kMaxLightCount = std::min( //
        {
            static_cast<size_t>(kMaxVectorSize<ResourceType>),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    size_t count = 0;
    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const bool hasLight = std::visit(
            [&](const auto* nodeDef)
            {
                return static_cast<bool>(nodeDef->Light)
                    && std::holds_alternative<DefType>(*nodeDef->Light);
            },
            flatNodeDef.NodeDefPtr);

        if(hasLight)
        {
            MLG_CHECKV(!safeint::AddWillOverflow(count, 1uz, kMaxLightCount),
                "Light count overflow");
            ++count;
        }
    }

    lights.reserve(count);

    for(size_t nodeIndex = 0; nodeIndex < flatNodeDefs.size(); ++nodeIndex)
    {
        const FlatNodeDef& flatNodeDef = flatNodeDefs[nodeIndex];

        const auto result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                const std::optional<LightDef>& optLight = nodeDef->Light;

                if(optLight && std::holds_alternative<DefType>(*optLight))
                {
                    const DefType& lightDef = std::get<DefType>(*optLight);

                    lights.push_back(MakeLightResource(lightDef, nodeIndex));
                }

                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    return lights;
}

Result<std::vector<SceneNodeResource>>
CollectSceneNodes(const std::span<const FlatNodeDef> flatNodeDefs,
    const StringLookup& stringIndexMap)
{
    std::vector<SceneNodeResource> sceneNodes;

    static const size_t kMaxSceneNodeCount = std::min( //
        {
            static_cast<size_t>(kMaxVectorSize<SceneNodeResource>),
            static_cast<size_t>(std::numeric_limits<uint32_t>::max()),
        });

    MLG_CHECKV(flatNodeDefs.size() <= kMaxSceneNodeCount, "Scene node count out of range");

    sceneNodes.reserve(flatNodeDefs.size());

    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const auto result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                auto it = stringIndexMap.find(nodeDef->Name);
                MLG_CHECKV(it != stringIndexMap.end(), "Node name not found: {}", nodeDef->Name);

                const auto& stringRsrc = it->second;
                const size_t childCount = nodeDef->Children.size();

                SceneNodeResource sceneNode //
                    {
                        .Name = stringRsrc,
                        .ParentIndex = flatNodeDef.ParentIndex,
                        .FirstChildIndex = flatNodeDef.FirstChildIndex,
                        .ChildCount = static_cast<IndexType>(childCount),
                        .LocalPos = nodeDef->Transform.T,
                        .LocalRot = nodeDef->Transform.R.ToVector(),
                    };

                if constexpr(std::is_same_v<std::decay_t<decltype(*nodeDef)>, ChildNodeDef>)
                {
                    sceneNode.LocalScale = nodeDef->Transform.S;
                }
                else
                {
                    sceneNode.LocalScale = Vec3f{ 1, 1, 1 };
                }

                // At this point it's been confirmed that flatNodeDefs does not
                // exceed the max cout so no bounds checking needed.
                sceneNodes.push_back(sceneNode);

                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    return sceneNodes;
}

/// Retrieves the header from the buffer. Returns nullptr if the buffer is too small.
const Header*
GetHeader(const std::span<const std::byte>& buffer)
{
    if(!MLG_VERIFY(buffer.size() >= sizeof(Header)))
    {
        return nullptr;
    }

    const void* p = buffer.data();
    return static_cast<const Header*>(p);
}

/// Converts a value to a span of bytes.
template<typename T>
std::span<const std::byte>
AsBytes(const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    const void* p = &value;
    return std::span(static_cast<const std::byte*>(p), sizeof(T));
}

/// Converts a span of values to a span of bytes.
template<typename T>
std::span<const std::byte>
AsBytes(const std::span<const T> span)
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    return std::as_bytes(span);
}

template<typename T>
Result<>
AppendSpan(const std::span<const T> span, std::vector<std::byte>& buffer)
{
    const Header* header = GetHeader(buffer);
    MLG_CHECKV(header != nullptr, "Header is not initialized");

    const std::span bytes = AsBytes(span);

    MLG_CHECKV(!safeint::AddWillOverflow(buffer.size(),
                   bytes.size(),
                   static_cast<size_t>(header->TotalSize)),
        "Appending span exceeds bundle capacity");

    buffer.insert(buffer.end(), bytes.begin(), bytes.end());

    return Result<>::Ok;
}

uint32_t
GetChecksum(const std::span<const std::byte>& buffer)
{
    const Header* header = GetHeader(buffer);
    if(!MLG_VERIFY(header != nullptr, "Header is not initialized"))
    {
        return 0;
    }

    static_assert(offsetof(Header, Checksum) == 0, "Checksum must be at offset 0");

    constexpr size_t crcStartOffset = offsetof(Header, Checksum) + sizeof(Header::Checksum);

    const void* crcStart = &buffer[crcStartOffset];
    const size_t crcLen = buffer.size() - crcStartOffset;
    return SDL_crc32(0, crcStart, crcLen);
}

} // namespace

// ResourceBundle

Result<std::unique_ptr<ResourceBundle::LoadTask>>
ResourceBundle::Load(const FilePath& filePath, FileFetcher& fileFetcher)
{
    std::unique_ptr<LoadTask> loadTask(new LoadTask(filePath, fileFetcher));

    MLG_CHECK(loadTask->Start(), "Failed to start load task");

    return loadTask;
}

ResourceBundle::ResourceBundle(std::vector<std::byte>&& buffer)
    : m_Buffer(std::move(buffer))
{
    MLG_ASSERT(m_Buffer.size() >= sizeof(Header));

    MLG_ASSERT(GetHeader()->TotalSize <= m_Buffer.size());
}

std::span<const std::byte>
ResourceBundle::GetBuffer() const
{
    MLG_ASSERT(!m_Buffer.empty(), "Buffer is empty");

    return std::span<const std::byte>(m_Buffer);
}

namespace
{
/// Provides compile-time information about the offset, length, and name of a header field for a
/// resource type T.
/// Used in the template Append() method for appending resource data to the bundle.
template<typename T>
struct HeaderFields
{
    /// Pointer to the offset field in the header for this resource type.
    static constexpr OffsetType Header::* Offset = nullptr;
    /// Pointer to the length field in the header for this resource type.
    static constexpr IndexType Header::* Length = nullptr;
    /// Name of resource type T. Used for logging.
    static constexpr const char* Name = "UNKNOWN";
};
} // namespace

template<typename RsrcType>
std::span<const RsrcType>
ResourceBundle::GetResources() const
{
    static_assert(HeaderFields<RsrcType>::Offset != nullptr, "Header offset is not initialized");
    static_assert(HeaderFields<RsrcType>::Length != nullptr, "Header length is not initialized");

    const Header* header = GetHeader();
    return MLG_VERIFY(header, "Header is not initialized")
        ? GetSpan<RsrcType>(header->*HeaderFields<RsrcType>::Offset,
              header->*HeaderFields<RsrcType>::Length)
        : std::span<const RsrcType>();
}

/// Defines the header fields for a specific resource type T.
#define HEADER_FIELDS(T, offset, length, name)                                                     \
    template<>                                                                                     \
    struct HeaderFields<T>                                                                         \
    {                                                                                              \
        static constexpr OffsetType Header::* Offset = &Header::offset;                            \
        static constexpr IndexType Header::* Length = &Header::length;                             \
        MLG_ASSERT_ONLY(static constexpr const char* Name = name;)                                 \
    };

#define INSTANTIATE_GET_RESOURCES(T, offset, length, name)                                         \
    HEADER_FIELDS(T, offset, length, name)                                                         \
    template std::span<const T> ResourceBundle::GetResources<T>() const;

INSTANTIATE_GET_RESOURCES(char, CharsOffset, CharsLength, "Chars")
INSTANTIATE_GET_RESOURCES(StringResource, StringsOffset, StringCount, "Strings")
INSTANTIATE_GET_RESOURCES(TextureResource, TexturesOffset, TextureCount, "Textures")
INSTANTIATE_GET_RESOURCES(MaterialResource, MaterialsOffset, MaterialCount, "Materials")
INSTANTIATE_GET_RESOURCES(Vertex, VerticesOffset, VertexCount, "Vertices")
INSTANTIATE_GET_RESOURCES(VertexIndex, IndicesOffset, IndexCount, "Indices")
INSTANTIATE_GET_RESOURCES(MeshResource, MeshesOffset, MeshCount, "Meshes")
INSTANTIATE_GET_RESOURCES(ModelResource, ModelsOffset, ModelCount, "Models")
INSTANTIATE_GET_RESOURCES(
    ModelInstanceResource, ModelInstancesOffset, ModelInstanceCount, "Model Instances")
INSTANTIATE_GET_RESOURCES(ColliderResource, CollidersOffset, ColliderCount, "Colliders")
INSTANTIATE_GET_RESOURCES(RigidBodyResource, RigidBodiesOffset, RigidBodyCount, "Rigid Bodies")
INSTANTIATE_GET_RESOURCES(CameraResource, CamerasOffset, CameraCount, "Cameras")
INSTANTIATE_GET_RESOURCES(PointLightResource, PointLightsOffset, PointLightCount, "Point Lights")
INSTANTIATE_GET_RESOURCES(
    DirectionalLightResource, DirectionalLightsOffset, DirectionalLightCount, "Directional Lights")
INSTANTIATE_GET_RESOURCES(SceneNodeResource, NodesOffset, NodeCount, "Scene Nodes")

#undef INSTANTIATE_GET_RESOURCES
#undef HEADER_FIELDS

std::span<const MeshResource>
ResourceBundle::GetMeshes(const ModelResource& modelRsrc) const
{
    const std::span meshes = GetResources<MeshResource>();

    if(MLG_VERIFY(modelRsrc.FirstMeshIndex < meshes.size())
        && MLG_VERIFY(meshes.size() - modelRsrc.FirstMeshIndex >= modelRsrc.MeshCount))
    {
        return meshes.subspan(modelRsrc.FirstMeshIndex, modelRsrc.MeshCount);
    }

    return std::span<const MeshResource>();
}

std::span<const ColliderResource>
ResourceBundle::GetColliders(const RigidBodyResource& rigidBodyRsrc) const
{
    const std::span colliders = GetResources<ColliderResource>();

    if(MLG_VERIFY(rigidBodyRsrc.FirstColliderIndex < colliders.size())
        && MLG_VERIFY(
            colliders.size() - rigidBodyRsrc.FirstColliderIndex >= rigidBodyRsrc.ColliderCount))
    {
        return colliders.subspan(rigidBodyRsrc.FirstColliderIndex, rigidBodyRsrc.ColliderCount);
    }

    return std::span<const ColliderResource>();
}

std::string_view
ResourceBundle::GetStringView(const StringResource& stringResource) const
{
    const std::span<const char> chars = GetResources<char>();
    if(!MLG_VERIFY((stringResource.CharIndex < chars.size())
               && (chars.size() - stringResource.CharIndex >= stringResource.Length),
           "Invalid string resource"))
    {
        return std::string_view();
    }

    return std::string_view(chars.subspan(stringResource.CharIndex, stringResource.Length));
}

bool
ResourceBundle::Validate(const std::span<const std::byte>& buffer)
{
    const Header* header = ::GetHeader(buffer);

    if(!MLG_VERIFY(header != nullptr, "Header is not initialized"))
    {
        return false;
    }

    const uint32_t checksum = ::GetChecksum(buffer);

    if(!MLG_VERIFY(checksum == header->Checksum, "Invalid resource bundle checksum"))
    {
        return false;
    }

    if(!MLG_VERIFY(header->Magic0 == kMagic0, "Unexpected resource bundle magic 0"))
    {
        return false;
    }

    if(!MLG_VERIFY(header->Magic1 == kMagic1, "Unexpected resource bundle magic 1"))
    {
        return false;
    }

    if(!MLG_VERIFY(header->Version == kVersion, "Unexpected resource bundle version"))
    {
        return false;
    }

    return true;
}

// private:

const ResourceBundle::Header*
ResourceBundle::GetHeader() const
{
    if(!MLG_VERIFY(m_Buffer.size() >= sizeof(Header), "Buffer is empty"))
    {
        return nullptr;
    }
    const void* p = m_Buffer.data();
    return static_cast<const Header*>(p);
}

// ResourceBundleBuilder

// private:

template<typename RsrcType>
Result<>
ResourceBundleBuilder::AppendResources(const std::span<const RsrcType> items)
{
    static_assert(HeaderFields<RsrcType>::Offset != nullptr, "Header offset is not initialized");
    static_assert(HeaderFields<RsrcType>::Length != nullptr, "Header length is not initialized");

    Header* h = GetHeader();

    MLG_CHECKV(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->*HeaderFields<RsrcType>::Offset == kInvalidOffset,
        "{} already appended",
        HeaderFields<RsrcType>::Name);

    AppendPad<RsrcType>(m_Buffer);
    h->*HeaderFields<RsrcType>::Offset = static_cast<OffsetType>(m_Buffer.size());
    h->*HeaderFields<RsrcType>::Length = static_cast<IndexType>(items.size());
    return AppendSpan(items, m_Buffer);
}

// public:

Result<ResourceBundle>
ResourceBundleBuilder::Build(const SceneDef& sceneDef)
{
    return Build(sceneDef, Options{});
}

Result<ResourceBundle>
ResourceBundleBuilder::Build(const SceneDef& sceneDef, const Options& options)
{
    // Free old buffer mem
    std::vector<std::byte>().swap(m_Buffer);

    const auto flatNodeDefs = FlattenNodesBreadthFirst(sceneDef.NodeDefs);
    MLG_CHECK(flatNodeDefs);

    const auto meshDefs = CollectMeshDefs(sceneDef.ModelDefs);
    MLG_CHECK(meshDefs);

    std::vector<char> chars;

    const auto stringIndexMap = CollectStrings(*flatNodeDefs, *meshDefs, chars);
    MLG_CHECK(stringIndexMap);
    const auto textureIndexMap = CollectTextures(*meshDefs, *stringIndexMap, options);
    MLG_CHECK(textureIndexMap);
    const auto materialIndexMap = CollectMaterials(*meshDefs, *textureIndexMap);
    MLG_CHECK(materialIndexMap);
    const auto vertices = CollectVertices(*meshDefs);
    MLG_CHECK(vertices);
    const auto indices = CollectIndices(*meshDefs);
    MLG_CHECK(indices);
    const auto meshes = CollectMeshes(*meshDefs, *materialIndexMap);
    MLG_CHECK(meshes);
    const auto modelIndexMap = CollectModels(sceneDef.ModelDefs, *meshes);
    MLG_CHECK(modelIndexMap);
    const auto modelInstances = CollectModelInstances(*flatNodeDefs, *modelIndexMap);
    MLG_CHECK(modelInstances);
    const auto colliders = CollectColliders(sceneDef.NodeDefs);
    MLG_CHECK(colliders);
    const auto rigidBodies = CollectRigidBodies(sceneDef.NodeDefs);
    MLG_CHECK(rigidBodies);
    const auto cameras = CollectCameras(*flatNodeDefs);
    MLG_CHECK(cameras);
    const auto pointLights = CollectLights<PointLightDef, PointLightResource>(*flatNodeDefs);
    MLG_CHECK(pointLights);
    const auto directionalLights =
        CollectLights<DirectionalLightDef, DirectionalLightResource>(*flatNodeDefs);
    MLG_CHECK(directionalLights);
    const auto nodes = CollectSceneNodes(*flatNodeDefs, *stringIndexMap);
    MLG_CHECK(nodes);

    const std::vector<StringResource> strings = MapToVector(*stringIndexMap);
    const std::vector<TextureResource> textures = MapToVector(*textureIndexMap);
    const std::vector<MaterialResource> materials = MapToVector(*materialIndexMap);
    const std::vector<ModelResource> models = MapToVector(*modelIndexMap);

    size_t totalSize = sizeof(Header);

    // Lambda to add the size of a collection to the total size and check for overflow
    auto addAndCheckOverflow = [&](const auto& container) -> Result<>
    {
        using Container = std::decay_t<decltype(container)>;
        using ValueType = Container::value_type;

        const size_t pad = Pad<ValueType>(totalSize);
        const size_t remaining = kMaxBundleSize - totalSize;

        MLG_CHECK(pad <= remaining, "Padding size exceeds maximum bundle size");
        MLG_CHECKV((remaining - pad) / sizeof(ValueType) >= container.size(),
            "Collection size would overflow maximum allowable size");

        const size_t byteSizeOfCollection = (container.size() * sizeof(ValueType)) + pad;

        totalSize += byteSizeOfCollection;

        return Result<>::Ok;
    };

    const std::tuple<std::span<const char>,
        std::span<const StringResource>,
        std::span<const TextureResource>,
        std::span<const MaterialResource>,
        std::span<const Vertex>,
        std::span<const VertexIndex>,
        std::span<const MeshResource>,
        std::span<const ModelResource>,
        std::span<const ModelInstanceResource>,
        std::span<const ColliderResource>,
        std::span<const RigidBodyResource>,
        std::span<const CameraResource>,
        std::span<const PointLightResource>,
        std::span<const DirectionalLightResource>,
        std::span<const SceneNodeResource>>
        collections(chars,
            strings,
            textures,
            materials,
            *vertices,
            *indices,
            *meshes,
            models,
            *modelInstances,
            *colliders,
            *rigidBodies,
            *cameras,
            *pointLights,
            *directionalLights,
            *nodes);

    // Calculate total size and check for overflow
    MLG_CHECK(std::apply([&](const auto&... collection)
        { return (addAndCheckOverflow(collection) && ...); },
        collections));

    m_Buffer.reserve(totalSize);

    AppendHeader(static_cast<OffsetType>(totalSize));

    // Append each collection of resources to the buffer
    MLG_CHECK(
        std::apply([&](const auto&... collection) { return (AppendResources(collection) && ...); },
            collections));

    GetHeader()->Checksum = GetChecksum(m_Buffer);

    MLG_CHECK(m_Buffer.size() == totalSize,
        "Buffer size does not match total size: {} != {}",
        m_Buffer.size(),
        totalSize);

    return ResourceBundle(std::move(m_Buffer));
}

// private:

void
ResourceBundleBuilder::AppendHeader(const OffsetType totalSize)
{
    MLG_ASSERT(m_Buffer.empty(), "Header already appended");

    Header header{};

    header.TotalSize = totalSize;

    m_Buffer.append_range(AsBytes(header));
}

/// ResourceBundle::LoadTask

ResourceBundle::LoadTask::LoadTask(const FilePath& filePath, FileFetcher& fileFetcher)
    : m_FilePath(filePath),
      m_FileFetcher(&fileFetcher)
{
}

Result<ResourceBundle>
ResourceBundle::LoadTask::Take()
{
    MLG_CHECK(!IsRunning(), "Task is still running");
    MLG_CHECKV(m_Result, "Result is not available");

    Result<ResourceBundle> result = std::move(*m_Result);
    m_Result = {};

    return result;
}

Result<>
ResourceBundle::LoadTask::OnStart()
{
    auto fetchRequestId = m_FileFetcher->Fetch(m_FilePath);
    MLG_CHECK(fetchRequestId, "Failed to fetch file: {}", m_FilePath);

    m_FetchRequestId = *fetchRequestId;

    return Result<>::Ok;
}

void
ResourceBundle::LoadTask::OnUpdate()
{
    if(!m_FileFetcher->IsPending(m_FetchRequestId))
    {
        MLG_DEFER
        {
            SetComplete();
        };

        auto fetchedData = m_FileFetcher->Take(m_FetchRequestId);
        if(!fetchedData)
        {
            MLG_ERROR("Failed to take fetched data");
            return;
        }

        if(!Validate(*fetchedData))
        {
            MLG_ERROR("Resource bundle validation failed");
            return;
        }

        m_Result = ResourceBundle(std::move(*fetchedData));
    }
}