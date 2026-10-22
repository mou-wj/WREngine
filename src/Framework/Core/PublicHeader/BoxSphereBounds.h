#pragma once

#include "Math.hpp"
#include "Common.h"
#include <algorithm>
#include <limits>



namespace Core {

// Axis-aligned bounding box (AABB)
struct CORE_API AABB {
	Float3 Min;
	Float3 Max;

	AABB() {
		float inf = std::numeric_limits<float>::infinity();
		Min = Float3(inf, inf, inf);
		Max = Float3(-inf, -inf, -inf);
	}

	AABB(const Float3& inMin, const Float3& inMax) : Min(inMin), Max(inMax) {}

	// Mark as empty
	void SetEmpty() {
		float inf = std::numeric_limits<float>::infinity();
		Min = Float3(inf, inf, inf);
		Max = Float3(-inf, -inf, -inf);
	}

	bool IsEmpty() const {
		return Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z;
	}

	// Expand the bounds to include a point
	void ExpandBy(const Float3& point) {
		if (IsEmpty()) {
			Min = point; Max = point; return;
		}
		Min = Float3(CORE_MIN(Min.X, point.X), CORE_MIN(Min.Y, point.Y), CORE_MIN(Min.Z, point.Z));
		Max = Float3(CORE_MAX(Max.X, point.X), CORE_MAX(Max.Y, point.Y), CORE_MAX(Max.Z, point.Z));
	}

	// Expand to include another AABB
	void Merge(const AABB& other) {
		if (other.IsEmpty()) return;
		if (IsEmpty()) { *this = other; return; }
		Min = Float3(CORE_MIN(Min.X, other.Min.X), CORE_MIN(Min.Y, other.Min.Y), CORE_MIN(Min.Z, other.Min.Z));
		Max = Float3(CORE_MAX(Max.X, other.Max.X), CORE_MAX(Max.Y, other.Max.Y), CORE_MAX(Max.Z, other.Max.Z));
	}

	// Center and extent
	Float3 GetCenter() const { return Float3((Min.X+Max.X)*0.5f, (Min.Y+Max.Y)*0.5f, (Min.Z+Max.Z)*0.5f); }
	Float3 GetExtent() const { return Float3((Max.X-Min.X)*0.5f, (Max.Y-Min.Y)*0.5f, (Max.Z-Min.Z)*0.5f); }
	// 计算点到 AABB 的最近距离
	float Distance(const Float3& position) const
	{
		return std::sqrt(DistanceSquared(position));
	}

	// 通常优先计算平方距离，避免不必要的开方（视工程需要，本处提供平方版本）
	float DistanceSquared(const Float3& position) const
	{
		float sqDist = 0.0f;

		// 遍历 X, Y, Z 三个轴
		// 轴 X
		if (position.X < Min.X)
		{
			float diff = Min.X - position.X;
			sqDist += diff * diff;
		}
		else if (position.X > Max.X)
		{
			float diff = position.X - Max.X;
			sqDist += diff * diff;
		}

		// 轴 Y
		if (position.Y < Min.Y)
		{
			float diff = Min.Y - position.Y;
			sqDist += diff * diff;
		}
		else if (position.Y > Max.Y)
		{
			float diff = position.Y - Max.Y;
			sqDist += diff * diff;
		}

		// 轴 Z
		if (position.Z < Min.Z)
		{
			float diff = Min.Z - position.Z;
			sqDist += diff * diff;
		}
		else if (position.Z > Max.Z)
		{
			float diff = position.Z - Max.Z;
			sqDist += diff * diff;
		}

		return sqDist;
	}
	bool Intersects(const AABB& other) const
	{
		return !(Max.X < other.Min.X || Min.X > other.Max.X ||
			Max.Y < other.Min.Y || Min.Y > other.Max.Y ||
			Max.Z < other.Min.Z || Min.Z > other.Max.Z);
	}
	bool Contains(const AABB& other) const
	{
		return other.Min.X >= Min.X && other.Max.X <= Max.X &&
			other.Min.Y >= Min.Y && other.Max.Y <= Max.Y &&
			other.Min.Z >= Min.Z && other.Max.Z <= Max.Z;
	}
};

// Bounding sphere
struct CORE_API BoundingSphere {
	Float3 Center;
	float Radius;

