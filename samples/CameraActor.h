#pragma once

#include "VecMath.h"
#include "ProjectionSettings.h"

#include <numbers>

class InputMapper;
class CameraNode;

class CameraActor
{
public:
    static constexpr float kDefaultRotPerMouseMove = 0.0001f * 2 * std::numbers::pi_v<float>;
    static constexpr float kMouseWheelScale = 20.0f;
    static constexpr float kDefaultMovePerSec = 5.0f;

    CameraActor() = delete;

    explicit CameraActor(InputMapper& inputMapper, CameraNode& cameraNode);

    void Update(const InputMapper& inputMapper, const float deltaSeconds);

    void SetLocation(const Vec3f& location);

    void SetRotation(const UnitQuatf& rotation);

    const ProjectionSettings& GetProjectionSettings() const { return m_ProjectionSettings; }

private:
    static constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

    static UnitQuatf ClampRot(const float delta, const Vec3f& axis)
    {
        float rot = std::max(delta, -kHalfPi);
        rot = std::min(rot, kHalfPi);
        return UnitQuatf(Radiansf(rot), axis);
    }

    CameraNode* m_CameraNode{ nullptr };

    Vec3f m_TargetLocation{ 0, 0, 0 };
    UnitQuatf m_TargetRotation = UnitQuatf::Identity();    
    ProjectionSettings m_ProjectionSettings;
};