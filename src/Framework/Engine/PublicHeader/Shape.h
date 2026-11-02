#pragma once

#include "StaticMesh.h"

namespace Engine {

enum class EBasicMeshShape { Cube, Sphere, Plane, Cylinder };

ENGINE_API extern StaticMeshSP GStaticMesh_Cube;
ENGINE_API extern StaticMeshSP GStaticMesh_Sphere;
ENGINE_API extern StaticMeshSP GStaticMesh_Plane;
ENGINE_API extern StaticMeshSP GStaticMesh_Cylinder;

ENGINE_API bool InitializeShapeStaticMeshes();
ENGINE_API void ReleaseShapeStaticMeshes();

} // namespace Engine
