#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier)
#define NOMINMAX

#define MLG_LOGGER_NAME "GLTF"

#include "GltfLoader.h"

#include "Defer.h"
#include "Log.h"
#include "SceneTypes.h"
#include "Vertex.h"

#include <cgltf.h>
#include <unordered_map>
#include <vector>

namespace
{
struct CgltfPrimitiveAttributes
{
    const cgltf_accessor* SrcIndices{ nullptr };
    const cgltf_accessor* SrcPosition{ nullptr };
    const cgltf_accessor* SrcNormal{ nullptr };
    const cgltf_accessor* SrcTexcoord0{ nullptr };
    const cgltf_accessor* SrcTexcoord1{ nullptr };
};

struct CgltfPrimitiveData
{
    const cgltf_primitive* Primitive;
    CgltfPrimitiveAttributes Attributes;
};

struct CgltfMeshData
{
    const cgltf_mesh* Mesh;
    std::vector<CgltfPrimitiveData> Primitives;
};

constexpr RgbaColorf kDefaultColor{ "#FF00FFFF"_rgba };

/// Makes a name for a model or primitive.
/// If the name is empty, a name will be generated using the baseName and index.
SceneDefs::NameString
MakeName(const std::string_view name, const char* baseName, const size_t index)
{
    return !name.empty() ? SceneDefs::NameString(name)
                         : SceneDefs::NameString(std::format("{}_{}", baseName, index));
}

Result<CgltfPrimitiveAttributes>
CollectAttributes(const cgltf_primitive& primitive)
{
    MLG_CHECK(primitive.type == cgltf_primitive_type_triangles,
        "Only triangle primitives are supported.");

    MLG_CHECK(primitive.attributes_count > 0, "Primitive does not have any attributes. Ignoring.");

    MLG_CHECK(primitive.targets_count == 0, "Morph targets are not supported. Ignoring.");

    MLG_CHECK(!primitive.has_draco_mesh_compression,
        "Draco mesh compression is not supported. Ignoring.");

    CgltfPrimitiveAttributes attrs //
        {
            .SrcIndices = primitive.indices,
        };

    const std::span<const cgltf_attribute> attrsSpan(primitive.attributes,
        primitive.attributes_count);

    for(const cgltf_attribute& attribute : attrsSpan)
    {
        MLG_LOG_SCOPE("attr {}", attribute.name ? attribute.name : "<unnamed>");

        MLG_CHECK(!attribute.data->is_sparse,
            "Sparse attribute data is not supported. Primitive will be ignored");

        switch(attribute.type)
        {
            case cgltf_attribute_type_position:
                attrs.SrcPosition = attribute.data;
                break;
            case cgltf_attribute_type_normal:
                attrs.SrcNormal = attribute.data;
                break;
            case cgltf_attribute_type_texcoord:
                if(0 == attribute.index)
                {
                    attrs.SrcTexcoord0 = attribute.data;
                }
                else if(1 == attribute.index)
                {
                    attrs.SrcTexcoord1 = attribute.data;
                }
                else
                {
                    /*Log::Warn(
                        "Unsupported texcoord index {} will be ignored",
                        attribute.index);*/
                }
                break;
            default:
                /*Log::Warn(
                    "Unsupported attribute type {} will be ignored",
                    std::to_underlying(attribute.type));*/
                break;
        }
    }

    MLG_CHECK(attrs.SrcPosition && attrs.SrcPosition->count > 0,
        "Primitive does not have a POSITION attribute.  Ignoring.");

    const size_t posCount = attrs.SrcPosition ? attrs.SrcPosition->count : 0;
    const size_t normalCount = attrs.SrcNormal ? attrs.SrcNormal->count : posCount;
    const size_t texcoord0Count = attrs.SrcTexcoord0 ? attrs.SrcTexcoord0->count : posCount;
    const size_t texcoord1Count = attrs.SrcTexcoord1 ? attrs.SrcTexcoord1->count : posCount;

    MLG_CHECK(normalCount == posCount,
        "Normal count {} does not match position count {}",
        normalCount,
        posCount);
    MLG_CHECK(texcoord0Count == posCount,
        "Texcoord0 count {} does not match position count {}",
        texcoord0Count,
        posCount);
    MLG_CHECK(texcoord1Count == posCount,
        "Texcoord1 count {} does not match position count {}",
        texcoord1Count,
        posCount);

    return attrs;
}

Result<std::vector<CgltfMeshData>>
CollectMeshes(const cgltf_data* gltfData)
{
    std::vector<CgltfMeshData> gltfMeshes;
    gltfMeshes.reserve(gltfData->meshes_count);

    const std::span<const cgltf_mesh> meshesSpan(gltfData->meshes, gltfData->meshes_count);
    for(const cgltf_mesh& mesh : meshesSpan)
    {
        MLG_LOG_SCOPE("mesh {}", mesh.name ? mesh.name : "<unnamed>");

        CgltfMeshData meshData //
            {
                .Mesh = &mesh,
            };

        const std::span<const cgltf_primitive> primsSpan(mesh.primitives, mesh.primitives_count);
        meshData.Primitives.reserve(mesh.primitives_count);

        for(const cgltf_primitive& prim : primsSpan)
        {
            MLG_LOG_SCOPE("prim {}", &prim - primsSpan.data());

            if(!prim.material)
            {
                MLG_WARN("Primitive has no material.");
            }

            auto attrs = CollectAttributes(prim);

            if(!attrs)
            {
                continue;
            }

            const CgltfPrimitiveData primData //
                {
                    .Primitive = &prim,
                    .Attributes = *attrs,
                };

            meshData.Primitives.push_back(primData);
        }

        gltfMeshes.push_back(std::move(meshData));
    }

    return gltfMeshes;
}

Result<MaterialDef>
CreateMaterialDef(const cgltf_material* gltfMaterial)
{
    SceneDefs::RelativePathString baseTexturePath;
    RgbaColorf color = kDefaultColor;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float metalness = 0;
    float roughness = 0;

    if(!gltfMaterial)
    {
        MLG_WARN("Primitive has no material.");
    }
    else if(gltfMaterial->has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness& pbr = gltfMaterial->pbr_metallic_roughness;
        color = RgbaColorf //
            {
                pbr.base_color_factor[0],
                pbr.base_color_factor[1],
                pbr.base_color_factor[2],
                pbr.base_color_factor[3],
            };
        metalness = pbr.metallic_factor;
        roughness = pbr.roughness_factor;

        if(!pbr.base_color_texture.texture)
        {
            MLG_WARN("Primitive has no base color texture");
        }
        else if(!pbr.base_color_texture.texture->image)
        {
            MLG_WARN("Primitive has no base color texture image");
        }
        else if(!pbr.base_color_texture.texture->image->uri)
        {
            MLG_WARN("Primitive has no base color texture image URI");
        }
        else
        {
            baseTexturePath = pbr.base_color_texture.texture->image->uri;
        }
    }
    else
    {
        MLG_WARN("Primitive has no PBR metallic-roughness material");
    }

    switch(gltfMaterial ? gltfMaterial->alpha_mode : cgltf_alpha_mode_opaque)
    {
    case cgltf_alpha_mode_opaque:
        alphaMode = AlphaMode::Opaque;
        break;
    case cgltf_alpha_mode_mask:
        alphaMode = AlphaMode::Mask;
        break;
    case cgltf_alpha_mode_blend:
        alphaMode = AlphaMode::Blend;
        break;
    default:
        MLG_WARN("Unknown alpha mode");
        break;
    }

    MaterialDef materialDef //
        {
            .BaseTexturePath{ baseTexturePath },
            .Color = color,
            .AlphaMode = alphaMode,
            .Metalness = metalness,
            .Roughness = roughness,
        };

    return materialDef;
}

Result<std::vector<Vertex>>
CollectVertices(const CgltfPrimitiveAttributes& attrs)
{
    std::vector<Vertex> vertices;
    vertices.reserve(attrs.SrcPosition->count);

    // GLTF uses a rignt handed coordinate system.  +Y up, +Z forward, -X right.
    // We use left handed.  +Y up, +Z forward, +X right.
    for(cgltf_size i = 0; i < attrs.SrcPosition->count; ++i)
    {
        std::array<float, 3> xyz{};
        Vertex vertex;

        MLG_CHECK(cgltf_accessor_read_float(attrs.SrcPosition, i, xyz.data(), xyz.size()),
            "Failed to read POSITION attribute");

        // Convert from right handed to left handed.
        vertex.pos.x = -xyz[0];
        vertex.pos.y = xyz[1];
        vertex.pos.z = xyz[2];

        if(attrs.SrcNormal)
        {
            std::array<float, 3> normal{};
            MLG_CHECK(cgltf_accessor_read_float(attrs.SrcNormal, i, normal.data(), normal.size()),
                "Failed to read NORMAL attribute");

            // Convert from right handed to left handed.
            vertex.normal.x = -normal[0];
            vertex.normal.y = normal[1];
            vertex.normal.z = normal[2];
        }

        if(attrs.SrcTexcoord0)
        {
            std::array<float, 2> texcoord0{};
            MLG_CHECK(cgltf_accessor_read_float(attrs.SrcTexcoord0,
                          i,
                          texcoord0.data(),
                          texcoord0.size()),
                "Failed to read TEXCOORD_0 attribute");
            vertex.uvs[0].u = texcoord0[0];
            vertex.uvs[0].v = texcoord0[1];
        }
        else
        {
            vertex.uvs[0].u = 0.0f;
            vertex.uvs[0].v = 0.0f;
        }

        vertices.push_back(vertex);
    }

    return vertices;
}

Result<std::vector<VertexIndex>>
CollectIndices(const CgltfPrimitiveAttributes& attrs)
{
    std::vector<VertexIndex> indices;

    if(attrs.SrcIndices)
    {
        indices.resize(attrs.SrcIndices->count);

        const size_t count = cgltf_accessor_unpack_indices(attrs.SrcIndices,
            indices.data(),
            sizeof(indices[0]),
            attrs.SrcIndices->count);

        MLG_CHECK(count == attrs.SrcIndices->count, "Failed to unpack all indices for primitive");
    }
    else
    {
        // Non-indexed primitive.
        indices.resize(attrs.SrcPosition->count);

        for(size_t i = 0; i < attrs.SrcPosition->count; ++i)
        {
            indices[i] = static_cast<VertexIndex>(i);
        }
    }

    MLG_CHECKV(indices.size() % 3 == 0,
        "Index count must be a multiple of three for triangle primitives");

    // Change winding from CCW to CW.
    const size_t endIndex = indices.size();
    for(size_t idx = 0; idx < endIndex; idx += 3)
    {
        std::swap(indices[idx + 1], indices[idx + 2]);
    }

    return indices;
}

Result<>
GenerateNormals(std::span<Vertex> vertices, std::span<const VertexIndex> indices)
{
    for(auto& v : vertices)
    {
        v.normal = { 0.0f, 0.0f, 0.0f };
    }

    MLG_CHECKV(indices.size() % 3 == 0,
        "Index count must be a multiple of three for triangle primitives");

    for(size_t i = 0; i < indices.size(); i += 3)
    {
        MLG_LOG_SCOPE("tri {}", i / 3);

        Vertex& v0 = vertices[indices[i + 0]];
        Vertex& v1 = vertices[indices[i + 1]];
        Vertex& v2 = vertices[indices[i + 2]];

        const Vec3f normal0 = (v2.pos - v0.pos).Cross(v1.pos - v0.pos).Normalize();
        const Vec3f normal1 = (v0.pos - v1.pos).Cross(v2.pos - v1.pos).Normalize();
        const Vec3f normal2 = (v1.pos - v2.pos).Cross(v0.pos - v2.pos).Normalize();

        v0.normal += normal0;
        v1.normal += normal1;
        v2.normal += normal2;
    }

    for(auto& v : vertices)
    {
        v.normal = v.normal.Normalize();
    }

    return Result<>::Ok;
}

struct ModelCollection
{
    std::unordered_map<const cgltf_mesh*, size_t> MeshModelMap;
    std::vector<ModelDef> Models;

