#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "GlobalShader.h"
#include "Math.hpp"
#include "BoxSphereBounds.h"
#include "Octree.hpp"
#include "ShaderParameter.h"
#include "StaticMesh.h"
#include "RenderGraphBuilder.h"

namespace Renderer
{
    struct SceneVoxelOctreeSettings
    {
        Core::Float3 WorldMin = Core::Float3(-128.0f, -128.0f, -128.0f);
        Core::Float3 WorldMax = Core::Float3(128.0f, 128.0f, 128.0f);
        uint32_t MaxDepth = 6;
        uint32_t MaxVoxelsPerLeaf = 8;
        uint32_t TargetResolution = 64;
        float VoxelSize = 0.25f;
        bool bAutoExpandBounds = true;
    };

    struct SceneVoxelCell
    {
        Core::AABB Bounds;
        Core::Float3 Normal = Core::Float3(0.0f, 1.0f, 0.0f);
        Core::Float3 BaseColor = Core::Float3(1.0f, 1.0f, 1.0f);
        uint32_t MeshId = 0xFFFFFFFFu;
        uint32_t MaterialId = 0xFFFFFFFFu;
        uint32_t TriangleOffset = 0;
        uint32_t TriangleCount = 0;
        uint32_t Flags = 0;
        uint32_t Padding = 0;
    };

    struct SceneVoxelCellTraits
    {
        static Core::BoxSphereBounds GetBounds(const SceneVoxelCell& primitive)
        {
            return Core::BoxSphereBounds(primitive.Bounds);
        }

        static Core::Float3 GetCentroid(const SceneVoxelCell& primitive)
        {
            return primitive.Bounds.GetCenter();
        }

        static bool RayIntersect(const SceneVoxelCell& primitive, const Core::Ray& ray, float& outDistance)
        {
            const bool hit = ray.RayIntersectAABB(primitive.Bounds);
            if (!hit)
            {
                outDistance = 0.0f;
                return false;
            }

            const float distance = primitive.Bounds.Distance(ray.Origin);
            outDistance = distance;
            return true;
        }

        static Core::Float3 ClosestPoint(const SceneVoxelCell& primitive, const Core::Float3& position)
        {
            return Core::Float3();
            //return Primitive.Bounds.GetClosestPoint(Position);
        }

        static float ClosestDistance(const SceneVoxelCell& primitive, const Core::Float3& position)
        {
            return primitive.Bounds.Distance(position);
        }
    };

    using SceneVoxelOctreeTemplate = Core::Octree<SceneVoxelCell, SceneVoxelCellTraits>;

    struct SceneVoxelOctreeNode
    {
        Core::AABB Bounds;
        std::array<uint32_t, 8> Children;
        uint32_t FirstVoxel = Core::OctreeInvalidNode;
        uint32_t VoxelCount = 0;
        uint32_t Parent = Core::OctreeInvalidNode;
        uint32_t Level = 0;
        bool bIsLeaf = true;

        SceneVoxelOctreeNode()
        {
            Children.fill(Core::OctreeInvalidNode);
        }
    };

