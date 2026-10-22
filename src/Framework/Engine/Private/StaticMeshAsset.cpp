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
StaticMeshAsset::StaticMeshAsset(const std::string& path) : m_Name(Core::GetFileName(path)), m_Path(path) {}
    namespace
    {
        template<typename T>
        const T* GetBufferData(
            const tinygltf::Model& model,
            const tinygltf::Accessor& accessor)
        {
            const auto& bufferView =
                model.bufferViews[accessor.bufferView];

            const auto& buffer =
                model.buffers[bufferView.buffer];

            const uint8_t* data =
                buffer.data.data()
                + bufferView.byteOffset
                + accessor.byteOffset;

            return reinterpret_cast<const T*>(data);
        }

        void ReadVec3Attribute(
            const tinygltf::Model& model,
            int accessorIndex,
            VertexBuffer& outBuffer)
        {
            if (accessorIndex < 0)
            {
                return;
            }

            const auto& accessor =
                model.accessors[accessorIndex];

            const float* data =
                GetBufferData<float>(model, accessor);

            outBuffer.NumComponents = 3;
            outBuffer.bValid = true;

            outBuffer.Vertices.reserve(
                outBuffer.Vertices.size()
                + accessor.count * 3);

            for (size_t i = 0; i < accessor.count; ++i)
            {
                outBuffer.Vertices.push_back(data[i * 3 + 0]);
                outBuffer.Vertices.push_back(data[i * 3 + 1]);
                outBuffer.Vertices.push_back(data[i * 3 + 2]);
            }
        }

        void ReadVec2Attribute(
            const tinygltf::Model& model,
            int accessorIndex,
            VertexBuffer& outBuffer)
        {
            if (accessorIndex < 0)
            {
                return;
            }

            const auto& accessor =
                model.accessors[accessorIndex];

            const float* data =
                GetBufferData<float>(model, accessor);

            outBuffer.NumComponents = 2;
            outBuffer.bValid = true;

            outBuffer.Vertices.reserve(
                outBuffer.Vertices.size()
                + accessor.count * 2);

            for (size_t i = 0; i < accessor.count; ++i)
            {
                outBuffer.Vertices.push_back(data[i * 2 + 0]);
                outBuffer.Vertices.push_back(data[i * 2 + 1]);
            }
        }
    }

    static StaticMeshSP LoadMesh(const std::string& path)
    {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        bool ret = false;
        if (path.ends_with(".glb")) {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, path.c_str());
        }
        else {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, path.c_str());
        }

        if (!warn.empty())
            std::cout << "GLTF Warning: " << warn << std::endl;
        if (!err.empty())
            std::cerr << "GLTF Error: " << err << std::endl;
        if (!ret) return nullptr;

        auto renderData = std::make_shared<StaticMeshRenderData>();
        auto staticMesh = std::make_shared<StaticMesh>(renderData);

        // ------------------- Materials -------------------
        std::vector<std::shared_ptr<MaterialAsset>> materials;
        materials.reserve(model.materials.size() + 1);

        for (size_t i = 0; i < model.materials.size(); ++i) {
            
            const auto& mat = model.materials[i];
            auto m = std::make_unique<Material>();
            m->SetBlendMode(EBlendMode::Opaque);
            m->SetShadingModel(EShadingModel::Lit);

            // BaseColor
            if (!mat.pbrMetallicRoughness.baseColorFactor.empty()) {
                std::array<float, 4> color = { 1,1,1,1 };
                for (int c = 0; c < 4; ++c)
                    color[c] = float(mat.pbrMetallicRoughness.baseColorFactor[c]);
                m->SetParameterValue<EMaterialParameterSemantic::BaseColor, FVectorType>("BaseColor", color);
            }

            // Metallic / Roughness
            m->SetParameterValue<EMaterialParameterSemantic::Metallic, FScalarType>("Metallic", float(mat.pbrMetallicRoughness.metallicFactor));
            m->SetParameterValue<EMaterialParameterSemantic::Roughness, FScalarType>("Roughness", float(mat.pbrMetallicRoughness.roughnessFactor));

            // Alpha mode
            if (mat.alphaMode == "OPAQUE") m->SetBlendMode(EBlendMode::Opaque);
            else if (mat.alphaMode == "MASK") m->SetBlendMode(EBlendMode::Masked);
            else if (mat.alphaMode == "BLEND") m->SetBlendMode(EBlendMode::Translucent);
            materials.push_back(MaterialAsset::Create(
                path + "#material/" + std::to_string(i), mat.name, std::move(m)));
        }
        // Missing primitive material references explicitly depend on the default asset.
        int defaultMaterialIndex = -1;
        for (const auto& sourceMesh : model.meshes)
            for (const auto& primitive : sourceMesh.primitives)
                if (primitive.material < 0 || static_cast<size_t>(primitive.material) >= model.materials.size())
                    defaultMaterialIndex = static_cast<int>(materials.size());
        if (defaultMaterialIndex >= 0)
            materials.push_back(AssetManager::Get().LoadSync<MaterialAsset>(
                Core::GetProjectDir() + "/resources/material/DefaultWhite/material.json"));

        // ------------------- Mesh LODs -------------------
        for (const auto& mesh : model.meshes) {
            LODResource lod;
            Core::BoxSphereBounds& bounds = renderData->Bounds;
            for (const auto& prim : mesh.primitives) {
                // Positions
                size_t vertexCount = 0;
                
                if (prim.attributes.count("POSITION")) {
                    const auto& accessor = model.accessors[prim.attributes.at("POSITION")];
                    vertexCount = accessor.count;

                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buf = model.buffers[view.buffer];
                    const float* data = reinterpret_cast<const float*>(&buf.data[view.byteOffset + accessor.byteOffset]);
                    lod.VertexBuffers.PositionBuffer.Vertices.assign(data, data + vertexCount * 3);
                    lod.VertexBuffers.PositionBuffer.NumComponents = 3;
                    lod.VertexBuffers.PositionBuffer.bValid = true;
                    for (size_t i = 0; i < vertexCount; ++i)
                    {
                        Core::Float3 v = Core::Float3(data[3*i], data[3 * i + 1], data[3 * i + 2]);
                        bounds.ExpandBy(v);
                    }


                }

                // Normal
                if (prim.attributes.count("NORMAL")) {
                    const auto& accessor = model.accessors[prim.attributes.at("NORMAL")];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buf = model.buffers[view.buffer];
                    const float* data = reinterpret_cast<const float*>(&buf.data[view.byteOffset + accessor.byteOffset]);
                    lod.VertexBuffers.NormalBuffer.Vertices.assign(data, data + vertexCount * 3);
                    lod.VertexBuffers.NormalBuffer.NumComponents = 3;
                    lod.VertexBuffers.NormalBuffer.bValid = true;
                }

                // UV
                if (prim.attributes.count("TEXCOORD_0")) {
                    const auto& accessor = model.accessors[prim.attributes.at("TEXCOORD_0")];
                    const auto& view = model.bufferViews[accessor.bufferView];
                    const auto& buf = model.buffers[view.buffer];
                    const float* data = reinterpret_cast<const float*>(&buf.data[view.byteOffset + accessor.byteOffset]);
                    lod.VertexBuffers.UVBuffer.Vertices.assign(data, data + vertexCount * 2);
                    lod.VertexBuffers.UVBuffer.NumComponents = 2;
                    lod.VertexBuffers.UVBuffer.bValid = true;
                }

                // Indices
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
                        lod.IndexBuffer.Indices.push_back(idx);
                    }
                }

                // Section
                SectionInfo section;
                section.FirstIndex = 0; // TODO: Accumulate section index offsets.
                section.NumIndices = lod.IndexBuffer.Indices.size();
                section.MaterialIndex = prim.material;
                if (prim.material < 0 || static_cast<size_t>(prim.material) >= model.materials.size())
                    section.MaterialIndex = defaultMaterialIndex;
                lod.Sections.push_back(section);
            }
            lod.InitializeResources();
			
            renderData->AddLOD(std::move(lod));
        }
        GDistanceFieldMgr.BuildMeshDistanceField(*renderData.get());
        for (auto materil : materials) {
			staticMesh->AddMaterial(materil);
        }
        return staticMesh;
    }
	bool StaticMeshAsset::Load()
	{
		m_Mesh = LoadMesh(m_Path);
        if (m_Mesh) m_Mesh->SetAsset(std::static_pointer_cast<StaticMeshAsset>(shared_from_this()));
        return m_Mesh != nullptr;
	}
}
