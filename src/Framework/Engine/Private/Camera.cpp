#include "Camera.h"
#include "Transform.hpp"

namespace Engine
{

namespace
{

Core::Float4 Multiply(
    const Core::Mat4& m,
    const Core::Float4& v)
{
    Core::Float4 result;

    result.X =
        m(0,0) * v.X +
        m(0,1) * v.Y +
        m(0,2) * v.Z +
        m(0,3) * v.W;

    result.Y =
        m(1,0) * v.X +
        m(1,1) * v.Y +
        m(1,2) * v.Z +
        m(1,3) * v.W;

    result.Z =
        m(2,0) * v.X +
        m(2,1) * v.Y +
        m(2,2) * v.Z +
        m(2,3) * v.W;

    result.W =
        m(3,0) * v.X +
        m(3,1) * v.Y +
        m(3,2) * v.Z +
        m(3,3) * v.W;

    return result;
}

}

Camera::Camera()
    :
    m_Position(0.f, 0.f, 5.f),
    m_Target(0.f, 0.f, 0.f),
    m_Up(0.f, 1.f, 0.f),
    m_ProjectionType(
        ProjectionType::Perspective),
    m_FovY(60.f * 3.1415926f / 180.f),
    m_Aspect(16.f / 9.f),
    m_OrthoWidth(10.f),
    m_OrthoHeight(10.f),
    m_NearPlane(0.1f),
    m_FarPlane(1000.f),
    m_bViewDirty(true),
    m_bProjectionDirty(true),
    m_bViewProjectionDirty(true),
    m_DepthRangeMode(EDepthRangeMode::ZeroToOne)
{
}
void Camera::SetDepthRangeMode(EDepthRangeMode mode) {
    m_DepthRangeMode = mode;
}
Camera::EDepthRangeMode Camera::GetDepthRangeMode() const {
    return m_DepthRangeMode;
}
void Camera::SetPosition(
    const Core::Float3& position)
{
    m_Position = position;

    m_bViewDirty = true;
    m_bViewProjectionDirty = true;
}

void Camera::SetTarget(
    const Core::Float3& target)
{
    m_Target = target;

    m_bViewDirty = true;
    m_bViewProjectionDirty = true;
}

void Camera::SetUp(
    const Core::Float3& up)
{
    m_Up = up;

    m_bViewDirty = true;
    m_bViewProjectionDirty = true;
}

const Core::Float3&
Camera::GetPosition() const
{
    return m_Position;
}

const Core::Float3&
Camera::GetTarget() const
{
    return m_Target;
}

const Core::Float3&
Camera::GetUp() const
{
    return m_Up;
}

void Camera::SetPerspective(
    float fovYRadians,
    float aspect,
    float nearPlane,
    float farPlane)
{
    m_ProjectionType =
        ProjectionType::Perspective;

    m_FovY = fovYRadians;
    m_Aspect = aspect;
    m_NearPlane = nearPlane;
    m_FarPlane = farPlane;

    m_bProjectionDirty = true;
    m_bViewProjectionDirty = true;
}

void Camera::SetOrthographic(
    float width,
    float height,
    float nearPlane,
    float farPlane)
{
    m_ProjectionType =
        ProjectionType::Orthographic;

    m_OrthoWidth = width;
    m_OrthoHeight = height;
    m_NearPlane = nearPlane;
    m_FarPlane = farPlane;

    m_bProjectionDirty = true;
    m_bViewProjectionDirty = true;
}

ProjectionType
Camera::GetProjectionType() const
{
    return m_ProjectionType;
}

void Camera::UpdateViewMatrix() const
{
    if (!m_bViewDirty)
        return;

    m_ViewMatrix =
        Core::LookAtRH(
            m_Position,
            m_Target,
            m_Up);

    m_InverseViewMatrix =
        Core::Inverse(
            m_ViewMatrix);

    m_bViewDirty = false;
}

void Camera::UpdateProjectionMatrix() const
{
    if (!m_bProjectionDirty)
        return;

    if (m_ProjectionType ==
        ProjectionType::Perspective)
    {
        if (m_DepthRangeMode == EDepthRangeMode::ZeroToOne) {
            m_ProjectionMatrix = Core::PerspectiveRhZo(
                m_FovY,
                m_Aspect,
                m_NearPlane,
                m_FarPlane);
        }
        else {
            m_ProjectionMatrix = Core::PerspectiveRhNo(
                m_FovY,
                m_Aspect,
                m_NearPlane,
                m_FarPlane);
        }

    }
    else
    {
        float halfW =
            m_OrthoWidth * 0.5f;

        float halfH =
            m_OrthoHeight * 0.5f;
        if (m_DepthRangeMode == EDepthRangeMode::ZeroToOne) {
            m_ProjectionMatrix =
                Core::OrthoRhZo(
                    -halfW,
                    halfW,
                    -halfH,
                    halfH,
                    m_NearPlane,
                    m_FarPlane);
        }
        else {
            m_ProjectionMatrix = Core::OrthoRhNo(
                -halfW,
                halfW,
                -halfH,
                halfH,
                m_NearPlane,
                m_FarPlane);
        }
    }

    m_InverseProjectionMatrix =
        Core::Inverse(
            m_ProjectionMatrix);

    m_bProjectionDirty = false;
}

void Camera::UpdateViewProjectionMatrix() const
{
    if (!m_bViewProjectionDirty)
        return;

    UpdateViewMatrix();
    UpdateProjectionMatrix();

    m_ViewProjectionMatrix =
        m_ProjectionMatrix *
        m_ViewMatrix;

    m_InverseViewProjectionMatrix =
        Core::Inverse(
            m_ViewProjectionMatrix);

    m_bViewProjectionDirty = false;
}

const Core::Mat4&
Camera::GetViewMatrix() const
{
    UpdateViewMatrix();
    return m_ViewMatrix;
}

const Core::Mat4&
Camera::GetProjectionMatrix() const
{
    UpdateProjectionMatrix();
    return m_ProjectionMatrix;
}

const Core::Mat4&
Camera::GetViewProjectionMatrix() const
{
    UpdateViewProjectionMatrix();
    return m_ViewProjectionMatrix;
}

const Core::Mat4&
Camera::GetInverseViewMatrix() const
{
    UpdateViewMatrix();
    return m_InverseViewMatrix;
}

const Core::Mat4&
Camera::GetInverseProjectionMatrix() const
{
    UpdateProjectionMatrix();
    return m_InverseProjectionMatrix;
}

const Core::Mat4&
Camera::GetInverseViewProjectionMatrix() const
{
    UpdateViewProjectionMatrix();
    return m_InverseViewProjectionMatrix;
}

Core::Float4
Camera::WorldToClip(
    const Core::Float3& worldPos) const
{
    UpdateViewProjectionMatrix();

    return Multiply(
        m_ViewProjectionMatrix,
        Core::Float4(
            worldPos.X,
            worldPos.Y,
            worldPos.Z,
            1.f));
}

Core::Float3
Camera::WorldToNdc(
    const Core::Float3& worldPos) const
{
    Core::Float4 clip =
        WorldToClip(worldPos);

    if (std::abs(clip.W) < 1e-6f)
    {
        return Core::Float3();
    }

    return Core::Float3(
        clip.X / clip.W,
        clip.Y / clip.W,
        clip.Z / clip.W);
}
float Camera::GetNearPlane() const
{
    return m_NearPlane;
}
float Camera::GetFarPlane() const
{
    return m_FarPlane;
}
}