// StaticMesh.cpp
#include "StaticMesh.h"
#include "MaterialAsset.h"
#include "Material.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshProxy.h"
#include "RenderResource.h"
namespace Engine {
    MaterialInterface* StaticMesh::GetMaterial(size_t index) const
    {
        const auto asset = GetMaterialAsset(index);
        return asset ? asset->GetMaterial() : nullptr;
    }

	void LODResource::InitializeResources() {
        // -----------------------------
   // 1. 生成 Vertex Buffer
   // -----------------------------
        auto createVertexBuffer = [](const std::vector<float>& data, const char* name)
            -> std::unique_ptr<RenderCore::RenderBuffer>
            {
                if (data.empty())
                    return nullptr;

                RHI::RHIBufferDesc desc;
                desc.Size = data.size() * sizeof(float);
                desc.Stride = sizeof(float);
                desc.Usage = RHI::ERHIBufferUsageFlag::Vertex | RHI::ERHIBufferUsageFlag::TransferDst;
                desc.bCPUAccessible = false;
                desc.DebugName = name;

                auto buffer = std::make_unique<RenderCore::RenderBuffer>(desc);

                buffer->InitRHIResource();

                // 假设你RHI有 Upload 接口（没有就替换成 staging）
                buffer->UploadData(data.data(), desc.Size);
				RenderCore::TransitionBufferImmediate(RHI::GRHIApi,buffer.get(), RHI::ERHIResourceAccess::VertexOrIndexBuffer, RHI::EQueueType::Graphics);
                return buffer;
            };

        // Position
        VertexBuffers.PositionBuffer.Buffer =
            createVertexBuffer(VertexBuffers.PositionBuffer.Vertices, "PositionBuffer");

        // UV
        VertexBuffers.UVBuffer.Buffer =
            createVertexBuffer(VertexBuffers.UVBuffer.Vertices, "UVBuffer");

        // Normal
        VertexBuffers.NormalBuffer.Buffer =
            createVertexBuffer(VertexBuffers.NormalBuffer.Vertices, "NormalBuffer");

        // Tangent
        VertexBuffers.TangentBuffer.Buffer =
            createVertexBuffer(VertexBuffers.TangentBuffer.Vertices, "TangentBuffer");

        // Color
        VertexBuffers.ColorBuffer.Buffer =
            createVertexBuffer(VertexBuffers.ColorBuffer.Vertices, "ColorBuffer");

        // -----------------------------
        // 2. Index Buffer
        // -----------------------------
        if (!IndexBuffer.Indices.empty())
        {
            RHI::RHIBufferDesc desc;
            desc.Size = IndexBuffer.Indices.size() * sizeof(uint32_t);
            desc.Stride = sizeof(uint32_t);
            desc.Usage = RHI::ERHIBufferUsageFlag::Index | RHI::ERHIBufferUsageFlag::TransferDst;
            desc.bCPUAccessible = false;
            desc.DebugName = "IndexBuffer";

            IndexBuffer.Buffer = std::make_unique<RenderCore::RenderBuffer>(desc);
            IndexBuffer.Buffer->InitRHIResource();

            IndexBuffer.Buffer->UploadData(
                IndexBuffer.Indices.data(),
                desc.Size
            );
            RenderCore::TransitionBufferImmediate(RHI::GRHIApi, IndexBuffer.Buffer.get(), RHI::ERHIResourceAccess::VertexOrIndexBuffer, RHI::EQueueType::Graphics);
        }

        // -----------------------------
        // 3. VertexFactory 初始化
        // -----------------------------
        if (!VertexFactory)
            VertexFactory = std::make_unique<LocalVertexFactory>();

        LocalVertexFactoryData vfData{};

        auto fillStream = [](auto& component, const std::unique_ptr<RenderCore::RenderBuffer>& buffer, uint32_t stride,ERHIFormat format)
            {
                if (!buffer)
                    return;

                component.Buffer = buffer->GetRHI();
                component.ComponentOffset = 0;
                component.Stride = stride;
                component.InputRate = RHI::ERHIInputRate::PerVertex;
                component.Format = format;
            };

        fillStream(vfData.PositionComponent, VertexBuffers.PositionBuffer.Buffer, sizeof(float) * 3,RHI::ERHIFormat::R32G32B32_Float);
        fillStream(vfData.UVComponent, VertexBuffers.UVBuffer.Buffer, sizeof(float) * 2, RHI::ERHIFormat::R32G32_Float);
        fillStream(vfData.NormalComponent, VertexBuffers.NormalBuffer.Buffer, sizeof(float) * 3, RHI::ERHIFormat::R32G32B32_Float);
        fillStream(vfData.TangentComponent, VertexBuffers.TangentBuffer.Buffer, sizeof(float) * 4, RHI::ERHIFormat::R32G32B32A32_Float);
        fillStream(vfData.ColorComponent, VertexBuffers.ColorBuffer.Buffer, sizeof(float) * 4, RHI::ERHIFormat::R32G32B32A32_Float);

        VertexFactory->SetData(vfData);
	}
	void LODResource::ReleaseResources() {
        // -----------------------------
    // 1. Vertex Factory 释放
    // -----------------------------
        if (VertexFactory)
        {
            // 如果你有GPU绑定缓存/PSO cache，这里应该清掉
            VertexFactory.reset();
        }

        // -----------------------------
        // 2. Vertex Buffers
        // -----------------------------
        auto releaseBuffer = [](std::unique_ptr<RenderCore::RenderBuffer>& buffer)
            {
                if (buffer)
                {
                    buffer->ReleaseRHIResource();
                    buffer.reset();
                }
            };

        releaseBuffer(VertexBuffers.PositionBuffer.Buffer);
        releaseBuffer(VertexBuffers.UVBuffer.Buffer);
        releaseBuffer(VertexBuffers.NormalBuffer.Buffer);
        releaseBuffer(VertexBuffers.TangentBuffer.Buffer);
        releaseBuffer(VertexBuffers.ColorBuffer.Buffer);

        // -----------------------------
        // 3. Index Buffer
        // -----------------------------
        releaseBuffer(IndexBuffer.Buffer);

        // -----------------------------
        // 4. 清 CPU 数据（可选）
        // -----------------------------
        VertexBuffers.PositionBuffer.Vertices.clear();
        VertexBuffers.UVBuffer.Vertices.clear();
        VertexBuffers.NormalBuffer.Vertices.clear();
        VertexBuffers.TangentBuffer.Vertices.clear();
        VertexBuffers.ColorBuffer.Vertices.clear();

        IndexBuffer.Indices.clear();

        Sections.clear();
	}
		


} // namespace Engine