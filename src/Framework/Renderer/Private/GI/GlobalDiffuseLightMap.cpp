#include "GlobalDiffuseLightMap.h"

#include "RHIPipelineStateCache.h"
#include "RHIApi.h"
#include "GlobalShader.h"

#include <algorithm>
#include <cmath>

namespace Renderer
{
    namespace
    {
        inline float SafeLengthSquared(const Core::Float3& V)
        {
            return V.X * V.X + V.Y * V.Y + V.Z * V.Z;
        }

        inline Core::Float3 NormalizeSafe(const Core::Float3& V)
        {
            const float LengthSq = SafeLengthSquared(V);
            if (LengthSq <= 1.0e-8f)
            {
                return Core::Float3(0.0f, 1.0f, 0.0f);
            }

            const float InvLen = 1.0f / std::sqrt(LengthSq);
            return V * InvLen;
        }

        inline Core::Float3 HemisphereSampleDirection(float U1, float U2)
        {
            const float Z = U1;
            const float R = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
            const float Phi = 2.0f * 3.14159265358979323846f * U2;
            return Core::Float3(R * std::cos(Phi), R * std::sin(Phi), Z);
        }

        inline bool IsShadowedByDistanceField(const Core::Float3& Position, const Core::Float3& Direction, float MaxDistance)
        {
            // Reserved for future global distance field occlusion.
            // Current implementation intentionally does nothing and always returns false.
            (void)Position;
            (void)Direction;
            (void)MaxDistance;
            return false;
        }

        inline Core::Float3 SampleEnvironmentMapLDR(const Core::Float3& Direction, const RenderCore::RenderTexture* EnvironmentMap)
        {
            (void)EnvironmentMap;
            const float NdotL = std::max(0.0f, Direction.Y);
            return Core::Float3(1.0f, 1.0f, 1.0f) * (0.3f + 0.7f * NdotL);
        }
    }

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        GlobalDiffuseLightMapInitCS,
        "GlobalDiffuseLightMapInitCS",
        "/GI/GlobalDiffuseLightMapInitCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        GlobalDiffuseLightMapDiffuseCS,
        "GlobalDiffuseLightMapDiffuseCS",
        "/GI/GlobalDiffuseLightMapDiffuseCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    void SceneVoxelOctree::Clear()
    {
        Tree.Clear();
        Nodes.clear();
        Voxels.clear();
        Bounds = Core::AABB();
    }

