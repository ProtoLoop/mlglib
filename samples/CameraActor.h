#pragma once

#include "VecMath.h"
#include "ProjectionSettings.h"

#include <numbers>

class InputMapper;

class CameraActor
{
public:
    static constexpr float kDefaultRotPerMouseMove = 0.0001f * 2 * std::numbers::pi_v<float>;
    static constexpr float kMouseWheelScale = 20.0f;
    static constexpr float kDefaultMovePerSec = 5.0f;

    CameraActor() = delete;

    explicit CameraActor(InputMapper& inputMapper);

    void Update(const InputMapper& inputMapper, const float deltaSeconds);

    void SetTransform(const TrTransformf& transform);

    const TrTransformf& GetTransform() const { return m_CurrentTransform; }

    const ProjectionSettings& GetProjectionSettings() const { return m_ProjectionSettings; }

private:
    static constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

    static UnitQuatf ClampRot(const float delta, const Vec3f& axis)
    {
        float rot = std::max(delta, -kHalfPi);
        rot = std::min(rot, kHalfPi);
        return UnitQuatf(Radiansf(rot), axis);
    }

    TrTransformf m_CurrentTransform = TrTransformf::Identity;
    TrTransformf m_TargetTransform = TrTransformf::Identity;
    ProjectionSettings m_ProjectionSettings;
};