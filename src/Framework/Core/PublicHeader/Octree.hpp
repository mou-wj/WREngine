#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

#include "BVH.hpp"

namespace Core
{
    static constexpr uint32_t OctreeInvalidNode = UINT32_MAX;

    struct OctreeNode
    {
        BoxSphereBounds Bounds;

        std::array<uint32_t, 8> Children;

        uint32_t FirstPrimitive = 0;
        uint32_t PrimitiveCount = 0;

        bool bLeaf = true;

        OctreeNode()
        {
            Children.fill(OctreeInvalidNode);
        }

        bool IsEmpty() const
        {
            return bLeaf && PrimitiveCount == 0;
        }
    };

    template<typename Primitive>
    struct OctreeTraits
    {
        static BoxSphereBounds GetBounds(const Primitive& primitive);

        static Float3 GetCentroid(const Primitive& primitive);

        static bool RayIntersect(const Primitive& primitive, const Ray& ray, float& outDistance);

        static Float3 ClosestPoint(const Primitive& primitive, const Float3& position);

        static float ClosestDistance(const Primitive& primitive, const Float3& position);
    };

    template<typename Primitive, typename Traits = OctreeTraits<Primitive>>
    class Octree
    {
    public:

        using PrimitiveType = Primitive;
        using TraitsType = Traits;

        Octree() = default;
        ~Octree() = default;

        void Build(const std::vector<Primitive>& inPrimitives);

        void Clear()
        {
            _nodes.clear();
            _primitiveIndices.clear();
            _primitives = nullptr;
        }

        bool IsEmpty() const
        {
            return _nodes.empty();
        }

        const OctreeNode& GetRootNode() const
        {
            return _nodes.front();
        }

        const std::vector<OctreeNode>& GetNodes() const
        {
            return _nodes;
        }

        const std::vector<uint32_t>& GetPrimitiveIndices() const
        {
            return _primitiveIndices;
        }

        const Primitive& GetPrimitive(uint32_t index) const
        {
            return (*_primitives)[index];
        }

        const std::vector<Primitive>& GetPrimitives() const
        {
            return *_primitives;
        }

        uint32_t GetNodeCount() const
        {
            return static_cast<uint32_t>(_nodes.size());
        }

        uint32_t GetPrimitiveCount() const
        {
            return static_cast<uint32_t>(_primitiveIndices.size());
        }

        void SetMaxDepth(uint32_t inMaxDepth)
        {
            _maxDepth = inMaxDepth;
        }

        void SetMaxPrimitivesPerLeaf(uint32_t inMaxPrimitivesPerLeaf)
        {
            _maxPrimitivesPerLeaf = inMaxPrimitivesPerLeaf;
        }

        float ClosestDistance(const Float3& position) const;

        void RayIntersect(const Ray& ray, std::set<uint32_t>& intersectPrimitiveIds) const;

