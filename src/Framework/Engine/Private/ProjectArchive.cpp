#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ProjectArchive.h"
#include "StaticMeshAsset.h"
#include "StaticMeshComponent.h"
#include "MaterialAsset.h"
#include "Material.h"
#include "TextureAsset.h"
#include "EnvironmentLightResourceManager.h"
#include "LightComponent.h"
#include "DistanceFieldMgr.h"
#include "EngineGlobal.h"
#include "RenderResource.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Engine
{
    namespace
    {
        using Json = nlohmann::json;
        namespace fs = std::filesystem;
        static_assert(std::endian::native == std::endian::little && sizeof(float) == 4);
        constexpr uint64_t HeaderSize = 8;

        void Require(bool condition, const std::string& message)
        { if (!condition) throw std::runtime_error("Project: " + message); }
        fs::path Inside(const fs::path& root, const std::string& text)
        {
            const auto path = fs::u8path(text);
            Require(!path.empty() && !path.is_absolute(), "expected a relative data path");
            const auto result = fs::weakly_canonical(root / path);
            const auto relative = result.lexically_relative(fs::weakly_canonical(root));
            Require(!relative.empty() && !relative.is_absolute() && *relative.begin() != "..", "data path leaves project directory");
            return result;
        }
        Json ReadJson(const fs::path& path)
        {
            Require(fs::file_size(path) <= 128ull * 1024 * 1024, "JSON description is too large");
            std::ifstream input(path, std::ios::binary);
            Require(bool(input), "cannot open description");
            auto result = Json::parse(input);
            Require(result.is_object(), "description must be an object");
            return result;
        }
        void WriteJson(const fs::path& path, const Json& data)
        {
            std::ofstream output(path, std::ios::binary);
            Require(bool(output) && bool(output << data.dump(2) << '\n'), "cannot write description");
            output.close(); Require(bool(output), "cannot flush description");
        }
        void Publish(const fs::path& temporary, const fs::path& target)
        {
#ifdef _WIN32
            Require(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "cannot publish project description (error " + std::to_string(GetLastError()) + ")");
#else
            fs::rename(temporary, target);
#endif
        }
        float Number(const Json& value)
        {
            Require(value.is_number(), "expected a number");
            const auto number = value.get<float>(); Require(std::isfinite(number), "non-finite number"); return number;
        }
        Core::Float3 Vector3(const Json& value)
        {
            Require(value.is_array() && value.size() == 3, "expected three vector components");
            return {Number(value[0]), Number(value[1]), Number(value[2])};
        }
        Json Vector3(const Core::Float3& value)
        {
            Require(std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z), "non-finite vector");
            return Json::array({value.X, value.Y, value.Z});
        }
        uint64_t Unsigned(const Json& value)
        {
            Require(value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0), "expected an unsigned integer");
            return value.get<uint64_t>();
        }
        uint32_t U32(const Json& value)
        {
            const auto result = Unsigned(value); Require(result <= UINT32_MAX, "integer exceeds 32 bits"); return static_cast<uint32_t>(result);
        }
        const char* TextureType(RHI::ERHITextureType type)
        {
            using T = RHI::ERHITextureType;
            switch (type) { case T::Texture2D: return "2D"; case T::Texture2DArray: return "2DArray"; case T::TextureCube: return "Cube";
            default: throw std::runtime_error("Project: unsupported texture type"); }
        }
        const RHI::FormatInfo& TextureFormat(RHI::ERHIFormat format)
        {
            const auto found = RHI::GFormatInfoMap.find(format);
            Require(found != RHI::GFormatInfoMap.end() && found->second.BytesPerPixel && !found->second.bIsDepth && !found->second.bIsStencil,
                "unsupported texture format");
            return found->second;
        }
        uint64_t TextureBytes(const RHI::RHITextureDesc& desc, uint32_t mip)
        {
            const uint64_t size = uint64_t(std::max(1u, desc.Width >> mip)) * std::max(1u, desc.Height >> mip) * TextureFormat(desc.Format).BytesPerPixel;
            Require(size <= UINT32_MAX, "texture subresource exceeds RHI upload limit"); return size;
        }
        double ReadChannel(const uint8_t* bytes, uint32_t size, bool floatingPoint)
        {
            if (!floatingPoint) return *bytes / 255.0;
            if (size == 4) {
                float value; std::memcpy(&value, bytes, sizeof(value));
                Require(std::isfinite(value), "non-finite texture channel"); return value;
            }
            uint16_t bits; std::memcpy(&bits, bytes, sizeof(bits));
            const auto exponent = (bits >> 10) & 31, fraction = bits & 1023;
            Require(exponent != 31, "non-finite half-float texture channel");
            const double value = exponent ? std::ldexp(double(1024 + fraction), int(exponent) - 25) : std::ldexp(double(fraction), -24);
            return bits & 0x8000 ? -value : value;
        }
        uint32_t RoundEven(double value)
        {
            auto integer = static_cast<uint32_t>(std::floor(value)); const auto fraction = value - integer;
            return integer + (fraction > 0.5 || (fraction == 0.5 && (integer & 1)) ? 1 : 0);
        }
        void WriteChannel(uint8_t* bytes, uint32_t size, bool floatingPoint, double value)
        {
            if (!floatingPoint) { *bytes = static_cast<uint8_t>(RoundEven(std::clamp(value, 0.0, 1.0) * 255.0)); return; }
            if (size == 4) { const float result = static_cast<float>(value); std::memcpy(bytes, &result, sizeof(result)); return; }
            const uint16_t sign = std::signbit(value) ? 0x8000 : 0;
            value = std::abs(value); int exponent = 0; std::frexp(value, &exponent);
            // IEEE binary16, round-to-nearest-even. An average of finite input
            // channels stays within the input range, so it cannot overflow half.
            uint32_t encoded = 0;
            if (value < std::ldexp(1.0, -14)) encoded = RoundEven(std::ldexp(value, 24));
            else {
                const auto significand = RoundEven(std::ldexp(value, 11 - exponent));
                encoded = (uint32_t(exponent + 14) << 10) + significand - 1024;
            }
            const uint16_t result = sign | static_cast<uint16_t>(encoded);
            std::memcpy(bytes, &result, sizeof(result));
        }
        std::vector<uint8_t> DownsampleTexture(const std::vector<uint8_t>& source, const RHI::FormatInfo& format,
            uint32_t width, uint32_t height)
        {
            const auto channels = format.NumComponents, channelBytes = format.BytesPerPixel / channels;
            Require((!format.bIsFloat && channelBytes == 1) || (format.bIsFloat && (channelBytes == 2 || channelBytes == 4)),
                "unsupported texture mip generation format");
            const uint32_t nextWidth = std::max(1u, width / 2), nextHeight = std::max(1u, height / 2);
            std::vector<uint8_t> result(size_t(nextWidth) * nextHeight * format.BytesPerPixel);
            for (uint32_t y = 0; y < nextHeight; ++y) for (uint32_t x = 0; x < nextWidth; ++x)
            {
                // Area coverage rather than an unconditional 2x2 average includes
                // the last row/column of odd-size images (e.g. 3x1 -> 1x1).
                const double x0 = double(x) * width / nextWidth, x1 = double(x + 1) * width / nextWidth;
                const double y0 = double(y) * height / nextHeight, y1 = double(y + 1) * height / nextHeight;
                for (uint32_t channel = 0; channel < channels; ++channel)
                {
                    const bool srgb = format.bIsSRGB && channel < 3; double total = 0;
                    for (uint32_t sy = uint32_t(y0); sy < uint32_t(std::ceil(y1)); ++sy)
                        for (uint32_t sx = uint32_t(x0); sx < uint32_t(std::ceil(x1)); ++sx)
                        {
                            const double weight = (std::min(x1, double(sx + 1)) - std::max(x0, double(sx))) *
                                (std::min(y1, double(sy + 1)) - std::max(y0, double(sy)));
                            double value = ReadChannel(source.data() + (size_t(sy) * width + sx) * format.BytesPerPixel + channel * channelBytes, channelBytes, format.bIsFloat);
                            if (srgb) value = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
                            total += value * weight;
                        }
                    double value = total / ((x1 - x0) * (y1 - y0));
                    if (srgb) value = value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
                    WriteChannel(result.data() + (size_t(y) * nextWidth + x) * format.BytesPerPixel + channel * channelBytes, channelBytes, format.bIsFloat, value);
                }
            }
            return result;
        }
        void ValidateTexture(const RHI::RHITextureDesc& desc)
        {
            TextureType(desc.Type); TextureFormat(desc.Format);
            Require(desc.Width && desc.Height && desc.Width <= 16384 && desc.Height <= 16384 && desc.Depth == 1 && desc.SampleCount == 1,
                "invalid or unsupported texture dimensions/samples");
            Require(desc.MipLevels && desc.MipLevels <= std::bit_width(std::max(desc.Width, desc.Height)), "invalid texture mip count");
            Require(desc.ArraySize && desc.ArraySize <= 2048, "invalid texture array size");
            Require(desc.Type != RHI::ERHITextureType::Texture2D || desc.ArraySize == 1, "2D texture must have one slice");
            Require(desc.Type != RHI::ERHITextureType::TextureCube || (desc.ArraySize == 6 && desc.Width == desc.Height), "invalid cube texture");
        }
        void SetTextureAccess(RenderCore::RenderTexture* texture,uint32_t mip,uint32_t slice,RHI::ERHIResourceAccess target)
        {
            auto& tracker=texture->GetTracker(); const RHI::RHISubresourceRange range(mip,slice,0);
            const auto before=tracker.GetSubresourceAccess(range); if (before==target) return;
            const auto queueType=tracker.GetLastAccessFence().QueueType; auto* queue=RHI::GRHIApi->GetQueue(queueType);
            std::vector<RHI::RHITransitionInfo> infos;
            infos.emplace_back(texture->GetRHI(),before,target,queueType,queueType,RHI::EResourceTransitionFlags::None,mip,slice,0);
            auto storage=std::make_unique<char[]>(RHI::G_RHITransition_TotalSize); auto* transition=new(storage.get()) RHI::RHITransition();
            RHI::GRHIApi->RHICreateTransition(transition,RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None,std::move(infos)));
            auto* context=queue->AcquireCommandContext(); RHI::RHICommandListBase commands(context); commands.SetImmediate(true);
            commands.Begin(); commands.BeginTransitions({transition}); commands.EndTransitions({transition}); commands.End();
            RHI::RHIFence fence; fence.QueueType=queueType; fence.Value=queue->ExecuteContext({context},{});
            tracker.UpdateSubresourceAccess(range,target); tracker.UpdateLastAccessFence(fence); queue->WaitValue(fence.Value);
            RHI::GRHIApi->RHIReleaseTransition(transition);
        }

        // Offsets and lengths refer to explicitly encoded data, never C++ structs.
        class BinaryWriter
        {
        public:
            BinaryWriter(const fs::path& path, const char* magic) : m_Output(path, std::ios::binary)
            { Require(bool(m_Output), "cannot create binary data file"); m_Output.write(magic, HeaderSize); }
            Json Write(const void* bytes, uint64_t size)
            {
                const auto offset = m_Output.tellp(); Require(offset >= 0, "invalid binary write offset");
                if (size) m_Output.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
                Require(bool(m_Output), "binary data write failed"); return {{"offset", uint64_t(offset)}, {"bytes", size}};
            }
            template<class T> Json Write(const std::vector<T>& values) { return Write(values.data(), uint64_t(values.size()) * sizeof(T)); }
            void Close() { m_Output.close(); Require(bool(m_Output), "cannot flush binary data"); }
        private:
            std::ofstream m_Output;
        };
        class BinaryReader
        {
        public:
            BinaryReader(const fs::path& path, const char* magic) : m_Input(path, std::ios::binary), m_Size(fs::file_size(path))
            {
                char actual[HeaderSize]{}; m_Input.read(actual, HeaderSize);
                Require(bool(m_Input) && std::equal(actual, actual + HeaderSize, magic), "binary header/version mismatch");
            }
            template<class T> std::vector<T> Read(const Json& range)
            {
                const auto offset = Unsigned(range.at("offset")), size = Unsigned(range.at("bytes"));
                Require(offset >= HeaderSize && offset <= m_Size && size <= m_Size - offset && size <= UINT32_MAX && size % sizeof(T) == 0,
                    "invalid binary data range");
                std::vector<T> values(static_cast<size_t>(size / sizeof(T)));
                m_Input.seekg(static_cast<std::streamoff>(offset));
                if (size) m_Input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(size));
                Require(bool(m_Input), "truncated binary data"); return values;
            }
        private:
            std::ifstream m_Input;
            uint64_t m_Size;
        };

        void ValidateMesh(const StaticMesh& mesh)
        {
            Require(mesh.GetLODCount() > 0 && mesh.GetLODCount() <= 64, "invalid mesh LOD count");
            for (size_t index = 0; index < mesh.GetLODCount(); ++index)
            {
                const auto& lod = mesh.GetLODResource(index); const auto& streams = lod.VertexBuffers;
                const VertexBuffer* buffers[] = {&streams.PositionBuffer, &streams.NormalBuffer, &streams.UVBuffer, &streams.TangentBuffer, &streams.ColorBuffer};
                const uint32_t components[] = {3,3,2,4,4}; const auto count = streams.PositionBuffer.GetNumVertices();
                Require(count > 0 && count <= UINT32_MAX / 16, "invalid mesh vertex count");
                for (size_t i = 0; i < 5; ++i)
                {
                    const auto& stream = *buffers[i];
                    Require(stream.Vertices.empty() || (stream.NumComponents == components[i] && stream.Vertices.size() == count * components[i]), "invalid vertex stream");
                    for (const float value : stream.Vertices) Require(std::isfinite(value), "non-finite vertex attribute");
                }
                Require(!lod.IndexBuffer.Indices.empty() && lod.IndexBuffer.Indices.size() <= UINT32_MAX / 4, "invalid index stream");
                const auto validateRange = [&](uint32_t first, uint32_t size, int32_t base) {
                    Require(size % 3 == 0 && uint64_t(first) + size <= lod.IndexBuffer.Indices.size(), "invalid section index range");
                    for (uint64_t i = first; i < uint64_t(first) + size; ++i) {
                        const auto vertex = int64_t(lod.IndexBuffer.Indices[i]) + base;
                        Require(vertex >= 0 && uint64_t(vertex) < count, "section index outside vertex stream");
                    }
                };
                if (lod.Sections.empty()) validateRange(0, static_cast<uint32_t>(lod.IndexBuffer.Indices.size()), 0);
                for (const auto& section : lod.Sections) {
                    validateRange(section.FirstIndex, section.NumIndices, section.BaseVertexIndex);
                    Require(section.MaterialIndex >= 0 && size_t(section.MaterialIndex) < mesh.GetNumMaterials(), "section material outside material table");
                }
            }
        }
        class SnapshotWriter
        {
        public:
            SnapshotWriter(const fs::path& root) : m_Geometry(root / "Geometry.geoms", "RLGEOM01"), m_Texture(root / "Texture.texs", "RLTEXS02")
            { Data = {{"version",2}, {"nodes",Json::array()}, {"meshes",Json::array()}, {"materials",Json::array()}, {"textures",Json::array()}}; }
            Json Data;
            uint64_t Texture(RenderCore::RenderTexture* texture, const std::string& name)
            {
                if (!texture) return 0;
                if (const auto found = m_TextureIds.find(texture); found != m_TextureIds.end()) return found->second;
                const auto& desc = texture->GetRHI()->GetDesc(); ValidateTexture(desc);
                Require(desc.Usage.HasAny(RHI::ERHITextureCreateFlag::TransferSrc), "texture was not created with readback/TransferSrc support");
                const uint64_t id = m_TextureIds.size() + 1; m_TextureIds.emplace(texture, id);
                // OpenGL's synchronous resource tasks and submitted command queues
                // have separate scheduling. Finish this texture's prior submission
                // before a task-based readback can overtake it; never wait the device.
                const auto prior=texture->GetTracker().GetLastAccessFence();
                if (prior.Value) RHI::GRHIApi->GetQueue(prior.QueueType)->WaitValue(prior.Value);
                Json slices = Json::array();
                for (uint32_t slice = 0; slice < desc.ArraySize; ++slice)
                {
                    // Persist only level zero. Higher levels are derived on load.
                    std::vector<uint8_t> pixels(static_cast<size_t>(TextureBytes(desc, 0)));
                    const auto before=texture->GetTracker().GetSubresourceAccess(RHI::RHISubresourceRange(0,slice,0));
                    Require(before!=RHI::ERHIResourceAccess::Unknown,"cannot save uninitialized texture contents");
                    texture->ReadData(pixels.data(), 0, slice);
                    SetTextureAccess(texture,0,slice,before);
                    slices.push_back(m_Texture.Write(pixels));
                }
                Data["textures"].push_back({{"id",id}, {"name",name}, {"type",TextureType(desc.Type)}, {"format",TextureFormat(desc.Format).Name},
                    {"width",desc.Width}, {"height",desc.Height}, {"mips",desc.MipLevels}, {"slices",desc.ArraySize}, {"data",std::move(slices)}});
                return id;
            }
            Json Parameters(const MaterialInterface& material, const MaterialInstance* instance = nullptr)
            {
                Json result = Json::array();
                for (const auto& definition : material.GetParameterDefinitions())
                {
                    if (instance && !instance->HasOverride(definition.Name, definition.Type)) continue;
                    const auto* parameter = material.GetParameter(definition.Name, definition.Type); Require(parameter, "material parameter is missing");
                    const auto value = parameter->GetValueVariant(); Json record{{"name",definition.Name}};
                    if (const auto* scalar = std::get_if<float>(&value)) { Number(Json(*scalar)); record["type"]="Scalar"; record["value"]=*scalar; }
                    else if (const auto* vector = std::get_if<std::array<float,4>>(&value)) {
                        for (float v : *vector) Number(Json(v)); record["type"]="Vector"; record["value"]=*vector;
                    }
                    else if (const auto* integer = std::get_if<int>(&value)) { record["type"]="Int"; record["value"]=*integer; }
                    else if (const auto* texture = std::get_if<std::shared_ptr<TextureAsset>>(&value)) {
                        record["type"]="Texture"; record["value"]=*texture ? Texture((*texture)->GetTexture(), (*texture)->GetName()) : 0;
                    }
                    result.push_back(std::move(record));
                }
                return result;
            }
            uint64_t MaterialData(const std::shared_ptr<MaterialAsset>& asset)
            {
                if (!asset) return 0;
                if (const auto found = m_MaterialIds.find(asset.get()); found != m_MaterialIds.end()) return found->second;
                const auto* material = asset->GetMaterial(); Require(material, "unloaded material");
                const uint64_t id = m_MaterialIds.size() + 1; m_MaterialIds.emplace(asset.get(), id);
                const char* blend = material->GetBlendMode()==EBlendMode::Opaque ? "Opaque" : material->GetBlendMode()==EBlendMode::Masked ? "Masked" : "Translucent";
                const char* cull = material->GetCullMode()==RHI::ERHICullMode::None ? "None" : material->GetCullMode()==RHI::ERHICullMode::Front ? "Front" : "Back";
                Data["materials"].push_back({{"id",id}, {"name",asset->GetName()}, {"blend",blend}, {"cull",cull}, {"twoSided",material->IsTwoSided()},
                    {"shading",material->GetShadingModel()==EShadingModel::Lit ? "Lit" : "Unlit"}, {"parameters",Parameters(*material)}});
                return id;
            }
            uint64_t Mesh(const std::shared_ptr<StaticMeshAsset>& asset)
            {
                if (!asset) return 0;
                if (const auto found = m_MeshIds.find(asset.get()); found != m_MeshIds.end()) return found->second;
                const auto* mesh = asset->GetMesh(); Require(mesh, "unloaded mesh"); ValidateMesh(*mesh);
                const uint64_t id = m_MeshIds.size() + 1; m_MeshIds.emplace(asset.get(), id);
                Json lods = Json::array(), materials = Json::array();
                for (size_t i = 0; i < mesh->GetNumMaterials(); ++i) materials.push_back(MaterialData(mesh->GetMaterialAsset(i)));
                for (size_t i = 0; i < mesh->GetLODCount(); ++i)
                {
                    const auto& lod = mesh->GetLODResource(i); const auto& v = lod.VertexBuffers;
                    Json streams = Json::array(), sections = Json::array();
                    for (const auto* stream : {&v.PositionBuffer,&v.NormalBuffer,&v.UVBuffer,&v.TangentBuffer,&v.ColorBuffer})
                        streams.push_back({{"components",stream->NumComponents}, {"valid",stream->bValid}, {"data",m_Geometry.Write(stream->Vertices)}});
                    for (const auto& section : lod.Sections) sections.push_back({{"first",section.FirstIndex}, {"count",section.NumIndices}, {"base",section.BaseVertexIndex}, {"material",section.MaterialIndex}});
                    lods.push_back({{"streams",std::move(streams)}, {"indices",m_Geometry.Write(lod.IndexBuffer.Indices)}, {"sections",std::move(sections)}});
                }
                Data["meshes"].push_back({{"id",id}, {"name",asset->GetName()}, {"materials",std::move(materials)}, {"lods",std::move(lods)}});
                return id;
            }
            void Nodes(const std::vector<ProjectNode>& nodes)
            {
                std::unordered_map<const SceneComponent*,uint64_t> ids; std::unordered_set<uint64_t> used;
                for (const auto& node : nodes) Require(node.Component && node.Id && used.insert(node.Id).second && ids.emplace(node.Component.get(),node.Id).second, "invalid/duplicate node identity");
                size_t skies = 0;
                for (const auto& node : nodes)
                {
                    const auto* component = node.Component.get(); uint64_t parent = 0;
                    std::unordered_set<const SceneComponent*> ancestors;
                    for (auto* p = component->GetAttachParent(); p; p = p->GetAttachParent()) Require(p != component && ancestors.insert(p).second, "node hierarchy cycle");
                    if (auto* p = component->GetAttachParent()) { Require(ids.contains(p), "parent is outside saved scene"); parent=ids.at(p); }
                    Json record{{"id",node.Id}, {"name",node.Name}, {"parent",parent}, {"position",Vector3(component->GetLocalLocation())},
                        {"rotation",Vector3(component->GetLocalRotation())}, {"scale",Vector3(component->GetLocalScale())}, {"visible",component->IsVisible()}};
                    if (const auto* mesh = dynamic_cast<const StaticMeshComponent*>(component))
                    {
                        Require(typeid(*component)==typeid(StaticMeshComponent),"unregistered mesh component subtype");
                        record["type"]="StaticMesh"; record["mesh"]=Mesh(mesh->GetStaticMesh() ? mesh->GetStaticMesh()->GetAsset() : nullptr);
                        record["forcedLod"]=mesh->GetForcedLOD(); Json slots=Json::array();
                        const auto& overrides=mesh->GetMaterialOverrides();
                        record["overrideCount"]=overrides.size();
                        for (uint32_t slot=0;slot<mesh->GetNumMaterials();++slot)
                        {
                            const auto* instance=mesh->GetMaterialInstance(slot);
                            const auto asset=slot<overrides.size() ? overrides[slot] : nullptr;
                            if (!asset && !instance) continue;
                            slots.push_back({{"slot",slot}, {"material",MaterialData(asset)}, {"instance",instance!=nullptr},
                                {"overrides",instance ? Parameters(*instance,instance) : Json::array()}});
                        }
                        record["materials"]=std::move(slots);
                    }
                    else if (const auto* light=dynamic_cast<const LightComponent*>(component))
                    {
                        Require(typeid(*light)==typeid(PointLightComponent) || typeid(*light)==typeid(SpotLightComponent) ||
                            typeid(*light)==typeid(DirectionalLightComponent) || typeid(*light)==typeid(SkyLightComponent),"unregistered light component subtype");
                        const char* types[]={"DirectionalLight","PointLight","SpotLight","SkyLight"}; record["type"]=types[static_cast<int>(light->GetLightType())];
                        record["enabled"]=light->IsEnabled(); record["color"]=Vector3(light->GetColor()); record["intensity"]=Number(Json(light->GetIntensity())); record["castShadow"]=light->IsCastShadow();
                        if (const auto* local=dynamic_cast<const LocalLightComponent*>(light)) record["range"]=Number(Json(local->GetAttenuationRadius()));
                        if (const auto* spot=dynamic_cast<const SpotLightComponent*>(light)) {
                            record["innerAngleDegrees"]=Core::RadToDeg(spot->GetInnerConeAngle()); record["outerAngleDegrees"]=Core::RadToDeg(spot->GetOuterConeAngle());
                        }
                        if (const auto* sky=dynamic_cast<const SkyLightComponent*>(light)) {
                            Require(++skies<=1,"multiple sky lights");
                            const auto& environment = sky->GetEnvironmentResource();
                            const auto input = environment ? environment->GetInputTexture() : nullptr;
                            record["environment"] = input ? Texture(input->GetTexture(), input->GetName()) : 0;
                        }
                    }
                    else { Require(typeid(*component)==typeid(SceneComponent),"unregistered component type"); record["type"]="SceneComponent"; }
                    Data["nodes"].push_back(std::move(record));
                }
            }
            void Close() { m_Geometry.Close(); m_Texture.Close(); }
        private:
            BinaryWriter m_Geometry, m_Texture;
            std::unordered_map<const StaticMeshAsset*,uint64_t> m_MeshIds;
            std::unordered_map<const MaterialAsset*,uint64_t> m_MaterialIds;
            std::unordered_map<const RenderCore::RenderTexture*,uint64_t> m_TextureIds;
        };

        template<class T> const std::shared_ptr<T>& Lookup(const std::unordered_map<uint64_t,std::shared_ptr<T>>& table, const Json& value)
        {
            const auto id=Unsigned(value); const auto found=table.find(id);
            Require(found!=table.end(),"missing object reference " + std::to_string(id)); return found->second;
        }
        template<class T> void Insert(std::unordered_map<uint64_t,std::shared_ptr<T>>& table,const Json& id,std::shared_ptr<T> value)
        { const auto key=Unsigned(id); Require(key && table.emplace(key,std::move(value)).second,"duplicate/zero resource ID"); }
        using Textures=std::unordered_map<uint64_t,std::shared_ptr<TextureAsset>>;
        void RestoreParameters(MaterialInterface& material,const Json& records,const Textures& textures)
        {
            Require(records.is_array(),"parameters must be an array"); std::unordered_set<std::string> seen;
            for (const auto& record:records)
            {
                const auto name=record.at("name").get<std::string>(), type=record.at("type").get<std::string>();
                Require(seen.insert(type+":"+name).second,"duplicate material parameter"); const auto& value=record.at("value");
                if (type=="Scalar") material.SetParameter(name,Number(value));
                else if (type=="Vector") {
                    Require(value.is_array() && value.size()==4,"invalid material vector");
                    material.SetParameter(name,std::array<float,4>{Number(value[0]),Number(value[1]),Number(value[2]),Number(value[3])});
                }
                else if (type=="Int") { Require(value.is_number_integer(),"invalid integer parameter"); material.SetParameter(name,value.get<int>()); }
                else if (type=="Texture") material.SetParameter(name,Lookup(textures,value));
                else Require(false,"unknown material parameter type");
            }
        }
    }

    void ProjectArchive::Save(const fs::path& directory,const std::vector<ProjectNode>& nodes,const std::vector<AssetSP>& assets)
    {
        const auto root=fs::weakly_canonical(fs::absolute(directory));
        Require(fs::is_directory(root/"Content"),"missing Content directory");
        auto manifest=ReadJson(root/"RenderingLearn.project.json");
        Require(manifest.at("contentDirectory")=="Content", "invalid project content directory");
        static std::atomic<uint64_t> serial{0};
        const auto generation="snapshot-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);
        const auto dataRoot=Inside(root,"Data"); fs::create_directories(dataRoot);
        const auto folder=dataRoot/generation;
        Require(fs::create_directory(folder),"snapshot generation already exists");
        const auto temporary=root/("RenderingLearn.project."+generation+".tmp");
        try
        {
            SnapshotWriter writer(folder); writer.Nodes(nodes);
            for (const auto& asset:assets) {
                if (auto mesh=std::dynamic_pointer_cast<StaticMeshAsset>(asset)) writer.Mesh(mesh);
                else if (auto material=std::dynamic_pointer_cast<MaterialAsset>(asset)) writer.MaterialData(material);
                else if (auto texture=std::dynamic_pointer_cast<TextureAsset>(asset)) writer.Texture(texture->GetTexture(),texture->GetName());
                else Require(false,"unsupported project asset type");
            }
            writer.Close();
            WriteJson(folder/"Scene.json",writer.Data);
            manifest["version"]=2; manifest["snapshot"]="Data/"+generation+"/Scene.json";
            WriteJson(temporary,manifest);
            // The previous generation remains readable until this single publication.
            Publish(temporary,root/"RenderingLearn.project.json");
        }
        catch (...) {
            std::error_code ignored; fs::remove(temporary,ignored);
            // Only the newly created, unpublished directory is ours to remove.
            fs::remove_all(folder,ignored); throw;
        }
    }

    ProjectSnapshot ProjectArchive::Load(const fs::path& directory) try
    {
        const auto root=fs::weakly_canonical(fs::absolute(directory)); const auto manifest=ReadJson(root/"RenderingLearn.project.json");
        const auto version=manifest.at("version").get<int>();
        Require((version==1 || version==2) && manifest.at("contentDirectory")=="Content", "unsupported project version");
        if (version==1) { Require(!manifest.contains("snapshot"),"unexpected legacy snapshot"); return {}; }
        const auto scenePath=Inside(root,manifest.at("snapshot").get<std::string>()); const auto data=ReadJson(scenePath);
        Require(data.at("version")==2,"unsupported scene version");
        BinaryReader geometry(Inside(scenePath.parent_path(),"Geometry.geoms"),"RLGEOM01"), pixels(Inside(scenePath.parent_path(),"Texture.texs"),"RLTEXS02");
        Textures textures{{0,nullptr}};
        for (const auto& record:data.at("textures"))
        {
            RHI::RHITextureDesc desc; desc.Width=U32(record.at("width")); desc.Height=U32(record.at("height"));
            desc.ArraySize=U32(record.at("slices")); desc.MipLevels=U32(record.at("mips"));
            const auto type=record.at("type").get<std::string>(), format=record.at("format").get<std::string>();
            if (type=="2D") desc.Type=RHI::ERHITextureType::Texture2D;
            else if (type=="2DArray") desc.Type=RHI::ERHITextureType::Texture2DArray;
            else if (type=="Cube") desc.Type=RHI::ERHITextureType::TextureCube;
            else Require(false,"unknown texture type");
            for (const auto& [key,info]:RHI::GFormatInfoMap) if (info.Name==format) desc.Format=key;
            ValidateTexture(desc); const auto& slices=record.at("data");
            Require(slices.is_array() && slices.size()==desc.ArraySize,"texture base slice count mismatch");
            desc.Usage=RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferSrc | RHI::ERHITextureCreateFlag::TransferDest;
            auto texture=std::make_shared<RenderCore::RenderTexture>(desc); texture->InitRHIResource();
            for (uint32_t slice=0;slice<desc.ArraySize;++slice)
            {
                auto bytes=pixels.Read<uint8_t>(slices[slice]);
                Require(bytes.size()==TextureBytes(desc,0),"texture base pixel size mismatch");
                for (uint32_t mip=0;mip<desc.MipLevels;++mip)
                {
                    RHI::RHIUpdateTextureRegion region; region.Width=std::max(1u,desc.Width>>mip); region.Height=std::max(1u,desc.Height>>mip);
                    region.Depth=1; region.MipLevel=mip; region.ArraySlice=slice;
                    texture->UploadData(bytes.data(),region);
                    SetTextureAccess(texture.get(),mip,slice,RHI::ERHIResourceAccess::SRV);
                    if (mip+1<desc.MipLevels) bytes=DownsampleTexture(bytes,TextureFormat(desc.Format),region.Width,region.Height);
                }
            }
            Insert(textures,record.at("id"),TextureAsset::Create(record.at("name").get<std::string>(),std::move(texture)));
        }
        std::unordered_map<uint64_t,std::shared_ptr<MaterialAsset>> materials{{0,nullptr}};
        for (const auto& record:data.at("materials"))
        {
            auto material=std::make_unique<Material>(); const auto blend=record.at("blend").get<std::string>(), shading=record.at("shading").get<std::string>(), cull=record.at("cull").get<std::string>();
            Require(blend=="Opaque" || blend=="Masked" || blend=="Translucent","unknown blend mode");
            Require(shading=="Lit" || shading=="Unlit","unknown shading model"); Require(cull=="None" || cull=="Front" || cull=="Back","unknown cull mode");
            material->SetBlendMode(blend=="Opaque" ? EBlendMode::Opaque : blend=="Masked" ? EBlendMode::Masked : EBlendMode::Translucent);
            material->SetShadingModel(shading=="Lit" ? EShadingModel::Lit : EShadingModel::Unlit);
            material->SetCullMode(cull=="None" ? RHI::ERHICullMode::None : cull=="Front" ? RHI::ERHICullMode::Front : RHI::ERHICullMode::Back);
            material->SetTwoSided(record.at("twoSided").get<bool>()); RestoreParameters(*material,record.at("parameters"),textures);
            Insert(materials,record.at("id"),MaterialAsset::Create("",record.at("name").get<std::string>(),std::move(material)));
        }
        std::unordered_map<uint64_t,std::shared_ptr<StaticMeshAsset>> meshes{{0,nullptr}};
        for (const auto& record:data.at("meshes"))
        {
            auto renderData=std::make_shared<StaticMeshRenderData>(); auto mesh=std::make_shared<StaticMesh>(renderData);
            for (const auto& id:record.at("materials")) mesh->AddMaterial(Lookup(materials,id));
            for (const auto& source:record.at("lods"))
            {
                LODResource lod; auto& v=lod.VertexBuffers; VertexBuffer* streams[]={&v.PositionBuffer,&v.NormalBuffer,&v.UVBuffer,&v.TangentBuffer,&v.ColorBuffer};
                Require(source.at("streams").size()==5,"mesh stream table mismatch");
                for (size_t i=0;i<5;++i) {
                    const auto& stream=source.at("streams")[i]; streams[i]->NumComponents=U32(stream.at("components"));
                    streams[i]->bValid=stream.at("valid").get<bool>(); streams[i]->Vertices=geometry.Read<float>(stream.at("data"));
                }
                lod.IndexBuffer.Indices=geometry.Read<uint32_t>(source.at("indices"));
                for (const auto& section:source.at("sections")) {
                    SectionInfo info; info.FirstIndex=U32(section.at("first")); info.NumIndices=U32(section.at("count"));
                    info.BaseVertexIndex=section.at("base").get<int32_t>(); info.MaterialIndex=section.at("material").get<int32_t>(); lod.Sections.push_back(info);
                }
                renderData->AddLOD(std::move(lod));
            }
            ValidateMesh(*mesh);
            for (auto& lod:renderData->LODResources) {
                const auto& positions=lod.VertexBuffers.PositionBuffer.Vertices;
                for (size_t i=0;i<positions.size();i+=3) renderData->Bounds.ExpandBy({positions[i],positions[i+1],positions[i+2]});
                lod.InitializeResources();
            }
            GDistanceFieldMgr.BuildMeshDistanceField(*renderData);
            Insert(meshes,record.at("id"),StaticMeshAsset::Create(record.at("name").get<std::string>(),std::move(mesh)));
        }
        const auto& records=data.at("nodes"); Require(records.is_array() && records.size()<=100000,"invalid node table");
        std::vector<ProjectNode> nodes; nodes.reserve(records.size()); std::unordered_map<uint64_t,size_t> ids; std::vector<uint64_t> parents; size_t skies=0;
        for (const auto& record:records)
        {
            ProjectNode node; node.Id=Unsigned(record.at("id")); node.Name=record.at("name").get<std::string>();
            Require(node.Id && node.Id<UINT64_MAX && ids.emplace(node.Id,nodes.size()).second,"invalid/duplicate node ID");
            const auto type=record.at("type").get<std::string>(); std::unique_ptr<SceneComponent> component;
            if (type=="StaticMesh") {
                auto mesh=std::make_unique<StaticMeshComponent>(); const auto asset=Lookup(meshes,record.at("mesh")); mesh->SetStaticMesh(asset ? asset->GetMesh() : nullptr);
                const auto lod=record.at("forcedLod").get<int32_t>(); Require(lod>=-1 && (lod==-1 || (asset && size_t(lod)<asset->GetMesh()->GetLODCount())),"invalid forced LOD"); mesh->SetForcedLOD(lod);
                const auto overrideCount=U32(record.at("overrideCount")); Require(overrideCount<65536,"invalid material override count");
                if (overrideCount) mesh->SetMaterial(overrideCount-1,nullptr);
                std::unordered_set<uint32_t> used;
                for (const auto& slot:record.at("materials")) {
                    const auto index=U32(slot.at("slot")); Require(index<65536 && used.insert(index).second,"invalid material slot");
                    const auto parent=Lookup(materials,slot.at("material")); if (parent) mesh->SetMaterial(index,parent->GetMaterial());
                    if (slot.at("instance").get<bool>()) RestoreParameters(*mesh->GetOrCreateMaterialInstance(index),slot.at("overrides"),textures);
                    else Require(slot.at("overrides").empty(),"overrides without a material instance");
                }
                component=std::move(mesh);
            }
            else if (type=="SceneComponent") component=std::make_unique<SceneComponent>();
            else {
                std::unique_ptr<LightComponent> light;
                if (type=="DirectionalLight") light=std::make_unique<DirectionalLightComponent>();
                else if (type=="PointLight") light=std::make_unique<PointLightComponent>();
                else if (type=="SpotLight") light=std::make_unique<SpotLightComponent>();
                else if (type=="SkyLight") {
                    Require(++skies<=1,"multiple sky lights"); auto sky=std::make_unique<SkyLightComponent>();
                    const auto& input=Lookup(textures,record.at("environment"));
                    auto& environments=EnvironmentLightResourceManager::Get();
                    environments.ApplyEnvironment(*sky,input ? environments.Build(input) : environments.GetDefaultEnvironment());
                    light=std::move(sky);
                }
                else Require(false,"unknown node type: "+type);
                const auto intensity=Number(record.at("intensity")); Require(intensity>=0,"negative light intensity");
                light->SetEnabled(record.at("enabled").get<bool>()); light->SetColor(Vector3(record.at("color"))); light->SetIntensity(intensity); light->SetCastShadow(record.at("castShadow").get<bool>());
                if (auto* local=dynamic_cast<LocalLightComponent*>(light.get())) {
                    const auto range=Number(record.at("range")); Require(range>0,"invalid light range"); local->SetAttenuationRadius(range);
                }
                if (auto* spot=dynamic_cast<SpotLightComponent*>(light.get())) {
                    const auto inner=Number(record.at("innerAngleDegrees")), outer=Number(record.at("outerAngleDegrees"));
                    Require(inner>=0 && inner<=outer && outer>0 && outer<90,"invalid spot cone angles"); spot->SetOuterConeAngle(outer); spot->SetInnerConeAngle(inner);
                }
                component=std::move(light);
            }
            component->SetLocalLocation(Vector3(record.at("position"))); component->SetLocalRotation(Vector3(record.at("rotation")));
            const auto scale=Vector3(record.at("scale")); Require(std::abs(scale.X)>1e-8f && std::abs(scale.Y)>1e-8f && std::abs(scale.Z)>1e-8f,"singular node scale");
            component->SetLocalScale(scale); component->SetVisible(record.at("visible").get<bool>());
            parents.push_back(Unsigned(record.at("parent"))); node.Component=std::move(component); nodes.push_back(std::move(node));
        }
        // Validate the complete parent graph before AttachTo can traverse it.
        std::vector<uint8_t> states(nodes.size()); std::vector<size_t> order;
        for (size_t start=0;start<nodes.size();++start) {
            size_t index=start; std::vector<size_t> chain;
            while (states[index]==0) {
                states[index]=1; chain.push_back(index); const auto parent=parents[index];
                if (!parent) break; Require(ids.contains(parent),"missing parent node"); index=ids.at(parent);
                Require(states[index]!=1,"node hierarchy cycle");
            }
            for (auto it=chain.rbegin();it!=chain.rend();++it) { states[*it]=2; order.push_back(*it); }
        }
        // Root-to-leaf order also refreshes derived light directions after reparenting.
        for (const auto i:order) {
            if (parents[i]) nodes[i].Component->AttachTo(nodes[ids.at(parents[i])].Component.get(),false);
        }
        ProjectSnapshot result; result.Nodes=std::move(nodes);
        for (const auto& [id,asset]:meshes) if (id) result.Assets.push_back(asset);
        for (const auto& [id,asset]:materials) if (id) result.Assets.push_back(asset);
        for (const auto& [id,asset]:textures) if (id) result.Assets.push_back(asset);
        return result;
    }
    catch (...) {
        // Function locals (including staged sky components) have been destroyed.
        // Build completed their initialization; the live scene still owns its refs.
        EnvironmentLightResourceManager::Get().ReleaseUnused();
        throw;
    }
}
