#pragma once

#include "Math.hpp"
#include "EngineExport.h"
#include "SceneComponent.h"

namespace Engine
{

enum class ProjectionType
{
    Perspective,
    Orthographic
};

class ENGINE_API Camera : public SceneComponent
{
public:

    Camera();

public:
    enum EDepthRangeMode
    {
        ZeroToOne,
        NegativeOneToOne
    };

    void SetDepthRangeMode(EDepthRangeMode mode);

    EDepthRangeMode GetDepthRangeMode() const;
    // -------------------------------------------------
    // Transform
    // -------------------------------------------------

    void SetPosition(
        const Core::Float3& position);

    void SetTarget(
        const Core::Float3& target);

    void SetUp(
        const Core::Float3& up);

    const Core::Float3&
        GetPosition() const;

    const Core::Float3&
        GetTarget() const;

    const Core::Float3&
        GetUp() const;

    // -------------------------------------------------
    // Projection
    // -------------------------------------------------

    void SetPerspective(
        float fovYRadians,
        float aspect,
        float nearPlane,
        float farPlane);

    void SetOrthographic(
        float width,
        float height,
        float nearPlane,
        float farPlane);

    ProjectionType
        GetProjectionType() const;

    // -------------------------------------------------
    // Matrix
    // -------------------------------------------------

    const Core::Mat4&
        GetViewMatrix() const;

    const Core::Mat4&
        GetProjectionMatrix() const;

    const Core::Mat4&
        GetViewProjectionMatrix() const;

    const Core::Mat4&
        GetInverseViewMatrix() const;

    const Core::Mat4&
        GetInverseProjectionMatrix() const;

    const Core::Mat4&
        GetInverseViewProjectionMatrix() const;

    // -------------------------------------------------
    // Coordinate Conversion
    // -------------------------------------------------

    Core::Float4 WorldToClip(
        const Core::Float3& worldPos) const;

    Core::Float3 WorldToNdc(
        const Core::Float3& worldPos) const;

    float GetNearPlane() const;
    float GetFarPlane() const;

private:

    void UpdateViewMatrix() const;
    void UpdateProjectionMatrix() const;
    void UpdateViewProjectionMatrix() const;

private:

    // -------------------------------------------------
    // Transform
    // -------------------------------------------------

    Core::Float3 m_Position;
    Core::Float3 m_Target;
    Core::Float3 m_Up;

    // -------------------------------------------------
    // Projection
    // -------------------------------------------------

    ProjectionType m_ProjectionType;
    EDepthRangeMode m_DepthRangeMode;

    float m_FovY;
    float m_Aspect;
    float m_OrthoWidth;
    float m_OrthoHeight;
    float m_NearPlane;
    float m_FarPlane;

    // -------------------------------------------------
    // Cache
    // -------------------------------------------------

    mutable Core::Mat4 m_ViewMatrix;
    mutable Core::Mat4 m_ProjectionMatrix;
    mutable Core::Mat4 m_ViewProjectionMatrix;

    mutable Core::Mat4 m_InverseViewMatrix;
    mutable Core::Mat4 m_InverseProjectionMatrix;
    mutable Core::Mat4 m_InverseViewProjectionMatrix;

    // -------------------------------------------------
    // Dirty Flags
    // -------------------------------------------------

    mutable bool m_bViewDirty;
    mutable bool m_bProjectionDirty;
    mutable bool m_bViewProjectionDirty;
};

}