    const ModelDef* GetModel(const cgltf_mesh* mesh) const
    {
        auto it = MeshModelMap.find(mesh);
        if(MLG_VERIFY(it != MeshModelMap.end()))
        {
            return &Models[it->second];
        }
        return nullptr;
    }
};

Result<ModelCollection>
CollectModels(const std::span<CgltfMeshData> gltfMeshes)
{
    ModelCollection collection;
    collection.Models.reserve(gltfMeshes.size());

    for(size_t i = 0; i < gltfMeshes.size(); ++i)
    {
        const auto& gltfMesh = gltfMeshes[i];

        MLG_LOG_SCOPE("mesh {}/{}", gltfMesh.Mesh->name ? gltfMesh.Mesh->name : "<unnamed>", i);

        std::vector<MeshDef> meshDefs;
        meshDefs.reserve(gltfMesh.Primitives.size());

        for(size_t j = 0; j < gltfMesh.Primitives.size(); ++j)
        {
            MLG_LOG_SCOPE("prim {}", j);

            const auto& primData = gltfMesh.Primitives[j];

            auto vertices = CollectVertices(primData.Attributes);
            MLG_CHECK(vertices);

            auto indices = CollectIndices(primData.Attributes);
            MLG_CHECK(indices);

            if(!primData.Attributes.SrcNormal)
            {
                MLG_CHECK(GenerateNormals(*vertices, *indices));
            }

            const cgltf_primitive& prim = *primData.Primitive;

            auto mtlDef = CreateMaterialDef(prim.material);
            MLG_CHECK(mtlDef);

            MeshDef meshDef //
                {
                    .Vertices = std::move(*vertices),
                    .Indices = std::move(*indices),
                    .MaterialDef = std::move(*mtlDef),
                };

            meshDefs.push_back(std::move(meshDef));
        }

        const std::string_view modelName = gltfMesh.Mesh->name ? gltfMesh.Mesh->name : "";

        ModelDef model //
            {
                .Name = MakeName(modelName, "Model", i),
                .MeshDefs = std::move(meshDefs),
            };

        collection.MeshModelMap.emplace(gltfMesh.Mesh, collection.Models.size());
        collection.Models.push_back(std::move(model));
    }

    return collection;
}

void
ConvertRHtoLH(Mat44f& M)
{
    // negate X components of Y and Z basis vectors
    M.m[1].x = -M.m[1].x;
    M.m[2].x = -M.m[2].x;

    // negate Y,Z components of X basis vector
    M.m[0].y = -M.m[0].y;
    M.m[0].z = -M.m[0].z;

    // negate translation X
    M.m[3].x = -M.m[3].x;
}

template<typename T>
Result<>
CollectNode(
    const cgltf_node& srcNode, std::vector<T>& nodeDefs, const ModelCollection& modelCollection)
{
    const SceneDefs::NameString nodeName =
        MakeName(srcNode.name ? srcNode.name : "", "Node", nodeDefs.size());

    MLG_LOG_SCOPE("node {}", nodeName);

    TrsTransformf nodeTransform;

    if(srcNode.has_matrix)
    {
        Mat44f m(srcNode.matrix);
        ConvertRHtoLH(m);
        nodeTransform = TrsTransformf::FromMatrix(m);
    }
    else
    {
        nodeTransform.T = Vec3f(srcNode.translation);
        nodeTransform.R = UnitQuatf(srcNode.rotation);
        nodeTransform.S = Vec3f(srcNode.scale);

        // X axis flip - convert from right handed to left handed.
        nodeTransform.T.x = -nodeTransform.T.x;
        const UnitQuatf flipX{ 1, 0, 0, 0 }; // xyzw, 180° around X
        nodeTransform.R = flipX * nodeTransform.R * flipX.Conjugate();
    }

    const ModelDef* modelDef = nullptr;

    if(srcNode.mesh)
    {
        modelDef = modelCollection.GetModel(srcNode.mesh);
        MLG_CHECKV(modelDef, "Mesh not found in modelCollection");
    }

    const SceneDefs::NameString modelName = modelDef ? modelDef->Name : SceneDefs::NameString{};

    MLG_CHECKV(!modelDef || !modelName.empty(), "Model name must not be empty");

    std::vector<ChildNodeDef> childNodes;
    childNodes.reserve(srcNode.children_count);

    const std::span<const cgltf_node* const> childrenSpan(srcNode.children, srcNode.children_count);

    for(const cgltf_node* child : childrenSpan)
    {
        MLG_CHECK(CollectNode(*child, childNodes, modelCollection));
    }

    if(childNodes.empty() && !srcNode.mesh)
    {
        // Node has no mesh and no descendents with meshes, skip it
        // This could be a procedurally generated mesh, e.g. the leaves of a tree.
        // In blender the following steps could be used to convert to a mesh:
        // 1. Select the node in the outliner.
        // 2. In the "Layout" editor select the "Object" menu and choose "Apply -> Make
        // Instances Real".
        //      This will generate meshes and possibly mesh instances.
        // 3. In the outliner select all the resulting objects.
        // 4. In the "Layout" editor select the "Object" menu and choose "Convert -> Mesh".
        //      This will materialize instances into stand alone meshes.
        // 5. In the outliner select all the resulting objects (meshes).
        // 6. In the "Layout" editor select the "Object" menu and choose "Join".
        //      This will join all the selected meshes into a single mesh.

        const char* type = "unknown";
        if(srcNode.camera)
        {
            type = "camera";
        }
        else if(srcNode.light)
        {
            type = "light";
        }
        else if(srcNode.skin)
        {
            type = "skin";
        }
        else if(srcNode.weights)
        {
            type = "weights";
        }

        MLG_WARN("{} node has no mesh and no children.  Ignoring.", type);
    }
    else
    {
        using NodeDef = T;

        NodeDef newNodeDef //
            {
                .Name{ nodeName },
                .Transform{ nodeTransform },
                .Children{ std::move(childNodes) },
            };

        if(!modelName.empty())
        {
            newNodeDef.Model = ModelRef{ .Name{ modelName } };
        }

        nodeDefs.push_back(std::move(newNodeDef));
    }

    return Result<>::Ok;
}

Result<std::vector<RootNodeDef>>
CollectNodes(const cgltf_data* gltfData, const ModelCollection& modelCollection)
{
    const std::span<const cgltf_scene> scenesSpan(gltfData->scenes, gltfData->scenes_count);

    MLG_CHECK(!scenesSpan.empty(), "No scenes found");
    MLG_CHECK(scenesSpan.size() == 1, "Multiple scenes found, only one scene is supported");

    const cgltf_scene& scene = scenesSpan[0];

    std::vector<RootNodeDef> rootNodeDefs;
    rootNodeDefs.reserve(scene.nodes_count);

    const std::span<const cgltf_node* const> nodesSpan(scene.nodes, scene.nodes_count);

    for(const cgltf_node* node : nodesSpan)
    {
        MLG_CHECK(CollectNode(*node, rootNodeDefs, modelCollection));
    }

    return rootNodeDefs;
}

} // namespace