    void SceneVoxelOctree::BuildFromStaticMeshes(
        const std::vector<const Engine::StaticMesh*>& Meshes,
        const SceneVoxelOctreeSettings& Settings)
    {
        Clear();
        Settings_ = Settings;

        std::vector<SceneVoxelCell> VoxelPrimitives;
        VoxelPrimitives.reserve(4096u);

        Bounds.SetEmpty();
        for (uint32_t MeshIndex = 0; MeshIndex < static_cast<uint32_t>(Meshes.size()); ++MeshIndex)
        {
            const Engine::StaticMesh* Mesh = Meshes[MeshIndex];
            if (Mesh == nullptr)
            {
                continue;
            }

            const Core::BoxSphereBounds& MeshBounds = Mesh->GetBounds();
            if (MeshBounds.Box.IsEmpty())
            {
                continue;
            }

            Bounds.Merge(MeshBounds.Box);

            const Core::Float3 VoxelExtents(Settings_.VoxelSize, Settings_.VoxelSize, Settings_.VoxelSize);
            const Core::Float3 MinCell = MeshBounds.Box.Min;
            const Core::Float3 MaxCell = MeshBounds.Box.Max;
            const int32_t MinX = static_cast<int32_t>(std::floor(MinCell.X / Settings_.VoxelSize));
            const int32_t MinY = static_cast<int32_t>(std::floor(MinCell.Y / Settings_.VoxelSize));
            const int32_t MinZ = static_cast<int32_t>(std::floor(MinCell.Z / Settings_.VoxelSize));
            const int32_t MaxX = static_cast<int32_t>(std::floor(MaxCell.X / Settings_.VoxelSize));
            const int32_t MaxY = static_cast<int32_t>(std::floor(MaxCell.Y / Settings_.VoxelSize));
            const int32_t MaxZ = static_cast<int32_t>(std::floor(MaxCell.Z / Settings_.VoxelSize));

            for (int32_t Z = MinZ; Z <= MaxZ; ++Z)
            {
                for (int32_t Y = MinY; Y <= MaxY; ++Y)
                {
                    for (int32_t X = MinX; X <= MaxX; ++X)
                    {
                        const Core::Float3 VoxelMin(
                            static_cast<float>(X) * Settings_.VoxelSize,
                            static_cast<float>(Y) * Settings_.VoxelSize,
                            static_cast<float>(Z) * Settings_.VoxelSize);
                        const Core::Float3 VoxelMax = VoxelMin + VoxelExtents;
                        const Core::AABB VoxelBounds(VoxelMin, VoxelMax);

                        if (!VoxelBounds.Intersects(MeshBounds.Box) && !VoxelBounds.Contains(MeshBounds.Box))
                        {
                            continue;
                        }

                        SceneVoxelCell Voxel;
                        Voxel.Bounds = VoxelBounds;
                        Voxel.MeshId = MeshIndex;
                        Voxel.MaterialId = 0;
                        Voxel.TriangleOffset = static_cast<uint32_t>(VoxelPrimitives.size());
                        Voxel.TriangleCount = 1;
                        Voxel.Flags = 1;
                        Voxel.Padding = 0;
                        Voxel.Normal = NormalizeSafe(MeshBounds.Box.GetCenter() - VoxelMin);
                        Voxel.BaseColor = Core::Float3(1.0f, 1.0f, 1.0f);
                        VoxelPrimitives.push_back(Voxel);
                    }
                }
            }
        }

        if (VoxelPrimitives.empty())
        {
            return;
        }

        if (Settings_.AutoExpandBounds)
        {
            const Core::Float3 Min = Core::Float3(
                std::min(Bounds.Min.X, Settings_.WorldMin.X),
                std::min(Bounds.Min.Y, Settings_.WorldMin.Y),
                std::min(Bounds.Min.Z, Settings_.WorldMin.Z));
            const Core::Float3 Max = Core::Float3(
                std::max(Bounds.Max.X, Settings_.WorldMax.X),
                std::max(Bounds.Max.Y, Settings_.WorldMax.Y),
                std::max(Bounds.Max.Z, Settings_.WorldMax.Z));
            Bounds = Core::AABB(Min, Max);
        }

        Tree.SetMaxDepth(Settings_.MaxDepth);
        Tree.SetMaxPrimitivesPerLeaf(Settings_.MaxVoxelsPerLeaf);
        Tree.Build(VoxelPrimitives);
        RebuildNodesAndVoxels();
    }

    const std::vector<SceneVoxelOctreeNode>& SceneVoxelOctree::GetNodes() const
    {
        return Nodes;
    }

    const std::vector<SceneVoxelCell>& SceneVoxelOctree::GetVoxels() const
    {
        return Voxels;
    }

    const Core::AABB& SceneVoxelOctree::GetBounds() const
    {
        return Bounds;
    }

    std::vector<SceneVoxelOctreeShaderNode> SceneVoxelOctree::FlattenForShader() const
    {
        std::vector<SceneVoxelOctreeShaderNode> FlattenedNodes;
        FlattenedNodes.reserve(Nodes.size());

        for (uint32_t NodeIndex = 0; NodeIndex < static_cast<uint32_t>(Nodes.size()); ++NodeIndex)
        {
            const SceneVoxelOctreeNode& Node = Nodes[NodeIndex];
            SceneVoxelOctreeShaderNode FlattenedNode;
            FlattenedNode.MinBounds = Node.Bounds.Min;
            FlattenedNode.MaxBounds = Node.Bounds.Max;
            FlattenedNode.Parent = Node.Parent;
            FlattenedNode.Level = Node.Level;
            FlattenedNode.FirstVoxel = Node.FirstVoxel;
            FlattenedNode.VoxelCount = Node.VoxelCount;
            FlattenedNode.FirstChild = Core::OctreeInvalidNode;
            FlattenedNode.ChildMask = 0;

            for (uint32_t ChildIndex = 0; ChildIndex < 8; ++ChildIndex)
            {
                if (Node.Children[ChildIndex] != Core::OctreeInvalidNode)
                {
                    FlattenedNode.ChildMask |= (1u << ChildIndex);
                    if (FlattenedNode.FirstChild == Core::OctreeInvalidNode)
                    {
                        FlattenedNode.FirstChild = Node.Children[ChildIndex];
                    }
                }
            }

            FlattenedNodes.push_back(FlattenedNode);
        }

        return FlattenedNodes;
    }

