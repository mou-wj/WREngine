#include "VirtualGeometryPages.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <numeric>

namespace Engine
{
    namespace
    {
        constexpr uint32_t Magic = 0x50475652; // RVGP, little-endian format version 1.
        constexpr uint64_t HeaderBytes = 40, PageRecordBytes = 40;

        uint32_t DirectoryChecksum(const std::vector<VirtualGeometryPage>& pages,
                                   const std::vector<VirtualGeometryPageLocation>& locations)
        {
            uint32_t hash = 2166136261u;
            const auto append = [&](uint64_t value, uint32_t bytes)
            {
                for (uint32_t i = 0; i < bytes; ++i)
                {
                    hash = (hash ^ uint8_t(value >> (i * 8))) * 16777619u;
                }
            };
            for (const auto& page : pages)
            {
                append(page.FileOffset, 8);
                for (const auto word : {page.StoredBytes, page.VertexCount, page.IndexCount, page.Codec, page.Checksum,
                                        uint32_t(page.bPinned), 0u, 0u})
                {
                    append(word, 4);
                }
            }
            for (const auto& location : locations)
            {
                append(location.Page, 4);
                append(location.VertexOffset, 4);
                append(location.IndexOffset, 4);
            }
            return hash;
        }

        uint64_t SourceHash(const VirtualGeometryResource& resource)
        {
            uint64_t hash = 14695981039346656037ull;
            const auto append = [&](const auto& values)
            {
                using Element = typename std::decay_t<decltype(values)>::value_type;
                const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
                for (size_t i = 0; i < values.size() * sizeof(Element); ++i)
                {
                    hash = (hash ^ bytes[i]) * 1099511628211ull;
                }
            };
            append(resource.Positions);
            append(resource.Normals);
            append(resource.Tangents);
            append(resource.TexCoords);
            append(resource.Colors);
            append(resource.Indices);
            append(resource.GroupChildClusterIndices);
            append(resource.GroupParentClusterIndices);
            append(resource.RootGroupIndices);
            for (const auto& c : resource.Clusters)
            {
                append(std::array{c.VertexOffset, c.VertexCount, c.IndexOffset, c.TriangleCount, c.GroupIndex,
                                  c.GeneratingGroupIndex, c.MaterialIndex, c.MipLevel});
            }
            return hash;
        }

        uint32_t Checksum(const std::vector<uint32_t>& words)
        {
            uint32_t hash = 2166136261u;
            for (const auto word : words)
            {
                for (uint32_t shift = 0; shift < 32; shift += 8)
                {
                    hash = (hash ^ ((word >> shift) & 255)) * 16777619u;
                }
            }
            return hash;
        }

        template<typename T>
        bool Write(std::ostream& file, const T& value)
        {
            static_assert(std::is_integral_v<T>);
            for (uint32_t byte = 0; byte < sizeof(T); ++byte)
            {
                file.put(static_cast<char>(value >> (byte * 8)));
            }
            return bool(file);
        }

        template<typename T>
        bool Read(std::istream& file, T& value)
        {
            value = 0;
            for (uint32_t byte = 0; byte < sizeof(T); ++byte)
            {
                const int part = file.get();
                if (part == EOF)
                {
                    return false;
                }
                value |= T(uint8_t(part)) << (byte * 8);
            }
            return true;
        }

        std::vector<uint8_t> Encode(const std::vector<uint32_t>& words)
        {
            std::vector<uint8_t> bytes;
            // Independent channel predictors retain exact float bits, including seams and signed zero.
            std::array<uint32_t, 16> previous{};
            for (size_t index = 0; index < words.size(); ++index)
            {
                auto& predictor = previous[index % 16];
                uint32_t value = words[index] ^ predictor;
                predictor = words[index];
                while (value >= 128)
                {
                    bytes.push_back(uint8_t(value) | 128);
                    value >>= 7;
                }
                bytes.push_back(uint8_t(value));
            }
            return bytes;
        }

