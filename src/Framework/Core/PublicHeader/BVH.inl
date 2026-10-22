#pragma once

#include <algorithm>

namespace Core
{

template<typename Primitive, typename Traits>
void BVH<Primitive, Traits>::Build(const std::vector<Primitive>& inPrimitives)
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

    _nodes.reserve(inPrimitives.size() * 2);

    BuildRecursive(0, static_cast<uint32_t>(_primitiveIndices.size()));
}



template<typename Primitive, typename Traits>
uint32_t BVH<Primitive, Traits>::BuildRecursive(uint32_t begin, uint32_t end)
{
    const uint32_t primitiveCount = end - begin;


    constexpr uint32_t leafPrimitiveCount = 4;


    if (primitiveCount <= leafPrimitiveCount)
    {
        return CreateLeaf(begin, end);
    }


    BoxSphereBounds bounds = ComputeBounds(begin, end);


    int axis = GetLongestAxis(bounds);


    uint32_t middle = begin + primitiveCount / 2;


    std::nth_element(
        _primitiveIndices.begin() + begin,
        _primitiveIndices.begin() + middle,
        _primitiveIndices.begin() + end,
        [&](uint32_t a, uint32_t b)
        {
            Float3 centerA =
                Traits::GetCentroid((*_primitives)[a]);

            Float3 centerB =
                Traits::GetCentroid((*_primitives)[b]);


            float valueA = 0.0f;
            float valueB = 0.0f;


            if (axis == 0)
            {
                valueA = centerA.X;
                valueB = centerB.X;
            }
            else if (axis == 1)
            {
                valueA = centerA.Y;
                valueB = centerB.Y;
            }
            else
            {
                valueA = centerA.Z;
                valueB = centerB.Z;
            }


            return valueA < valueB;
        });


    uint32_t nodeIndex = CreateInternalNode();


    uint32_t leftChild =
        BuildRecursive(begin, middle);


    uint32_t rightChild =
        BuildRecursive(middle, end);



    BVHNode& node = _nodes[nodeIndex];


    node.LeftChild = leftChild;
    node.RightChild = rightChild;


    node.Bounds = _nodes[leftChild].Bounds;

    node.Bounds.Merge(
        _nodes[rightChild].Bounds);



    return nodeIndex;
}



template<typename Primitive, typename Traits>
uint32_t BVH<Primitive, Traits>::CreateLeaf(
    uint32_t begin,
    uint32_t end)
{
    BVHNode node;


    node.Bounds =
        ComputeBounds(begin, end);


    node.LeftChild = BVHInvalidNode;
    node.RightChild = BVHInvalidNode;


    node.FirstPrimitive = begin;
    node.PrimitiveCount = end - begin;


    _nodes.push_back(node);


    return static_cast<uint32_t>(_nodes.size() - 1);
}



template<typename Primitive, typename Traits>
uint32_t BVH<Primitive, Traits>::CreateInternalNode()
{
    BVHNode node;

    node.LeftChild = BVHInvalidNode;
    node.RightChild = BVHInvalidNode;

    _nodes.push_back(node);

    return static_cast<uint32_t>(_nodes.size() - 1);
}



template<typename Primitive, typename Traits>
BoxSphereBounds BVH<Primitive, Traits>::ComputeBounds(
    uint32_t begin,
    uint32_t end) const
{
    BoxSphereBounds bounds;

    bounds.SetEmpty();


    for (uint32_t i = begin; i < end; i++)
    {
        const Primitive& primitiveI =
            (*_primitives)[_primitiveIndices[i]];


        bounds.Merge(
            Traits::GetBounds(primitiveI));
    }


    return bounds;
}



template<typename Primitive, typename Traits>
int BVH<Primitive, Traits>::GetLongestAxis(
    const BoxSphereBounds& bounds) const
{
    Float3 extent =
        bounds.Box.GetExtent();


    if (extent.X > extent.Y &&
        extent.X > extent.Z)
    {
        return 0;
    }


    if (extent.Y > extent.Z)
    {
        return 1;
    }


    return 2;
}