    Core::AABB SceneVoxelOctree::ComputeSceneBounds(const std::vector<SceneVoxelCell>& VoxelPrimitives)
    {
        Core::AABB Result;
        Result.SetEmpty();

        for (const SceneVoxelCell& Voxel : VoxelPrimitives)
        {
            Result.Merge(Voxel.Bounds);
        }

        return Result;
    }

    void SceneVoxelOctree::RebuildNodesAndVoxels()
    {
        const std::vector<Core::OctreeNode>& TreeNodes = Tree.GetNodes();
        Nodes.clear();
        Nodes.resize(TreeNodes.size());

        for (uint32_t Index = 0; Index < static_cast<uint32_t>(TreeNodes.size()); ++Index)
        {
            const Core::OctreeNode& TreeNode = TreeNodes[Index];
            SceneVoxelOctreeNode& Node = Nodes[Index];
            Node.Bounds = TreeNode.Bounds.Box;
            Node.Children = TreeNode.Children;
            Node.IsLeaf = TreeNode.bLeaf;
            Node.Parent = Core::OctreeInvalidNode;
            Node.Level = 0;
            Node.FirstVoxel = Core::OctreeInvalidNode;
            Node.VoxelCount = 0;
        }

        std::function<void(uint32_t, uint32_t)> BuildHierarchy = [&](uint32_t NodeIndex, uint32_t ParentIndex)
        {
            SceneVoxelOctreeNode& ThisNode = Nodes[NodeIndex];
            ThisNode.Parent = ParentIndex;
            ThisNode.Level = ParentIndex == Core::OctreeInvalidNode ? 0u : Nodes[ParentIndex].Level + 1u;

            for (uint32_t ChildIndex = 0; ChildIndex < 8; ++ChildIndex)
            {
                const uint32_t ChildNodeIndex = ThisNode.Children[ChildIndex];
                if (ChildNodeIndex != Core::OctreeInvalidNode)
                {
                    BuildHierarchy(ChildNodeIndex, NodeIndex);
                }
            }
        };

        if (!Nodes.empty())
        {
            BuildHierarchy(0u, Core::OctreeInvalidNode);
        }

        Voxels.clear();
        for (uint32_t NodeIndex = 0; NodeIndex < static_cast<uint32_t>(Nodes.size()); ++NodeIndex)
        {
            SceneVoxelOctreeNode& Node = Nodes[NodeIndex];
            if (!Node.IsLeaf)
            {
                continue;
            }

            const Core::OctreeNode& TreeNode = TreeNodes[NodeIndex];
            if (TreeNode.PrimitiveCount == 0)
            {
                continue;
            }

            Node.FirstVoxel = static_cast<uint32_t>(Voxels.size());
            Node.VoxelCount = TreeNode.PrimitiveCount;

            for (uint32_t PrimitiveOffset = 0; PrimitiveOffset < TreeNode.PrimitiveCount; ++PrimitiveOffset)
            {
                const uint32_t PrimitiveIndex = Tree.GetPrimitiveIndices()[TreeNode.FirstPrimitive + PrimitiveOffset];
                const SceneVoxelCell& Primitive = Tree.GetPrimitive(PrimitiveIndex);
                Voxels.push_back(Primitive);
            }
        }
    }

    void GlobalDiffuseLightMapVoxelizer::SetSettings(const SceneVoxelOctreeSettings& InSettings)
    {
        Settings = InSettings;
    }

    void GlobalDiffuseLightMapVoxelizer::VoxelizeStaticMeshes(const std::vector<const Engine::StaticMesh*>& Meshes)
    {
        SceneOctree.BuildFromStaticMeshes(Meshes, Settings);
    }

