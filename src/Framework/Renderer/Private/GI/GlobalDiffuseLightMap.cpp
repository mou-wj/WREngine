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
        inline float SafeLengthSquared(const Core::Float3& v)
        {
            return v.X * v.X + v.Y * v.Y + v.Z * v.Z;
        }

        inline Core::Float3 NormalizeSafe(const Core::Float3& v)
        {
            const float lengthSq = SafeLengthSquared(v);
            if (lengthSq <= 1.0e-8f)
            {
                return Core::Float3(0.0f, 1.0f, 0.0f);
            }

            const float invLen = 1.0f / std::sqrt(lengthSq);
            return v * invLen;
        }

        inline Core::Float3 HemisphereSampleDirection(float u1, float u2)
        {
            const float z = u1;
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float phi = 2.0f * 3.14159265358979323846f * u2;
            return Core::Float3(r * std::cos(phi), r * std::sin(phi), z);
        }

        inline bool IsShadowedByDistanceField(const Core::Float3& position, const Core::Float3& direction, float maxDistance)
        {
            // Reserved for future global distance field occlusion.
            // Current implementation intentionally does nothing and always returns false.
            (void)position;
            (void)direction;
            (void)maxDistance;
            return false;
        }

        inline Core::Float3 SampleEnvironmentMapLDR(const Core::Float3& direction, const RenderCore::RenderTexture* environmentMap)
        {
            (void)environmentMap;
            const float ndotL = std::max(0.0f, direction.Y);
            return Core::Float3(1.0f, 1.0f, 1.0f) * (0.3f + 0.7f * ndotL);
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
        _tree.Clear();
        _nodes.clear();
        _voxels.clear();
        _bounds = Core::AABB();
    }

    void SceneVoxelOctree::BuildFromStaticMeshes(
        const std::vector<const Engine::StaticMesh*>& meshes,
        const SceneVoxelOctreeSettings& settings)
    {
        Clear();
        _settings = settings;

        std::vector<SceneVoxelCell> voxelPrimitives;
        voxelPrimitives.reserve(4096u);

        _bounds.SetEmpty();
        for (uint32_t meshIndex = 0; meshIndex < static_cast<uint32_t>(meshes.size()); ++meshIndex)
        {
            const Engine::StaticMesh* mesh = meshes[meshIndex];
            if (mesh == nullptr)
            {
                continue;
            }

            const Core::BoxSphereBounds& meshBounds = mesh->GetBounds();
            if (meshBounds.Box.IsEmpty())
            {
                continue;
            }

            _bounds.Merge(meshBounds.Box);

            const Core::Float3 voxelExtents(_settings.VoxelSize, _settings.VoxelSize, _settings.VoxelSize);
            const Core::Float3 minCell = meshBounds.Box.Min;
            const Core::Float3 maxCell = meshBounds.Box.Max;
            const int32_t minX = static_cast<int32_t>(std::floor(minCell.X / _settings.VoxelSize));
            const int32_t minY = static_cast<int32_t>(std::floor(minCell.Y / _settings.VoxelSize));
            const int32_t minZ = static_cast<int32_t>(std::floor(minCell.Z / _settings.VoxelSize));
            const int32_t maxX = static_cast<int32_t>(std::floor(maxCell.X / _settings.VoxelSize));
            const int32_t maxY = static_cast<int32_t>(std::floor(maxCell.Y / _settings.VoxelSize));
            const int32_t maxZ = static_cast<int32_t>(std::floor(maxCell.Z / _settings.VoxelSize));

            for (int32_t z = minZ; z <= maxZ; ++z)
            {
                for (int32_t y = minY; y <= maxY; ++y)
                {
                    for (int32_t x = minX; x <= maxX; ++x)
                    {
                        const Core::Float3 voxelMin(
                            static_cast<float>(x) * _settings.VoxelSize,
                            static_cast<float>(y) * _settings.VoxelSize,
                            static_cast<float>(z) * _settings.VoxelSize);
                        const Core::Float3 voxelMax = voxelMin + voxelExtents;
                        const Core::AABB voxelBounds(voxelMin, voxelMax);

                        if (!voxelBounds.Intersects(meshBounds.Box) && !voxelBounds.Contains(meshBounds.Box))
                        {
                            continue;
                        }

                        SceneVoxelCell voxel;
                        voxel.Bounds = voxelBounds;
                        voxel.MeshId = meshIndex;
                        voxel.MaterialId = 0;
                        voxel.TriangleOffset = static_cast<uint32_t>(voxelPrimitives.size());
                        voxel.TriangleCount = 1;
                        voxel.Flags = 1;
                        voxel.Padding = 0;
                        voxel.Normal = NormalizeSafe(meshBounds.Box.GetCenter() - voxelMin);
                        voxel.BaseColor = Core::Float3(1.0f, 1.0f, 1.0f);
                        voxelPrimitives.push_back(voxel);
                    }
                }
            }
        }

        if (voxelPrimitives.empty())
        {
            return;
        }

        if (_settings.bAutoExpandBounds)
        {
            const Core::Float3 min = Core::Float3(
                std::min(_bounds.Min.X, _settings.WorldMin.X),
                std::min(_bounds.Min.Y, _settings.WorldMin.Y),
                std::min(_bounds.Min.Z, _settings.WorldMin.Z));
            const Core::Float3 max = Core::Float3(
                std::max(_bounds.Max.X, _settings.WorldMax.X),
                std::max(_bounds.Max.Y, _settings.WorldMax.Y),
                std::max(_bounds.Max.Z, _settings.WorldMax.Z));
            _bounds = Core::AABB(min, max);
        }

        _tree.SetMaxDepth(_settings.MaxDepth);
        _tree.SetMaxPrimitivesPerLeaf(_settings.MaxVoxelsPerLeaf);
        _tree.Build(voxelPrimitives);
        RebuildNodesAndVoxels();
    }

    const std::vector<SceneVoxelOctreeNode>& SceneVoxelOctree::GetNodes() const
    {
        return _nodes;
    }

    const std::vector<SceneVoxelCell>& SceneVoxelOctree::GetVoxels() const
    {
        return _voxels;
    }

    const Core::AABB& SceneVoxelOctree::GetBounds() const
    {
        return _bounds;
    }

    std::vector<SceneVoxelOctreeShaderNode> SceneVoxelOctree::FlattenForShader() const
    {
        std::vector<SceneVoxelOctreeShaderNode> flattenedNodes;
        flattenedNodes.reserve(_nodes.size());

        for (uint32_t nodeIndex = 0; nodeIndex < static_cast<uint32_t>(_nodes.size()); ++nodeIndex)
        {
            const SceneVoxelOctreeNode& node = _nodes[nodeIndex];
            SceneVoxelOctreeShaderNode flattenedNode;
            flattenedNode.MinBounds = node.Bounds.Min;
            flattenedNode.MaxBounds = node.Bounds.Max;
            flattenedNode.Parent = node.Parent;
            flattenedNode.Level = node.Level;
            flattenedNode.FirstVoxel = node.FirstVoxel;
            flattenedNode.VoxelCount = node.VoxelCount;
            flattenedNode.FirstChild = Core::OctreeInvalidNode;
            flattenedNode.ChildMask = 0;

            for (uint32_t childIndex = 0; childIndex < 8; ++childIndex)
            {
                if (node.Children[childIndex] != Core::OctreeInvalidNode)
                {
                    flattenedNode.ChildMask |= (1u << childIndex);
                    if (flattenedNode.FirstChild == Core::OctreeInvalidNode)
                    {
                        flattenedNode.FirstChild = node.Children[childIndex];
                    }
                }
            }

            flattenedNodes.push_back(flattenedNode);
        }

        return flattenedNodes;
    }

    Core::AABB SceneVoxelOctree::ComputeSceneBounds(const std::vector<SceneVoxelCell>& voxelPrimitives)
    {
        Core::AABB result;
        result.SetEmpty();

        for (const SceneVoxelCell& voxel : voxelPrimitives)
        {
            result.Merge(voxel.Bounds);
        }

        return result;
    }

    void SceneVoxelOctree::RebuildNodesAndVoxels()
    {
        const std::vector<Core::OctreeNode>& treeNodes = _tree.GetNodes();
        _nodes.clear();
        _nodes.resize(treeNodes.size());

        for (uint32_t index = 0; index < static_cast<uint32_t>(treeNodes.size()); ++index)
        {
            const Core::OctreeNode& treeNode = treeNodes[index];
            SceneVoxelOctreeNode& node = _nodes[index];
            node.Bounds = treeNode.Bounds.Box;
            node.Children = treeNode.Children;
            node.bIsLeaf = treeNode.bLeaf;
            node.Parent = Core::OctreeInvalidNode;
            node.Level = 0;
            node.FirstVoxel = Core::OctreeInvalidNode;
            node.VoxelCount = 0;
        }

        std::function<void(uint32_t, uint32_t)> buildHierarchy = [&](uint32_t nodeIndex, uint32_t parentIndex)
        {
            SceneVoxelOctreeNode& thisNode = _nodes[nodeIndex];
            thisNode.Parent = parentIndex;
            thisNode.Level = parentIndex == Core::OctreeInvalidNode ? 0u : _nodes[parentIndex].Level + 1u;

            for (uint32_t childIndex = 0; childIndex < 8; ++childIndex)
            {
                const uint32_t childNodeIndex = thisNode.Children[childIndex];
                if (childNodeIndex != Core::OctreeInvalidNode)
                {
                    buildHierarchy(childNodeIndex, nodeIndex);
                }
            }
        };

        if (!_nodes.empty())
        {
            buildHierarchy(0u, Core::OctreeInvalidNode);
        }

        _voxels.clear();
        for (uint32_t nodeIndex = 0; nodeIndex < static_cast<uint32_t>(_nodes.size()); ++nodeIndex)
        {
            SceneVoxelOctreeNode& node = _nodes[nodeIndex];
            if (!node.bIsLeaf)
            {
                continue;
            }

            const Core::OctreeNode& treeNode = treeNodes[nodeIndex];
            if (treeNode.PrimitiveCount == 0)
            {
                continue;
            }

            node.FirstVoxel = static_cast<uint32_t>(_voxels.size());
            node.VoxelCount = treeNode.PrimitiveCount;

            for (uint32_t primitiveOffset = 0; primitiveOffset < treeNode.PrimitiveCount; ++primitiveOffset)
            {
                const uint32_t primitiveIndex = _tree.GetPrimitiveIndices()[treeNode.FirstPrimitive + primitiveOffset];
                const SceneVoxelCell& primitive = _tree.GetPrimitive(primitiveIndex);
                _voxels.push_back(primitive);
            }
        }
    }

    void GlobalDiffuseLightMapVoxelizer::SetSettings(const SceneVoxelOctreeSettings& inSettings)
    {
        m_Settings = inSettings;
    }

    void GlobalDiffuseLightMapVoxelizer::VoxelizeStaticMeshes(const std::vector<const Engine::StaticMesh*>& meshes)
    {
        m_SceneOctree.BuildFromStaticMeshes(meshes, m_Settings);
    }

    const SceneVoxelOctree& GlobalDiffuseLightMapVoxelizer::GetSceneVoxelOctree() const
    {
        return m_SceneOctree;
    }

    void GlobalDiffuseLightMapVoxelizer::BuildShaderParameters(SceneVoxelOctreeShaderParameters& outParameters) const
    {
        const std::vector<SceneVoxelOctreeShaderNode> flattenedNodes = m_SceneOctree.FlattenForShader();
        outParameters.NodeCount = static_cast<uint32_t>(flattenedNodes.size());
        outParameters.VoxelCount = static_cast<uint32_t>(m_SceneOctree.GetVoxels().size());
        outParameters.MaxDepth = m_Settings.MaxDepth;
        outParameters.SceneMin = m_SceneOctree.GetBounds().Min;
        outParameters.VoxelSize = m_Settings.VoxelSize;
        outParameters.Resolution = Core::UInt3(
            static_cast<unsigned int>(m_Settings.TargetResolution),
            static_cast<unsigned int>(m_Settings.TargetResolution),
            static_cast<unsigned int>(m_Settings.TargetResolution));
        //OutParameters.Nodes = FlattenedNodes;
        //OutParameters.Voxels = SceneOctree.GetVoxels();
    }

    GlobalDiffuseLightMap::GlobalDiffuseLightMap(SceneVoxelOctreeSettings inSettings)
        : m_Settings(std::move(inSettings))
    {
    }

    void GlobalDiffuseLightMap::SetSettings(const SceneVoxelOctreeSettings& inSettings)
    {
        m_Settings = inSettings;
    }

    void GlobalDiffuseLightMap::VoxelizeStaticMeshes(const std::vector<const Engine::StaticMesh*>& meshes)
    {
        m_SceneOctree.BuildFromStaticMeshes(meshes, m_Settings);
    }

    const SceneVoxelOctree& GlobalDiffuseLightMap::GetSceneVoxelOctree() const
    {
        return m_SceneOctree;
    }

    void GlobalDiffuseLightMap::BuildShaderParameters(SceneVoxelOctreeShaderParameters& outParameters) const
    {
        const std::vector<SceneVoxelOctreeShaderNode> flattenedNodes = m_SceneOctree.FlattenForShader();
        outParameters.NodeCount = static_cast<uint32_t>(flattenedNodes.size());
        outParameters.VoxelCount = static_cast<uint32_t>(m_SceneOctree.GetVoxels().size());
        outParameters.MaxDepth = m_Settings.MaxDepth;
        outParameters.SceneMin = m_SceneOctree.GetBounds().Min;
        outParameters.VoxelSize = m_Settings.VoxelSize;
        outParameters.Resolution = Core::UInt3(
            static_cast<unsigned int>(m_Settings.TargetResolution),
            static_cast<unsigned int>(m_Settings.TargetResolution),
            static_cast<unsigned int>(m_Settings.TargetResolution));
        //OutParameters.Nodes = FlattenedNodes;
        //OutParameters.Voxels = SceneOctree.GetVoxels();
    }

    void ExecuteGlobalDiffuseLightMapInit(RenderCore::RenderGraphBuilder& graphBuilder, const GlobalDiffuseLightMapInitExecuteInput& input)
    {
        if (!input.EnvironmentMap || !input.NodesBuffer || !input.VoxelsBuffer || !input.InitialDiffuseBuffer)
        {
            return;
        }

        SceneVoxelDiffuseInitParameters params;
        params.NodeCount = input.NodeCount;
        params.VoxelCount = input.VoxelCount;
        params.MaxDepth = input.MaxDepth;
        params.SceneMin = input.Settings.WorldMin;
        params.VoxelSize = input.Settings.VoxelSize;
        params.Resolution = Core::UInt3(
            static_cast<unsigned int>(input.Settings.TargetResolution),
            static_cast<unsigned int>(input.Settings.TargetResolution),
            static_cast<unsigned int>(input.Settings.TargetResolution));
        //Params.Nodes = Input.NodesBuffer;
        //Params.Voxels = Input.VoxelsBuffer;
        params.EnvironmentMap = input.EnvironmentMap;
        params.EnvironmentMapSampler = RenderCore::GlobalSampler.get();
        //Params.InitialDiffuse = Input.InitialDiffuseBuffer;

        auto& gShaderMap = RenderCore::GShaderMap;
        auto shaderType = RenderCore::ShaderType::GetRegisterMap()[RenderCore::ShaderType::EShaderTypeFlag::Global]["GlobalDiffuseLightMapInitCS"];
        auto shader = gShaderMap.GetShader(shaderType, 0);
        if (!shader)
        {
            return;
        }

        auto computeContext = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->AcquireCastedCommandContext<RHI::RHIComputeContex>();
        RHI::RHIComputeCommandList cmdList(computeContext);
        cmdList.SetImmediate(true);
        cmdList.Begin();

        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        auto pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
        cmdList.SetComputePipelineState(pipelineState);

        SetShaderParameters(cmdList, shader, SceneVoxelDiffuseInitParameters::GetMetaData(), &params);

        const uint32_t dispatchCount = std::max<uint32_t>(1u, input.VoxelCount);
        const uint32_t groupX = (dispatchCount + 63u) / 64u;
        cmdList.Dispatch(groupX, 1u, 1u);

        cmdList.End();
        auto fence = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->ExecuteContext(computeContext);
        (void)fence;
    }

    void ExecuteGlobalDiffuseLightMapDiffuse(RenderCore::RenderGraphBuilder& graphBuilder, const GlobalDiffuseLightMapDiffuseExecuteInput& input)
    {
        (void)graphBuilder;

        if (!input.NodesBuffer || !input.VoxelsBuffer || !input.InputDiffuseBuffer || !input.OutputDiffuseBuffer || !input.DiffusionScratchBuffer)
        {
            return;
        }

        SceneVoxelDiffusePropagationParameters params;
        params.NodeCount = input.NodeCount;
        params.VoxelCount = input.VoxelCount;
        params.MaxDepth = input.MaxDepth;
        params.SceneMin = input.Settings.WorldMin;
        params.VoxelSize = input.Settings.VoxelSize;
        params.Resolution = Core::UInt3(
            static_cast<unsigned int>(input.Settings.TargetResolution),
            static_cast<unsigned int>(input.Settings.TargetResolution),
            static_cast<unsigned int>(input.Settings.TargetResolution));
        //Params.Nodes = Input.NodesBuffer;
        //Params.Voxels = Input.VoxelsBuffer;
        //Params.InputDiffuse = Input.InputDiffuseBuffer;
        //Params.OutputDiffuse = Input.OutputDiffuseBuffer;
        //Params.DiffusionScratch = Input.DiffusionScratchBuffer;

        auto& gShaderMap = RenderCore::GShaderMap;
        auto shaderType = RenderCore::ShaderType::GetRegisterMap()[RenderCore::ShaderType::EShaderTypeFlag::Global]["GlobalDiffuseLightMapDiffuseCS"];
        auto shader = gShaderMap.GetShader(shaderType, 0);
        if (!shader)
        {
            return;
        }

        auto computeContext = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->AcquireCastedCommandContext<RHI::RHIComputeContex>();
        RHI::RHIComputeCommandList cmdList(computeContext);
        cmdList.SetImmediate(true);
        cmdList.Begin();

        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        auto pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
        cmdList.SetComputePipelineState(pipelineState);

        SetShaderParameters(cmdList, shader, SceneVoxelDiffusePropagationParameters::GetMetaData(), &params);

        const uint32_t dispatchCount = std::max<uint32_t>(1u, input.VoxelCount);
        const uint32_t groupX = (dispatchCount + 63u) / 64u;
        cmdList.Dispatch(groupX, 1u, 1u);

        cmdList.End();
        auto fence = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute)->ExecuteContext(computeContext);
        (void)fence;
    }
}
