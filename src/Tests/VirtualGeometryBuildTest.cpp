#include "TestBase.h"
#include "VirtualGeometryResource.h"
#include "StaticMesh.h"
#include "RHIApi.h"
#include "RHICommandList.h"
#include <cstring>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace Engine
{
    namespace
    {
        using TestPoint = std::array<float, 3>;
        using TestEdge = std::pair<TestPoint, TestPoint>;

        Core::Double3 ToDouble3(const Core::Float3& v)
        {
            return {v.X, v.Y, v.Z};
        }

        TestPoint Point(const Core::Float3& p)
        {
            return {p.X, p.Y, p.Z};
        }

        void Check(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("VirtualGeometry test: ") + message);
            }
        }

        std::shared_ptr<StaticMesh> Mesh(const std::vector<Core::Float3>& positions,
                                         const std::vector<uint32_t>& indices)
        {
            auto data = std::make_shared<StaticMeshRenderData>();
            data->LODResources.emplace_back();
            auto& lod = data->LODResources[0];
            lod.VertexBuffers.PositionBuffer.bValid = true;
            lod.VertexBuffers.PositionBuffer.NumComponents = 3;
            for (const auto& p : positions)
            {
                lod.VertexBuffers.PositionBuffer.Vertices.insert(lod.VertexBuffers.PositionBuffer.Vertices.end(),
                                                                 {p.X, p.Y, p.Z});
            }
            lod.IndexBuffer.Indices = indices;
            return std::make_shared<StaticMesh>(std::move(data));
        }

        std::shared_ptr<StaticMesh> Grid(uint32_t n, bool curved = false, bool seam = false, bool hole = false)
        {
            std::vector<Core::Float3> positions;
            std::vector<uint32_t> indices;
            std::vector<Core::Float2> uv;
            const auto vertex = [&](uint32_t x, uint32_t z, bool right)
            {
                const float px = float(x) / n, pz = float(z) / n;
                positions.emplace_back(px, curved ? 0.12f * std::sin(px * 9) * std::cos(pz * 7) : 0, pz);
                uv.emplace_back(px + (seam && right ? 2.0f : 0.0f), pz);
                return uint32_t(positions.size() - 1);
            };
            for (uint32_t z = 0; z < n; ++z)
            {
                for (uint32_t x = 0; x < n; ++x)
                {
                    if (hole && x > n / 3 && x < 2 * n / 3 && z > n / 3 && z < 2 * n / 3)
                    {
                        continue;
                    }
                    const bool right = x >= n / 2;
                    const auto a = vertex(x, z, right), b = vertex(x + 1, z, right), c = vertex(x, z + 1, right),
                               d = vertex(x + 1, z + 1, right);
                    indices.insert(indices.end(), {a, c, b, b, c, d});
                }
            }
            // Deliberately scatter input order; adjacency must drive clustering.
            std::vector<uint32_t> scattered;
            for (size_t parity = 0; parity < 2; ++parity)
            {
                for (size_t i = parity * 3; i < indices.size(); i += 6)
                {
                    scattered.insert(scattered.end(), indices.begin() + i, indices.begin() + i + 3);
                }
            }
            auto mesh = Mesh(positions, scattered);
            auto& streams = mesh->GetRenderData()->LODResources[0].VertexBuffers;
            streams.UVBuffer.bValid = true;
            streams.UVBuffer.NumComponents = 2;
            streams.ColorBuffer.bValid = true;
            streams.ColorBuffer.NumComponents = 4;
            for (size_t i = 0; i < uv.size(); ++i)
            {
                streams.UVBuffer.Vertices.insert(streams.UVBuffer.Vertices.end(), {uv[i].X, uv[i].Y});
                streams.ColorBuffer.Vertices.insert(streams.ColorBuffer.Vertices.end(),
                                                    {positions[i].X, positions[i].Z, 0.25f, 1});
            }
            // Explicit smooth normals make per-cell duplicate vertices weldable.
            streams.NormalBuffer.bValid = true;
            streams.NormalBuffer.NumComponents = 3;
            for (const auto& p : positions)
            {
                Core::Float3 normal(0, 1, 0);
                if (curved)
                {
                    normal = Core::Normalize(Core::Float3(-1.08f * std::cos(p.X * 9) * std::cos(p.Z * 7), 1,
                                                          0.84f * std::sin(p.X * 9) * std::sin(p.Z * 7)));
                }
                streams.NormalBuffer.Vertices.insert(streams.NormalBuffer.Vertices.end(),
                                                     {normal.X, normal.Y, normal.Z});
            }
            return mesh;
        }

        std::shared_ptr<StaticMesh> Cube()
        {
            std::vector<Core::Float3> p;
            std::vector<uint32_t> indices;
            for (int axis = 0; axis < 3; ++axis)
            {
                for (int sign : {-1, 1})
                {
                    Core::Float3 n;
                    n[axis] = float(sign);
                    Core::Float3 u, v;
                    u[(axis + 1) % 3] = 1;
                    v[(axis + 2) % 3] = float(sign);
                    const auto base = uint32_t(p.size());
                    for (const auto& corner : std::array<Core::Float2, 4>{{{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}})
                    {
                        p.push_back(n + u * corner.X + v * corner.Y);
                    }
                    indices.insert(indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
                }
            }
            auto mesh = Mesh(p, indices);
            auto& b = mesh->GetRenderData()->LODResources[0].VertexBuffers.NormalBuffer;
            b.bValid = true;
            b.NumComponents = 3;
            for (int axis = 0; axis < 3; ++axis)
            {
                for (int sign : {-1, 1})
                {
                    for (int j = 0; j < 4; ++j)
                    {
                        Core::Float3 n;
                        n[axis] = float(sign);
                        b.Vertices.insert(b.Vertices.end(), {n.X, n.Y, n.Z});
                    }
                }
            }
            return mesh;
        }

        std::shared_ptr<StaticMesh> SphereMesh()
        {
            constexpr uint32_t rings = 12, sides = 24;
            constexpr double pi = 3.14159265358979323846;
            std::vector<Core::Float3> positions;
            std::vector<uint32_t> indices;
            for (uint32_t y = 0; y <= rings; ++y)
            {
                for (uint32_t x = 0; x <= sides; ++x)
                {
                    const double a = pi * y / rings, b = 2 * pi * (x == sides ? 0 : x) / sides;
                    positions.push_back(
                        y == 0       ? Core::Float3(0, 1, 0)
                        : y == rings ? Core::Float3(0, -1, 0)
                                     : Core::Float3(std::sin(a) * std::cos(b), std::cos(a), std::sin(a) * std::sin(b)));
                }
            }
            for (uint32_t y = 0; y < rings; ++y)
            {
                for (uint32_t x = 0; x < sides; ++x)
                {
                    const auto a = y * (sides + 1) + x, b = a + 1, c = a + sides + 1, d = c + 1;
                    if (y > 0)
                    {
                        indices.insert(indices.end(), {a, b, c});
                    }
                    if (y + 1 < rings)
                    {
                        indices.insert(indices.end(), {b, d, c});
                    }
                }
            }
            auto mesh = Mesh(positions, indices);
            auto& n = mesh->GetRenderData()->LODResources[0].VertexBuffers.NormalBuffer;
            n.bValid = true;
            n.NumComponents = 3;
            for (const auto& p : positions)
            {
                n.Vertices.insert(n.Vertices.end(), {p.X, p.Y, p.Z});
            }
            return mesh;
        }

        std::multiset<TestEdge> Boundary(const VirtualGeometryResource& r, const std::vector<uint32_t>& clusters,
                                         uint32_t material = INVALID_VIRTUAL_GEOMETRY_INDEX, bool filter = false)
        {
            std::map<TestEdge, std::vector<TestEdge>> uses;
            for (const auto index : clusters)
            {
                const auto& c = r.Clusters.at(index);
                if (filter && c.MaterialIndex != material)
                {
                    continue;
                }
                for (uint32_t t = 0; t < c.TriangleCount; ++t)
                {
                    std::array<TestPoint, 3> p;
                    for (uint32_t j = 0; j < 3; ++j)
                    {
                        p[j] = Point(r.Positions.at(c.VertexOffset + r.Indices.at(c.IndexOffset + t * 3 + j)));
                    }
                    for (size_t j = 0; j < 3; ++j)
                    {
                        const TestEdge edge{p[j], p[(j + 1) % 3]};
                        uses[std::minmax(edge.first, edge.second)].push_back(edge);
                    }
                }
            }
            std::multiset<TestEdge> out;
            for (const auto& [edge, list] : uses)
            {
                if (list.size() != 2 || list[0].first != list[1].second || list[0].second != list[1].first)
                {
                    for (const auto& directed : list)
                    {
                        out.insert(directed);
                    }
                }
            }
            return out;
        }

        void Validate(const VirtualGeometryResource& r, size_t original)
        {
            Check(r.IsValid() && !r.RootGroupIndices.empty(), "valid nonempty CPU result");
            size_t leaves = 0;
            std::vector<int> state(r.Groups.size());
            const auto visit = [&](auto&& self, uint32_t g) -> void
            {
                Check(g < r.Groups.size() && state[g] != 1, "acyclic DAG");
                if (state[g] == 2)
                {
                    return;
                }
                state[g] = 1;
                const auto& group = r.Groups[g];
                for (uint32_t j = 0; j < group.ParentClusterCount; ++j)
                {
                    const auto p = r.GroupParentClusterIndices.at(group.ParentClusterOffset + j);
                    self(self, r.Clusters.at(p).GroupIndex);
                }
                state[g] = 2;
            };
            for (uint32_t i = 0; i < r.Groups.size(); ++i)
            {
                visit(visit, i);
            }
            for (const auto& c : r.Clusters)
            {
                Check(c.TriangleCount > 0 && c.TriangleCount <= 128, "cluster triangle cap");
                if (c.GeneratingGroupIndex == INVALID_VIRTUAL_GEOMETRY_INDEX)
                {
                    leaves += c.TriangleCount;
                }
                for (uint32_t t = 0; t < c.TriangleCount; ++t)
                {
                    std::array<Core::Double3, 3> p;
                    for (uint32_t j = 0; j < 3; ++j)
                    {
                        const auto local = r.Indices.at(c.IndexOffset + t * 3 + j);
                        Check(local < c.VertexCount, "local indices");
                        p[j] = ToDouble3(r.Positions.at(c.VertexOffset + local));
                    }
                    Check((Core::Cross(p[1] - p[0], p[2] - p[0])).Length() > 0, "nondegenerate output");
                }
                for (uint32_t v = 0; v < c.VertexCount; ++v)
                {
                    const auto p = r.Positions.at(c.VertexOffset + v);
                    Check((ToDouble3(p) - ToDouble3(c.Bounds.Sphere.Center)).Length() <= double(c.Bounds.Sphere.Radius),
                          "cluster sphere contains vertices");
                    Check(r.Bounds.Box.Contains(c.Bounds.Box), "resource AABB contains cluster");
                    Check((ToDouble3(p) - ToDouble3(r.Bounds.Sphere.Center)).Length() <= double(r.Bounds.Sphere.Radius),
                          "resource sphere contains vertices");
                    Check(p.X >= c.Bounds.Box.Min.X && p.Y >= c.Bounds.Box.Min.Y && p.Z >= c.Bounds.Box.Min.Z &&
                              p.X <= c.Bounds.Box.Max.X && p.Y <= c.Bounds.Box.Max.Y && p.Z <= c.Bounds.Box.Max.Z,
                          "cluster AABB contains vertices");
                }
            }
            Check(leaves == original, "LOD0 retains every input face");
            for (uint32_t g = 0; g < r.Groups.size(); ++g)
            {
                const auto& group = r.Groups[g];
                std::vector<uint32_t> children, parents;
                size_t before = 0, after = 0;
                for (uint32_t j = 0; j < group.ChildClusterCount; ++j)
                {
                    const auto c = r.GroupChildClusterIndices.at(group.ChildClusterOffset + j);
                    Check(r.Clusters.at(c).GroupIndex == g, "child consumer");
                    children.push_back(c);
                    before += r.Clusters[c].TriangleCount;
                    const auto generated = r.Clusters[c].GeneratingGroupIndex;
                    if (generated != INVALID_VIRTUAL_GEOMETRY_INDEX)
                    {
                        Check(group.ChildMetric.Error >= r.Groups.at(generated).ParentMetric.Error,
                              "monotone accumulated errors");
                    }
                }
                for (uint32_t j = 0; j < group.ParentClusterCount; ++j)
                {
                    const auto p = r.GroupParentClusterIndices.at(group.ParentClusterOffset + j);
                    Check(r.Clusters.at(p).GeneratingGroupIndex == g, "parent generator");
                    parents.push_back(p);
                    after += r.Clusters[p].TriangleCount;
                }
                Check(group.ChildMetric.Bounds.Box.Min == group.ParentMetric.Bounds.Box.Min &&
                          group.ChildMetric.Bounds.Box.Max == group.ParentMetric.Bounds.Box.Max &&
                          group.ChildMetric.Bounds.Sphere.Center == group.ParentMetric.Bounds.Sphere.Center &&
                          group.ChildMetric.Bounds.Sphere.Radius == group.ParentMetric.Bounds.Sphere.Radius,
                      "shared replacement bounds");
                Check(std::isfinite(group.ParentMetric.Error) && group.ParentMetric.Error >= group.ChildMetric.Error &&
                          group.ParentMetric.Error >= group.SimplificationError,
                      "finite separated error metrics");
                if (!parents.empty())
                {
                    Check(after < before, "strict triangle reduction");
                    Check(Boundary(r, children) == Boundary(r, parents), "directed external boundary equality");
                    for (const auto c : children)
                    {
                        Check(Boundary(r, children, r.Clusters[c].MaterialIndex, true) ==
                                  Boundary(r, parents, r.Clusters[c].MaterialIndex, true),
                              "material boundary equality");
                    }
                }
                children.insert(children.end(), parents.begin(), parents.end());
                for (const auto c : children)
                {
                    Check(group.ParentMetric.Bounds.Box.Contains(r.Clusters[c].Bounds.Box),
                          "group AABB contains cluster");
                    for (uint32_t v = 0; v < r.Clusters[c].VertexCount; ++v)
                    {
                        Check((ToDouble3(r.Positions.at(r.Clusters[c].VertexOffset + v)) -
                               ToDouble3(group.ParentMetric.Bounds.Sphere.Center))
                                      .Length() <= double(group.ParentMetric.Bounds.Sphere.Radius),
                              "shared group LOD sphere");
                    }
                }
            }
        }

        bool Reduced(const VirtualGeometryResource& r)
        {
            return std::any_of(r.Groups.begin(), r.Groups.end(), [](const auto& g)
            {
                return g.ParentClusterCount > 0;
            });
        }

    } // namespace

    class VirtualGeometryBuildTest final : public Test::TestBase
    {
    public:
        void Setup() override {}

        void Teardown() override {}

        void Run() override
        {
            Check(RHI::GRHIApi == nullptr, "CPU test starts without an RHI device");
            VirtualGeometryResource result;
            auto plane = Grid(12);
            const auto original = plane->GetLODResource(0).IndexBuffer.Indices;
            const auto vertices = plane->GetLODResource(0).VertexBuffers.PositionBuffer.Vertices;
            result.Build(plane.get());
            Validate(result, original.size() / 3);
            Check(Reduced(result), "plane QEM reduces triangles");
            Check(original == plane->GetLODResource(0).IndexBuffer.Indices &&
                      vertices == plane->GetLODResource(0).VertexBuffers.PositionBuffer.Vertices,
                  "traditional geometry unmodified");
            const auto firstPositions = result.Positions;
            const auto firstIndices = result.Indices;
            const auto firstChildren = result.GroupChildClusterIndices;
            const auto firstParents = result.GroupParentClusterIndices;
            result.Build(plane.get());
            Check(result.Positions == firstPositions && result.Indices == firstIndices &&
                      result.GroupChildClusterIndices == firstChildren &&
                      result.GroupParentClusterIndices == firstParents,
                  "deterministic rebuild");
            auto cube = Cube();
            result.Build(cube.get());
            Validate(result, 12);
            Check(!Reduced(result), "hard-normal cube corners are protected");
            auto sphere = SphereMesh();
            result.Build(sphere.get());
            Validate(result, sphere->GetLODResource(0).IndexBuffer.Indices.size() / 3);
            Check(Reduced(result), "sphere QEM reduces triangles");
            Check(std::any_of(result.Groups.begin(), result.Groups.end(),
                              [](const auto& g)
            {
                return g.SimplificationError > 1e-4f;
            }),
                  "curved surface geometric error is not zero QEM cost");
            bool regrouped = false, manyToMany = false;
            for (const auto& g : result.Groups)
            {
                std::set<uint32_t> generators;
                for (uint32_t j = 0; j < g.ChildClusterCount; ++j)
                {
                    const auto generated =
                        result.Clusters[result.GroupChildClusterIndices[g.ChildClusterOffset + j]].GeneratingGroupIndex;
                    if (generated != INVALID_VIRTUAL_GEOMETRY_INDEX)
                    {
                        generators.insert(generated);
                    }
                }
                if (g.ParentClusterCount && generators.size() > 1)
                {
                    regrouped = true;
                }
                if (g.ChildClusterCount > 1 && g.ParentClusterCount > 1)
                {
                    manyToMany = true;
                }
            }
            Check(regrouped && manyToMany,
                  "regrouping crosses former group boundaries and retains many-to-many replacement");
            auto terrain = Grid(24, true, true, true);
            result.Build(terrain.get());
            Validate(result, terrain->GetLODResource(0).IndexBuffer.Indices.size() / 3);
            Check(Reduced(result), "curved perforated UV-seam mesh can reduce");
            for (const auto& g : result.Groups)
            {
                if (g.ParentClusterCount)
                {
                    std::set<std::array<float, 5>> seam;
                    for (uint32_t j = 0; j < g.ChildClusterCount; ++j)
                    {
                        const auto& c = result.Clusters[result.GroupChildClusterIndices[g.ChildClusterOffset + j]];
                        for (uint32_t v = 0; v < c.VertexCount; ++v)
                        {
                            const auto p = result.Positions[c.VertexOffset + v];
                            const auto uv = result.TexCoords[c.VertexOffset + v];
                            if (p.X == 0.5f)
                            {
                                seam.insert({p.X, p.Y, p.Z, uv.X, uv.Y});
                            }
                        }
                    }
                    for (uint32_t j = 0; j < g.ParentClusterCount; ++j)
                    {
                        const auto& c = result.Clusters[result.GroupParentClusterIndices[g.ParentClusterOffset + j]];
                        for (uint32_t v = 0; v < c.VertexCount; ++v)
                        {
                            const auto p = result.Positions[c.VertexOffset + v];
                            const auto uv = result.TexCoords[c.VertexOffset + v];
                            seam.erase({p.X, p.Y, p.Z, uv.X, uv.Y});
                        }
                    }
                    Check(seam.empty(), "UV seam wedges retain their exact position and UV on each replacement");
                }
            }
            auto materials = Grid(8);
            auto& lod = materials->GetRenderData()->LODResources[0];
            materials->AddMaterial(nullptr);
            materials->AddMaterial(nullptr);
            const auto half = uint32_t(lod.IndexBuffer.Indices.size() / 2);
            lod.Sections = {{0, half, 0, 0}, {half, half, 1, 0}};
            result.Build(materials.get());
            Validate(result, lod.IndexBuffer.Indices.size() / 3);
            Check(std::any_of(result.Clusters.begin(), result.Clusters.end(),
                              [](const auto& c)
            {
                return c.MaterialIndex == 1;
            }),
                  "material slot retained");
            auto disconnected =
                Mesh({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {10, 0, 0}, {11, 0, 0}, {10, 0, 1}}, {0, 1, 2, 3, 4, 5});
            result.Build(disconnected.get());
            Validate(result, 2);
            Check(result.RootGroupIndices.size() == 2, "disconnected islands have multiple roots");
            auto nonmanifold =
                Mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}}, {0, 1, 2, 1, 0, 3, 0, 1, 4});
            result.Build(nonmanifold.get());
            Validate(result, 3);
            Check(!Reduced(result), "non-manifold edge preserved conservatively");
            auto base = Mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {2, 3, 4});
            base->GetRenderData()->LODResources[0].Sections = {{0, 3, -1, -2}};
            result.Build(base.get());
            Validate(result, 1);
            auto positive = Mesh({{9, 9, 9}, {9, 9, 9}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2});
            positive->GetRenderData()->LODResources[0].Sections = {{0, 3, -1, 2}};
            result.Build(positive.get());
            Validate(result, 1);
            for (float scale : {1e-8f, 1e8f})
            {
                auto scaled = Grid(5);
                for (auto& v : scaled->GetRenderData()->LODResources[0].VertexBuffers.PositionBuffer.Vertices)
                {
                    v *= scale;
                }
                result.Build(scaled.get());
                Validate(result, 50);
                Check(Reduced(result), "scale-independent geometric predicates");
            }
            auto degenerate = Mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2, 0, 0, 1});
            result.Build(degenerate.get());
            Validate(result, 1);
            Check(RHI::GRHIApi == nullptr, "CPU build never initialized an RHI device");
            std::cout << "VIRTUAL_GEOMETRY_CPU_BUILD_TESTS_OK" << std::endl;
        }
    };

    // Each invalid input is run separately. Tests/main.cpp reports the thrown
    // validation error; destruction then checks the failed resource's state.
    class VirtualGeometryInvalidInputTest final : public Test::TestBase
    {
    public:
        explicit VirtualGeometryInvalidInputTest(uint32_t testCase) : m_TestCase(testCase) {}

        ~VirtualGeometryInvalidInputTest() override
        {
            if (!m_bCheckFailureState)
            {
                return;
            }
            const bool empty = !m_Resource.IsValid() && m_Resource.Clusters.empty() && m_Resource.Groups.empty() &&
                               m_Resource.GroupChildClusterIndices.empty() &&
                               m_Resource.GroupParentClusterIndices.empty() && m_Resource.Positions.empty() &&
                               m_Resource.Normals.empty() && m_Resource.Tangents.empty() &&
                               m_Resource.TexCoords.empty() && m_Resource.Colors.empty() &&
                               m_Resource.Indices.empty() && m_Resource.RootGroupIndices.empty();
            if (!empty || RHI::GRHIApi != nullptr)
            {
                std::cerr << "VIRTUAL_GEOMETRY_INVALID_RESOURCE_STATE" << std::endl;
                std::abort();
            }
            std::cout << "VIRTUAL_GEOMETRY_FAILED_BUILD_STATE_EMPTY_OK" << std::endl;
        }

        void Setup() override
        {
            Check(RHI::GRHIApi == nullptr, "invalid input test starts without RHI");
            const auto valid = Mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2});
            m_Resource.Build(valid.get());
            Check(m_Resource.IsValid(), "invalid rebuild starts with a successful resource");
        }

        void Run() override
        {
            switch (m_TestCase)
            {
            case 0:
                m_Input = Mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 999});
                break;
            case 1:
                break; // Null input.
            case 2:
                m_Input = Mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2, 2, 1, 0});
                break;
            case 3:
                m_Input = Grid(2);
                m_Input->GetRenderData()->LODResources[0].VertexBuffers.PositionBuffer.Vertices[0] =
                    std::numeric_limits<float>::quiet_NaN();
                break;
            case 4:
                m_Input = Grid(2);
                m_Input->GetRenderData()->LODResources[0].Sections = {{0, 3, -1, 0}};
                break;
            case 5:
                m_Input = Grid(2);
                m_Input->GetRenderData()->LODResources[0].VertexBuffers.UVBuffer.Vertices.pop_back();
                break;
            case 6:
                m_Input = Grid(2);
                m_Input->GetRenderData()->LODResources[0].Sections = {{0, 24, 1, 0}};
                break;
            case 7:
                m_Input = std::make_shared<StaticMesh>();
                break;
            default:
                Check(false, "unknown invalid input case");
            }
            m_bCheckFailureState = true;
            m_Resource.Build(m_Input.get());
            Check(false, "invalid input unexpectedly built successfully");
        }

        void Teardown() override {}

    private:
        uint32_t m_TestCase = 0;
        bool m_bCheckFailureState = false;
        VirtualGeometryResource m_Resource;
        std::shared_ptr<StaticMesh> m_Input;
    };

    class VirtualGeometryResourceRHITest final : public Test::TestBase
    {
    public:
        void Setup() override
        {
            RHI::GRHIApi = Test::CreateTestRHIApi();
            RHI::GRHIApi->Init();
        }

        void Run() override
        {
            auto mesh = Grid(5);
            auto& resource = mesh->GetRenderData()->VirtualGeometryData;
            resource.InitializeResources();
            Check(!resource.PositionBuffer, "unbuilt geometry creates no RHI buffer");
            resource.Build(mesh.get());
            resource.InitializeResources();

            const auto verify = [](const auto& data, const auto& buffer)
            {
                if (data.empty())
                {
                    Check(!buffer, "empty array has no GPU buffer");
                    return;
                }
                Check(buffer && buffer->IsInitialized() && buffer->GetRHI(), "RHI buffer initialized");
                const size_t bytes = data.size() * sizeof(data[0]);
                const auto& desc = buffer->GetRHI()->GetDesc();
                Check(desc.Size == bytes && desc.Stride == sizeof(data[0]), "GPU size and stride match CPU array");
                Check(buffer->GetTracker().GetLastAccess() == RHI::ERHIResourceAccess::SRV,
                      "upload finishes with explicit shader-read access");
                RenderCore::TransitionBufferImmediate(RHI::GRHIApi, buffer.get(), RHI::ERHIResourceAccess::TransferSrc,
                                                      RHI::EQueueType::Graphics);
                auto* queue = RHI::GRHIApi->GetQueue(RHI::EQueueType::Graphics);
                auto* context = queue->AcquireCommandContext();
                RHI::RHICommandListBase commands(context);
                commands.SetImmediate(true);
                commands.Begin();
                RHI::RHIReadBufferInfo info{};
                info.Size = bytes;
                void* mapped = RHI::GRHIApi->MapReadBuffer(commands, buffer->GetRHI(), info);
                Check(mapped != nullptr, "GPU readback mapping succeeds");
                commands.End();
                RHI::RHIFence fence;
                fence.QueueType = RHI::EQueueType::Graphics;
                fence.Value = queue->ExecuteContext(context);
                queue->WaitValue(fence.Value);
                buffer->GetTracker().UpdateLastAccessFence(fence);
                const bool matches = std::memcmp(mapped, data.data(), bytes) == 0;
                RHI::GRHIApi->Unmap(mapped);
                Check(matches, "GPU readback exactly matches CPU bytes");
                RenderCore::TransitionBufferImmediate(RHI::GRHIApi, buffer.get(), RHI::ERHIResourceAccess::SRV,
                                                      RHI::EQueueType::Graphics);
            };
            verify(resource.Positions, resource.PositionBuffer);
            verify(resource.Normals, resource.NormalBuffer);
            verify(resource.Tangents, resource.TangentBuffer);
            verify(resource.TexCoords, resource.TexCoordBuffer);
            verify(resource.Colors, resource.ColorBuffer);
            verify(resource.Indices, resource.IndexBuffer);
            verify(resource.Clusters, resource.ClusterBuffer);
            verify(resource.Groups, resource.GroupBuffer);
            verify(resource.GroupChildClusterIndices, resource.GroupChildClusterIndexBuffer);
            verify(resource.GroupParentClusterIndices, resource.GroupParentClusterIndexBuffer);
            verify(resource.RootGroupIndices, resource.RootGroupIndexBuffer);
            const auto positions = resource.Positions;
            resource.InitializeResources();
            verify(resource.Positions, resource.PositionBuffer);
            resource.ReleaseResources();
            Check(resource.IsValid() && resource.Positions == positions, "RHI release retains CPU geometry");
            Check(!resource.PositionBuffer && !resource.NormalBuffer && !resource.TangentBuffer &&
                      !resource.TexCoordBuffer && !resource.ColorBuffer && !resource.IndexBuffer &&
                      !resource.ClusterBuffer && !resource.GroupBuffer && !resource.GroupChildClusterIndexBuffer &&
                      !resource.GroupParentClusterIndexBuffer && !resource.RootGroupIndexBuffer,
                  "all RHI buffers released");
            resource.ReleaseResources();
            resource.InitializeResources();
            verify(resource.Indices, resource.IndexBuffer);
            resource.Clear();
            Check(!resource.IsValid() && resource.Bounds.Box.IsEmpty() && !resource.PositionBuffer &&
                      !resource.GroupBuffer,
                  "Clear resets CPU geometry, bounds and GPU buffers");
            std::cout << "VIRTUAL_GEOMETRY_RHI_RESOURCE_TEST_OK" << std::endl;
        }

        void Teardown() override
        {
            RHI::GRHIApi->Shutdown();
            delete RHI::GRHIApi;
            RHI::GRHIApi = nullptr;
        }
    };

    const bool RegisteredVirtualGeometryBuildTest = []()
    {
        Test::RenderTestRegistry::Register("VirtualGeometryResourceRHITest", []()
        {
            return std::make_unique<VirtualGeometryResourceRHITest>();
        });
        Test::RenderTestRegistry::Register("VirtualGeometryBuildTest", []()
        {
            return std::make_unique<VirtualGeometryBuildTest>();
        });
        for (uint32_t testCase = 0; testCase < 8; ++testCase)
        {
            Test::RenderTestRegistry::Register("VirtualGeometryInvalidInput" + std::to_string(testCase), [testCase]()
            {
                return std::make_unique<VirtualGeometryInvalidInputTest>(testCase);
            });
        }
        return true;
    }();
} // namespace Engine
