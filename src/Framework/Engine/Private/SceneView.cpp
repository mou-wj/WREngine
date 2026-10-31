#include "SceneView.h"
#include "SceneInterface.h"
#include "SceneViewport.h"
#include "BVH.hpp"
#include <cassert>
#include <cstring>

namespace Engine {

    /*
    ===============================================================================
        SceneView 实现
    ===============================================================================
    */

    SceneView::SceneView()
        : NearClip(0.1f)
        , FarClip(1000.0f)
        , RenderTargetWidth(0.0f)
        , RenderTargetHeight(0.0f)
        , FrameIndex(0)
        , FrameTimeSeconds(0.0)
        , Flags(VF_None)
    {
        Viewport = { 0, 0, 0, 0 };
        // 初始化矩阵为单位阵
        ViewMatrix = Core::Float4x4::Identity();
        ProjectionMatrix = Core::Float4x4::Identity();
        ViewProjectionMatrix = Core::Float4x4::Identity();
        InvViewProjectionMatrix = Core::Float4x4::Identity();
    }

    SceneView::~SceneView() = default;

    bool SceneView::MakeRay(Core::Float2 position, Core::Ray& outRay, float& outMaxDistance) const
    {
        outRay = {}; outMaxDistance = 0;
        if (Viewport.Width<=0 || Viewport.Height<=0 || !std::isfinite(position.X) || !std::isfinite(position.Y) ||
            position.X<0 || position.Y<0 || position.X>=Viewport.Width || position.Y>=Viewport.Height) return false;
        const float x = 2*position.X/Viewport.Width-1, y = 1-2*position.Y/Viewport.Height;
        const auto nearPoint = InvViewProjectionMatrix*std::array<float,4>{x,y,bDepthRangeZeroToOne ? 0.0f : -1.0f,1};
        const auto farPoint = InvViewProjectionMatrix*std::array<float,4>{x,y,1,1};
        for (int i=0; i<4; ++i)
            if (!std::isfinite(nearPoint[i]) || !std::isfinite(farPoint[i])) return false;
        if (std::abs(nearPoint[3])<1e-7f || std::abs(farPoint[3])<1e-7f) return false;
        const Core::Float3 origin{nearPoint[0]/nearPoint[3],nearPoint[1]/nearPoint[3],nearPoint[2]/nearPoint[3]};
        const Core::Float3 delta{farPoint[0]/farPoint[3]-origin.X,farPoint[1]/farPoint[3]-origin.Y,farPoint[2]/farPoint[3]-origin.Z};
        const float length = delta.Length();
        if (!std::isfinite(origin.X) || !std::isfinite(origin.Y) || !std::isfinite(origin.Z) ||
            !std::isfinite(length) || length<=1e-6f) return false;
        outRay = {origin,delta/length}; outMaxDistance = length;
        return true;
    }

    void SceneView::RebuildDerivedMatrices()
    {
        // 计算 ViewProjectionMatrix = ProjectionMatrix * ViewMatrix
        // 针对你的行主序 (Row-Major) 矩阵模板实现标准乘法
        for (size_t r = 0; r < 4; ++r)
        {
            for (size_t c = 0; c < 4; ++c)
            {
                float sum = 0.0f;
                for (size_t k = 0; k < 4; ++k)
                {
                    // 寻址公式: row * Cols + col
                    sum += ProjectionMatrix(r, k) * ViewMatrix(k, c);
                }
                ViewProjectionMatrix(r, c) = sum;
            }
        }

        // 工业级框架预留：在此处计算 InvViewProjectionMatrix 用于延迟管线或遮挡剔除
        // 为了保持核心骨架精简，此处先省略高阶矩阵求逆算法
    }

