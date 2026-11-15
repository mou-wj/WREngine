#pragma once
#include "VirtualGeometryResource.h"
#include <filesystem>
#include <string>

namespace Engine
{
    constexpr uint32_t VIRTUAL_GEOMETRY_PAGE_VERTICES = 768;
    constexpr uint32_t VIRTUAL_GEOMETRY_PAGE_INDICES = 1536;
    constexpr uint32_t VIRTUAL_GEOMETRY_PAGE_BYTES = VIRTUAL_GEOMETRY_PAGE_VERTICES * 64 + VIRTUAL_GEOMETRY_PAGE_INDICES * 4;

    struct VirtualGeometryPageLocation
    {
        uint32_t Page = 0;
        uint32_t VertexOffset = 0;
        uint32_t IndexOffset = 0;
    };

    struct VirtualGeometryPage
    {
        uint64_t FileOffset = 0;
        uint32_t StoredBytes = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t Codec = 0; // 0: raw words; 1: XOR prediction and unsigned varints. Both lossless.
        uint32_t Checksum = 0;
        bool bPinned = false;
        std::vector<uint32_t> Clusters;
    };

    struct VirtualGeometryPageData
    {
        std::vector<Core::Float3> Positions;
        std::vector<Core::Float3> Normals;
        std::vector<Core::Float4> Tangents;
        std::vector<Core::Float2> TexCoords;
        std::vector<Core::Float4> Colors;
        std::vector<uint32_t> Indices;
    };

    // Immutable, independently seekable CPU archive. No RHI calls or worker thread ownership.
    class ENGINE_API VirtualGeometryPageStorage
    {
    public:
        bool Build(const VirtualGeometryResource& resource, const std::filesystem::path& path, std::string& error);
        bool Open(const std::filesystem::path& path, std::string& error);
        bool ReadPage(uint32_t page, VirtualGeometryPageData& output, std::string& error) const;
        bool Matches(const VirtualGeometryResource& resource) const;
        const std::vector<VirtualGeometryPage>& GetPages() const { return m_Pages; }
        const std::vector<VirtualGeometryPageLocation>& GetLocations() const { return m_Locations; }
        uint64_t GetStoredBytes() const { return m_StoredBytes; }
        uint64_t GetRawBytes() const { return m_RawBytes; }
        const std::filesystem::path& GetPath() const { return m_Path; }
    private:
        std::filesystem::path m_Path;
        std::vector<VirtualGeometryPage> m_Pages;
        std::vector<VirtualGeometryPageLocation> m_Locations;
        uint64_t m_StoredBytes = 0;
        uint64_t m_RawBytes = 0;
        uint64_t m_SourceHash = 0;
    };
}
