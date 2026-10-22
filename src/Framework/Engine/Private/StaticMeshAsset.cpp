#include "StaticMeshAsset.h"
#include "AssetManager.h"
#include "StaticMesh.h"
#include "Material.h"
#include "EngineGlobal.h"
#include "DistanceFieldMgr.h"
#include "PathInfo.h"
#include "tiny_gltf.h"
#include <iostream>

using namespace RenderCore;
namespace Engine {
StaticMeshAsset::StaticMeshAsset(const std::string& path) : Name(Core::GetFileName(path)), Path(path) {}
    namespace
    {
        template<typename T>
        const T* GetBufferData(
            const tinygltf::Model& Model,
            const tinygltf::Accessor& Accessor)
        {
            const auto& BufferView =
                Model.bufferViews[Accessor.bufferView];

            const auto& Buffer =
                Model.buffers[BufferView.buffer];

            const uint8_t* Data =
                Buffer.data.data()
                + BufferView.byteOffset
                + Accessor.byteOffset;

            return reinterpret_cast<const T*>(Data);
        }

        void ReadVec3Attribute(
            const tinygltf::Model& Model,
            int AccessorIndex,
            VertexBuffer& OutBuffer)
        {
            if (AccessorIndex < 0)
            {
                return;
            }

            const auto& Accessor =
                Model.accessors[AccessorIndex];

            const float* Data =
                GetBufferData<float>(Model, Accessor);

            OutBuffer.NumComponents = 3;
            OutBuffer.Valid = true;

            OutBuffer.Vertices.reserve(
                OutBuffer.Vertices.size()
                + Accessor.count * 3);

            for (size_t i = 0; i < Accessor.count; ++i)
            {
                OutBuffer.Vertices.push_back(Data[i * 3 + 0]);
                OutBuffer.Vertices.push_back(Data[i * 3 + 1]);
                OutBuffer.Vertices.push_back(Data[i * 3 + 2]);
            }
        }

        void ReadVec2Attribute(
            const tinygltf::Model& Model,
            int AccessorIndex,
            VertexBuffer& OutBuffer)
        {
            if (AccessorIndex < 0)
            {
                return;
            }

            const auto& Accessor =
                Model.accessors[AccessorIndex];

            const float* Data =
                GetBufferData<float>(Model, Accessor);

            OutBuffer.NumComponents = 2;
            OutBuffer.Valid = true;

            OutBuffer.Vertices.reserve(
                OutBuffer.Vertices.size()
                + Accessor.count * 2);

            for (size_t i = 0; i < Accessor.count; ++i)
            {
                OutBuffer.Vertices.push_back(Data[i * 2 + 0]);
                OutBuffer.Vertices.push_back(Data[i * 2 + 1]);
            }
        }
    }