    BEGIN_SHADER_PARAMETER_STRUCT(SceneVoxelCellBounds)
        SHADER_PARAMETER(Core::Float3, Min)
        SHADER_PARAMETER(Core::Float3, Max)
    END_SHADER_PARAMETER_STRUCT(SceneVoxelCellBounds)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneVoxelOctreeShaderNode)
        SHADER_PARAMETER(Core::Float3, MinBounds)
        SHADER_PARAMETER(Core::Float3, MaxBounds)
        SHADER_PARAMETER(uint32_t, ChildMask)
        SHADER_PARAMETER(uint32_t, FirstChild)
        SHADER_PARAMETER(uint32_t, Parent)
        SHADER_PARAMETER(uint32_t, FirstVoxel)
        SHADER_PARAMETER(uint32_t, VoxelCount)
        SHADER_PARAMETER(uint32_t, Level)
        SHADER_PARAMETER(uint32_t, Padding)
    END_SHADER_PARAMETER_STRUCT(SceneVoxelOctreeShaderNode)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneVoxelCellShaderData)
        SHADER_PARAMETER_STRUCT_NESTED(SceneVoxelCellBounds, Bounds)
        SHADER_PARAMETER(Core::Float3, Normal)
        SHADER_PARAMETER(Core::Float3, BaseColor)
        SHADER_PARAMETER(uint32_t, MeshId)
        SHADER_PARAMETER(uint32_t, MaterialId)
        SHADER_PARAMETER(uint32_t, TriangleOffset)
        SHADER_PARAMETER(uint32_t, TriangleCount)
        SHADER_PARAMETER(uint32_t, Flags)
        SHADER_PARAMETER(uint32_t, Padding)
    END_SHADER_PARAMETER_STRUCT(SceneVoxelCellShaderData)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneVoxelLeafIrradiance)
        SHADER_PARAMETER(Core::Float3, Irradiance)
        SHADER_PARAMETER(float, Weight)
    END_SHADER_PARAMETER_STRUCT(SceneVoxelLeafIrradiance)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneVoxelOctreeShaderParameters)
        SHADER_PARAMETER(uint32_t, NodeCount)
        SHADER_PARAMETER(uint32_t, VoxelCount)
        SHADER_PARAMETER(uint32_t, MaxDepth)
        SHADER_PARAMETER(Core::Float3, SceneMin)
        SHADER_PARAMETER(float, VoxelSize)
        SHADER_PARAMETER(Core::UInt3, Resolution)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SceneVoxelOctreeShaderNode, Nodes)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SceneVoxelCellShaderData, Voxels)
    END_SHADER_PARAMETER_STRUCT(SceneVoxelOctreeShaderParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneVoxelDiffuseInitParameters)
        SHADER_PARAMETER(uint32_t, NodeCount)
        SHADER_PARAMETER(uint32_t, VoxelCount)
        SHADER_PARAMETER(uint32_t, MaxDepth)
        SHADER_PARAMETER(Core::Float3, SceneMin)
        SHADER_PARAMETER(float, VoxelSize)
        SHADER_PARAMETER(Core::UInt3, Resolution)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SceneVoxelOctreeShaderNode, Nodes)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SceneVoxelCellShaderData, Voxels)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube, EnvironmentMap)
        SHADER_PARAMETER_SAMPLER(EnvironmentMapSampler)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SceneVoxelLeafIrradiance, InitialDiffuse)
    END_SHADER_PARAMETER_STRUCT(SceneVoxelDiffuseInitParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneVoxelDiffusePropagationParameters)
        SHADER_PARAMETER(uint32_t, NodeCount)
        SHADER_PARAMETER(uint32_t, VoxelCount)
        SHADER_PARAMETER(uint32_t, MaxDepth)
        SHADER_PARAMETER(Core::Float3, SceneMin)
        SHADER_PARAMETER(float, VoxelSize)
        SHADER_PARAMETER(Core::UInt3, Resolution)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SceneVoxelOctreeShaderNode, Nodes)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SceneVoxelCellShaderData, Voxels)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SceneVoxelLeafIrradiance, InputDiffuse)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SceneVoxelLeafIrradiance, OutputDiffuse)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SceneVoxelLeafIrradiance, DiffusionScratch)
    END_SHADER_PARAMETER_STRUCT(SceneVoxelDiffusePropagationParameters)

    class SceneVoxelOctree
    {
    public:
        SceneVoxelOctree() = default;

        void Clear();

        void BuildFromStaticMeshes(
            const std::vector<const Engine::StaticMesh*>& meshes,
            const SceneVoxelOctreeSettings& settings = SceneVoxelOctreeSettings());

        const std::vector<SceneVoxelOctreeNode>& GetNodes() const;
        const std::vector<SceneVoxelCell>& GetVoxels() const;
        const Core::AABB& GetBounds() const;
        std::vector<SceneVoxelOctreeShaderNode> FlattenForShader() const;

    protected:
        static Core::AABB ComputeSceneBounds(const std::vector<SceneVoxelCell>& voxelPrimitives);
        void RebuildNodesAndVoxels();

        SceneVoxelOctreeSettings _settings;
        SceneVoxelOctreeTemplate _tree;
        std::vector<SceneVoxelOctreeNode> _nodes;
        std::vector<SceneVoxelCell> _voxels;
        Core::AABB _bounds;
    };

    class GlobalDiffuseLightMapVoxelizer
    {
    public:
        void SetSettings(const SceneVoxelOctreeSettings& inSettings);
        void VoxelizeStaticMeshes(const std::vector<const Engine::StaticMesh*>& meshes);
        const SceneVoxelOctree& GetSceneVoxelOctree() const;
        void BuildShaderParameters(SceneVoxelOctreeShaderParameters& outParameters) const;

    private:
        SceneVoxelOctreeSettings m_Settings;
        SceneVoxelOctree m_SceneOctree;
    };

    class GlobalDiffuseLightMap
    {
    public:
        explicit GlobalDiffuseLightMap(SceneVoxelOctreeSettings inSettings = SceneVoxelOctreeSettings());

        void SetSettings(const SceneVoxelOctreeSettings& inSettings);
        void VoxelizeStaticMeshes(const std::vector<const Engine::StaticMesh*>& meshes);
        const SceneVoxelOctree& GetSceneVoxelOctree() const;
        void BuildShaderParameters(SceneVoxelOctreeShaderParameters& outParameters) const;

    private:
        SceneVoxelOctreeSettings m_Settings;
        SceneVoxelOctree m_SceneOctree;
    };

    class GlobalDiffuseLightMapInitCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(GlobalDiffuseLightMapInitCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return SceneVoxelDiffuseInitParameters::GetMetaData();
        }
    };

    class GlobalDiffuseLightMapDiffuseCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(GlobalDiffuseLightMapDiffuseCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return SceneVoxelDiffusePropagationParameters::GetMetaData();
        }
    };

    struct GlobalDiffuseLightMapInitExecuteInput
    {
        RenderCore::RenderGraphTextureRef EnvironmentMap = nullptr;
        RenderCore::RenderGraphBufferRef NodesBuffer = nullptr;
        RenderCore::RenderGraphBufferRef VoxelsBuffer = nullptr;
        RenderCore::RenderGraphBufferRef InitialDiffuseBuffer = nullptr;
        uint32_t NodeCount = 0;
        uint32_t VoxelCount = 0;
        uint32_t MaxDepth = 0;
        SceneVoxelOctreeSettings Settings = SceneVoxelOctreeSettings();
    };

    struct GlobalDiffuseLightMapDiffuseExecuteInput
    {
        RenderCore::RenderGraphBufferRef NodesBuffer = nullptr;
        RenderCore::RenderGraphBufferRef VoxelsBuffer = nullptr;
        RenderCore::RenderGraphBufferRef InputDiffuseBuffer = nullptr;
        RenderCore::RenderGraphBufferRef OutputDiffuseBuffer = nullptr;
        RenderCore::RenderGraphBufferRef DiffusionScratchBuffer = nullptr;
        uint32_t NodeCount = 0;
        uint32_t VoxelCount = 0;
        uint32_t MaxDepth = 0;
        SceneVoxelOctreeSettings Settings = SceneVoxelOctreeSettings();
    };

    RENDERER_API void ExecuteGlobalDiffuseLightMapInit(RenderCore::RenderGraphBuilder& graphBuilder, const GlobalDiffuseLightMapInitExecuteInput& input);
    RENDERER_API void ExecuteGlobalDiffuseLightMapDiffuse(RenderCore::RenderGraphBuilder& graphBuilder, const GlobalDiffuseLightMapDiffuseExecuteInput& input);
}

