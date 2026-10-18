#include "CameraActor.h"

#include "CommonActionIds.h"

#include <SDL3/SDL_events.h>

namespace
{

constexpr ActionMapping actionMappings[] //
    {
        {
            .ActionId = CommonActionIds::MoveForward,
            .Trigger = InputButton::KeyHeld(SDL_SCANCODE_W),
            .Scale = 1,
        },
        {
            .ActionId = CommonActionIds::MoveBackward,
            .Trigger = InputButton::KeyHeld(SDL_SCANCODE_S),
            .Scale = -1,
        },
        {
            .ActionId = CommonActionIds::MoveLeft,
            .Trigger = InputButton::KeyHeld(SDL_SCANCODE_A),
            .Scale = -1,
        },
        {
            .ActionId = CommonActionIds::MoveRight,
            .Trigger = InputButton::KeyHeld(SDL_SCANCODE_D),
            .Scale = 1,
        },
        {
            .ActionId = CommonActionIds::MoveUp,
            .Trigger = InputButton::KeyHeld(SDL_SCANCODE_UP),
            .Scale = 1,
        },
        {
            .ActionId = CommonActionIds::MoveDown,
            .Trigger = InputButton::KeyHeld(SDL_SCANCODE_DOWN),
            .Scale = -1,
        },
        {
            .ActionId = CommonActionIds::LookLeftRight,
            .Trigger = InputAxis::MouseMoveX(),
            .Scale = CameraActor::kDefaultRotPerMouseMove,
        },
        {
            .ActionId = CommonActionIds::LookUpDown,
            .Trigger = InputAxis::MouseMoveY(),
            .Scale = CameraActor::kDefaultRotPerMouseMove,
        },
        {
            .ActionId = CommonActionIds::MoveUpDown,
            .Trigger = InputAxis::MouseWheelY(),
            .Scale = CameraActor::kMouseWheelScale,
        },
    };
}

CameraActor::CameraActor(InputMapper& inputMapper)
{
    MLG_ABORTIF(!inputMapper.AddActionMappings(actionMappings), "Failed to add action mappings");
}

void
CameraActor::Update(const InputMapper& inputMapper, const float deltaSeconds)
{
    float actionValue = 0;

    UnitQuatf yaw = UnitQuatf::Identity, pitch = UnitQuatf::Identity;
    Vec3f moveDelta(0);

    if(inputMapper.IsActionTriggered(CommonActionIds::MoveForward, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, 0, actionValue);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveBackward, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, 0, actionValue);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveLeft, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(actionValue, 0, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveRight, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(actionValue, 0, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveUp, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, actionValue, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveDown, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, actionValue, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveUpDown, actionValue))
    {
        moveDelta += m_CurrentTransform.R * Vec3f(0, actionValue, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::LookLeftRight, actionValue))
    {
        yaw = ClampRot(actionValue, Vec3f::YAXIS());
        m_TargetTransform.R = yaw * m_TargetTransform.R;
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::LookUpDown, actionValue))
    {
        pitch = ClampRot(actionValue, Vec3f::XAXIS());
        m_TargetTransform.R = m_TargetTransform.R * pitch;
    }

    m_TargetTransform.T += moveDelta * kDefaultMovePerSec * deltaSeconds;

    constexpr float kTransformTimeToTarget = 0.1f;
    // constexpr float kRotationTimeToTarget = 0.01f;

    const float dtT = deltaSeconds / kTransformTimeToTarget;
    // const float dtR = deltaSeconds / kRotationTimeToTarget;

    m_CurrentTransform.T = m_CurrentTransform.T.Lerp(m_TargetTransform.T, dtT);
    // m_CurrentTransform.R = m_CurrentTransform.R.Lerp(m_TargetTransform.R, dtR);
    m_CurrentTransform.R = m_TargetTransform.R;
}