        bool Decode(const std::vector<uint8_t>& bytes, uint32_t count, std::vector<uint32_t>& words)
        {
            words.resize(count);
            size_t cursor = 0;
            std::array<uint32_t, 16> previous{};
            for (uint32_t index = 0; index < count; ++index)
            {
                uint32_t value = 0, shift = 0;
                while (true)
                {
                    if (cursor >= bytes.size() || shift > 28)
                    {
                        return false;
                    }
                    const uint8_t byte = bytes[cursor++];
                    if (shift == 28 && byte > 15)
                    {
                        return false;
                    }
                    value |= uint32_t(byte & 127) << shift;
                    if (!(byte & 128))
                    {
                        break;
                    }
                    shift += 7;
                }
                auto& predictor = previous[index % 16];
                predictor ^= value;
                words[index] = predictor;
            }
            return cursor == bytes.size();
        }

        void AppendVertex(std::vector<uint32_t>& words, const VirtualGeometryResource& resource, uint32_t vertex)
        {
            const auto& p = resource.Positions[vertex];
            const auto& n = resource.Normals[vertex];
            const auto& t = resource.Tangents[vertex];
            const auto& uv = resource.TexCoords[vertex];
            const auto& color = resource.Colors[vertex];
            for (const float value : {p.X, p.Y, p.Z, n.X, n.Y, n.Z, t.X, t.Y, t.Z, t.W, uv.X, uv.Y,
                                      color.X, color.Y, color.Z, color.W})
            {
                words.push_back(std::bit_cast<uint32_t>(value));
            }
        }
    }

    bool VirtualGeometryPageStorage::Build(const VirtualGeometryResource& resource, const std::filesystem::path& path,
                                           std::string& error)
    {
        *this = {};
        error.clear();
        if (!resource.IsValid() || resource.Normals.size() != resource.Positions.size() ||
            resource.Tangents.size() != resource.Positions.size() || resource.TexCoords.size() != resource.Positions.size() ||
            resource.Colors.size() != resource.Positions.size() || resource.Clusters.size() > UINT32_MAX)
        {
            error = "Invalid geometry page source attributes";
            return false;
        }
        std::vector<uint8_t> pinned(resource.Clusters.size());
        for (const auto root : resource.RootGroupIndices)
        {
            if (root >= resource.Groups.size())
            {
                error = "Invalid page root group";
                return false;
            }
            const auto& group = resource.Groups[root];
            if (uint64_t(group.ChildClusterOffset) + group.ChildClusterCount > resource.GroupChildClusterIndices.size())
            {
                error = "Invalid page root child range";
                return false;
            }
            for (uint32_t i = 0; i < group.ChildClusterCount; ++i)
            {
                const auto cluster = resource.GroupChildClusterIndices[group.ChildClusterOffset + i];
                if (cluster >= pinned.size())
                {
                    error = "Invalid pinned cluster";
                    return false;
                }
                pinned[cluster] = 1;
            }
        }
        std::vector<uint32_t> order(resource.Clusters.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
        {
            return pinned[a] != pinned[b] ? pinned[a] > pinned[b] : resource.Clusters[a].MipLevel > resource.Clusters[b].MipLevel;
        });
        m_Locations.resize(order.size());
        for (const auto cluster : order)
        {
            const auto& c = resource.Clusters[cluster];
            if (!c.VertexCount || c.VertexCount > VIRTUAL_GEOMETRY_PAGE_VERTICES || !c.TriangleCount ||
                c.TriangleCount > VIRTUAL_GEOMETRY_MAX_CLUSTER_TRIANGLES ||
                uint64_t(c.VertexOffset) + c.VertexCount > resource.Positions.size() ||
                uint64_t(c.IndexOffset) + c.TriangleCount * 3ull > resource.Indices.size())
            {
                error = "Invalid cluster page range";
                return false;
            }
            if (m_Pages.empty() || m_Pages.back().bPinned != bool(pinned[cluster]) ||
                m_Pages.back().VertexCount + c.VertexCount > VIRTUAL_GEOMETRY_PAGE_VERTICES ||
                m_Pages.back().IndexCount + c.TriangleCount * 3 > VIRTUAL_GEOMETRY_PAGE_INDICES)
            {
                m_Pages.emplace_back().bPinned = pinned[cluster] != 0;
            }
            auto& page = m_Pages.back();
            m_Locations[cluster] = {uint32_t(m_Pages.size() - 1), page.VertexCount, page.IndexCount};
            page.Clusters.push_back(cluster);
            page.VertexCount += c.VertexCount;
            page.IndexCount += c.TriangleCount * 3;
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "Cannot create geometry page archive";
            return false;
        }
        const uint64_t payloadOffset = HeaderBytes + m_Pages.size() * PageRecordBytes + m_Locations.size() * 12ull;
        file.seekp(payloadOffset);
        for (auto& page : m_Pages)
        {
            std::vector<uint32_t> words;
            words.reserve(page.VertexCount * 16 + page.IndexCount);
            for (const auto cluster : page.Clusters)
            {
                const auto& c = resource.Clusters[cluster];
                for (uint32_t i = 0; i < c.VertexCount; ++i)
                {
                    AppendVertex(words, resource, c.VertexOffset + i);
                }
            }
            for (const auto cluster : page.Clusters)
            {
                const auto& c = resource.Clusters[cluster];
                for (uint32_t i = 0; i < c.TriangleCount * 3; ++i)
                {
                    const auto index = resource.Indices[c.IndexOffset + i];
                    if (index >= c.VertexCount)
                    {
                        error = "Page contains invalid cluster-local index";
                        return false;
                    }
                    words.push_back(index);
                }
            }
            auto bytes = Encode(words);
            page.Codec = bytes.size() < words.size() * 4 ? 1 : 0;
            if (!page.Codec)
            {
                bytes.clear();
                for (const auto word : words)
                {
                    for (uint32_t shift = 0; shift < 32; shift += 8)
                    {
                        bytes.push_back(uint8_t(word >> shift));
                    }
                }
            }
            page.Checksum = Checksum(words);
            page.FileOffset = uint64_t(file.tellp());
            page.StoredBytes = uint32_t(bytes.size());
            file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            m_StoredBytes += bytes.size();
            m_RawBytes += words.size() * 4;
        }
        file.seekp(0);
        Write(file, Magic);
        Write(file, uint32_t(1));
        Write(file, uint32_t(m_Pages.size()));
        Write(file, uint32_t(m_Locations.size()));
        Write(file, m_RawBytes);
        m_SourceHash = SourceHash(resource);
        Write(file, m_SourceHash);
        Write(file, DirectoryChecksum(m_Pages, m_Locations));
        Write(file, uint32_t(0));
        for (const auto& page : m_Pages)
        {
            Write(file, page.FileOffset);
            for (const auto value : {page.StoredBytes, page.VertexCount, page.IndexCount, page.Codec, page.Checksum,
                                     uint32_t(page.bPinned), uint32_t(0), uint32_t(0)})
            {
                Write(file, value);
            }
        }
        for (const auto& location : m_Locations)
        {
            Write(file, location.Page);
            Write(file, location.VertexOffset);
            Write(file, location.IndexOffset);
        }
        file.flush();
        if (!file)
        {
            error = "Geometry page archive write failed";
            return false;
        }
        m_Path = path;
        return true;
    }