    void SceneView::BuildFrustum()
    {
        // 从行主序的 View-Projection 矩阵中提取 6 个视锥体裁剪面 (Gribb-Hartmann 方法)
        // 寻址格式: M(row, col)
        const auto& m = ViewProjectionMatrix;

        // 左裁剪面 (Left Plane)
        CachedFrustum.Planes[0] = { m(3,0) + m(0,0), m(3,1) + m(0,1), m(3,2) + m(0,2), m(3,3) + m(0,3) };
        // 右裁剪面 (Right Plane)
        CachedFrustum.Planes[1] = { m(3,0) - m(0,0), m(3,1) - m(0,1), m(3,2) - m(0,2), m(3,3) - m(0,3) };
        // 下裁剪面 (Bottom Plane)
        CachedFrustum.Planes[2] = { m(3,0) + m(1,0), m(3,1) + m(1,1), m(3,2) + m(1,2), m(3,3) + m(1,3) };
        // 上裁剪面 (Top Plane)
        CachedFrustum.Planes[3] = { m(3,0) - m(1,0), m(3,1) - m(1,1), m(3,2) - m(1,2), m(3,3) - m(1,3) };
        // 近裁剪面 (Near Plane)
        CachedFrustum.Planes[4] = { m(3,0) + m(2,0), m(3,1) + m(2,1), m(3,2) + m(2,2), m(3,3) + m(2,3) };
        // 远裁剪面 (Far Plane)
        CachedFrustum.Planes[5] = { m(3,0) - m(2,0), m(3,1) - m(2,1), m(3,2) - m(2,2), m(3,3) - m(2,3) };

        // 归一化所有裁剪面方程，确保距离测试准确
        for (auto& plane : CachedFrustum.Planes)
        {
            float length = std::sqrt(plane.A * plane.A + plane.B * plane.B + plane.C * plane.C);
            if (length > 0.0f)
            {
                plane.A /= length;
                plane.B /= length;
                plane.C /= length;
                plane.D /= length;
            }
        }
    }

    bool SceneView::IsBoxVisible(const Core::AABB& box) const
    {
        // 工业级标准：高效 Frustum-AABB 检测 (利用正负顶点测试，避免遍历 8 个顶点)
        // 传入的 Box 内含 Min 和 Max 两个三维向量
        for (const auto& plane : CachedFrustum.Planes)
        {
            // 寻找在当前平面法线方向上，AABB 距离平面最近的“最远点 (Positive Vertex)”
            float pvX = (plane.A > 0.0f) ? box.Max.X : box.Min.X;
            float pvY = (plane.B > 0.0f) ? box.Max.Y : box.Min.Y;
            float pvZ = (plane.C > 0.0f) ? box.Max.Z : box.Min.Z;

            // 如果连这个最容易留在平面内部的点都在平面外侧（即算出来的带符号距离 < 0），说明被彻底裁剪
            if (plane.A * pvX + plane.B * pvY + plane.C * pvZ + plane.D < 0.0f)
            {
                return false; // 隐藏，不可见
            }
        }
        return true; // 可见
    }