        void GetPrimitiveIdsInsideBounds(const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const;

    protected:

        uint32_t BuildRecursive(uint32_t begin, uint32_t end, const AABB& bounds, uint32_t depth);

        uint32_t CreateLeaf(uint32_t begin, uint32_t end, const BoxSphereBounds& bounds);

        uint32_t CreateInternalNode(const BoxSphereBounds& bounds);

        BoxSphereBounds ComputeBounds(uint32_t begin, uint32_t end) const;

        BoxSphereBounds ComputeBounds() const;

        static int GetChildIndex(const Float3& position, const AABB& bounds);

        static AABB GetChildBounds(const AABB& parentBounds, uint32_t childIndex);

        void ClosestDistanceRecursive(uint32_t nodeIndex, const Float3& position, float& minDistance) const;

        void RayIntersectNode(
            uint32_t nodeIndex,
            const Ray& ray,
            std::set<uint32_t>& outPrimitiveIds) const;

        void QueryBoundsRecursive(uint32_t nodeIndex, const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const;

    protected:

        const std::vector<Primitive>* _primitives = nullptr;

        std::vector<uint32_t> _primitiveIndices;

        std::vector<OctreeNode> _nodes;

        uint32_t _maxDepth = 8;
        uint32_t _maxPrimitivesPerLeaf = 8;
    };

    template<typename Primitive, typename Traits>
    void Octree<Primitive, Traits>::Build(const std::vector<Primitive>& inPrimitives)
    {
        Clear();

        if (inPrimitives.empty())
        {
            return;
        }

        _primitives = &inPrimitives;

        _primitiveIndices.resize(inPrimitives.size());

        for (uint32_t i = 0; i < static_cast<uint32_t>(inPrimitives.size()); i++)
        {
            _primitiveIndices[i] = i;
        }

        _nodes.reserve(inPrimitives.size() * 8);

        const BoxSphereBounds rootBounds = ComputeBounds();

        if (rootBounds.Box.IsEmpty())
        {
            return;
        }

        BuildRecursive(0, static_cast<uint32_t>(inPrimitives.size()), rootBounds.Box, 0);
    }

    template<typename Primitive, typename Traits>
    uint32_t Octree<Primitive, Traits>::BuildRecursive(uint32_t begin, uint32_t end, const AABB& bounds, uint32_t depth)
    {
        const uint32_t primitiveCount = end - begin;

        if (primitiveCount <= _maxPrimitivesPerLeaf || depth >= _maxDepth)
        {
            return CreateLeaf(begin, end, BoxSphereBounds(bounds));
        }

        std::array<std::vector<uint32_t>, 8> childPrimitives;

        for (uint32_t i = begin; i < end; i++)
        {
            uint32_t primitiveIndex = _primitiveIndices[i];
            const Primitive& primitiveValue = (*_primitives)[primitiveIndex];
            const Float3 primitiveCenter = Traits::GetCentroid(primitiveValue);
            const int childIndex = GetChildIndex(primitiveCenter, bounds);
            childPrimitives[childIndex].push_back(primitiveIndex);
        }

        bool hasAnyChild = false;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (!childPrimitives[i].empty())
            {
                hasAnyChild = true;
                break;
            }
        }

        if (!hasAnyChild)
        {
            return CreateLeaf(begin, end, BoxSphereBounds(bounds));
        }

        uint32_t nodeIndex = CreateInternalNode(BoxSphereBounds(bounds));
        OctreeNode& node = _nodes[nodeIndex];

        uint32_t writeIndex = begin;

        for (uint32_t childIndex = 0; childIndex < 8; childIndex++)
        {
            if (childPrimitives[childIndex].empty())
            {
                node.Children[childIndex] = OctreeInvalidNode;
                continue;
            }

            const uint32_t childBegin = writeIndex;
            const uint32_t childEnd = childBegin + static_cast<uint32_t>(childPrimitives[childIndex].size());

            for (uint32_t primitiveIndex : childPrimitives[childIndex])
            {
                _primitiveIndices[writeIndex++] = primitiveIndex;
            }

            const AABB childBounds = GetChildBounds(bounds, childIndex);
            node.Children[childIndex] = BuildRecursive(childBegin, childEnd, childBounds, depth + 1);
        }

        node.bLeaf = false;

        return nodeIndex;
    }

    template<typename Primitive, typename Traits>
    uint32_t Octree<Primitive, Traits>::CreateLeaf(uint32_t begin, uint32_t end, const BoxSphereBounds& bounds)
    {
        OctreeNode node;

        node.Bounds = bounds;
        node.FirstPrimitive = begin;
        node.PrimitiveCount = end - begin;
        node.bLeaf = true;
        node.Children.fill(OctreeInvalidNode);

        _nodes.push_back(node);

        return static_cast<uint32_t>(_nodes.size() - 1);
    }

    template<typename Primitive, typename Traits>
    uint32_t Octree<Primitive, Traits>::CreateInternalNode(const BoxSphereBounds& bounds)
    {
        OctreeNode node;

        node.Bounds = bounds;
        node.bLeaf = false;
        node.Children.fill(OctreeInvalidNode);

        _nodes.push_back(node);

        return static_cast<uint32_t>(_nodes.size() - 1);
    }

    template<typename Primitive, typename Traits>
    BoxSphereBounds Octree<Primitive, Traits>::ComputeBounds(uint32_t begin, uint32_t end) const
    {
        BoxSphereBounds bounds;
        bounds.SetEmpty();

        for (uint32_t i = begin; i < end; i++)
        {
            const Primitive& primitiveValue = (*_primitives)[_primitiveIndices[i]];
            bounds.Merge(Traits::GetBounds(primitiveValue));
        }

        return bounds;
    }

    template<typename Primitive, typename Traits>
    BoxSphereBounds Octree<Primitive, Traits>::ComputeBounds() const
    {
        BoxSphereBounds bounds;
        bounds.SetEmpty();

        for (const Primitive& primitiveValue : *_primitives)
        {
            bounds.Merge(Traits::GetBounds(primitiveValue));
        }

        return bounds;
    }

    template<typename Primitive, typename Traits>
    int Octree<Primitive, Traits>::GetChildIndex(const Float3& position, const AABB& bounds)
    {
        const Float3 center = bounds.GetCenter();

        int childIndex = 0;

        if (position.X >= center.X)
        {
            childIndex |= 1;
        }

        if (position.Y >= center.Y)
        {
            childIndex |= 2;
        }

        if (position.Z >= center.Z)
        {
            childIndex |= 4;
        }

        return childIndex;
    }