    const SceneVoxelOctree& GlobalDiffuseLightMapVoxelizer::GetSceneVoxelOctree() const
    {
        return SceneOctree;
    }

    void GlobalDiffuseLightMapVoxelizer::BuildShaderParameters(SceneVoxelOctreeShaderParameters& OutParameters) const
    {
        const std::vector<SceneVoxelOctreeShaderNode> FlattenedNodes = SceneOctree.FlattenForShader();
        OutParameters.NodeCount = static_cast<uint32_t>(FlattenedNodes.size());
        OutParameters.VoxelCount = static_cast<uint32_t>(SceneOctree.GetVoxels().size());
        OutParameters.MaxDepth = Settings.MaxDepth;
        OutParameters.SceneMin = SceneOctree.GetBounds().Min;
        OutParameters.VoxelSize = Settings.VoxelSize;
        OutParameters.Resolution = Core::UInt3(
            static_cast<unsigned int>(Settings.TargetResolution),
            static_cast<unsigned int>(Settings.TargetResolution),
            static_cast<unsigned int>(Settings.TargetResolution));
        //OutParameters.Nodes = FlattenedNodes;
        //OutParameters.Voxels = SceneOctree.GetVoxels();
    }

    GlobalDiffuseLightMap::GlobalDiffuseLightMap(SceneVoxelOctreeSettings InSettings)
        : Settings(std::move(InSettings))
    {
    }

    void GlobalDiffuseLightMap::SetSettings(const SceneVoxelOctreeSettings& InSettings)
    {
        Settings = InSettings;
    }

    void GlobalDiffuseLightMap::VoxelizeStaticMeshes(const std::vector<const Engine::StaticMesh*>& Meshes)
    {
        SceneOctree.BuildFromStaticMeshes(Meshes, Settings);
    }

    const SceneVoxelOctree& GlobalDiffuseLightMap::GetSceneVoxelOctree() const
    {
        return SceneOctree;
    }

    void GlobalDiffuseLightMap::BuildShaderParameters(SceneVoxelOctreeShaderParameters& OutParameters) const
    {
        const std::vector<SceneVoxelOctreeShaderNode> FlattenedNodes = SceneOctree.FlattenForShader();
        OutParameters.NodeCount = static_cast<uint32_t>(FlattenedNodes.size());
        OutParameters.VoxelCount = static_cast<uint32_t>(SceneOctree.GetVoxels().size());
        OutParameters.MaxDepth = Settings.MaxDepth;
        OutParameters.SceneMin = SceneOctree.GetBounds().Min;
        OutParameters.VoxelSize = Settings.VoxelSize;
        OutParameters.Resolution = Core::UInt3(
            static_cast<unsigned int>(Settings.TargetResolution),
            static_cast<unsigned int>(Settings.TargetResolution),
            static_cast<unsigned int>(Settings.TargetResolution));
        //OutParameters.Nodes = FlattenedNodes;
        //OutParameters.Voxels = SceneOctree.GetVoxels();
    }

