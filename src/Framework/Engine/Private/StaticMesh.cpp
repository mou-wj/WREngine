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
        auto CreateVertexBuffer = [](const std::vector<float>& Data, const char* Name)
            -> std::unique_ptr<RenderCore::RenderBuffer>
            {
                if (Data.empty())
                    return nullptr;

                RHI::RHIBufferDesc Desc;
                Desc.Size = Data.size() * sizeof(float);
                Desc.Stride = sizeof(float);
                Desc.Usage = RHI::ERHIBufferUsageFlag::Vertex | RHI::ERHIBufferUsageFlag::TransferDst;
                Desc.bCPUAccessible = false;
                Desc.DebugName = Name;

                auto Buffer = std::make_unique<RenderCore::RenderBuffer>(Desc);

                Buffer->InitRHIResource();

                // 假设你RHI有 Upload 接口（没有就替换成 staging）
                Buffer->UploadData(Data.data(), Desc.Size);
				RenderCore::TransitionBufferImmediate(RHI::GRHIApi,Buffer.get(), RHI::ERHIResourceAccess::VertexOrIndexBuffer, RHI::EQueueType::Graphics);
                return Buffer;
            };

        // Position
        VertexBuffers.PositionBuffer.Buffer =
            CreateVertexBuffer(VertexBuffers.PositionBuffer.Vertices, "PositionBuffer");

        // UV
        VertexBuffers.UVBuffer.Buffer =
            CreateVertexBuffer(VertexBuffers.UVBuffer.Vertices, "UVBuffer");

        // Normal
        VertexBuffers.NormalBuffer.Buffer =
            CreateVertexBuffer(VertexBuffers.NormalBuffer.Vertices, "NormalBuffer");

        // Tangent
        VertexBuffers.TangentBuffer.Buffer =
            CreateVertexBuffer(VertexBuffers.TangentBuffer.Vertices, "TangentBuffer");

        // Color
        VertexBuffers.ColorBuffer.Buffer =
            CreateVertexBuffer(VertexBuffers.ColorBuffer.Vertices, "ColorBuffer");

        // -----------------------------
        // 2. Index Buffer
        // -----------------------------
        if (!IndexBuffer.Indices.empty())
        {
            RHI::RHIBufferDesc Desc;
            Desc.Size = IndexBuffer.Indices.size() * sizeof(uint32_t);
            Desc.Stride = sizeof(uint32_t);
            Desc.Usage = RHI::ERHIBufferUsageFlag::Index | RHI::ERHIBufferUsageFlag::TransferDst;
            Desc.bCPUAccessible = false;
            Desc.DebugName = "IndexBuffer";

            IndexBuffer.Buffer = std::make_unique<RenderCore::RenderBuffer>(Desc);
            IndexBuffer.Buffer->InitRHIResource();

            IndexBuffer.Buffer->UploadData(
                IndexBuffer.Indices.data(),
                Desc.Size
            );
            RenderCore::TransitionBufferImmediate(RHI::GRHIApi, IndexBuffer.Buffer.get(), RHI::ERHIResourceAccess::VertexOrIndexBuffer, RHI::EQueueType::Graphics);
        }

        // -----------------------------
        // 3. VertexFactory 初始化
        // -----------------------------
        if (!VertexFactory)
            VertexFactory = std::make_unique<LocalVertexFactory>();

        LocalVertexFactoryData VFData{};

        auto FillStream = [](auto& Component, const std::unique_ptr<RenderCore::RenderBuffer>& Buffer, uint32_t Stride,ERHIFormat format)
            {
                if (!Buffer)
                    return;

                Component.Buffer = Buffer->GetRHI();
                Component.ComponentOffset = 0;
                Component.Stride = Stride;
                Component.InputRate = RHI::ERHIInputRate::PerVertex;
                Component.Format = format;
            };

        FillStream(VFData.PositionComponent, VertexBuffers.PositionBuffer.Buffer, sizeof(float) * 3,RHI::ERHIFormat::R32G32B32_Float);
        FillStream(VFData.UVComponent, VertexBuffers.UVBuffer.Buffer, sizeof(float) * 2, RHI::ERHIFormat::R32G32_Float);
        FillStream(VFData.NormalComponent, VertexBuffers.NormalBuffer.Buffer, sizeof(float) * 3, RHI::ERHIFormat::R32G32B32_Float);
        FillStream(VFData.TangentComponent, VertexBuffers.TangentBuffer.Buffer, sizeof(float) * 4, RHI::ERHIFormat::R32G32B32A32_Float);
        FillStream(VFData.ColorComponent, VertexBuffers.ColorBuffer.Buffer, sizeof(float) * 4, RHI::ERHIFormat::R32G32B32A32_Float);

        VertexFactory->SetData(VFData);
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
        auto ReleaseBuffer = [](std::unique_ptr<RenderCore::RenderBuffer>& Buffer)
            {
                if (Buffer)
                {
                    Buffer->ReleaseRHIResource();
                    Buffer.reset();
                }
            };

        ReleaseBuffer(VertexBuffers.PositionBuffer.Buffer);
        ReleaseBuffer(VertexBuffers.UVBuffer.Buffer);
        ReleaseBuffer(VertexBuffers.NormalBuffer.Buffer);
        ReleaseBuffer(VertexBuffers.TangentBuffer.Buffer);
        ReleaseBuffer(VertexBuffers.ColorBuffer.Buffer);

        // -----------------------------
        // 3. Index Buffer
        // -----------------------------
        ReleaseBuffer(IndexBuffer.Buffer);

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