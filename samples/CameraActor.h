#pragma once

#include "Camera.h"

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

    const Camera& GetCamera() const { return m_Camera; }

    void SetTransform(const TrTransformf& transform);

    void SetAspectRatio(const float aspectRatio);

private:
    static constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

    static UnitQuatf ClampRot(const float delta, const Vec3f& axis)
    {
        float rot = std::max(delta, -kHalfPi);
        rot = std::min(rot, kHalfPi);
        return UnitQuatf(Radiansf(rot), axis);
    }

    Camera m_Camera //
        {
            PerspectiveProjection{ 1.0f },
            TrTransformf::Identity,
        };

    TrTransformf m_TargetTransform = TrTransformf::Identity;
};