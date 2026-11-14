#include "VirtualGeometryLODSelection.h"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace Renderer
{
    namespace
    {
        using namespace Engine;

        void Require(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::invalid_argument(std::string("Virtual geometry LOD: ") + message);
            }
        }

        bool ValidMetric(const VirtualGeometryLODMetric& metric)
        {
            const auto& bounds = metric.Bounds;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(bounds.Box.Min[axis]) || !std::isfinite(bounds.Box.Max[axis]) ||
                    bounds.Box.Min[axis] > bounds.Box.Max[axis] || !std::isfinite(bounds.Sphere.Center[axis]))
                {
                    return false;
                }
            }
            return std::isfinite(metric.Error) && metric.Error >= 0 &&
                   std::isfinite(bounds.Sphere.Radius) && bounds.Sphere.Radius >= 0;
        }

        void ValidateDAG(const VirtualGeometryResource& resource)
        {
            Require(resource.IsValid() && !resource.Groups.empty() && !resource.RootGroupIndices.empty(),
                    "resource has no complete Build DAG");
            Require(resource.Clusters.size() < UINT32_MAX && resource.Groups.size() < UINT32_MAX,
                    "DAG exceeds index range");
            std::vector<uint8_t> consumers(resource.Clusters.size()), generators(resource.Clusters.size());
            std::vector<uint8_t> roots(resource.Groups.size());
            for (const auto root : resource.RootGroupIndices)
            {
                Require(root < roots.size() && !roots[root], "invalid or duplicate root");
                roots[root] = 1;
            }
            for (uint32_t index = 0; index < resource.Groups.size(); ++index)
            {
                const auto& group = resource.Groups[index];
                Require(group.ChildClusterCount &&
                            uint64_t(group.ChildClusterOffset) + group.ChildClusterCount <= resource.GroupChildClusterIndices.size() &&
                            uint64_t(group.ParentClusterOffset) + group.ParentClusterCount <= resource.GroupParentClusterIndices.size(),
                        "invalid Group range");
                Require(bool(roots[index]) == (group.ParentClusterCount == 0), "root is not a terminal Group");
                Require(ValidMetric(group.ChildMetric) && ValidMetric(group.ParentMetric) &&
                            group.ParentMetric.Error >= group.ChildMetric.Error,
                        "invalid or non-monotone Group metric");
                for (uint32_t offset = 0; offset < group.ChildClusterCount; ++offset)
                {
                    const auto child = resource.GroupChildClusterIndices[group.ChildClusterOffset + offset];
                    Require(child < resource.Clusters.size() && !consumers[child] &&
                                resource.Clusters[child].GroupIndex == index,
                            "invalid or duplicate Child reference");
                    consumers[child] = 1;
                    const auto generator = resource.Clusters[child].GeneratingGroupIndex;
                    if (generator != INVALID_VIRTUAL_GEOMETRY_INDEX)
                    {
                        // Build orders generation before consumption; checking this also rejects cycles.
                        Require(generator < index && resource.Groups[generator].ParentMetric.Error <= group.ChildMetric.Error,
                                "cyclic or non-monotone dependency");
                    }
                }
                for (uint32_t offset = 0; offset < group.ParentClusterCount; ++offset)
                {
                    const auto parent = resource.GroupParentClusterIndices[group.ParentClusterOffset + offset];
                    Require(parent < resource.Clusters.size() && !generators[parent] &&
                                resource.Clusters[parent].GeneratingGroupIndex == index,
                            "invalid or duplicate Parent reference");
                    generators[parent] = 1;
                }
            }
            for (uint32_t index = 0; index < resource.Clusters.size(); ++index)
            {
                Require(consumers[index] && bool(generators[index]) ==
                            (resource.Clusters[index].GeneratingGroupIndex != INVALID_VIRTUAL_GEOMETRY_INDEX),
                        "orphan Cluster or missing generation reference");
            }
        }

        struct Projection
        {
            Core::Mat4d LocalToClip;
            std::array<double, 4> RowLengths{};
            Core::Double3 NearNormal;
            double NearOffset = 0;
            double Width = 0;
            double Height = 0;
        };

        Projection MakeProjection(const Core::Mat4& localToWorld, const SceneView& view)
        {
            Require(view.Viewport.Width > 0 && view.Viewport.Height > 0, "invalid viewport");
            Projection result;
            result.Width = view.Viewport.Width;
            result.Height = view.Viewport.Height;
            for (uint32_t row = 0; row < 4; ++row)
            {
                for (uint32_t col = 0; col < 4; ++col)
                {
                    Require(std::isfinite(localToWorld(row, col)) && std::isfinite(view.ViewProjectionMatrix(row, col)),
                            "non-finite transform or view projection");
                }
            }
            Require(localToWorld(3, 0) == 0 && localToWorld(3, 1) == 0 &&
                        localToWorld(3, 2) == 0 && localToWorld(3, 3) == 1,
                    "instance transform must be affine");
            const Core::Double3 a{localToWorld(0, 0), localToWorld(1, 0), localToWorld(2, 0)};
            const Core::Double3 b{localToWorld(0, 1), localToWorld(1, 1), localToWorld(2, 1)};
            const Core::Double3 c{localToWorld(0, 2), localToWorld(1, 2), localToWorld(2, 2)};
            Require(Core::Dot(a, Core::Cross(b, c)) != 0, "singular instance transform");
            for (uint32_t row = 0; row < 4; ++row)
            {
                for (uint32_t col = 0; col < 4; ++col)
                {
                    for (uint32_t inner = 0; inner < 4; ++inner)
                    {
                        result.LocalToClip(row, col) +=
                            double(view.ViewProjectionMatrix(row, inner)) * localToWorld(inner, col);
                    }
                }
                result.RowLengths[row] = Core::Double3{result.LocalToClip(row, 0), result.LocalToClip(row, 1),
                                                       result.LocalToClip(row, 2)}.Length();
            }
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                result.NearNormal[axis] = result.LocalToClip(2, axis) +
                                         (view.bDepthRangeZeroToOne ? 0 : result.LocalToClip(3, axis));
            }
            result.NearOffset = result.LocalToClip(2, 3) +
                                (view.bDepthRangeZeroToOne ? 0 : result.LocalToClip(3, 3));
            return result;
        }

        double PixelError(const Projection& projection, const VirtualGeometryLODMetric& metric)
        {
            const auto& sphere = metric.Bounds.Sphere;
            const Core::Double3 center{sphere.Center.X, sphere.Center.Y, sphere.Center.Z};
            const double radius = sphere.Radius, error = metric.Error;
            const auto clip = projection.LocalToClip * std::array<double, 4>{center.X, center.Y, center.Z, 1};
            const double minimumW = clip[3] - radius * projection.RowLengths[3];
            const double perturbedW = minimumW - error * projection.RowLengths[3];
            // Near-plane/eye intersections are refined, including off-screen or behind-camera regions.
            // No culling is performed here, so this cannot remove an unrepresented region.
            if (minimumW <= 0 || perturbedW <= 0 ||
                Core::Dot(projection.NearNormal, center) + projection.NearOffset <=
                    (radius + error) * projection.NearNormal.Length())
            {
                return std::numeric_limits<double>::infinity();
            }
            std::array<double, 2> pixels{};
            for (uint32_t axis = 0; axis < 2; ++axis)
            {
                const double maximumNDC = (std::abs(clip[axis]) + radius * projection.RowLengths[axis]) / minimumW;
                const double ndcError = error * (projection.RowLengths[axis] +
                                                maximumNDC * projection.RowLengths[3]) / perturbedW;
                pixels[axis] = ndcError * (axis == 0 ? projection.Width : projection.Height) * 0.5;
            }
            // The homogeneous quotient bound includes nonuniform scale, shear, off-axis views and orthographic views.
            const double result = std::hypot(pixels[0], pixels[1]);
            return std::isfinite(result) ? result : std::numeric_limits<double>::infinity();
        }
    }

    void ValidateVirtualGeometryLODInput(const Engine::VirtualGeometryResource& resource,
        const Core::Mat4& localToWorld, const Engine::SceneView& view, float targetPixelError)
    {
        Require(std::isfinite(targetPixelError) && targetPixelError >= 0, "target pixel error must be finite and nonnegative");
        MakeProjection(localToWorld, view);
        ValidateDAG(resource);
    }

    std::vector<uint32_t> SelectVirtualGeometryClusters(const Engine::VirtualGeometryResource& resource,
        const Core::Mat4& localToWorld, const Engine::SceneView& view, float targetPixelError)
    {
        Require(std::isfinite(targetPixelError) && targetPixelError >= 0, "target pixel error must be finite and nonnegative");
        const auto projection = MakeProjection(localToWorld, view);
        ValidateDAG(resource);
        std::vector<uint8_t> selected(resource.Clusters.size()), applied(resource.Groups.size());
        for (uint32_t cluster = 0; cluster < resource.Clusters.size(); ++cluster)
        {
            selected[cluster] = resource.Clusters[cluster].GeneratingGroupIndex == Engine::INVALID_VIRTUAL_GEOMETRY_INDEX;
        }
        for (uint32_t index = 0; index < resource.Groups.size(); ++index)
        {
            const auto& group = resource.Groups[index];
            if (!group.ParentClusterCount || targetPixelError == 0 ||
                PixelError(projection, group.ParentMetric) > targetPixelError)
            {
                continue;
            }
            bool complete = true;
            for (uint32_t offset = 0; offset < group.ChildClusterCount; ++offset)
            {
                complete &= bool(selected[resource.GroupChildClusterIndices[group.ChildClusterOffset + offset]]);
            }
            if (!complete)
            {
                continue;
            }
            // One atomic hyperedge replacement: a Parent has no individual Child-to-Parent correspondence.
            for (uint32_t offset = 0; offset < group.ChildClusterCount; ++offset)
            {
                selected[resource.GroupChildClusterIndices[group.ChildClusterOffset + offset]] = 0;
            }
            for (uint32_t offset = 0; offset < group.ParentClusterCount; ++offset)
            {
                selected[resource.GroupParentClusterIndices[group.ParentClusterOffset + offset]] = 1;
            }
            applied[index] = 1;
        }
        std::vector<uint32_t> result;
        for (uint32_t index = 0; index < resource.Clusters.size(); ++index)
        {
            const auto& cluster = resource.Clusters[index];
            const bool generated = cluster.GeneratingGroupIndex == Engine::INVALID_VIRTUAL_GEOMETRY_INDEX ||
                                   applied[cluster.GeneratingGroupIndex];
            Require(bool(selected[index]) == (generated && !applied[cluster.GroupIndex]), "invalid final DAG frontier");
            if (selected[index])
            {
                result.push_back(index);
            }
        }
        Require(!result.empty(), "empty DAG cut");
        return result;
    }
}