    bool VirtualGeometryPageStorage::Open(const std::filesystem::path& path, std::string& error)
    {
        *this = {};
        error.clear();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto fileSize = file ? uint64_t(file.tellg()) : 0;
        file.seekg(0);
        uint32_t magic = 0, version = 0, pages = 0, clusters = 0;
        uint32_t directoryChecksum = 0, headerReserved = 0;
        if (!Read(file, magic) || !Read(file, version) || !Read(file, pages) || !Read(file, clusters) ||
            !Read(file, m_RawBytes) || !Read(file, m_SourceHash) || !Read(file, directoryChecksum) ||
            !Read(file, headerReserved) || headerReserved != 0 || magic != Magic || version != 1 || !pages || !clusters ||
            HeaderBytes + uint64_t(pages) * PageRecordBytes + uint64_t(clusters) * 12 > fileSize)
        {
            error = "Invalid geometry page archive header";
            return false;
        }
        m_Pages.resize(pages);
        for (auto& page : m_Pages)
        {
            uint32_t pinned = 0, reserved = 0;
            if (!Read(file, page.FileOffset) || !Read(file, page.StoredBytes) || !Read(file, page.VertexCount) ||
                !Read(file, page.IndexCount) || !Read(file, page.Codec) || !Read(file, page.Checksum) ||
                !Read(file, pinned) || !Read(file, reserved) || !Read(file, reserved) ||
                !page.VertexCount || page.VertexCount > VIRTUAL_GEOMETRY_PAGE_VERTICES ||
                page.IndexCount > VIRTUAL_GEOMETRY_PAGE_INDICES || page.Codec > 1 || pinned > 1 ||
                !page.StoredBytes || page.StoredBytes > VIRTUAL_GEOMETRY_PAGE_BYTES ||
                page.FileOffset < HeaderBytes + uint64_t(pages) * PageRecordBytes + uint64_t(clusters) * 12 ||
                page.FileOffset > fileSize || page.StoredBytes > fileSize - page.FileOffset)
            {
                error = "Invalid geometry page directory";
                return false;
            }
            page.bPinned = pinned != 0;
            m_StoredBytes += page.StoredBytes;
        }
        m_Locations.resize(clusters);
        for (uint32_t c = 0; c < clusters; ++c)
        {
            auto& location = m_Locations[c];
            if (!Read(file, location.Page) || !Read(file, location.VertexOffset) || !Read(file, location.IndexOffset) ||
                location.Page >= pages || location.VertexOffset >= m_Pages[location.Page].VertexCount ||
                location.IndexOffset >= m_Pages[location.Page].IndexCount)
            {
                error = "Invalid geometry page cluster directory";
                return false;
            }
            m_Pages[location.Page].Clusters.push_back(c);
        }
        uint64_t rawBytes = 0;
        for (const auto& page : m_Pages)
        {
            rawBytes += uint64_t(page.VertexCount) * 64 + uint64_t(page.IndexCount) * 4;
        }
        if (DirectoryChecksum(m_Pages, m_Locations) != directoryChecksum || rawBytes != m_RawBytes)
        {
            error = "Geometry page directory checksum/size failed";
            return false;
        }
        m_Path = path;
        return true;
    }

