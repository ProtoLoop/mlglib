#include "CameraActor.h"

#include "CommonActionIds.h"
#include "SceneTypes.h"

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

CameraActor::CameraActor(InputMapper& inputMapper, CameraNode& cameraNode)
    : m_CameraNode(&cameraNode)
{
    MLG_ABORTIF(!inputMapper.AddActionMappings(actionMappings), "Failed to add action mappings");
}

void
CameraActor::Update(const InputMapper& inputMapper, const float deltaSeconds)
{
    float actionValue = 0;

    UnitQuatf yaw = UnitQuatf::Identity(), pitch = UnitQuatf::Identity();
    Vec3f moveDelta(0);

    UnitQuatf curRot = m_CameraNode->GetRotation();
    Vec3f curPos = m_CameraNode->GetLocation();

    if(inputMapper.IsActionTriggered(CommonActionIds::MoveForward, actionValue))
    {
        moveDelta += curRot * Vec3f(0, 0, actionValue);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveBackward, actionValue))
    {
        moveDelta += curRot * Vec3f(0, 0, actionValue);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveLeft, actionValue))
    {
        moveDelta += curRot * Vec3f(actionValue, 0, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveRight, actionValue))
    {
        moveDelta += curRot * Vec3f(actionValue, 0, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveUp, actionValue))
    {
        moveDelta += curRot * Vec3f(0, actionValue, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveDown, actionValue))
    {
        moveDelta += curRot * Vec3f(0, actionValue, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::MoveUpDown, actionValue))
    {
        moveDelta += curRot * Vec3f(0, actionValue, 0);
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::LookLeftRight, actionValue))
    {
        yaw = ClampRot(actionValue, Vec3f::YAXIS());
        m_TargetRotation = yaw * m_TargetRotation;
    }
    if(inputMapper.IsActionTriggered(CommonActionIds::LookUpDown, actionValue))
    {
        pitch = ClampRot(actionValue, Vec3f::XAXIS());
        m_TargetRotation = m_TargetRotation * pitch;
    }

    m_TargetLocation += moveDelta * kDefaultMovePerSec * deltaSeconds;

    constexpr float kTransformTimeToTarget = 0.1f;
    // constexpr float kRotationTimeToTarget = 0.01f;

    const float dtT = deltaSeconds / kTransformTimeToTarget;
    // const float dtR = deltaSeconds / kRotationTimeToTarget;

    curPos = curPos.Lerp(m_TargetLocation, dtT);
    // m_CurrentTransform.R = m_CurrentTransform.R.Lerp(m_TargetTransform.R, dtR);
    curRot = m_TargetRotation;

    m_CameraNode->SetLocation(curPos);
    m_CameraNode->SetRotation(curRot);
}

void
CameraActor::SetLocation(const Vec3f& location)
{
    m_TargetLocation = location;
    m_CameraNode->SetLocation(location);
}

void
CameraActor::SetRotation(const UnitQuatf& rotation)
{
    m_TargetRotation = rotation;
    m_CameraNode->SetRotation(rotation);
}