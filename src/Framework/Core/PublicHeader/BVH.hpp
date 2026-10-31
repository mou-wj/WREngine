#pragma once

#include <vector>
#include <cstdint>
#include <set>
#include <cmath>
#include "Common.h"
#include "BoxSphereBounds.h"

namespace Core
{

    struct Ray {
		Float3 Origin;
		Float3 Direction;
		Ray() = default;
		Ray(const Float3& inOrigin, const Float3& inDirection)
			: Origin(inOrigin), Direction(inDirection) {
		}
        bool RayIntersectAABB(
            const AABB& box, float maxParameter = FLT_MAX) const
        {
            if (box.IsEmpty() || !std::isfinite(maxParameter) || maxParameter < 0) return false;
            float tMin = 0.0f;
            float tMax = maxParameter;


            for (int axis = 0; axis < 3; axis++)
            {
                float ori = Origin[axis];
                float dir = Direction[axis];
                if (!std::isfinite(ori) || !std::isfinite(dir) ||
                    !std::isfinite(box.Min[axis]) || !std::isfinite(box.Max[axis])) return false;
                // A parallel ray on the slab boundary is valid; avoid 0*inf.
                if (dir == 0)
                {
                    if (ori < box.Min[axis] || ori > box.Max[axis]) return false;
                    continue;
                }


                float t0 =
                    (box.Min[axis] - ori) / dir;


                float t1 =
                    (box.Max[axis] - ori) / dir;


                if (t0 > t1)
                {
                    std::swap(t0, t1);
                }


                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);


                if (tMax < tMin)
                {
                    return false;
                }
            }


            return true;
        }
    };

    static constexpr uint32_t BVHInvalidNode = UINT32_MAX;

    struct BVHNode
    {
        BoxSphereBounds Bounds;

        uint32_t LeftChild = BVHInvalidNode;
        uint32_t RightChild = BVHInvalidNode;

        uint32_t FirstPrimitive = 0;
        uint32_t PrimitiveCount = 0;

        bool IsLeaf() const { return LeftChild == BVHInvalidNode; }
    };

    template<typename Primitive>
    struct BVHTraits
    {
        static BoxSphereBounds GetBounds(const Primitive& primitive);

        static Float3 GetCentroid(const Primitive& primitive);

        static bool RayIntersect(const Primitive& primitive, const Ray& ray, float& outDistance);

        static Float3 ClosestPoint(const Primitive& primitive, const Float3& position);

        static float ClosestDistance(const Primitive& primitive, const Float3& position);
    };

    template<typename Primitive, typename Traits = BVHTraits<Primitive>>
    class BVH
    {
    public:

        using PrimitiveType = Primitive;
        using TraitsType = Traits;

        BVH() = default;
        ~BVH() = default;

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

        const BVHNode& GetRootNode() const
        {
            return _nodes.front();
        }

        const std::vector<BVHNode>& GetNodes() const
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

        float ClosestDistance(const Float3& position) const;

        void RayIntersect(const Ray& ray, std::set<uint32_t>& intersectPrimitiveIds) const;

        void GetPrimitiveIdsInsideBounds(const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const;

    protected:

        uint32_t BuildRecursive(uint32_t begin, uint32_t end);

        uint32_t CreateLeaf(uint32_t begin, uint32_t end);

        uint32_t CreateInternalNode();

        BoxSphereBounds ComputeBounds(uint32_t begin, uint32_t end) const;

        int GetLongestAxis(const BoxSphereBounds& bounds) const;

        void ClosestDistanceRecursive(uint32_t nodeIndex, const Float3& position, float& minDistance) const;

        void RayIntersectNode(
            uint32_t nodeIndex,
            const Ray& ray,
            std::set<uint32_t>& outPrimitiveIds) const;

        void QueryBoundsRecursive(uint32_t nodeIndex, const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const;
    protected:




        const std::vector<Primitive>* _primitives = nullptr;

        std::vector<uint32_t> _primitiveIndices;

        std::vector<BVHNode> _nodes;
    };

}
#include "BVH.inl"
