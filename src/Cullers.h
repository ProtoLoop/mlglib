#pragma once

#include "Frustum.h"

#include <cstddef>
#include <iterator>
#include <span>
#include <optional>

class ModelNode;
class Mesh;

/// Culls model nodes against a frustum.
/// Lazily iterates over models whose bounding volumes lie within or intersect the frustum.
/// Usage:
/// \code
/// ModelCuller culler(modelNodes, frustum);
/// for (const auto& culledModel : culler)
/// {
///     // Process culled model
/// }
/// \endcode
///
/// The container used to produce the span of ModelNodes must outlive the ModelCuller instance.
/// Mutating the the source container while iterating may lead to undefined behavior.
/// Incrementing the iterator will invalidate the current reference.
class ModelCuller
{
public:
    struct CulledModel
    {
        const ModelNode* Model;
        Frustum::ContainsResult ContainsResult;
    };

    ModelCuller() = delete;

    ModelCuller(const std::span<const ModelNode> modelNodes, Frustum frustum);

    class iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = CulledModel;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;

        iterator() = default;

        reference operator*() const;

        iterator& operator++();

        iterator operator++(int);

        friend bool operator==(const iterator& lhs, const iterator& rhs)
        {
            return (lhs.IsEnd() && rhs.IsEnd()) || (lhs.m_It == rhs.m_It);
        }

        friend bool operator!=(const iterator& lhs, const iterator& rhs)
        {
            return !(lhs == rhs);
        }

    private:
        friend class ModelCuller;

        explicit iterator(const ModelCuller& culler);

        /// Overload used for creating the end iterator.
        iterator(const ModelCuller& culler, const int);

        bool IsEnd() const { return m_It == m_End; }

        void Evaluate();

        using value_iterator = std::span<const ModelNode>::iterator;

        const ModelCuller* m_Culler{ nullptr };
        value_iterator m_It;
        value_iterator m_End;
        std::optional<value_type> m_CurrentValue;
    };

    iterator begin() const { return iterator(*this); }

    iterator end() const { return iterator(*this, 0); }

private:
    std::span<const ModelNode> m_ModelNodes;
    Frustum m_Frustum;
};

/// Culls meshes against a frustum.
/// Lazily iterates over meshes whose bounding volumes lie within or intersect the frustum.
/// The frustum against which the meshes are culled must be the same as that used for culling the parent model.
/// Incrementing the iterator will invalidate the current reference.
/// Usage:
/// \code
/// ModelCuller culler(modelNodes, frustum);
/// for (const auto& culledModel : culler)
/// {
///     MeshCuller culler(culledModel, frustum);
///     for (const auto& culledMesh : culler)
///     {
///         // Process culled mesh
///     }
/// }
/// \endcode
class MeshCuller
{
public:
    struct CulledMesh
    {
        const Mesh* Mesh;
        /// World space position of the mesh's bounding volume center.
        Vec3f WorldSpacePos;
        Frustum::ContainsResult ContainsResult;
        /// Index of the mesh instance within the scene's mesh instance collection.
        uint32_t InstanceIndex;
    };

    MeshCuller() = delete;

    MeshCuller(const ModelCuller::CulledModel& culledModel, Frustum frustum);

    class iterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = CulledMesh;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;

        iterator() = default;

        reference operator*() const;

        iterator& operator++();

        iterator operator++(int);

        friend bool operator==(const iterator& lhs, const iterator& rhs)
        {
            return (lhs.IsEnd() && rhs.IsEnd()) || (lhs.m_It == rhs.m_It);
        }

        friend bool operator!=(const iterator& lhs, const iterator& rhs)
        {
            return !(lhs == rhs);
        }

    private:
        friend class MeshCuller;

        explicit iterator(const MeshCuller& culler);

        /// Overload used for creating the end iterator.
        iterator(const MeshCuller& culler, const int);

        bool IsEnd() const { return m_It == m_End; }

        void Evaluate();

        using value_iterator = std::span<const Mesh>::iterator;

        const MeshCuller* m_Culler{ nullptr };
        value_iterator m_It;
        value_iterator m_End;
        Mat44f m_WsMatrix{ 1 };
        std::optional<value_type> m_CurrentValue;
        uint32_t m_InstanceIndex{ 0 };
    };

    iterator begin() const { return iterator(*this); }

    iterator end() const { return iterator(*this, 0); }

private:
    const ModelNode* m_ModelNode{ nullptr };
    Frustum m_Frustum;
    Frustum::ContainsResult m_ParentCullResult{ Frustum::ContainsResult::Outside };
};