    void ExecuteGlobalDiffuseLightMapInit(RenderCore::RenderGraphBuilder& GraphBuilder, const GlobalDiffuseLightMapInitExecuteInput& Input)
    {
        if (!Input.EnvironmentMap || !Input.NodesBuffer || !Input.VoxelsBuffer || !Input.InitialDiffuseBuffer)
        {
            return;
        }

        SceneVoxelDiffuseInitParameters Params;
        Params.NodeCount = Input.NodeCount;
        Params.VoxelCount = Input.VoxelCount;
        Params.MaxDepth = Input.MaxDepth;
        Params.SceneMin = Input.Settings.WorldMin;
        Params.VoxelSize = Input.Settings.VoxelSize;
        Params.Resolution = Core::UInt3(
            static_cast<unsigned int>(Input.Settings.TargetResolution),
            static_cast<unsigned int>(Input.Settings.TargetResolution),
            static_cast<unsigned int>(Input.Settings.TargetResolution));
        //Params.Nodes = Input.NodesBuffer;
        //Params.Voxels = Input.VoxelsBuffer;
        Params.EnvironmentMap = Input.EnvironmentMap;
        Params.EnvironmentMapSampler = RenderCore::GlobalSampler.get();
        //Params.InitialDiffuse = Input.InitialDiffuseBuffer;

        auto& GShaderMap = RenderCore::GShaderMap;
        auto shaderType = RenderCore::ShaderType::GetRegisterMap()[RenderCore::ShaderType::EShaderTypeFlag::Global]["GlobalDiffuseLightMapInitCS"];
        auto shader = GShaderMap.GetShader(shaderType, 0);
        if (!shader)
        {
            return;
        }

        auto computeContext = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->AcquireCastedCommandContext<RHI::RHIComputeContex>();
        RHI::RHIComputeCommandList CmdList(computeContext);
        CmdList.SetImmediate(true);
        CmdList.Begin();

        RHI::RHIComputePipelineStateDesc ComputeDesc;
        ComputeDesc.computeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        auto PipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(ComputeDesc);
        CmdList.SetComputePipelineState(PipelineState);

        SetShaderParameters(CmdList, shader, SceneVoxelDiffuseInitParameters::GetMetaData(), &Params);

        const uint32_t DispatchCount = std::max<uint32_t>(1u, Input.VoxelCount);
        const uint32_t GroupX = (DispatchCount + 63u) / 64u;
        CmdList.Dispatch(GroupX, 1u, 1u);

        CmdList.End();
        auto Fence = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->ExecuteContext(computeContext);
        (void)Fence;
    }

    void ExecuteGlobalDiffuseLightMapDiffuse(RenderCore::RenderGraphBuilder& GraphBuilder, const GlobalDiffuseLightMapDiffuseExecuteInput& Input)
    {
        (void)GraphBuilder;

        if (!Input.NodesBuffer || !Input.VoxelsBuffer || !Input.InputDiffuseBuffer || !Input.OutputDiffuseBuffer || !Input.DiffusionScratchBuffer)
        {
            return;
        }

        SceneVoxelDiffusePropagationParameters Params;
        Params.NodeCount = Input.NodeCount;
        Params.VoxelCount = Input.VoxelCount;
        Params.MaxDepth = Input.MaxDepth;
        Params.SceneMin = Input.Settings.WorldMin;
        Params.VoxelSize = Input.Settings.VoxelSize;
        Params.Resolution = Core::UInt3(
            static_cast<unsigned int>(Input.Settings.TargetResolution),
            static_cast<unsigned int>(Input.Settings.TargetResolution),
            static_cast<unsigned int>(Input.Settings.TargetResolution));
        //Params.Nodes = Input.NodesBuffer;
        //Params.Voxels = Input.VoxelsBuffer;
        //Params.InputDiffuse = Input.InputDiffuseBuffer;
        //Params.OutputDiffuse = Input.OutputDiffuseBuffer;
        //Params.DiffusionScratch = Input.DiffusionScratchBuffer;

        auto& GShaderMap = RenderCore::GShaderMap;
        auto shaderType = RenderCore::ShaderType::GetRegisterMap()[RenderCore::ShaderType::EShaderTypeFlag::Global]["GlobalDiffuseLightMapDiffuseCS"];
        auto shader = GShaderMap.GetShader(shaderType, 0);
        if (!shader)
        {
            return;
        }

        auto computeContext = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->AcquireCastedCommandContext<RHI::RHIComputeContex>();
        RHI::RHIComputeCommandList CmdList(computeContext);
        CmdList.SetImmediate(true);
        CmdList.Begin();

        RHI::RHIComputePipelineStateDesc ComputeDesc;
        ComputeDesc.computeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        auto PipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(ComputeDesc);
        CmdList.SetComputePipelineState(PipelineState);

        SetShaderParameters(CmdList, shader, SceneVoxelDiffusePropagationParameters::GetMetaData(), &Params);

        const uint32_t DispatchCount = std::max<uint32_t>(1u, Input.VoxelCount);
        const uint32_t GroupX = (DispatchCount + 63u) / 64u;
        CmdList.Dispatch(GroupX, 1u, 1u);

        CmdList.End();
        auto Fence = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->ExecuteContext(computeContext);
        (void)Fence;
    }
}