    template<typename Primitive, typename Traits>
    AABB Octree<Primitive, Traits>::GetChildBounds(const AABB& parentBounds, uint32_t childIndex)
    {
        const Float3 center = parentBounds.GetCenter();

        Float3 min = parentBounds.Min;
        Float3 max = parentBounds.Max;

        if ((childIndex & 1) != 0)
        {
            min.X = center.X;
        }
        else
        {
            max.X = center.X;
        }

        if ((childIndex & 2) != 0)
        {
            min.Y = center.Y;
        }
        else
        {
            max.Y = center.Y;
        }

        if ((childIndex & 4) != 0)
        {
            min.Z = center.Z;
        }
        else
        {
            max.Z = center.Z;
        }

        return AABB(min, max);
    }

    template<typename Primitive, typename Traits>
    float Octree<Primitive, Traits>::ClosestDistance(const Float3& position) const
    {
        float minDistance = std::numeric_limits<float>::max();

        if (_nodes.empty())
        {
            return minDistance;
        }

        ClosestDistanceRecursive(0, position, minDistance);

        return minDistance;
    }

    template<typename Primitive, typename Traits>
    void Octree<Primitive, Traits>::ClosestDistanceRecursive(uint32_t nodeIndex, const Float3& position, float& minDistance) const
    {
        const OctreeNode& node = _nodes[nodeIndex];

        float boundsDistance = node.Bounds.Box.Distance(position);

        if (boundsDistance > minDistance)
        {
            return;
        }

        if (node.bLeaf)
        {
            for (uint32_t i = 0; i < node.PrimitiveCount; i++)
            {
                uint32_t primitiveIndex = _primitiveIndices[node.FirstPrimitive + i];
                float distance = Traits::ClosestDistance((*_primitives)[primitiveIndex], position);
                minDistance = std::min(minDistance, distance);
            }
            return;
        }

        for (uint32_t i = 0; i < 8; i++)
        {
            if (node.Children[i] != OctreeInvalidNode)
            {
                ClosestDistanceRecursive(node.Children[i], position, minDistance);
            }
        }
    }

    template<typename Primitive, typename Traits>
    void Octree<Primitive, Traits>::RayIntersect(const Ray& ray, std::set<uint32_t>& intersectPrimitiveIds) const
    {
        intersectPrimitiveIds.clear();

        if (_nodes.empty())
        {
            return;
        }

        RayIntersectNode(0, ray, intersectPrimitiveIds);
    }

    template<typename Primitive, typename Traits>
    void Octree<Primitive, Traits>::RayIntersectNode(uint32_t nodeIndex, const Ray& ray, std::set<uint32_t>& outPrimitiveIds) const
    {
        const OctreeNode& node = _nodes[nodeIndex];

        if (!ray.RayIntersectAABB(node.Bounds.Box))
        {
            return;
        }

        if (node.bLeaf)
        {
            for (uint32_t i = 0; i < node.PrimitiveCount; i++)
            {
                uint32_t primitiveIndex = _primitiveIndices[node.FirstPrimitive + i];
                float distance = 0.0f;
                if (Traits::RayIntersect((*_primitives)[primitiveIndex], ray, distance))
                {
                    outPrimitiveIds.insert(primitiveIndex);
                }
            }
            return;
        }

        for (uint32_t i = 0; i < 8; i++)
        {
            if (node.Children[i] != OctreeInvalidNode)
            {
                RayIntersectNode(node.Children[i], ray, outPrimitiveIds);
            }
        }
    }

    template<typename Primitive, typename Traits>
    void Octree<Primitive, Traits>::GetPrimitiveIdsInsideBounds(const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const
    {
        outPrimitiveIds.clear();

        if (_nodes.empty())
        {
            return;
        }

        QueryBoundsRecursive(0, box, outPrimitiveIds);
    }

    template<typename Primitive, typename Traits>
    void Octree<Primitive, Traits>::QueryBoundsRecursive(uint32_t nodeIndex, const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const
    {
        const OctreeNode& node = _nodes[nodeIndex];

        if (!node.Bounds.Intersects(box))
        {
            return;
        }

        if (node.bLeaf)
        {
            for (uint32_t i = 0; i < node.PrimitiveCount; i++)
            {
                uint32_t primitiveIndex = _primitiveIndices[node.FirstPrimitive + i];
                outPrimitiveIds.insert(primitiveIndex);
            }
            return;
        }

        for (uint32_t i = 0; i < 8; i++)
        {
            if (node.Children[i] != OctreeInvalidNode)
            {
                QueryBoundsRecursive(node.Children[i], box, outPrimitiveIds);
            }
        }
    }
}
