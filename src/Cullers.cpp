#include "Cullers.h"

#include "SceneTypes.h"

/// ModelCuller

ModelCuller::ModelCuller(const std::span<const ModelNode> modelNodes, Frustum frustum)
    : m_ModelNodes(modelNodes),
      m_Frustum(std::move(frustum))
{
}

ModelCuller::iterator::reference
ModelCuller::iterator::operator*() const
{
    MLG_ABORTIF(!m_CurrentValue.has_value(), "Current value is not set");

    return *m_CurrentValue;
}

ModelCuller::iterator&
ModelCuller::iterator::operator++()
{
    if(m_It != m_End)
    {
        ++m_It;
        Evaluate();
    }

    return *this;
}

ModelCuller::iterator
ModelCuller::iterator::operator++(int)
{
    iterator tmp = *this;
    ++(*this);
    return tmp;
}

// private:
ModelCuller::iterator::iterator(const ModelCuller& culler)
    : m_Culler(&culler),
      m_It(culler.m_ModelNodes.begin()),
      m_End(culler.m_ModelNodes.end())
{
    Evaluate();
}

ModelCuller::iterator::iterator(const ModelCuller& culler, const int)
    : m_Culler(&culler),
      m_It(culler.m_ModelNodes.end()),
      m_End(culler.m_ModelNodes.end())
{
}

void
ModelCuller::iterator::Evaluate()
{
    m_CurrentValue.reset();

    for(; m_It != m_End && !m_CurrentValue.has_value(); ++m_It)
    {
        const BoundingSphere wsBounds = m_It->GetWsMatrix() * m_It->GetLocalSpaceBoundingSphere();

        const Frustum::ContainsResult containsResult = m_Culler->m_Frustum.Contains(wsBounds);

        if(containsResult == Frustum::ContainsResult::Outside)
        {
            continue;
        }

        m_CurrentValue = CulledModel //
            {
                .Model = &(*m_It),
                .ContainsResult = containsResult,
            };

        return;
    }
}

/// MeshCuller

MeshCuller::MeshCuller(const ModelCuller::CulledModel& culledModel, Frustum frustum)
    : m_ModelNode(culledModel.Model),
      m_Frustum(std::move(frustum)),
      m_ParentCullResult(culledModel.ContainsResult)
{
    MLG_ASSERT(m_ParentCullResult != Frustum::ContainsResult::Outside,
        "Parent is outside the frustum");
}

MeshCuller::iterator::reference
MeshCuller::iterator::operator*() const
{
    MLG_ABORTIF(!m_CurrentValue.has_value(), "Current value is not set");

    return *m_CurrentValue;
}

MeshCuller::iterator&
MeshCuller::iterator::operator++()
{
    if(m_It != m_End)
    {
        ++m_It;
        ++m_InstanceIndex;
        Evaluate();
    }

    return *this;
}

MeshCuller::iterator
MeshCuller::iterator::operator++(int)
{
    iterator tmp = *this;
    ++(*this);
    return tmp;
}

MeshCuller::iterator::iterator(const MeshCuller& culler)
    : m_Culler(&culler),
      m_It(culler.m_ModelNode->GetMeshes().begin()),
      m_End(culler.m_ModelNode->GetMeshes().end()),
      m_WsMatrix(culler.m_ModelNode->GetWsMatrix()),
      m_InstanceIndex(culler.m_ModelNode->GetFirstMeshInstanceIndex())
{
    Evaluate();
}

MeshCuller::iterator::iterator(const MeshCuller& culler, const int)
    : m_Culler(&culler),
      m_It(culler.m_ModelNode->GetMeshes().end()),
      m_End(culler.m_ModelNode->GetMeshes().end())
{
}

void
MeshCuller::iterator::Evaluate()
{
    m_CurrentValue.reset();

    const bool parentInside = m_Culler->m_ParentCullResult == Frustum::ContainsResult::Inside;

    for(; m_It != m_End && !m_CurrentValue.has_value(); ++m_It, ++m_InstanceIndex)
    {
        const BoundingSphere wsBounds = m_WsMatrix * m_It->GetBoundingSphere();

        const Frustum::ContainsResult containsResult =
            parentInside ? Frustum::ContainsResult::Inside : m_Culler->m_Frustum.Contains(wsBounds);

        if(containsResult == Frustum::ContainsResult::Outside)
        {
            continue;
        }

        m_CurrentValue = CulledMesh //
            {
                .Mesh = &(*m_It),
                .WorldSpacePos = wsBounds.GetCenter(),
                .ContainsResult = containsResult,
                .InstanceIndex = m_InstanceIndex,
            };

        return;
    }
}