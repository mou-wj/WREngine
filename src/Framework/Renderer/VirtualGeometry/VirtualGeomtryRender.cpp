#include "VirtualGeomtryRender.h"
#include "VirtualGeometryMaterialResolvePass.h"
#include "Scene.h"
#include "StaticMeshComponent.h"
#include "StaticMeshProxy.h"
#include "GBufferInfo.h"
#include <stdexcept>

namespace Renderer
{
    namespace
    {
        std::vector<VirtualGeometryInstance> BuildVirtualGeometryInstances(Scene& scene, const Engine::SceneView& view)
        {
            std::vector<VirtualGeometryInstance> instances;
            const auto& instanceIDs = scene.GetGPUResourceInfo().PrimitiveResourceInfo
                                          .LocalVertexFactoryInstanceInfo.StaticMeshProxyToInstanceId;
            for (auto* primitive : scene.GatherVisiblePrimitives(view))
            {
                if (!primitive || !primitive->IsA<Engine::StaticMeshProxy>())
                {
                    continue;
                }
                auto* proxy = static_cast<Engine::StaticMeshProxy*>(primitive);
                const auto* component = proxy->GetStaticMeshComponent();
                const auto* mesh = component ? component->GetStaticMesh() : nullptr;
                auto data = mesh ? mesh->GetRenderData() : nullptr;
                if (!data || !data->VirtualGeometryData.IsValid())
                {
                    continue;
                }
                const auto instanceID = instanceIDs.find(proxy);
                if (instanceID == instanceIDs.end())
                {
                    throw std::invalid_argument("Virtual geometry scene instance is missing; flush scene updates first");
                }
                VirtualGeometryInstance instance{data, proxy->GetLocalToWorld(), instanceID->second};
                instance.Materials.reserve(component->GetNumMaterials());
                for (uint32_t slot = 0; slot < component->GetNumMaterials(); ++slot)
                {
                    auto* material = component->GetMaterial(slot);
                    instance.Materials.push_back(material ? material->GetRenderProxyShared() : nullptr);
                }
                instances.push_back(std::move(instance));
            }
            return instances;
        }
    } // namespace

    std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRender(
        RenderCore::RenderGraphBuilder& builder, Scene* scene, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneDepth, RenderCore::RenderGraphTextureRef gbufferA,
        RenderCore::RenderGraphTextureRef gbufferB, RenderCore::RenderGraphTextureRef gbufferC)
    {
        if (!scene || !sceneDepth || !gbufferA || !gbufferB || !gbufferC)
        {
            throw std::invalid_argument("Virtual geometry render requires a scene, depth and GBuffer A/B/C");
        }
        const auto& depthDesc = sceneDepth->GetDesc();
        // The current raster clear covers the entire attachment. Do not erase another view implicitly.
        if (view.Viewport.X != 0 || view.Viewport.Y != 0 || view.Viewport.Width <= 0 || view.Viewport.Height <= 0 ||
            uint32_t(view.Viewport.Width) != depthDesc.Width || uint32_t(view.Viewport.Height) != depthDesc.Height)
        {
            throw std::invalid_argument("Virtual geometry render currently requires one full-target view");
        }
        const auto layout = CreateGBufferInfo({});
        const RenderCore::RenderGraphTextureRef buffers[] = {gbufferA, gbufferB, gbufferC};
        for (uint32_t index = 0; index < 3; ++index)
        {
            const auto& desc = buffers[index]->GetDesc();
            if (desc.Width != depthDesc.Width || desc.Height != depthDesc.Height ||
                desc.Format != layout.Targets[index].TargetFormat || desc.Type != RHI::ERHITextureType::Texture2D ||
                desc.SampleCount != 1 || desc.ArraySize != 1 ||
                !EnumHasAnyFlags(desc.Usage, RHI::ERHITextureCreateFlag::UAV))
            {
                throw std::invalid_argument("Virtual geometry render requires matching UAV GBuffer targets");
            }
        }
        auto instances = BuildVirtualGeometryInstances(*scene, view);
        auto visibility = AddVirtualGeometryRasterPass(builder, view, instances, sceneDepth);
        AddVirtualGeometryMaterialResolvePass(builder, view, visibility, gbufferA, gbufferB, gbufferC);
        return visibility;
    }
} // namespace Renderer