	BoundingSphere() : Center(0.0f,0.0f,0.0f), Radius(0.0f) {}
	BoundingSphere(const Float3& inCenter, float inRadius) : Center(inCenter), Radius(inRadius) {}

	// Build sphere from AABB (sphere that encloses box)
	static BoundingSphere FromAABB(const AABB& box) {
		Float3 c = box.GetCenter();
		Float3 e = box.GetExtent();
		float r = std::sqrt(e.X*e.X + e.Y*e.Y + e.Z*e.Z);
		return BoundingSphere(c, r);
	}
	bool Intersects(const BoundingSphere& other) const
	{
		const Float3 delta = Center - other.Center;

		float distanceSquared =
			delta.X * delta.X +
			delta.Y * delta.Y +
			delta.Z * delta.Z;

		float radiusSum = Radius + other.Radius;

		return distanceSquared <= radiusSum * radiusSum;
	}

	bool Contains(const BoundingSphere& other) const
	{
		const Float3 delta = other.Center - Center;

		float distance =
			std::sqrt(
				delta.X * delta.X +
				delta.Y * delta.Y +
				delta.Z * delta.Z);


		return distance + other.Radius <= Radius;
	}
};

// Combined box + sphere bounds (common in rendering engines)
struct CORE_API BoxSphereBounds {
	AABB Box;
	BoundingSphere Sphere;

	BoxSphereBounds() : Box(), Sphere() {}

	explicit BoxSphereBounds(const AABB& inBox) : Box(inBox), Sphere(BoundingSphere::FromAABB(inBox)) {}

	void UpdateFromAABB(const AABB& inBox) {
		Box = inBox;
		Sphere = BoundingSphere::FromAABB(inBox);
	}

	// Expand bounds to include a point
	void ExpandBy(const Float3& point) {
		Box.ExpandBy(point);
		Sphere = BoundingSphere::FromAABB(Box);
	}

	// Merge with another bounds
	void Merge(const BoxSphereBounds& other) {
		Box.Merge(other.Box);
		Sphere = BoundingSphere::FromAABB(Box);
	}

	void SetEmpty() {
        Box.SetEmpty();
        Sphere = BoundingSphere();
     }

	void Transform(const Mat4& transform) {
		if (Box.IsEmpty()) {
			SetEmpty();
			return;
		}

		const Float3 center = Box.GetCenter();
		const Float3 extent = Box.GetExtent();

		const Float3 newCenter(
			transform(0, 0) * center.X + transform(0, 1) * center.Y + transform(0, 2) * center.Z + transform(0, 3),
			transform(1, 0) * center.X + transform(1, 1) * center.Y + transform(1, 2) * center.Z + transform(1, 3),
			transform(2, 0) * center.X + transform(2, 1) * center.Y + transform(2, 2) * center.Z + transform(2, 3));

		const Float3 newExtent(
			std::fabs(transform(0, 0)) * extent.X + std::fabs(transform(0, 1)) * extent.Y + std::fabs(transform(0, 2)) * extent.Z,
			std::fabs(transform(1, 0)) * extent.X + std::fabs(transform(1, 1)) * extent.Y + std::fabs(transform(1, 2)) * extent.Z,
			std::fabs(transform(2, 0)) * extent.X + std::fabs(transform(2, 1)) * extent.Y + std::fabs(transform(2, 2)) * extent.Z);

		Box = AABB(
			Float3(newCenter.X - newExtent.X, newCenter.Y - newExtent.Y, newCenter.Z - newExtent.Z),
			Float3(newCenter.X + newExtent.X, newCenter.Y + newExtent.Y, newCenter.Z + newExtent.Z));

		Sphere = BoundingSphere::FromAABB(Box);
	}
	bool Intersects(const BoxSphereBounds& other) const
	{
		return Box.Intersects(other.Box);
	}


	bool Contains(const BoxSphereBounds& other) const
	{
		return Box.Contains(other.Box);
	}
};

} // namespace NSCore

