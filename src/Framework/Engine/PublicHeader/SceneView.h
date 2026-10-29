// SceneView.h
#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <functional>
#include "EngineExport.h"
#include "BoxSphereBounds.h"
#include "RenderGraphResource.h"

// 包含你刚才定义的数学基础头文件
#include "Math.hpp"

namespace Engine {
    // 前置声明
    class SceneInterface;
    class RenderTarget;
    class SceneView;

    // Adds passes after the scene has been built, before the graph executes.
    // The builder and textures are borrowed for this graph only; capture pass
    // data by value rather than referencing mutable UI/component state.
    struct SceneViewDrawExtension {
        using DrawCallback = std::function<void(
            RenderCore::RenderGraphBuilder& graphBuilder,
            const SceneView& view,
            RenderCore::RenderGraphTextureRef sceneColor,
            RenderCore::RenderGraphTextureRef sceneDepth)>;

        DrawCallback Callback;
    };

    // 结构体对齐定义
    struct ViewRect { int X; int Y; int Width; int Height; };
    struct Plane { float A, B, C, D; };
    struct Frustum { std::array<Plane, 6> Planes; };

    /* ===============================================================================
        ViewUniforms (16字节严格对齐的 C++ 镜像缓冲区)
        直接通过你的 Float4x4 和 Float4 拼装，可直接一键 memcpy 发送至 RHI
    ===============================================================================
    */
    struct ViewUniforms {
        Core::Float4x4 ViewProj;
        Core::Float4x4 InvViewProj;
        Core::Float4 CameraPos; // 扩展为 Float4 彻底杜绝内存错位
    };

    enum ViewFlags : uint32_t {
        VF_None = 0,
        VF_EnableShadows = 1 << 0,
        VF_EnablePostFX = 1 << 1
    };

    /*
    ===============================================================================
        SceneView (单个视口相机的状态快照)
    ===============================================================================
    */
    class ENGINE_API SceneView {
    public:
        SceneView();
        ~SceneView();

        // 核心变换矩阵（完全对齐你的 Matrix 模板类型）
        Core::Float4x4 ViewMatrix;            // World -> View
        Core::Float4x4 ProjectionMatrix;      // View -> Clip
        Core::Float4x4 ViewProjectionMatrix;  // Precomputed Projection * View  
        Core::Float4x4 InvViewProjectionMatrix; // 预留逆矩阵（用于延迟渲染位置重建）

        // 相机空间属性
        Core::Float3 CameraWorldPos; // 保持 Float3
        Core::Float3 CameraWorldDir; // 保持 Float3

        // 裁剪面
        float NearClip;
        float FarClip;
        bool bDepthRangeZeroToOne;

        // 视口与画布规格
        ViewRect Viewport;
        float RenderTargetWidth;
        float RenderTargetHeight;

        uint64_t FrameIndex;
        double FrameTimeSeconds;

        uint32_t Flags;

        // Invoked in list order for this view. Empty callbacks are skipped.
        // Populate before BeginRender; do not modify the list while rendering.
        std::vector<SceneViewDrawExtension> DrawExtensions;

        // 视锥体缓存
        Frustum CachedFrustum;
		static const uint32_t CascadeCount = 4;
        std::array<float, CascadeCount + 1> SplitDepths;
        // 渲染线程专用工具函数 (构建时或构建后执行)
        void RebuildDerivedMatrices();
        void BuildFrustum();
        bool IsBoxVisible(const Core::AABB& box) const;
        void PackViewUniforms(ViewUniforms& outUniforms) const;
        std::array<Core::Float3, 8> GetFrustumCornersWS(float nearDepth,float farDepth) const;
        void BuildSplitDepths();
    };

    /*
    ===============================================================================
        SceneViewFamily (视图族：整合单帧渲染的场景、画布与多视角相机队列)
    ===============================================================================
    */
    class ENGINE_API SceneViewFamily {
    public:
        SceneViewFamily() = default;

        // 工业级规范：严禁意外拷贝视图族，防止产生每帧 vector 复制带来的无谓开销
        SceneViewFamily(const SceneViewFamily&) = delete;
        SceneViewFamily& operator=(const SceneViewFamily&) = delete;
        SceneViewFamily(SceneViewFamily&&) = default;

        // 视图管理
        int AddView(const SceneView& view);
        int AddView(SceneView&& view);
        bool RemoveView(int index);
        void ClearViews();

        SceneView* GetView(int index);
        const std::vector<SceneView>& GetViews() const { return m_Views; }
        int Size() const { return static_cast<int>(m_Views.size()); }

        SceneInterface* GetScene() const { return Scene; }
        RenderTarget* GetRenderTarget() const { return RenderTarget; }

        // 批量更新控制
        void RebuildAllDerivedMatrices();
        void BuildAllViews();

        // 视锥体批量可见性过滤：返回所有能看到这个 AABB 的 View 索引列表
        std::vector<int> FindViewsThatSeeBox(const Core::AABB& box) const;

        // 快捷遍历器
        template<typename Fn>
        void ForEachView(Fn&& callback) {
            for (auto& view : m_Views) {
                callback(view);
            }
        }
        SceneInterface* Scene;
        RenderTarget* RenderTarget;
    private:
        std::vector<SceneView> m_Views;

    };

} // namespace Engine