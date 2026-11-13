#include "VirtualGeometryResource.h"
#include "StaticMesh.h"
#include "DistanceFieldMgr.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <type_traits>

namespace Engine
{
    namespace
    {
        using PositionKey = std::array<float, 3>;
        using Edge = std::pair<uint32_t, uint32_t>;
        using Graph = std::vector<std::map<uint32_t, double>>;
        constexpr uint32_t GroupSize = 4;

        struct BuildVertex
        {
            Core::Float3 Position{}, Normal{};
            Core::Float4 Tangent{1, 0, 0, 1}, Color{1, 1, 1, 1};
            Core::Float2 UV{};
        };

        struct BuildTriangle
        {
            std::array<uint32_t, 3> Vertices{};
            uint32_t Material = INVALID_VIRTUAL_GEOMETRY_INDEX;
        };

        struct BuildMesh
        {
            std::vector<BuildVertex> Vertices;
            std::vector<BuildTriangle> Triangles;
        };

        struct EdgeUse
        {
            uint32_t Triangle = 0, From = 0, To = 0;
        };

        struct Topology
        {
            std::vector<Core::Float3> Positions;
            std::vector<uint32_t> VertexGeometry;
            std::vector<std::array<uint32_t, 3>> Faces;
            std::vector<std::vector<uint32_t>> Incident;
            std::map<Edge, std::vector<EdgeUse>> Edges;
        };

        using Boundary = std::multiset<std::pair<PositionKey, PositionKey>>;