    static StaticMeshSP LoadMesh(const std::string& Path)
    {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        bool ret = false;
        if (Path.ends_with(".glb")) {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, Path.c_str());
        }
        else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, Path.c_str());
        }

        if (!warn.empty())
            std::cout << "GLTF Warning: " << warn << std::endl;
        if (!err.empty())
            std::cerr << "GLTF Error: " << err << std::endl;
        if (!ret) return nullptr;

        auto RenderData = std::make_shared<StaticMeshRenderData>();
        auto staticMesh = std::make_shared<StaticMesh>(RenderData);

        // ------------------- ���� -------------------
        std::vector<std::shared_ptr<MaterialAsset>> Materials;
        Materials.reserve(model.materials.size() + 1);

        for (size_t i = 0; i < model.materials.size(); ++i) {
            
            const auto& mat = model.materials[i];
            auto M = std::make_unique<Material>();
            M->SetBlendMode(EBlendMode::Opaque);
            M->SetShadingModel(EShadingModel::Lit);

            // BaseColor
            if (!mat.pbrMetallicRoughness.baseColorFactor.empty()) {
                std::array<float, 4> color = { 1,1,1,1 };
                for (int c = 0; c < 4; ++c)
                    color[c] = float(mat.pbrMetallicRoughness.baseColorFactor[c]);
                M->SetParameterValue<EMaterialParameterSemantic::BaseColor, FVectorType>("BaseColor", color);
            }

            // Metallic / Roughness
            M->SetParameterValue<EMaterialParameterSemantic::Metallic, FScalarType>("Metallic", float(mat.pbrMetallicRoughness.metallicFactor));
            M->SetParameterValue<EMaterialParameterSemantic::Roughness, FScalarType>("Roughness", float(mat.pbrMetallicRoughness.roughnessFactor));

            // Alpha mode
            if (mat.alphaMode == "OPAQUE") M->SetBlendMode(EBlendMode::Opaque);
            else if (mat.alphaMode == "MASK") M->SetBlendMode(EBlendMode::Masked);
            else if (mat.alphaMode == "BLEND") M->SetBlendMode(EBlendMode::Translucent);
            Materials.push_back(MaterialAsset::Create(
                Path + "#material/" + std::to_string(i), mat.name, std::move(M)));
        }
        // Missing primitive material references explicitly depend on the default asset.
        int defaultMaterialIndex = -1;
        for (const auto& sourceMesh : model.meshes)
            for (const auto& primitive : sourceMesh.primitives)
                if (primitive.material < 0 || static_cast<size_t>(primitive.material) >= model.materials.size())
                    defaultMaterialIndex = static_cast<int>(Materials.size());
        if (defaultMaterialIndex >= 0)
            Materials.push_back(AssetManager::Get().LoadSync<MaterialAsset>(
                Core::GetProjectDir() + "/resources/material/DefaultWhite/material.json"));

        // ------------------- Mesh LODs -------------------
        for (const auto& mesh : model.meshes) {
            LODResource LOD;
            Core::BoxSphereBounds& Bounds = RenderData->Bounds;
            for (const auto& prim : mesh.primitives) {
                // ����
                size_t vertexCount = 0;
                
                if (prim.attributes.count("POSITION")) {
                    const auto& accessor = model.accessors[prim.attributes.at("POSITION")];
                    vertexCount = accessor.count;

                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buf = model.buffers[view.buffer];
                    const float* data = reinterpret_cast<const float*>(&buf.data[view.byteOffset + accessor.byteOffset]);
                    LOD.VertexBuffers.PositionBuffer.Vertices.assign(data, data + vertexCount * 3);
                    LOD.VertexBuffers.PositionBuffer.NumComponents = 3;
                    LOD.VertexBuffers.PositionBuffer.Valid = true;
                    for (size_t i = 0; i < vertexCount; ++i)
                    {
                        Core::Float3 v = Core::Float3(data[3*i], data[3 * i + 1], data[3 * i + 2]);
                        Bounds.ExpandBy(v);
                    }


                }

                // Normal
                if (prim.attributes.count("NORMAL")) {
                    const auto& accessor = model.accessors[prim.attributes.at("NORMAL")];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buf = model.buffers[view.buffer];
                    const float* data = reinterpret_cast<const float*>(&buf.data[view.byteOffset + accessor.byteOffset]);
                    LOD.VertexBuffers.NormalBuffer.Vertices.assign(data, data + vertexCount * 3);
                    LOD.VertexBuffers.NormalBuffer.NumComponents = 3;
                    LOD.VertexBuffers.NormalBuffer.Valid = true;
                }

                // UV
                if (prim.attributes.count("TEXCOORD_0")) {
                    const auto& accessor = model.accessors[prim.attributes.at("TEXCOORD_0")];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buf = model.buffers[view.buffer];
                    const float* data = reinterpret_cast<const float*>(&buf.data[view.byteOffset + accessor.byteOffset]);
                    LOD.VertexBuffers.UVBuffer.Vertices.assign(data, data + vertexCount * 2);
                    LOD.VertexBuffers.UVBuffer.NumComponents = 2;
                    LOD.VertexBuffers.UVBuffer.Valid = true;
                }

                // ����
                if (prim.indices >= 0) {
                    const auto& accessor = model.accessors[prim.indices];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buf = model.buffers[view.buffer];
                    const void* ptr = &buf.data[view.byteOffset + accessor.byteOffset];
                    for (size_t i = 0; i < accessor.count; ++i) {
                        uint32_t idx = 0;
                        switch (accessor.componentType) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: idx = ((const uint16_t*)ptr)[i]; break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: idx = ((const uint32_t*)ptr)[i]; break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: idx = ((const uint8_t*)ptr)[i]; break;
                        }
                        LOD.IndexBuffer.Indices.push_back(idx);
                    }
                }

                // Section
                SectionInfo section;
                section.FirstIndex = 0; // TODO: �ۼ�
                section.NumIndices = LOD.IndexBuffer.Indices.size();
                section.MaterialIndex = prim.material;
                if (prim.material < 0 || static_cast<size_t>(prim.material) >= model.materials.size())
                    section.MaterialIndex = defaultMaterialIndex;
                LOD.Sections.push_back(section);
            }
            LOD.InitializeResources();
			
            RenderData->AddLOD(std::move(LOD));
        }
        GDistanceFieldMgr.BuildMeshDistanceField(*RenderData.get());
        for (auto materil : Materials) {
			staticMesh->AddMaterial(materil);
        }
        return staticMesh;
    }
	bool StaticMeshAsset::Load()
	{
		Mesh = LoadMesh(Path);
        if (Mesh) Mesh->SetAsset(std::static_pointer_cast<StaticMeshAsset>(shared_from_this()));
        return Mesh != nullptr;
	}
}