template<typename Primitive, typename Traits>
float BVH<Primitive,Traits>::ClosestDistance(const Float3& position) const
{
    float minDistance = std::numeric_limits<float>::max();

    ClosestDistanceRecursive(
        0,
        position,
        minDistance);

    return minDistance;
}
template<typename Primitive, typename Traits>
void BVH<Primitive,Traits>::ClosestDistanceRecursive(
    uint32_t nodeIndex,
    const Float3& position,
    float& minDistance) const
{
    const BVHNode& node = _nodes[nodeIndex];


    float boundsDistance =
        node.Bounds.Box.Distance(position);


    if (boundsDistance > minDistance)
    {
        return;
    }



    if (node.IsLeaf())
    {
        for(uint32_t i=0;i<node.PrimitiveCount;i++)
        {
            uint32_t primitiveIndex =
                _primitiveIndices[
                    node.FirstPrimitive+i];


            float distance =
                Traits::ClosestDistance(
                    (*_primitives)[primitiveIndex],
                    position);


            minDistance =
                std::min(
                    minDistance,
                    distance);
        }

        return;
    }



    ClosestDistanceRecursive(
        node.LeftChild,
        position,
        minDistance);


    ClosestDistanceRecursive(
        node.RightChild,
        position,
        minDistance);
}

template<typename Primitive, typename Traits>
void BVH<Primitive, Traits>::RayIntersect(
    const Ray& ray,
    std::set<uint32_t>& intersectPrimitiveIds) const
{
    intersectPrimitiveIds.clear();

    if (_nodes.empty())
    {
        return;
    }

    RayIntersectNode(
        0,
        ray,
        intersectPrimitiveIds);
}
template<typename Primitive, typename Traits>
void BVH<Primitive, Traits>::RayIntersectNode(
    uint32_t nodeIndex,
    const Ray& ray,
    std::set<uint32_t>& outPrimitiveIds) const
{
    const BVHNode& node = _nodes[nodeIndex];


    if (!ray.RayIntersectAABB(node.Bounds.Box))
    {
        return;
    }


    if (node.IsLeaf())
    {
        for (uint32_t i = 0; i < node.PrimitiveCount; i++)
        {
            uint32_t internalIndex =
                node.FirstPrimitive + i;


            uint32_t primitiveId =
                _primitiveIndices[internalIndex];
            float dis = 0;
            if (Traits::RayIntersect((*_primitives)[primitiveId], ray, dis)) {
                outPrimitiveIds.insert(
                    primitiveId);
            }

        }

        return;
    }


    RayIntersectNode(
        node.LeftChild,
        ray,
        outPrimitiveIds);


    RayIntersectNode(
        node.RightChild,
        ray,
        outPrimitiveIds);
}

template<typename Primitive, typename Traits>
void BVH<Primitive, Traits>::GetPrimitiveIdsInsideBounds(const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const
{
    outPrimitiveIds.clear();

    if (_nodes.empty())
    {
        return;
    }

    QueryBoundsRecursive(0, box, outPrimitiveIds);
}


template<typename Primitive, typename Traits>
void BVH<Primitive, Traits>::QueryBoundsRecursive(uint32_t nodeIndex, const BoxSphereBounds& box, std::set<uint32_t>& outPrimitiveIds) const
{
    const BVHNode& node = _nodes[nodeIndex];


    if (!node.Bounds.Intersects(box))
    {
        return;
    }


    if (node.IsLeaf())
    {
        for (uint32_t i = 0; i < node.PrimitiveCount; i++)
        {
            uint32_t primitiveIndex = _primitiveIndices[node.FirstPrimitive + i];

            outPrimitiveIds.insert(primitiveIndex);
        }

        return;
    }


    if (node.LeftChild != BVHInvalidNode)
    {
        QueryBoundsRecursive(node.LeftChild, box, outPrimitiveIds);
    }


    if (node.RightChild != BVHInvalidNode)
    {
        QueryBoundsRecursive(node.RightChild, box, outPrimitiveIds);
    }
}

}