        void Require(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("VirtualGeometry: ") + message);
            }
        }

        uint32_t Index(size_t value)
        {
            Require(value < INVALID_VIRTUAL_GEOMETRY_INDEX, "32-bit resource capacity exceeded");
            return static_cast<uint32_t>(value);
        }

        PositionKey Key(const Core::Float3& p)
        {
            return {p.X, p.Y, p.Z};
        }

        Edge MakeEdge(uint32_t a, uint32_t b)
        {
            return std::minmax(a, b);
        }

        template <typename V> bool Finite(const V& v)
        {
            for (const auto component : v.Data)
            {
                if (!std::isfinite(component))
                {
                    return false;
                }
            }
            return true;
        }

        Core::Double2 ToDouble2(const Core::Float2& v)
        {
            return {v.X, v.Y};
        }

        Core::Double3 ToDouble3(const Core::Float3& v)
        {
            return {v.X, v.Y, v.Z};
        }

        Core::Double3 ToDouble3(const Core::Float4& v)
        {
            return {v.X, v.Y, v.Z};
        }

        Core::Float3 ToFloat3(const Core::Double3& v)
        {
            return {static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)};
        }

        Core::Double3 Cross(const BuildMesh& mesh, const BuildTriangle& t)
        {
            const auto a = ToDouble3(mesh.Vertices[t.Vertices[0]].Position);
            return Core::Cross(ToDouble3(mesh.Vertices[t.Vertices[1]].Position) - a,
                               ToDouble3(mesh.Vertices[t.Vertices[2]].Position) - a);
        }

        bool Nondegenerate(const BuildMesh& mesh, const BuildTriangle& t)
        {
            const auto a = ToDouble3(mesh.Vertices[t.Vertices[0]].Position),
                       b = ToDouble3(mesh.Vertices[t.Vertices[1]].Position),
                       c = ToDouble3(mesh.Vertices[t.Vertices[2]].Position);
            const double length2 =
                std::max({Core::Dot(b - a, b - a), Core::Dot(c - b, c - b), Core::Dot(a - c, a - c)});
            const auto normal = Cross(mesh, t);
            return length2 > 0 && Core::Dot(normal, normal) > length2 * length2 * 1e-24;
        }

        std::array<float, 16> VertexKey(const BuildVertex& v)
        {
            return {v.Position.X, v.Position.Y, v.Position.Z, v.Normal.X,  v.Normal.Y, v.Normal.Z,
                    v.Tangent.X,  v.Tangent.Y,  v.Tangent.Z,  v.Tangent.W, v.UV.X,     v.UV.Y,
                    v.Color.X,    v.Color.Y,    v.Color.Z,    v.Color.W};
        }

        BuildMesh Compact(const BuildMesh& input)
        {
            BuildMesh result;
            std::map<std::array<float, 16>, uint32_t> vertices;
            for (auto triangle : input.Triangles)
            {
                for (auto& index : triangle.Vertices)
                {
                    const auto& vertex = input.Vertices[index];
                    const auto [it, inserted] = vertices.emplace(VertexKey(vertex), Index(result.Vertices.size()));
                    if (inserted)
                    {
                        result.Vertices.push_back(vertex);
                    }
                    index = it->second;
                }
                result.Triangles.push_back(triangle);
            }
            return result;
        }

        BuildMesh Merge(const std::vector<BuildMesh>& meshes, const std::vector<uint32_t>& indices)
        {
            BuildMesh result;
            for (const auto index : indices)
            {
                const auto& mesh = meshes[index];
                const auto offset = Index(result.Vertices.size());
                Index(result.Vertices.size() + mesh.Vertices.size());
                result.Vertices.insert(result.Vertices.end(), mesh.Vertices.begin(), mesh.Vertices.end());
                for (auto triangle : mesh.Triangles)
                {
                    for (auto& vertex : triangle.Vertices)
                    {
                        vertex += offset;
                    }
                    result.Triangles.push_back(triangle);
                }
            }
            return Compact(result);
        }

        Topology MakeTopology(const BuildMesh& mesh)
        {
            Topology result;
            std::map<PositionKey, uint32_t> positions;
            for (const auto& vertex : mesh.Vertices)
            {
                const auto [it, inserted] = positions.emplace(Key(vertex.Position), Index(result.Positions.size()));
                if (inserted)
                {
                    result.Positions.push_back(vertex.Position);
                }
                result.VertexGeometry.push_back(it->second);
            }
            result.Incident.resize(result.Positions.size());
            for (const auto& triangle : mesh.Triangles)
            {
                const auto index = Index(result.Faces.size());
                std::array<uint32_t, 3> face;
                for (size_t i = 0; i < 3; ++i)
                {
                    Require(triangle.Vertices[i] < mesh.Vertices.size(), "invalid triangle vertex");
                    face[i] = result.VertexGeometry[triangle.Vertices[i]];
                    result.Incident[face[i]].push_back(index);
                }
                result.Faces.push_back(face);
                for (size_t i = 0; i < 3; ++i)
                {
                    const auto a = face[i], b = face[(i + 1) % 3];
                    result.Edges[MakeEdge(a, b)].push_back({index, a, b});
                }
            }
            return result;
        }

        bool ManifoldEdge(const std::vector<EdgeUse>& uses)
        {
            return uses.size() == 2 && uses[0].From == uses[1].To && uses[0].To == uses[1].From;
        }

        Boundary GetBoundary(const Topology& topology)
        {
            Boundary result;
            // Directed multiplicity protects holes, winding and non-manifold
            // edges; fixed endpoint positions alone do not preserve topology.
            for (const auto& [edge, uses] : topology.Edges)
            {
                if (!ManifoldEdge(uses))
                {
                    for (const auto& use : uses)
                    {
                        result.emplace(Key(topology.Positions[use.From]), Key(topology.Positions[use.To]));
                    }
                }
            }
            return result;
        }

        bool InteriorFan(const Topology& topology, uint32_t vertex)
        {
            std::map<uint32_t, std::set<uint32_t>> link;
            for (const auto faceId : topology.Incident[vertex])
            {
                const auto& face = topology.Faces[faceId];
                std::vector<uint32_t> other;
                for (const auto p : face)
                {
                    if (p != vertex)
                    {
                        other.push_back(p);
                    }
                }
                if (other.size() != 2)
                {
                    return false;
                }
                link[other[0]].insert(other[1]);
                link[other[1]].insert(other[0]);
            }
            if (link.size() < 3)
            {
                return false;
            }
            for (const auto& [p, neighbors] : link)
            {
                if (neighbors.size() != 2)
                {
                    return false;
                }
            }
            std::set<uint32_t> visited;
            std::vector<uint32_t> pending{link.begin()->first};
            while (!pending.empty())
            {
                const auto p = pending.back();
                pending.pop_back();
                if (!visited.insert(p).second)
                {
                    continue;
                }
                for (const auto n : link.at(p))
                {
                    pending.push_back(n);
                }
            }
            return visited.size() == link.size();
        }

        std::set<PositionKey> ProtectedVertices(const BuildMesh& mesh, const Topology& topology)
        {
            std::set<PositionKey> result;
            std::vector<std::set<std::array<float, 16>>> variants(topology.Positions.size());
            std::vector<std::set<uint32_t>> materials(topology.Positions.size());
            for (size_t i = 0; i < mesh.Vertices.size(); ++i)
            {
                variants[topology.VertexGeometry[i]].insert(VertexKey(mesh.Vertices[i]));
            }
            for (size_t i = 0; i < mesh.Triangles.size(); ++i)
            {
                for (const auto p : topology.Faces[i])
                {
                    materials[p].insert(mesh.Triangles[i].Material);
                }
            }
            for (uint32_t i = 0; i < Index(topology.Positions.size()); ++i)
            {
                if (variants[i].size() > 1 || materials[i].size() > 1 || !InteriorFan(topology, i))
                {
                    result.insert(Key(topology.Positions[i]));
                }
            }
            for (const auto& [edge, uses] : topology.Edges)
            {
                if (!ManifoldEdge(uses))
                {
                    result.insert(Key(topology.Positions[edge.first]));
                    result.insert(Key(topology.Positions[edge.second]));
                }
            }
            return result;
        }

        void ValidateFaces(const BuildMesh& mesh)
        {
            const auto topology = MakeTopology(mesh);
            std::set<std::array<uint32_t, 3>> faces;
            for (size_t i = 0; i < mesh.Triangles.size(); ++i)
            {
                Require(Nondegenerate(mesh, mesh.Triangles[i]), "degenerate triangle");
                auto key = topology.Faces[i];
                std::sort(key.begin(), key.end());
                Require(faces.insert(key).second, "duplicate geometric face (including opposite winding)");
            }
        }

        float RoundUp(double value)
        {
            Require(std::isfinite(value) && value >= 0 && value < std::numeric_limits<float>::max(),
                    "non-finite bound or error");
            return value == 0 ? 0 : std::nextafter(static_cast<float>(value), std::numeric_limits<float>::infinity());
        }

        Core::BoxSphereBounds MeshBounds(const BuildMesh& mesh)
        {
            Require(!mesh.Vertices.empty(), "empty bounds");
            Core::BoxSphereBounds bounds;
            for (const auto& vertex : mesh.Vertices)
            {
                bounds.Box.ExpandBy(vertex.Position);
            }
            const auto center = ToFloat3(ToDouble3(bounds.Box.Min) * 0.5 + ToDouble3(bounds.Box.Max) * 0.5);
            double radius = 0;
            for (const auto& vertex : mesh.Vertices)
            {
                radius = std::max(radius, (ToDouble3(vertex.Position) - ToDouble3(center)).Length());
            }
            bounds.Sphere = {center, RoundUp(radius)};
            return bounds;
        }

        Core::BoundingSphere UnionSphere(const Core::BoundingSphere& a, const Core::BoundingSphere& b)
        {
            const auto delta = ToDouble3(b.Center) - ToDouble3(a.Center);
            const double distance = (delta).Length();
            if (double(a.Radius) >= distance + b.Radius)
            {
                return a;
            }
            if (double(b.Radius) >= distance + a.Radius)
            {
                return b;
            }
            const double radius = (distance + a.Radius + b.Radius) * 0.5;
            const auto exact =
                ToDouble3(a.Center) + (distance > 0 ? delta * ((radius - a.Radius) / distance) : Core::Double3{});
            const auto center = ToFloat3(exact);
            return {center, RoundUp(radius + (exact - ToDouble3(center)).Length())};
        }

        Core::BoxSphereBounds UnionBounds(const Core::BoxSphereBounds& a, const Core::BoxSphereBounds& b)
        {
            if (a.Box.IsEmpty())
            {
                return b;
            }
            if (b.Box.IsEmpty())
            {
                return a;
            }
            Core::BoxSphereBounds bounds;
            bounds.Box = a.Box;
            bounds.Box.Merge(b.Box);
            // Preserve the rounded, conservative sphere of the descendant region.
            bounds.Sphere = UnionSphere(a.Sphere, b.Sphere);
            return bounds;
        }

        bool ValidBounds(const Core::BoxSphereBounds& bounds)
        {
            return !bounds.Box.IsEmpty() && Finite(bounds.Box.Min) && Finite(bounds.Box.Max) &&
                   Finite(bounds.Sphere.Center) && std::isfinite(bounds.Sphere.Radius) && bounds.Sphere.Radius >= 0;
        }

        // Shared edge length rewards small cuts; centroid distance discourages
        // thin regions. Candidates must belong to the adjacency frontier.
        std::vector<std::vector<uint32_t>> Partition(const Graph& graph, const std::vector<Core::Double3>& centers,
                                                     const std::vector<double>& perimeters, uint32_t limit)
        {
            std::vector<uint32_t> order(graph.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
            {
                return std::tie(centers[a].X, centers[a].Y, centers[a].Z, a) <
                       std::tie(centers[b].X, centers[b].Y, centers[b].Z, b);
            });
            std::vector<bool> used(graph.size());
            std::vector<std::vector<uint32_t>> result;
            for (const auto seed : order)
            {
                if (!used[seed])
                {
                    std::vector<uint32_t> part;
                    std::set<uint32_t> frontier{seed};
                    Core::Double3 center;
                    const double scale = std::max(perimeters[seed], 1e-30);
                    while (!frontier.empty() && part.size() < limit)
                    {
                        uint32_t best = *frontier.begin();
                        double bestScore = std::numeric_limits<double>::infinity();
                        for (const auto candidate : frontier)
                        {
                            double shared = 0;
                            for (const auto n : part)
                            {
                                if (const auto it = graph[candidate].find(n); it != graph[candidate].end())
                                {
                                    shared += it->second;
                                }
                            }
                            const double distance =
                                part.empty() ? 0 : (centers[candidate] - center / double(part.size())).Length();
                            const double score = (perimeters[candidate] - 2 * shared) / scale + 0.15 * distance / scale;
                            if (score < bestScore)
                            {
                                bestScore = score;
                                best = candidate;
                            }
                        }
                        frontier.erase(best);
                        used[best] = true;
                        part.push_back(best);
                        center += centers[best];
                        for (const auto& [neighbor, length] : graph[best])
                        {
                            if (!used[neighbor])
                            {
                                frontier.insert(neighbor);
                            }
                        }
                    }
                    result.push_back(std::move(part));
                }
            }
            return result;
        }

        std::vector<BuildMesh> Cluster(const BuildMesh& mesh)
        {
            const auto topology = MakeTopology(mesh);
            Graph graph(mesh.Triangles.size());
            std::vector<Core::Double3> centers(graph.size(), Core::Double3{});
            std::vector<double> perimeters(graph.size());
            for (size_t i = 0; i < graph.size(); ++i)
            {
                for (const auto v : mesh.Triangles[i].Vertices)
                {
                    centers[i] += ToDouble3(mesh.Vertices[v].Position) / 3.0;
                }
                for (size_t j = 0; j < 3; ++j)
                {
                    perimeters[i] += (ToDouble3(topology.Positions[topology.Faces[i][j]]) -
                                      ToDouble3(topology.Positions[topology.Faces[i][(j + 1) % 3]]))
                                         .Length();
                }
            }
            for (const auto& [edge, uses] : topology.Edges)
            {
                if (ManifoldEdge(uses) &&
                    mesh.Triangles[uses[0].Triangle].Material == mesh.Triangles[uses[1].Triangle].Material)
                {
                    const double length =
                        (ToDouble3(topology.Positions[edge.first]) - ToDouble3(topology.Positions[edge.second]))
                            .Length();
                    graph[uses[0].Triangle][uses[1].Triangle] += length;
                    graph[uses[1].Triangle][uses[0].Triangle] += length;
                }
            }
            std::vector<BuildMesh> result;
            for (const auto& part : Partition(graph, centers, perimeters, VIRTUAL_GEOMETRY_MAX_CLUSTER_TRIANGLES))
            {
                BuildMesh cluster;
                std::map<uint32_t, uint32_t> remap;
                for (const auto t : part)
                {
                    auto triangle = mesh.Triangles[t];
                    for (auto& v : triangle.Vertices)
                    {
                        const auto [it, inserted] = remap.emplace(v, Index(cluster.Vertices.size()));
                        if (inserted)
                        {
                            cluster.Vertices.push_back(mesh.Vertices[v]);
                        }
                        v = it->second;
                    }
                    cluster.Triangles.push_back(triangle);
                }
                result.push_back(std::move(cluster));
            }
            return result;
        }

        struct FrontierData
        {
            BuildMesh Mesh;
            Topology Adjacency;
            Graph Neighbors;
            std::vector<double> Perimeters;
            std::vector<Core::Double3> Centers;
            std::set<PositionKey> Protected;
        };

        FrontierData Frontier(const std::vector<BuildMesh>& meshes, const std::vector<uint32_t>& active)
        {
            FrontierData result;
            result.Mesh = Merge(meshes, active);
            result.Adjacency = MakeTopology(result.Mesh);
            result.Protected = ProtectedVertices(result.Mesh, result.Adjacency);
            result.Neighbors.resize(active.size());
            result.Perimeters.resize(active.size());
            result.Centers.resize(active.size());
            std::vector<uint32_t> owner;
            for (uint32_t i = 0; i < Index(active.size()); ++i)
            {
                for (size_t t = 0; t < meshes[active[i]].Triangles.size(); ++t)
                {
                    owner.push_back(i);
                }
                result.Centers[i] = ToDouble3(MeshBounds(meshes[active[i]]).Sphere.Center);
            }
            for (const auto& [edge, uses] : result.Adjacency.Edges)
            {
                const double length = (ToDouble3(result.Adjacency.Positions[edge.first]) -
                                       ToDouble3(result.Adjacency.Positions[edge.second]))
                                          .Length();
                if (ManifoldEdge(uses) && owner[uses[0].Triangle] == owner[uses[1].Triangle])
                {
                    continue;
                }
                for (const auto& use : uses)
                {
                    result.Perimeters[owner[use.Triangle]] += length;
                }
                if (ManifoldEdge(uses))
                {
                    const auto a = owner[uses[0].Triangle], b = owner[uses[1].Triangle];
                    result.Neighbors[a][b] += length;
                    result.Neighbors[b][a] += length;
                }
            }
            return result;
        }

        struct Quadric
        {
            Core::Mat4d Matrix{0};

            double Cost(const Core::Double3& p) const
            {
                const Core::Double4 h(p, 1);
                // Matrix is row-major; keep the four-term product grouped explicitly.
                const auto product = [&](size_t row)
                {
                    return (Matrix(row, 0) * h.X + Matrix(row, 1) * h.Y) +
                           (Matrix(row, 2) * h.Z + Matrix(row, 3) * h.W);
                };
                return std::max(0.0, (h.X * product(0) + h.Y * product(1)) + (h.Z * product(2) + h.W * product(3)));
            }

            void AddPlane(const Core::Double4& plane)
            {
                for (size_t row = 0; row < 4; ++row)
                {
                    for (size_t col = 0; col < 4; ++col)
                    {
                        Matrix(row, col) += plane[row] * plane[col];
                    }
                }
            }

            bool Optimum(Core::Double3& p) const
            {
                double a[3][4];
                double scale = 0;
                for (int row = 0; row < 3; ++row)
                {
                    for (int col = 0; col < 4; ++col)
                    {
                        a[row][col] = col == 3 ? -Matrix(row, 3) : Matrix(row, col);
                        if (col < 3)
                        {
                            scale = std::max(scale, std::abs(a[row][col]));
                        }
                    }
                }
                if (scale == 0)
                {
                    return false;
                }
                for (int i = 0; i < 3; ++i)
                {
                    int pivot = i;
                    for (int row = i + 1; row < 3; ++row)
                    {
                        if (std::abs(a[row][i]) > std::abs(a[pivot][i]))
                        {
                            pivot = row;
                        }
                    }
                    if (std::abs(a[pivot][i]) < scale * 1e-12)
                    {
                        return false;
                    }
                    for (int col = i; col < 4; ++col)
                    {
                        std::swap(a[i][col], a[pivot][col]);
                    }
                    const double divisor = a[i][i];
                    for (int col = i; col < 4; ++col)
                    {
                        a[i][col] /= divisor;
                    }
                    for (int row = 0; row < 3; ++row)
                    {
                        if (row != i)
                        {
                            const double factor = a[row][i];
                            for (int col = i; col < 4; ++col)
                            {
                                a[row][col] -= factor * a[i][col];
                            }
                        }
                    }
                }
                p = {a[0][3], a[1][3], a[2][3]};
                return std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z);
            }
        };

        struct Collapse
        {
            double Cost = 0;
            uint32_t A = 0, B = 0;
            Core::Float3 Position{};

            bool operator<(const Collapse& other) const
            {
                return std::tie(Cost, A, B, Position.X, Position.Y, Position.Z) >
                       std::tie(other.Cost, other.A, other.B, other.Position.X, other.Position.Y, other.Position.Z);
            }
        };

        BuildVertex Interpolate(const BuildVertex& a, const BuildVertex& b, const Core::Float3& position)
        {
            const auto edge = ToDouble3(b.Position) - ToDouble3(a.Position);
            const float t = static_cast<float>(std::clamp(
                Core::Dot(ToDouble3(position) - ToDouble3(a.Position), edge) / Core::Dot(edge, edge), 0.0, 1.0));
            BuildVertex v;
            v.Position = position;
            v.UV = Core::Lerp(a.UV, b.UV, t);
            v.Color = Core::Lerp(a.Color, b.Color, t);
            const auto mixed = Core::Lerp(ToDouble3(a.Normal), ToDouble3(b.Normal), double(t));
            const auto normal =
                (mixed).Length() > 1e-20 ? Core::Normalize(mixed) : Core::Normalize(ToDouble3(a.Normal));
            v.Normal = ToFloat3(normal);
            auto tangent = Core::Lerp(ToDouble3(a.Tangent), ToDouble3(b.Tangent), double(t));
            tangent -= normal * Core::Dot(normal, tangent);
            if ((tangent).Length() < 1e-20)
            {
                tangent =
                    Core::Cross(normal, std::abs(normal.X) < 0.9 ? Core::Double3(1, 0, 0) : Core::Double3(0, 1, 0));
            }
            v.Tangent = {ToFloat3(Core::Normalize(tangent)), a.Tangent.W};
            return v;
        }

        bool TryCollapse(const BuildMesh& mesh, const Topology& topology, const Collapse& candidate,
                         const Boundary& boundary, const std::set<PositionKey>& locked, BuildMesh& result)
        {
            const auto a = candidate.A, b = candidate.B, ga = topology.VertexGeometry[a],
                       gb = topology.VertexGeometry[b];
            const auto& uses = topology.Edges.at(MakeEdge(ga, gb));
            std::set<uint32_t> na, nb, opposite;
            for (const auto f : topology.Incident[ga])
            {
                for (const auto v : topology.Faces[f])
                {
                    if (v != ga)
                    {
                        na.insert(v);
                    }
                }
            }
            for (const auto f : topology.Incident[gb])
            {
                for (const auto v : topology.Faces[f])
                {
                    if (v != gb)
                    {
                        nb.insert(v);
                    }
                }
            }
            for (const auto& use : uses)
            {
                for (const auto v : topology.Faces[use.Triangle])
                {
                    if (v != ga && v != gb)
                    {
                        opposite.insert(v);
                    }
                }
            }
            std::vector<uint32_t> common;
            std::set_intersection(na.begin(), na.end(), nb.begin(), nb.end(), std::back_inserter(common));
            // Link condition + duplicate-face/fan checks also reject the
            // tetrahedron case, which a neighbor count alone would miss.
            if (opposite.size() != 2 || std::set<uint32_t>(common.begin(), common.end()) != opposite)
            {
                return false;
            }
            for (size_t i = 0; i < mesh.Vertices.size(); ++i)
            {
                if (i != a && i != b && Key(mesh.Vertices[i].Position) == Key(candidate.Position))
                {
                    return false;
                }
            }
            result = mesh;
            result.Vertices[a] = Interpolate(mesh.Vertices[a], mesh.Vertices[b], candidate.Position);
            result.Triangles.clear();
            for (const auto& triangle : mesh.Triangles)
            {
                const bool hasA =
                    std::find(triangle.Vertices.begin(), triangle.Vertices.end(), a) != triangle.Vertices.end();
                const bool hasB =
                    std::find(triangle.Vertices.begin(), triangle.Vertices.end(), b) != triangle.Vertices.end();
                if (hasA && hasB)
                {
                    continue;
                }
                auto changed = triangle;
                for (auto& v : changed.Vertices)
                {
                    if (v == b)
                    {
                        v = a;
                    }
                }
                if (hasA || hasB)
                {
                    if (!Nondegenerate(result, changed))
                    {
                        return false;
                    }
                    const auto before = Cross(mesh, triangle), after = Cross(result, changed);
                    if (Core::Dot(before, after) <= 0.1 * (before).Length() * (after).Length())
                    {
                        return false;
                    }
                    const auto uvArea = [](const BuildMesh& input, const BuildTriangle& face)
                    {
                        const auto a = ToDouble2(input.Vertices[face.Vertices[1]].UV) -
                                       ToDouble2(input.Vertices[face.Vertices[0]].UV),
                                   b = ToDouble2(input.Vertices[face.Vertices[2]].UV) -
                                       ToDouble2(input.Vertices[face.Vertices[0]].UV);
                        return a.X * b.Y - a.Y * b.X;
                    };
                    const double uvBefore = uvArea(mesh, triangle), uvAfter = uvArea(result, changed);
                    if (uvBefore != 0 && uvBefore * uvAfter <= 0)
                    {
                        return false;
                    }
                }
                result.Triangles.push_back(changed);
            }
            const auto next = MakeTopology(result);
            std::set<std::array<uint32_t, 3>> faces;
            for (auto face : next.Faces)
            {
                std::sort(face.begin(), face.end());
                if (!faces.insert(face).second)
                {
                    return false;
                }
            }
            if (GetBoundary(next) != boundary)
            {
                return false;
            }
            for (uint32_t v = 0; v < Index(next.Positions.size()); ++v)
            {
                if (!next.Incident[v].empty() && !locked.contains(Key(next.Positions[v])) && !InteriorFan(next, v))
                {
                    return false;
                }
            }
            return !result.Triangles.empty();
        }

        BuildMesh Simplify(const BuildMesh& input, const std::set<PositionKey>& globalLocked)
        {
            BuildMesh mesh = input;
            const auto initial = MakeTopology(mesh);
            auto locked = ProtectedVertices(mesh, initial);
            locked.insert(globalLocked.begin(), globalLocked.end());
            const auto boundary = GetBoundary(initial);
            Core::Double3 low = ToDouble3(mesh.Vertices.front().Position), high = low;
            for (const auto& v : mesh.Vertices)
            {
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    low[axis] = std::min(low[axis], double(v.Position[axis]));
                    high[axis] = std::max(high[axis], double(v.Position[axis]));
                }
            }
            const auto origin = low * 0.5 + high * 0.5;
            const double scale = std::max({(high - low).X, (high - low).Y, (high - low).Z});
            std::vector<Quadric> quadrics(mesh.Vertices.size());
            for (const auto& t : mesh.Triangles)
            {
                const auto p = (ToDouble3(mesh.Vertices[t.Vertices[0]].Position) - origin) / scale;
                const auto q = (ToDouble3(mesh.Vertices[t.Vertices[1]].Position) - origin) / scale,
                           r = (ToDouble3(mesh.Vertices[t.Vertices[2]].Position) - origin) / scale;
                // Preserve reciprocal normalization in QEM: roundoff affects near-tied collapse costs.
                const auto cross = Core::Cross(q - p, r - p);
                const auto normal = cross * (1.0 / cross.Length());
                const Core::Double4 plane(normal, -Core::Dot(normal, p));
                for (const auto v : t.Vertices)
                {
                    quadrics[v].AddPlane(plane);
                }
            }
            const auto target = std::max<size_t>(1, (mesh.Triangles.size() + 1) / 2);
            while (mesh.Triangles.size() > target)
            {
                const auto topology = MakeTopology(mesh);
                std::vector<uint32_t> representative(topology.Positions.size(), INVALID_VIRTUAL_GEOMETRY_INDEX);
                // Only referenced vertices participate; a removed wedge can
                // retain an old position in scratch storage after a collapse.
                for (const auto& t : mesh.Triangles)
                {
                    for (const auto v : t.Vertices)
                    {
                        representative[topology.VertexGeometry[v]] = v;
                    }
                }
                std::priority_queue<Collapse> queue;
                for (const auto& [edge, uses] : topology.Edges)
                {
                    if (!ManifoldEdge(uses) || locked.contains(Key(topology.Positions[edge.first])) ||
                        locked.contains(Key(topology.Positions[edge.second])))
                    {
                        continue;
                    }
                    const auto a = representative[edge.first], b = representative[edge.second];
                    if (mesh.Vertices[a].Tangent.W != mesh.Vertices[b].Tangent.W)
                    {
                        continue;
                    }
                    Quadric q;
                    q.Matrix = quadrics[a].Matrix + quadrics[b].Matrix;
                    const auto pa = (ToDouble3(mesh.Vertices[a].Position) - origin) / scale,
                               pb = (ToDouble3(mesh.Vertices[b].Position) - origin) / scale;
                    std::vector<Core::Double3> options{pa, pb, (pa + pb) * 0.5};
                    Core::Double3 optimum;
                    if (q.Optimum(optimum))
                    {
                        options.push_back(optimum);
                    }
                    for (const auto& p : options)
                    {
                        const auto position = ToFloat3(origin + p * scale);
                        if (!Finite(position))
                        {
                            continue;
                        }
                        const double cost = q.Cost((ToDouble3(position) - origin) / scale);
                        if (std::isfinite(cost))
                        {
                            queue.push({cost, a, b, position});
                        }
                    }
                }
                bool collapsed = false;
                while (!queue.empty())
                {
                    const auto candidate = queue.top();
                    queue.pop();
                    BuildMesh next;
                    if (!TryCollapse(mesh, topology, candidate, boundary, locked, next))
                    {
                        continue;
                    }
                    quadrics[candidate.A].Matrix = quadrics[candidate.A].Matrix + quadrics[candidate.B].Matrix;
                    mesh = std::move(next);
                    collapsed = true;
                    break;
                }
                // Rebuild the bounded group's queue after an accepted collapse:
                // no stale costs/topology, and rejected edges can become legal.
                if (!collapsed)
                {
                    break;
                }
            }
            return Compact(mesh);
        }

        struct ErrorTraits : DistanceFieldTriangleBVHTraits
        {
            static float ClosestDistance(const DistanceFieldTriangle& t, const Core::Float3& p)
            {
                const Core::Double3 a(t.V0.X, t.V0.Y, t.V0.Z), b(t.V1.X, t.V1.Y, t.V1.Z), c(t.V2.X, t.V2.Y, t.V2.Z),
                    point(p.X, p.Y, p.Z);
                const auto cross = Core::Cross(b - a, c - a);
                const double size =
                    std::max({Core::Dot(b - a, b - a), Core::Dot(c - b, c - b), Core::Dot(a - c, a - c)});
                if (Core::Dot(cross, cross) > size * size * 1e-12)
                {
                    return DistanceFieldTriangleBVHTraits::ClosestDistance(t, p);
                }
                double distance = std::numeric_limits<double>::infinity();
                for (const auto& edge :
                     std::array<std::pair<Core::Double3, Core::Double3>, 3>{{{a, b}, {b, c}, {c, a}}})
                {
                    const auto delta = edge.second - edge.first;
                    const double length = Core::Dot(delta, delta);
                    const auto q =
                        edge.first +
                        delta * (length > 0 ? std::clamp(Core::Dot(point - edge.first, delta) / length, 0.0, 1.0) : 0);
                    distance = std::min(distance, (point - q).Length());
                }
                return static_cast<float>(distance);
            }
        };

        float GeometricError(const BuildMesh& before, const BuildMesh& after)
        {
            const auto sphere = UnionSphere(MeshBounds(before).Sphere, MeshBounds(after).Sphere);
            const auto origin = ToDouble3(sphere.Center);
            const double scale = sphere.Radius;
            Require(scale > 0, "zero error-query scale");
            const auto point = [&](const Core::Double3& p)
            {
                const auto q = (p - origin) / scale;
                return Core::Float3(static_cast<float>(q.X), static_cast<float>(q.Y), static_cast<float>(q.Z));
            };
            const auto primitives = [&](const BuildMesh& mesh)
            {
                std::vector<DistanceFieldTriangle> result;
                for (const auto& t : mesh.Triangles)
                {
                    result.push_back({point(ToDouble3(mesh.Vertices[t.Vertices[0]].Position)),
                                      point(ToDouble3(mesh.Vertices[t.Vertices[1]].Position)),
                                      point(ToDouble3(mesh.Vertices[t.Vertices[2]].Position))});
                }
                return result;
            };
            const auto oldTriangles = primitives(before), newTriangles = primitives(after);
            Core::BVH<DistanceFieldTriangle, ErrorTraits> oldBVH, newBVH;
            oldBVH.Build(oldTriangles);
            newBVH.Build(newTriangles);
            double maximum = 0;
            const auto sample = [&](const BuildMesh& mesh, const auto& bvh)
            {
                for (const auto& t : mesh.Triangles)
                {
                    std::array<Core::Double3, 3> p;
                    for (size_t i = 0; i < 3; ++i)
                    {
                        p[i] = ToDouble3(mesh.Vertices[t.Vertices[i]].Position);
                    }
                    for (size_t i = 0; i < 3; ++i)
                    {
                        maximum = std::max(maximum, double(bvh.ClosestDistance(point(p[i]))));
                        maximum = std::max(maximum, double(bvh.ClosestDistance(point((p[i] + p[(i + 1) % 3]) * 0.5))));
                    }
                    maximum = std::max(maximum, double(bvh.ClosestDistance(point((p[0] + p[1] + p[2]) / 3.0))));
                }
            };
            sample(before, newBVH);
            sample(after, oldBVH);
            // Bidirectional samples, not a certified Hausdorff bound. This floor
            // accounts for float normalization/query roundoff, not unsampled area.
            return RoundUp((maximum + 8 * std::numeric_limits<float>::epsilon()) * scale);
        }

        BuildMesh ReadMesh(const StaticMesh* staticMesh, size_t& discarded)
        {
            Require(staticMesh && staticMesh->GetLODCount() > 0, "missing StaticMesh LOD0");
            const auto& lod = staticMesh->GetLODResource(0);
            const auto& streams = lod.VertexBuffers;
            const auto& positions = streams.PositionBuffer;
            Require(positions.bValid && positions.NumComponents == 3 && !positions.Vertices.empty() &&
                        positions.Vertices.size() % 3 == 0,
                    "invalid CPU position stream");
            const auto count = Index(positions.GetNumVertices());
            BuildMesh mesh;
            mesh.Vertices.resize(count);
            const auto validate = [&](const VertexBuffer& stream, uint32_t min, uint32_t max)
            {
                if (!stream.bValid)
                {
                    return;
                }
                Require(stream.NumComponents >= min && stream.NumComponents <= max &&
                            stream.GetNumVertices() == count &&
                            stream.Vertices.size() == size_t(count) * stream.NumComponents,
                        "invalid CPU attribute stream");
                for (const auto v : stream.Vertices)
                {
                    Require(std::isfinite(v), "non-finite vertex attribute");
                }
            };
            validate(positions, 3, 3);
            validate(streams.NormalBuffer, 3, 3);
            validate(streams.TangentBuffer, 3, 4);
            validate(streams.UVBuffer, 2, 2);
            validate(streams.ColorBuffer, 3, 4);
            for (uint32_t i = 0; i < count; ++i)
            {
                auto& v = mesh.Vertices[i];
                const auto offset = size_t(i) * 3;
                v.Position = {positions.Vertices[offset], positions.Vertices[offset + 1],
                              positions.Vertices[offset + 2]};
                const auto read = [&](const VertexBuffer& b, uint32_t c)
                {
                    return b.Vertices[size_t(i) * b.NumComponents + c];
                };
                if (streams.NormalBuffer.bValid)
                {
                    v.Normal = {read(streams.NormalBuffer, 0), read(streams.NormalBuffer, 1),
                                read(streams.NormalBuffer, 2)};
                    Require(Core::Dot(ToDouble3(v.Normal), ToDouble3(v.Normal)) > 0, "zero supplied normal");
                }
                if (streams.TangentBuffer.bValid)
                {
                    v.Tangent = {read(streams.TangentBuffer, 0), read(streams.TangentBuffer, 1),
                                 read(streams.TangentBuffer, 2),
                                 streams.TangentBuffer.NumComponents == 4 ? read(streams.TangentBuffer, 3) : 1};
                    Require(Core::Dot(ToDouble3(v.Tangent), ToDouble3(v.Tangent)) > 0 && std::abs(v.Tangent.W) == 1,
                            "invalid supplied tangent");
                }
                if (streams.UVBuffer.bValid)
                {
                    v.UV = {read(streams.UVBuffer, 0), read(streams.UVBuffer, 1)};
                }
                if (streams.ColorBuffer.bValid)
                {
                    v.Color = {read(streams.ColorBuffer, 0), read(streams.ColorBuffer, 1), read(streams.ColorBuffer, 2),
                               streams.ColorBuffer.NumComponents == 4 ? read(streams.ColorBuffer, 3) : 1};
                }
            }
            const auto& indices = lod.IndexBuffer.Indices;
            Require(!indices.empty() && indices.size() % 3 == 0, "invalid CPU triangle index stream");
            Index(indices.size());
            std::vector<bool> covered(indices.size() / 3);
            const auto section = [&](const SectionInfo& s)
            {
                Require(s.FirstIndex % 3 == 0 && s.NumIndices % 3 == 0 &&
                            uint64_t(s.FirstIndex) + s.NumIndices <= indices.size(),
                        "invalid section index range");
                Require(s.MaterialIndex >= -1 &&
                            (s.MaterialIndex < 0 || size_t(s.MaterialIndex) < staticMesh->GetNumMaterials()),
                        "invalid section material slot");
                for (size_t i = s.FirstIndex; i < size_t(s.FirstIndex) + s.NumIndices; i += 3)
                {
                    Require(!covered[i / 3], "overlapping sections");
                    covered[i / 3] = true;
                    BuildTriangle triangle;
                    triangle.Material =
                        s.MaterialIndex < 0 ? INVALID_VIRTUAL_GEOMETRY_INDEX : uint32_t(s.MaterialIndex);
                    for (size_t j = 0; j < 3; ++j)
                    {
                        const int64_t v = int64_t(indices[i + j]) + s.BaseVertexIndex;
                        Require(v >= 0 && v < count, "section base vertex/index out of range");
                        triangle.Vertices[j] = uint32_t(v);
                    }
                    if (Nondegenerate(mesh, triangle))
                    {
                        mesh.Triangles.push_back(triangle);
                    }
                    else
                    {
                        ++discarded;
                    }
                }
            };
            if (lod.Sections.empty())
            {
                section({0, Index(indices.size()), staticMesh->GetNumMaterials() ? 0 : -1, 0});
            }
            else
            {
                for (const auto& s : lod.Sections)
                {
                    section(s);
                }
            }
            Require(std::all_of(covered.begin(), covered.end(),
                                [](bool value)
            {
                return value;
            }),
                    "sections do not cover the index stream");
            Require(!mesh.Triangles.empty(), "no nondegenerate triangles");
            if (!streams.NormalBuffer.bValid)
            {
                std::vector<Core::Double3> normals(count, Core::Double3{});
                for (const auto& t : mesh.Triangles)
                {
                    const auto normal = Cross(mesh, t);
                    for (const auto v : t.Vertices)
                    {
                        normals[v] += normal;
                    }
                }
                for (uint32_t i = 0; i < count; ++i)
                {
                    mesh.Vertices[i].Normal =
                        (normals[i]).Length() > 0 ? ToFloat3(Core::Normalize(normals[i])) : Core::Float3(0, 1, 0);
                }
            }
            if (!streams.TangentBuffer.bValid)
            {
                for (auto& v : mesh.Vertices)
                {
                    const auto normal = Core::Normalize(ToDouble3(v.Normal));
                    const auto tangent = Core::Normalize(Core::Cross(
                        normal, std::abs(normal.X) < 0.9 ? Core::Double3(1, 0, 0) : Core::Double3(0, 1, 0)));
                    v.Tangent = {ToFloat3(tangent), 1};
                }
            }
            mesh = Compact(mesh);
            ValidateFaces(mesh);
            return mesh;
        }

        void AppendCluster(VirtualGeometryResource& resource, std::vector<BuildMesh>& meshes, BuildMesh mesh,
                           uint32_t mip, uint32_t generating)
        {
            Require(!mesh.Triangles.empty() && mesh.Triangles.size() <= VIRTUAL_GEOMETRY_MAX_CLUSTER_TRIANGLES,
                    "invalid cluster size");
            VirtualGeometryCluster cluster;
            cluster.VertexOffset = Index(resource.Positions.size());
            cluster.VertexCount = Index(mesh.Vertices.size());
            cluster.IndexOffset = Index(resource.Indices.size());
            cluster.TriangleCount = Index(mesh.Triangles.size());
            cluster.MipLevel = mip;
            cluster.GeneratingGroupIndex = generating;
            cluster.MaterialIndex = mesh.Triangles.front().Material;
            Index(resource.Positions.size() + mesh.Vertices.size());
            Index(resource.Indices.size() + mesh.Triangles.size() * 3);
            cluster.Bounds = MeshBounds(mesh);
            for (const auto& v : mesh.Vertices)
            {
                resource.Positions.push_back(v.Position);
                resource.Normals.push_back(v.Normal);
                resource.Tangents.push_back(v.Tangent);
                resource.TexCoords.push_back(v.UV);
                resource.Colors.push_back(v.Color);
            }
            for (const auto& t : mesh.Triangles)
            {
                Require(t.Material == cluster.MaterialIndex, "mixed material cluster");
                resource.Indices.insert(resource.Indices.end(), t.Vertices.begin(), t.Vertices.end());
            }
            resource.Bounds = UnionBounds(resource.Bounds, cluster.Bounds);
            resource.Clusters.push_back(cluster);
            meshes.push_back(std::move(mesh));
        }

        VirtualGeometryLODMetric ChildMetric(const VirtualGeometryResource& resource,
                                             const std::vector<uint32_t>& children)
        {
            VirtualGeometryLODMetric metric;
            std::set<uint32_t> generators;
            for (const auto c : children)
            {
                const auto& cluster = resource.Clusters[c];
                metric.Bounds = UnionBounds(metric.Bounds, cluster.Bounds);
                if (cluster.GeneratingGroupIndex != INVALID_VIRTUAL_GEOMETRY_INDEX)
                {
                    generators.insert(cluster.GeneratingGroupIndex);
                }
            }
            for (const auto g : generators)
            {
                const auto& previous = resource.Groups[g].ParentMetric;
                metric.Error = std::max(metric.Error, previous.Error);
                metric.Bounds = UnionBounds(metric.Bounds, previous.Bounds);
            }
            return metric;
        }

        void AddGroup(VirtualGeometryResource& resource, const std::vector<uint32_t>& children,
                      const std::vector<uint32_t>& parents, VirtualGeometryLODMetric metric, float localError)
        {
            const auto index = Index(resource.Groups.size());
            VirtualGeometryGroup group;
            group.ChildClusterOffset = Index(resource.GroupChildClusterIndices.size());
            group.ChildClusterCount = Index(children.size());
            group.ParentClusterOffset = Index(resource.GroupParentClusterIndices.size());
            group.ParentClusterCount = Index(parents.size());
            group.SimplificationError = localError;
            for (const auto p : parents)
            {
                metric.Bounds = UnionBounds(metric.Bounds, resource.Clusters[p].Bounds);
            }
            group.ChildMetric = metric;
            group.ParentMetric = metric;
            group.ParentMetric.Error = parents.empty() ? metric.Error : RoundUp(double(metric.Error) + localError);
            for (const auto c : children)
            {
                Require(resource.Clusters[c].GroupIndex == INVALID_VIRTUAL_GEOMETRY_INDEX, "cluster consumed twice");
                resource.Clusters[c].GroupIndex = index;
                group.MipLevel = std::max(group.MipLevel, resource.Clusters[c].MipLevel);
            }
            Index(resource.GroupChildClusterIndices.size() + children.size());
            Index(resource.GroupParentClusterIndices.size() + parents.size());
            resource.GroupChildClusterIndices.insert(resource.GroupChildClusterIndices.end(), children.begin(),
                                                     children.end());
            resource.GroupParentClusterIndices.insert(resource.GroupParentClusterIndices.end(), parents.begin(),
                                                      parents.end());
            resource.Groups.push_back(group);
            if (parents.empty())
            {
                resource.RootGroupIndices.push_back(index);
            }
        }

        void ValidateResource(const VirtualGeometryResource& resource, const std::vector<BuildMesh>& meshes)
        {
            Require(ValidBounds(resource.Bounds) && resource.IsValid() &&
                        resource.Positions.size() == resource.Normals.size() &&
                        resource.Positions.size() == resource.Tangents.size() &&
                        resource.Positions.size() == resource.TexCoords.size() &&
                        resource.Positions.size() == resource.Colors.size(),
                    "invalid resource arrays");
            for (size_t v = 0; v < resource.Positions.size(); ++v)
            {
                Require(Finite(resource.Positions[v]) && Finite(resource.Normals[v]) && Finite(resource.Tangents[v]) &&
                            Finite(resource.TexCoords[v]) && Finite(resource.Colors[v]),
                        "non-finite output attribute");
            }
            std::vector<uint32_t> consumers(resource.Clusters.size()), generators(resource.Clusters.size());
            std::set<uint32_t> roots(resource.RootGroupIndices.begin(), resource.RootGroupIndices.end());
            Require(roots.size() == resource.RootGroupIndices.size(), "duplicate root group");
            for (uint32_t i = 0; i < Index(resource.Clusters.size()); ++i)
            {
                const auto& c = resource.Clusters[i];
                Require(c.TriangleCount > 0 && c.TriangleCount <= 128 &&
                            uint64_t(c.VertexOffset) + c.VertexCount <= resource.Positions.size() &&
                            uint64_t(c.IndexOffset) + uint64_t(c.TriangleCount) * 3 <= resource.Indices.size(),
                        "cluster range invalid");
                ValidateFaces(meshes[i]);
                for (uint32_t j = 0; j < c.TriangleCount * 3; ++j)
                {
                    Require(resource.Indices[c.IndexOffset + j] < c.VertexCount, "cluster local index invalid");
                }
                Require(ValidBounds(c.Bounds) && c.GroupIndex < resource.Groups.size(), "cluster metric/group invalid");
            }
            for (uint32_t i = 0; i < Index(resource.Groups.size()); ++i)
            {
                const auto& g = resource.Groups[i];
                Require(g.ChildClusterCount > 0 && g.ChildClusterCount <= GroupSize &&
                            uint64_t(g.ChildClusterOffset) + g.ChildClusterCount <=
                                resource.GroupChildClusterIndices.size() &&
                            uint64_t(g.ParentClusterOffset) + g.ParentClusterCount <=
                                resource.GroupParentClusterIndices.size(),
                        "group range invalid");
                Require(roots.contains(i) == (g.ParentClusterCount == 0), "invalid root group");
                Require(ValidBounds(g.ChildMetric.Bounds) && ValidBounds(g.ParentMetric.Bounds) &&
                            std::isfinite(g.ParentMetric.Error) && g.ParentMetric.Error >= g.ChildMetric.Error &&
                            g.ChildMetric.Error >= 0,
                        "invalid/non-monotone LOD metric");
                std::vector<uint32_t> children, parents;
                size_t before = 0, after = 0;
                for (uint32_t j = 0; j < g.ChildClusterCount; ++j)
                {
                    const auto c = resource.GroupChildClusterIndices[g.ChildClusterOffset + j];
                    Require(c < resource.Clusters.size() && resource.Clusters[c].GroupIndex == i,
                            "invalid group child");
                    ++consumers[c];
                    children.push_back(c);
                    before += resource.Clusters[c].TriangleCount;
                    const auto generator = resource.Clusters[c].GeneratingGroupIndex;
                    if (generator != INVALID_VIRTUAL_GEOMETRY_INDEX)
                    {
                        Require(generator < i && resource.Groups[generator].ParentMetric.Error <= g.ChildMetric.Error,
                                "cyclic/non-monotone DAG");
                    }
                }
                for (uint32_t j = 0; j < g.ParentClusterCount; ++j)
                {
                    const auto c = resource.GroupParentClusterIndices[g.ParentClusterOffset + j];
                    Require(c < resource.Clusters.size() && resource.Clusters[c].GeneratingGroupIndex == i &&
                                resource.Clusters[c].MipLevel == g.MipLevel + 1,
                            "invalid group parent");
                    ++generators[c];
                    parents.push_back(c);
                    after += resource.Clusters[c].TriangleCount;
                    for (const auto child : children)
                    {
                        Require(child < c, "cyclic cluster DAG");
                    }
                }
                if (!parents.empty())
                {
                    Require(after < before, "non-reducing replacement");
                    Require(GetBoundary(MakeTopology(Merge(meshes, children))) ==
                                GetBoundary(MakeTopology(Merge(meshes, parents))),
                            "group boundary changed");
                }
            }
            for (size_t i = 0; i < resource.Clusters.size(); ++i)
            {
                Require(consumers[i] == 1 &&
                            generators[i] ==
                                (resource.Clusters[i].GeneratingGroupIndex == INVALID_VIRTUAL_GEOMETRY_INDEX ? 0u : 1u),
                        "orphan/duplicate cluster DAG reference");
            }
            for (const auto root : roots)
            {
                Require(root < resource.Groups.size(), "invalid root index");
            }
            const auto contains = [](const Core::BoxSphereBounds& a, const Core::BoxSphereBounds& b)
            {
                return a.Box.Contains(b.Box) &&
                       (ToDouble3(a.Sphere.Center) - ToDouble3(b.Sphere.Center)).Length() + double(b.Sphere.Radius) <=
                           double(a.Sphere.Radius);
            };
            for (const auto& c : resource.Clusters)
            {
                Require(contains(resource.Groups[c.GroupIndex].ChildMetric.Bounds, c.Bounds),
                        "LOD bounds do not contain cluster sphere");
                if (c.GeneratingGroupIndex != INVALID_VIRTUAL_GEOMETRY_INDEX)
                {
                    Require(contains(resource.Groups[c.GroupIndex].ChildMetric.Bounds,
                                     resource.Groups[c.GeneratingGroupIndex].ParentMetric.Bounds),
                            "LOD bounds lost descendant region");
                }
            }
        }
    } // namespace

    void VirtualGeometryResource::InitializeResources()
    {
        // An unbuilt mesh keeps using its traditional LOD resources.
        if (Positions.empty() && Indices.empty() && Clusters.empty() && Groups.empty())
        {
            ReleaseResources();
            return;
        }
        Require(IsValid() && !Groups.empty() && !RootGroupIndices.empty(), "cannot upload incomplete CPU geometry");
        Require(Normals.size() == Positions.size() && Tangents.size() == Positions.size() &&
                    TexCoords.size() == Positions.size() && Colors.size() == Positions.size(),
                "cannot upload mismatched vertex attributes");
        Require(RHI::GRHIApi != nullptr, "InitializeResources requires an initialized RHI");

        const auto createBuffer = [](const auto& data, const char* name, RHI::ERHIBufferUsageFlag usage)
        {
            using Element = typename std::decay_t<decltype(data)>::value_type;
            static_assert(std::is_trivially_copyable_v<Element>);
            Require(data.size() <= std::numeric_limits<uint32_t>::max() / sizeof(Element),
                    "buffer exceeds the RenderBuffer upload size limit");
            std::unique_ptr<RenderCore::RenderBuffer> buffer;
            if (data.empty())
            {
                return buffer;
            }
            RHI::RHIBufferDesc desc;
            desc.Size = data.size() * sizeof(Element);
            desc.Stride = sizeof(Element);
            desc.Usage = usage | RHI::ERHIBufferUsageFlag::Structured | RHI::ERHIBufferUsageFlag::ShaderResource |
                         RHI::ERHIBufferUsageFlag::TransferDst | RHI::ERHIBufferUsageFlag::TransferSrc;
            desc.bCPUAccessible = false;
            desc.DebugName = name;
            buffer = std::make_unique<RenderCore::RenderBuffer>(desc);
            buffer->InitRHIResource();
            Require(buffer->GetRHI() != nullptr, "RHI buffer creation failed");
            buffer->UploadData(data.data(), static_cast<uint32_t>(desc.Size));
            RenderCore::TransitionBufferImmediate(RHI::GRHIApi, buffer.get(), RHI::ERHIResourceAccess::SRV,
                                                  RHI::EQueueType::Graphics);
            return buffer;
        };

        // Publish only after every upload succeeds. Local owners unwind naturally on failure.
        auto buffers = std::array{
            createBuffer(Positions, "VirtualGeometry.Positions", RHI::ERHIBufferUsageFlag::Vertex),
            createBuffer(Normals, "VirtualGeometry.Normals", RHI::ERHIBufferUsageFlag::Vertex),
            createBuffer(Tangents, "VirtualGeometry.Tangents", RHI::ERHIBufferUsageFlag::Vertex),
            createBuffer(TexCoords, "VirtualGeometry.TexCoords", RHI::ERHIBufferUsageFlag::Vertex),
            createBuffer(Colors, "VirtualGeometry.Colors", RHI::ERHIBufferUsageFlag::Vertex),
            createBuffer(Indices, "VirtualGeometry.Indices", RHI::ERHIBufferUsageFlag::Index),
            createBuffer(Clusters, "VirtualGeometry.Clusters", RHI::ERHIBufferUsageFlag::None),
            createBuffer(Groups, "VirtualGeometry.Groups", RHI::ERHIBufferUsageFlag::None),
            createBuffer(GroupChildClusterIndices, "VirtualGeometry.GroupChildClusterIndices",
                         RHI::ERHIBufferUsageFlag::None),
            createBuffer(GroupParentClusterIndices, "VirtualGeometry.GroupParentClusterIndices",
                         RHI::ERHIBufferUsageFlag::None),
            createBuffer(RootGroupIndices, "VirtualGeometry.RootGroupIndices", RHI::ERHIBufferUsageFlag::None)};
        ReleaseResources();
        PositionBuffer = std::move(buffers[0]);
        NormalBuffer = std::move(buffers[1]);
        TangentBuffer = std::move(buffers[2]);
        TexCoordBuffer = std::move(buffers[3]);
        ColorBuffer = std::move(buffers[4]);
        IndexBuffer = std::move(buffers[5]);
        ClusterBuffer = std::move(buffers[6]);
        GroupBuffer = std::move(buffers[7]);
        GroupChildClusterIndexBuffer = std::move(buffers[8]);
        GroupParentClusterIndexBuffer = std::move(buffers[9]);
        RootGroupIndexBuffer = std::move(buffers[10]);
    }

    void VirtualGeometryResource::ReleaseResources()
    {
        // The caller must retire outstanding GPU users before release/rebuild. No global wait is added.
        for (auto* buffer : std::array{&PositionBuffer, &NormalBuffer, &TangentBuffer, &TexCoordBuffer, &ColorBuffer,
                                       &IndexBuffer, &ClusterBuffer, &GroupBuffer, &GroupChildClusterIndexBuffer,
                                       &GroupParentClusterIndexBuffer, &RootGroupIndexBuffer})
        {
            if (*buffer)
            {
                (*buffer)->GetViewCache().Clear();
                (*buffer)->ReleaseRHIResource();
                buffer->reset();
            }
        }
    }

    void VirtualGeometryResource::Build(const StaticMesh* staticMesh)
    {
        // Build in local storage; failure leaves this resource empty without
        // an exception handler or a separate cleanup object.
        Clear();
        const auto started = std::chrono::steady_clock::now();
        size_t discarded = 0;
        auto source = ReadMesh(staticMesh, discarded);
        const size_t original = staticMesh->GetLODResource(0).IndexBuffer.Indices.size() / 3;

        VirtualGeometryResource result;
        std::vector<BuildMesh> meshes;
        for (auto& cluster : Cluster(source))
        {
            AppendCluster(result, meshes, std::move(cluster), 0, INVALID_VIRTUAL_GEOMETRY_INDEX);
        }

        std::vector<uint32_t> active(result.Clusters.size());
        std::iota(active.begin(), active.end(), 0);
        size_t attempts = 0, successes = 0, iteration = 0;
        double errorSum = 0, maximumError = 0;

        while (true)
        {
            size_t triangles = 0;
            for (const auto c : active)
            {
                triangles += result.Clusters[c].TriangleCount;
            }
            std::cout << "[VirtualGeometry] frontier " << iteration << ": clusters=" << active.size()
                      << " triangles=" << triangles << '\n';
            auto frontier = Frontier(meshes, active);
            const auto parts = Partition(frontier.Neighbors, frontier.Centers, frontier.Perimeters, GroupSize);
            std::vector<uint32_t> next;
            bool reduced = false;
            for (const auto& part : parts)
            {
                std::vector<uint32_t> children;
                for (const auto c : part)
                {
                    children.push_back(active[c]);
                }
                auto merged = Merge(meshes, children);
                ++attempts;
                auto simplified = Simplify(merged, frontier.Protected);
                if (simplified.Triangles.size() >= merged.Triangles.size())
                {
                    next.insert(next.end(), children.begin(), children.end());
                    continue;
                }
                auto clusters = Cluster(simplified);
                if (clusters.size() > children.size())
                {
                    next.insert(next.end(), children.begin(), children.end());
                    continue;
                }
                Require(GetBoundary(MakeTopology(merged)) == GetBoundary(MakeTopology(simplified)),
                        "simplification changed group boundary");
                const float localError = GeometricError(merged, simplified);
                const auto metric = ChildMetric(result, children);
                const auto group = Index(result.Groups.size());
                uint32_t mip = 0;
                for (const auto c : children)
                {
                    mip = std::max(mip, result.Clusters[c].MipLevel + 1);
                }
                std::vector<uint32_t> parents;
                for (auto& cluster : clusters)
                {
                    const auto index = Index(result.Clusters.size());
                    parents.push_back(index);
                    next.push_back(index);
                    AppendCluster(result, meshes, std::move(cluster), mip, group);
                }
                AddGroup(result, children, parents, metric, localError);
                ++successes;
                errorSum += localError;
                maximumError = std::max(maximumError, double(localError));
                reduced = true;
            }
            active = std::move(next);
            ++iteration;
            if (!reduced)
            {
                break;
            }
            // Every accepted group reduces triangles and does not increase
            // cluster count. Failed regions can regroup on the next round.
        }

        const auto finalFrontier = Frontier(meshes, active);
        for (const auto& part :
             Partition(finalFrontier.Neighbors, finalFrontier.Centers, finalFrontier.Perimeters, GroupSize))
        {
            std::vector<uint32_t> children;
            for (const auto c : part)
            {
                children.push_back(active[c]);
            }
            AddGroup(result, children, {}, ChildMetric(result, children), 0);
        }
        ValidateResource(result, meshes);

        float maximumLOD = 0;
        double sumLOD = 0;
        std::map<uint32_t, std::pair<size_t, size_t>> levels;
        for (const auto& group : result.Groups)
        {
            maximumLOD = std::max(maximumLOD, group.ParentMetric.Error);
            sumLOD += group.ParentMetric.Error;
        }
        for (const auto& cluster : result.Clusters)
        {
            auto& level = levels[cluster.MipLevel];
            ++level.first;
            level.second += cluster.TriangleCount;
        }
        for (const auto& [mip, level] : levels)
        {
            std::cout << "[VirtualGeometry] mip " << mip << ": clusters=" << level.first
                      << " triangles=" << level.second << '\n';
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "[VirtualGeometry] original=" << original << " discarded_degenerate=" << discarded
                  << " clusters=" << result.Clusters.size() << " groups=" << result.Groups.size()
                  << " roots=" << result.RootGroupIndices.size() << " simplification=" << successes << '/' << attempts
                  << " local_error_max=" << maximumError
                  << " local_error_mean=" << (successes ? errorSum / double(successes) : 0)
                  << " accumulated_error_max=" << maximumLOD
                  << " accumulated_error_mean=" << (sumLOD / double(result.Groups.size())) << " seconds=" << seconds
                  << std::endl;

        *this = std::move(result);
    }
} // namespace Engine
