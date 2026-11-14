#include "TestBase.h"
#include "../Framework/Renderer/VirtualGeometry/VirtualGeometryLODSelection.h"
#include "StaticMesh.h"
#include "Transform.hpp"
#include "RHIApi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace Test
{
    namespace
    {
        using namespace Engine;

        void VerifyLOD(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("Virtual geometry LOD test: ") + message);
            }
        }

        SceneView OrthographicView()
        {
            SceneView view;
            view.Viewport = {0, 0, 100, 100};
            view.ProjectionMatrix = Core::Mat4::Identity();
            view.ProjectionMatrix(2, 2) = -0.01f;
            view.ProjectionMatrix(2, 3) = 0.5f;
            view.ViewProjectionMatrix = view.ProjectionMatrix;
            return view;
        }

        SceneView PerspectiveView(float distance)
        {
            SceneView view;
            view.Viewport = {0, 0, 256, 256};
            view.CameraWorldPos = {0, 0, distance};
            view.ViewMatrix = Core::LookAtRH(view.CameraWorldPos, {0, 0, 0}, {0, 1, 0});
            view.ProjectionMatrix = Core::PerspectiveRhZo(Core::DegToRad(60.0f), 1, 0.1f, 1000);
            view.ViewProjectionMatrix = view.ProjectionMatrix * view.ViewMatrix;
            return view;
        }

        // Graph fixture only; actual Build geometry/boundary properties are checked separately below.
        VirtualGeometryResource CrossingDAG()
        {
            VirtualGeometryResource resource;
            resource.Positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
            resource.Indices = {0, 1, 2};
            resource.Clusters.resize(12);
            for (auto& cluster : resource.Clusters)
            {
                cluster.VertexCount = 3;
                cluster.TriangleCount = 1;
                cluster.Bounds.Box = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
                cluster.Bounds.Sphere.Center = {0, 0, 0};
                cluster.Bounds.Sphere.Radius = 0.5f;
            }
            const auto group = [&](std::initializer_list<uint32_t> children, std::initializer_list<uint32_t> parents,
                                   float childError, float parentError)
            {
                const auto index = uint32_t(resource.Groups.size());
                VirtualGeometryGroup g;
                g.ChildClusterOffset = uint32_t(resource.GroupChildClusterIndices.size());
                g.ChildClusterCount = uint32_t(children.size());
                g.ParentClusterOffset = uint32_t(resource.GroupParentClusterIndices.size());
                g.ParentClusterCount = uint32_t(parents.size());
                g.ChildMetric.Bounds = g.ParentMetric.Bounds = resource.Clusters.front().Bounds;
                g.ChildMetric.Error = childError;
                g.ParentMetric.Error = parentError;
                for (const auto child : children)
                {
                    resource.Clusters[child].GroupIndex = index;
                    resource.GroupChildClusterIndices.push_back(child);
                }
                for (const auto parent : parents)
                {
                    resource.Clusters[parent].GeneratingGroupIndex = index;
                    resource.GroupParentClusterIndices.push_back(parent);
                }
                resource.Groups.push_back(g);
                if (!parents.size())
                {
                    resource.RootGroupIndices.push_back(index);
                }
            };
            group({0, 1}, {6, 7}, 0, 0.01f);
            group({2, 3}, {8, 9}, 0, 0.02f);
            group({7, 8}, {10}, 0.02f, 0.04f); // Crosses both previous Group partitions.
            group({6, 9, 10}, {11}, 0.04f, 0.08f);
            group({4}, {}, 0, 0); // Two stalled islands must survive every cut.
            group({5}, {}, 0, 0);
            group({11}, {}, 0.08f, 0.08f);
            return resource;
        }

        void VerifyCut(const VirtualGeometryResource& resource, const std::vector<uint32_t>& cut)
        {
            std::set<uint32_t> frontier(cut.begin(), cut.end());
            VerifyLOD(frontier.size() == cut.size(), "duplicate selected Cluster");
            // Independent reverse proof: undo complete Parent sets in reverse dependency order.
            for (size_t index = resource.Groups.size(); index-- > 0;)
            {
                const auto& group = resource.Groups[index];
                if (!group.ParentClusterCount)
                {
                    continue;
                }
                uint32_t present = 0;
                for (uint32_t offset = 0; offset < group.ParentClusterCount; ++offset)
                {
                    present += frontier.contains(resource.GroupParentClusterIndices[group.ParentClusterOffset + offset]);
                }
                VerifyLOD(present == 0 || present == group.ParentClusterCount, "partial Group replacement");
                if (!present)
                {
                    continue;
                }
                for (uint32_t offset = 0; offset < group.ParentClusterCount; ++offset)
                {
                    frontier.erase(resource.GroupParentClusterIndices[group.ParentClusterOffset + offset]);
                }
                for (uint32_t offset = 0; offset < group.ChildClusterCount; ++offset)
                {
                    VerifyLOD(frontier.insert(resource.GroupChildClusterIndices[group.ChildClusterOffset + offset]).second,
                              "Parent/Child overlap in cut");
                }
            }
            std::set<uint32_t> leaves;
            for (uint32_t index = 0; index < resource.Clusters.size(); ++index)
            {
                if (resource.Clusters[index].GeneratingGroupIndex == INVALID_VIRTUAL_GEOMETRY_INDEX)
                {
                    leaves.insert(index);
                }
            }
            VerifyLOD(frontier == leaves, "cut does not reconstruct the complete leaf frontier");
        }

        using Point = std::array<float, 3>;
        using Edge = std::pair<Point, Point>;
        std::map<Edge, int> Boundary(const VirtualGeometryResource& resource, const std::vector<uint32_t>& cut)
        {
            std::map<Edge, int> edges;
            for (const auto clusterIndex : cut)
            {
                const auto& cluster = resource.Clusters[clusterIndex];
                for (uint32_t triangle = 0; triangle < cluster.TriangleCount; ++triangle)
                {
                    std::array<Point, 3> points;
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        const auto local = resource.Indices[cluster.IndexOffset + triangle * 3 + corner];
                        const auto& p = resource.Positions[cluster.VertexOffset + local];
                        points[corner] = {p.X, p.Y, p.Z};
                    }
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        const auto& a = points[corner];
                        const auto& b = points[(corner + 1) % 3];
                        edges[a < b ? Edge{a, b} : Edge{b, a}] += a < b ? 1 : -1;
                    }
                }
            }
            std::erase_if(edges, [](const auto& item) { return item.second == 0; });
            return edges;
        }

        std::shared_ptr<StaticMesh> CurvedGrid()
        {
            auto data = std::make_shared<StaticMeshRenderData>();
            auto& lod = data->LODResources.emplace_back();
            auto& positions = lod.VertexBuffers.PositionBuffer;
            positions.bValid = true;
            positions.NumComponents = 3;
            for (uint32_t y = 0; y <= 12; ++y)
            {
                for (uint32_t x = 0; x <= 24; ++x)
                {
                    const float px = float(x) / 12 - 1, py = float(y) / 6 - 1;
                    positions.Vertices.insert(positions.Vertices.end(), {px, py, 0.1f * std::sin(px * 3) * std::cos(py * 2)});
                }
            }
            for (uint32_t y = 0; y < 12; ++y)
            {
                for (uint32_t x = 0; x < 24; ++x)
                {
                    const auto a = y * 25 + x;
                    lod.IndexBuffer.Indices.insert(lod.IndexBuffer.Indices.end(), {a, a + 1, a + 25, a + 1, a + 26, a + 25});
                }
            }
            return std::make_shared<StaticMesh>(data);
        }
    }

    class VirtualGeometryLODSelectionTest final : public TestBase
    {
    public:
        void Setup() override {}
        void Teardown() override {}
        void Run() override
        {
            VerifyLOD(RHI::GRHIApi == nullptr, "CPU selection must not initialize RHI");
            const auto resource = CrossingDAG();
            auto view = OrthographicView();
            const auto transform = Core::Mat4::Identity();
            const std::vector<std::pair<float, std::vector<uint32_t>>> expected = {
                {0, {0, 1, 2, 3, 4, 5}}, {1, {2, 3, 4, 5, 6, 7}}, {2, {4, 5, 6, 7, 8, 9}},
                {3, {4, 5, 6, 9, 10}}, {6, {4, 5, 11}}};
            for (const auto& [threshold, cut] : expected)
            {
                const auto actual = Renderer::SelectVirtualGeometryClusters(resource, transform, view, threshold);
                VerifyLOD(actual == cut, "cross-Group/multi-root cut differs from expected atomic replacements");
                VerifyCut(resource, actual);
            }
            view.Viewport.Width = view.Viewport.Height = 200;
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, view, 1) == expected[0].second,
                      "larger viewport must increase pixel error");
            view = OrthographicView();
            view.ViewProjectionMatrix(0, 0) = view.ViewProjectionMatrix(1, 1) = 2;
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, view, 1) == expected[0].second,
                      "projection magnification must increase pixel error");
            view = OrthographicView();
            auto scaled = transform;
            scaled(0, 0) = -3;
            scaled(0, 1) = 1;
            scaled(1, 1) = 2;
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, scaled, view, 1) == expected[0].second,
                      "nonuniform mirrored/sheared transform must affect the metric");
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, PerspectiveView(100), 1) == expected.back().second,
                      "distant perspective instance should coarsen completely");
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, PerspectiveView(0.4f), 1000000) == expected[0].second,
                      "eye/near-plane intersection must refine");
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, PerspectiveView(10), 1).size() >
                          Renderer::SelectVirtualGeometryClusters(resource, transform, PerspectiveView(100), 1).size(),
                      "perspective distance must change the cut");
            view = PerspectiveView(10);
            view.ProjectionMatrix(0, 2) = 2;
            view.ViewProjectionMatrix = view.ProjectionMatrix * view.ViewMatrix;
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, view, 1) == expected[1].second,
                      "off-axis perspective quotient must affect the metric");
            view = OrthographicView();
            view.bDepthRangeZeroToOne = false;
            view.ViewProjectionMatrix(2, 3) = 0;
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, view, 3) == expected[3].second,
                      "negative-one-to-one near-plane convention must remain valid");
            view = OrthographicView();
            view.ViewProjectionMatrix(2, 3) += 0.1f;
            VerifyLOD(Renderer::SelectVirtualGeometryClusters(resource, transform, view, 3) == expected[3].second,
                      "orthographic depth translation must not scale projected error");
            auto mesh = CurvedGrid();
            VirtualGeometryResource built;
            built.Build(mesh.get());
            const auto leaves = Renderer::SelectVirtualGeometryClusters(built, transform, PerspectiveView(10), 0);
            const auto boundary = Boundary(built, leaves);
            size_t previousTriangles = built.Indices.size();
            bool coarse = false;
            bool mixed = false;
            for (const float threshold : {0.0f, 0.0001f, 0.01f, 0.1f, 0.2f, 0.3f, 0.4f,
                                          0.5f, 0.75f, 1.0f, 4.0f, 16.0f, 256.0f, 1000000.0f})
            {
                const auto cut = Renderer::SelectVirtualGeometryClusters(built, transform, PerspectiveView(10), threshold);
                VerifyCut(built, cut);
                VerifyLOD(Boundary(built, cut) == boundary, "mixed LOD cut changed oriented geometric boundary");
                size_t triangles = 0;
                std::set<uint32_t> levels;
                for (const auto cluster : cut)
                {
                    triangles += built.Clusters[cluster].TriangleCount;
                    levels.insert(built.Clusters[cluster].MipLevel);
                    coarse |= built.Clusters[cluster].GeneratingGroupIndex != INVALID_VIRTUAL_GEOMETRY_INDEX;
                }
                VerifyLOD(triangles <= previousTriangles, "relaxed error budget increased geometric complexity");
                previousTriangles = triangles;
                mixed |= levels.size() > 1;
                std::cout << "LOD_CUT target=" << threshold << " clusters=" << cut.size() << " triangles=" << triangles
                          << " levels=" << levels.size() << std::endl;
            }
            VerifyLOD(coarse, "built mesh did not exercise generated Cluster selection");
            VerifyLOD(mixed, "built mesh did not exercise mixed-level boundary preservation");
            VerifyLOD(RHI::GRHIApi == nullptr, "CPU test created RHI state");
            std::cout << "VIRTUAL_GEOMETRY_LOD_SELECTION_TEST_OK" << std::endl;
        }
    };

    class VirtualGeometryLODInvalidInputTest final : public TestBase
    {
    public:
        explicit VirtualGeometryLODInvalidInputTest(uint32_t testCase) : m_TestCase(testCase) {}
        void Setup() override {}
        void Teardown() override {}
        void Run() override
        {
            auto resource = CrossingDAG();
            auto view = OrthographicView();
            auto transform = Core::Mat4::Identity();
            float target = 1;
            switch (m_TestCase)
            {
            case 0: target = -1; break;
            case 1: resource.RootGroupIndices.push_back(999); break;
            case 2: resource.GroupChildClusterIndices[1] = 0; break;
            case 3: resource.Groups[0].ChildClusterOffset = UINT32_MAX; break;
            case 4: resource.Clusters[0].GeneratingGroupIndex = 3; break;
            case 5: resource.Groups[0].ParentMetric.Error = -1; break;
            case 6: transform(3, 0) = 1; break;
            case 7: view.Viewport.Height = 0; break;
            }
            Renderer::SelectVirtualGeometryClusters(resource, transform, view, target);
            throw std::runtime_error("Invalid LOD fixture was unexpectedly accepted");
        }
    private:
        uint32_t m_TestCase;
    };

    const bool RegisteredVirtualGeometryLODTests = []()
    {
        RenderTestRegistry::Register("VirtualGeometryLODSelectionTest", []()
        {
            return std::make_unique<VirtualGeometryLODSelectionTest>();
        });
        for (uint32_t index = 0; index < 8; ++index)
        {
            RenderTestRegistry::Register("VirtualGeometryLODInvalidInput" + std::to_string(index), [index]()
            {
                return std::make_unique<VirtualGeometryLODInvalidInputTest>(index);
            });
        }
        return true;
    }();
}