Result<SceneDef>
GltfLoader::Load(const std::string_view path)
{
    MLG_LOG_SCOPE(path);

    // Use this to create a null terminated string for cgltf_parse_file.
    const std::string filePathStr(path);

    const cgltf_options options = {};
    cgltf_data* gltfData = nullptr;
    const cgltf_result result = cgltf_parse_file(&options, filePathStr.c_str(), &gltfData);
    MLG_CHECK(result == cgltf_result_success, "Failed to load glTF file");

    auto cleanup = MLG_MAKE_DEFERRED
    {
        if(gltfData)
        {
            cgltf_free(gltfData);
        }
    };

    MLG_CHECK(gltfData->scenes_count > 0, "No scenes found");
    MLG_CHECK(gltfData->scenes_count == 1, "Multiple scenes found, only one scene is supported");

    const std::span<const cgltf_buffer_view> bufferViewsSpan(gltfData->buffer_views,
        gltfData->buffer_views_count);
    for(const cgltf_buffer_view& bufferView : bufferViewsSpan)
    {
        MLG_CHECK(!bufferView.has_meshopt_compression,
            "Unsupported meshopt compression in buffer view {}",
            bufferView.name ? bufferView.name : "<unnamed>");
    }

    const cgltf_result loadBuffersResult = cgltf_load_buffers(&options, gltfData, filePathStr.c_str());
    MLG_CHECK(loadBuffersResult == cgltf_result_success, "Failed to load buffers");

    auto gltfMeshes = CollectMeshes(gltfData);
    MLG_CHECK(gltfMeshes)

    auto modelCollection = CollectModels(*gltfMeshes);
    MLG_CHECK(modelCollection);

    auto rootNodeDefs = CollectNodes(gltfData, *modelCollection);
    MLG_CHECK(rootNodeDefs);

    SceneDef sceneDef //
        {
            .ModelDefs = std::move(modelCollection->Models),
            .NodeDefs = std::move(*rootNodeDefs),
        };
        
    return std::move(sceneDef);
}