    bool VirtualGeometryPageStorage::ReadPage(uint32_t index, VirtualGeometryPageData& output, std::string& error) const
    {
        output = {};
        error.clear();
        if (index >= m_Pages.size())
        {
            error = "Geometry page index out of range";
            return false;
        }
        const auto& page = m_Pages[index];
        std::ifstream file(m_Path, std::ios::binary);
        file.seekg(page.FileOffset);
        std::vector<uint8_t> bytes(page.StoredBytes);
        file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        std::vector<uint32_t> words;
        const uint32_t wordCount = page.VertexCount * 16 + page.IndexCount;
        bool valid = bool(file);
        if (page.Codec)
        {
            valid = valid && Decode(bytes, wordCount, words);
        }
        else
        {
            valid = valid && bytes.size() == wordCount * 4;
            if (valid)
            {
                words.resize(wordCount);
                for (uint32_t i = 0; i < wordCount; ++i)
                {
                    words[i] = uint32_t(bytes[i * 4]) | uint32_t(bytes[i * 4 + 1]) << 8 |
                               uint32_t(bytes[i * 4 + 2]) << 16 | uint32_t(bytes[i * 4 + 3]) << 24;
                }
            }
        }
        if (!valid || Checksum(words) != page.Checksum)
        {
            error = "Geometry page read/decode/checksum failed";
            return false;
        }
        for (uint32_t vertex = 0; vertex < page.VertexCount; ++vertex)
        {
            std::array<float, 16> f;
            for (uint32_t channel = 0; channel < 16; ++channel)
            {
                f[channel] = std::bit_cast<float>(words[vertex * 16 + channel]);
            }
            output.Positions.push_back({f[0], f[1], f[2]});
            output.Normals.push_back({f[3], f[4], f[5]});
            output.Tangents.push_back({f[6], f[7], f[8], f[9]});
            output.TexCoords.push_back({f[10], f[11]});
            output.Colors.push_back({f[12], f[13], f[14], f[15]});
        }
        output.Indices.assign(words.begin() + page.VertexCount * 16, words.end());
        return true;
    }

    bool VirtualGeometryPageStorage::Matches(const VirtualGeometryResource& resource) const
    {
        return !m_Path.empty() && !m_Pages.empty() && m_Locations.size() == resource.Clusters.size() &&
               m_SourceHash == SourceHash(resource);
    }
}