    void SceneView::PackViewUniforms(ViewUniforms& outUniforms) const
    {
        // 1. 复制矩阵
        outUniforms.ViewProj = ViewProjectionMatrix;
        outUniforms.InvViewProj = InvViewProjectionMatrix;

        // 2. 复制相机位置，并对齐打入 Float4 内存中
        outUniforms.CameraPos.X = CameraWorldPos.X;
        outUniforms.CameraPos.Y = CameraWorldPos.Y;
        outUniforms.CameraPos.Z = CameraWorldPos.Z;
        outUniforms.CameraPos.W = 1.0f; // 常规齐次坐标预留
    }
    static Core::Float3 UnprojectFromNDC(
        const Core::Float4x4& invVP,
        float x,
        float y,
        float z)
    {
        Core::Float4 p(x, y, z, 1.0f);

        Core::Float4 world = invVP * p.Data;

        if (std::abs(world.W) > 1e-6f)
        {
            world.X /= world.W;
            world.Y /= world.W;
            world.Z /= world.W;
        }

        return Core::Float3(world.X, world.Y, world.Z);
    }
    std::array<Core::Float3, 8> SceneView::GetFrustumCornersWS(float nearDepth, float farDepth) const {
        std::array<Core::Float3, 8> corners;

        //----------------------------------------
        // 1. Full frustum corners in world space
        //----------------------------------------
        std::array<Core::Float3, 8> fullCorners;
        float nearP = 0;
        if (!bDepthRangeZeroToOne) {
            nearP = -1.0f;
        }

        // Near plane (z=0)
        fullCorners[0] = UnprojectFromNDC(InvViewProjectionMatrix, -1.f, -1.f, nearP);
        fullCorners[1] = UnprojectFromNDC(InvViewProjectionMatrix, 1.f, -1.f, nearP);
        fullCorners[2] = UnprojectFromNDC(InvViewProjectionMatrix, 1.f, 1.f, nearP);
        fullCorners[3] = UnprojectFromNDC(InvViewProjectionMatrix, -1.f, 1.f, nearP);

        // Far plane (z=1)
        fullCorners[4] = UnprojectFromNDC(InvViewProjectionMatrix, -1.f, -1.f, 1.f);
        fullCorners[5] = UnprojectFromNDC(InvViewProjectionMatrix, 1.f, -1.f, 1.f);
        fullCorners[6] = UnprojectFromNDC(InvViewProjectionMatrix, 1.f, 1.f, 1.f);
        fullCorners[7] = UnprojectFromNDC(InvViewProjectionMatrix, -1.f, 1.f, 1.f);

        //----------------------------------------
        // 2. Convert depth -> ratio
        //----------------------------------------
        float nearRatio =
            (nearDepth - NearClip) /
            (FarClip - NearClip);

        float farRatio =
            (farDepth - NearClip) /
            (FarClip - NearClip);

        nearRatio = std::clamp(nearRatio, 0.0f, 1.0f);
        farRatio = std::clamp(farRatio, 0.0f, 1.0f);

        //----------------------------------------
        // 3. Interpolate cascade corners
        //----------------------------------------
        for (uint32_t i = 0; i < 4; i++)
        {
            Core::Float3 nearCorner = fullCorners[i];
            Core::Float3 farCorner = fullCorners[i + 4];

            Core::Float3 ray = farCorner - nearCorner;

            corners[i] =
                nearCorner + ray * nearRatio;

            corners[i + 4] =
                nearCorner + ray * farRatio;
        }

        return corners;
    }
    void SceneView::BuildSplitDepths() {
        
        SplitDepths[0] = NearClip;
        SplitDepths[4] = FarClip;
        float lambda = 0.7f;
        for (uint32_t i = 1; i < CascadeCount; i++)
        {
            //构建每个cascade深度
            float p = float(i) / 4.0f;

            float logSplit =
                NearClip *
                std::pow(FarClip / NearClip, p);

            float linearSplit =
                NearClip +
                (FarClip - NearClip) * p;

            SplitDepths[i] =
                Core::Lerp(linearSplit, logSplit, lambda);
        }
    }

    int SceneViewFamily::AddView(const SceneView& view)
    {
        m_Views.push_back(view);
        return static_cast<int>(m_Views.size()) - 1;
    }

    int SceneViewFamily::AddView(SceneView&& view)
    {
        m_Views.push_back(std::move(view));
        return static_cast<int>(m_Views.size()) - 1;
    }

    bool SceneViewFamily::RemoveView(int index)
    {
        if (index >= 0 && index < static_cast<int>(m_Views.size()))
        {
            m_Views.erase(m_Views.begin() + index);
            return true;
        }
        return false;
    }
    void SceneViewFamily::ClearViews() 
    {
        m_Views.clear();
    }

    SceneView* SceneViewFamily::GetView(int index)
    {
        if (index >= 0 && index < static_cast<int>(m_Views.size()))
        {
            return &m_Views[index];
        }
        return nullptr;
    }

    void SceneViewFamily::RebuildAllDerivedMatrices()
    {
        for (auto& view : m_Views)
        {
            view.RebuildDerivedMatrices();
        }
    }

    void SceneViewFamily::BuildAllViews() {
        for (auto& view : m_Views)
        {
            view.BuildSplitDepths();
            view.BuildFrustum();
        }
    }

    std::vector<int> SceneViewFamily::FindViewsThatSeeBox(const Core::AABB& box) const
    {
        std::vector<int> visibleViewIndices;
        visibleViewIndices.reserve(m_Views.size());

        for (size_t i = 0; i < m_Views.size(); ++i)
        {
            if (m_Views[i].IsBoxVisible(box))
            {
                visibleViewIndices.push_back(static_cast<int>(i));
            }
        }
        return visibleViewIndices;
    }

} // namespace Engine
