#include "Shape.h"
#include "StaticMeshResources.h"
#include "BoxSphereBounds.h"
#include <memory>
#include <vector>
#include <cmath>

namespace Engine {

namespace {

static void FillVertexBuffer(VertexBuffer& buffer, std::vector<float>&& data, uint32_t numComponents)
{
    buffer.Vertices = std::move(data);
    buffer.NumComponents = numComponents;
    buffer.bValid = !buffer.Vertices.empty();
}

static void FillLODFromData(
    LODResource& outLod,
    std::vector<float>&& positions,
    std::vector<float>&& uVs,
    std::vector<float>&& normals,
    std::vector<float>&& tangents,
    std::vector<uint32_t>&& indices)
{
    FillVertexBuffer(outLod.VertexBuffers.PositionBuffer, std::move(positions), 3);
    FillVertexBuffer(outLod.VertexBuffers.UVBuffer, std::move(uVs), 2);
    FillVertexBuffer(outLod.VertexBuffers.NormalBuffer, std::move(normals), 3);
    FillVertexBuffer(outLod.VertexBuffers.TangentBuffer, std::move(tangents), 4);
    outLod.IndexBuffer.Indices = std::move(indices);

    SectionInfo section;
    section.FirstIndex = 0;
    section.NumIndices = static_cast<uint32_t>(outLod.IndexBuffer.Indices.size());
    section.MaterialIndex = 0;
    outLod.Sections.push_back(section);
}

static void ComputeBoundsFromPositions(Core::BoxSphereBounds& outBounds, const std::vector<float>& positions)
{
    Core::AABB bounds;
    bounds.SetEmpty();

    const size_t vertexCount = positions.size() / 3;
    for (size_t index = 0; index < vertexCount; ++index) {
        Core::Float3 position(
            positions[index * 3 + 0],
            positions[index * 3 + 1],
            positions[index * 3 + 2]);
        bounds.ExpandBy(position);
    }

    outBounds.UpdateFromAABB(bounds);
}

static std::shared_ptr<StaticMesh> CreateStaticMeshFromData(
    std::vector<float>&& positions,
    std::vector<float>&& uVs,
    std::vector<float>&& normals,
    std::vector<float>&& tangents,
    std::vector<uint32_t>&& indices)
{
    auto renderData = std::make_shared<StaticMeshRenderData>();
    LODResource lod;
    FillLODFromData(lod, std::move(positions), std::move(uVs), std::move(normals), std::move(tangents), std::move(indices));
    lod.InitializeResources();
    ComputeBoundsFromPositions(renderData->Bounds, lod.VertexBuffers.PositionBuffer.Vertices);
    renderData->AddLOD(std::move(lod));
    return std::make_shared<StaticMesh>(renderData);
}

static void BuildCubeData(
    std::vector<float>& outPositions,
    std::vector<float>& outUVs,
    std::vector<float>& outNormals,
    std::vector<float>& outTangents,
    std::vector<uint32_t>& outIndices)
{
    const float facePositions[] = {
        // +X face
         1, -1, -1,  1, -1,  1,  1,  1,  1,  1,  1, -1,
        // -X face
        -1, -1,  1, -1, -1, -1, -1,  1, -1, -1,  1,  1,
        // +Y face
        -1,  1,  1,  1,  1,  1,  1,  1, -1, -1,  1, -1,
        // -Y face
        -1, -1, -1,  1, -1, -1,  1, -1,  1, -1, -1,  1,
        // +Z face
        -1, -1,  1,  1, -1,  1,  1,  1,  1, -1,  1,  1,
        // -Z face
         1, -1, -1, -1, -1, -1, -1,  1, -1,  1,  1, -1,
    };

    const float faceNormals[] = {
        // +X
         1, 0, 0,  1, 0, 0,  1, 0, 0,  1, 0, 0,
        // -X
        -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0,
        // +Y
         0, 1, 0,  0, 1, 0,  0, 1, 0,  0, 1, 0,
        // -Y
         0,-1, 0,  0,-1, 0,  0,-1, 0,  0,-1, 0,
        // +Z
         0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1,
        // -Z
         0, 0,-1,  0, 0,-1,  0, 0,-1,  0, 0,-1,
    };

    const float faceUVs[] = {
        0, 0,  1, 0,  1, 1,  0, 1,
        0, 0,  1, 0,  1, 1,  0, 1,
        0, 0,  1, 0,  1, 1,  0, 1,
        0, 0,  1, 0,  1, 1,  0, 1,
        0, 0,  1, 0,  1, 1,  0, 1,
        0, 0,  1, 0,  1, 1,  0, 1,
    };

    const float faceTangents[] = {
        // +X
         0, 0, 1, 0,  0, 0, 1, 0,  0, 0, 1, 0,  0, 0, 1, 0,
        // -X
         0, 0,-1, 0,  0, 0,-1, 0,  0, 0,-1, 0,  0, 0,-1, 0,
        // +Y
         1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0,
        // -Y
         1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0,
        // +Z
         1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0,
        // -Z
        -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0,
    };

    outPositions.assign(std::begin(facePositions), std::end(facePositions));
    outNormals.assign(std::begin(faceNormals), std::end(faceNormals));
    outUVs.assign(std::begin(faceUVs), std::end(faceUVs));
    outTangents.assign(std::begin(faceTangents), std::end(faceTangents));

    static const uint32_t faceIndices[] = {
        0,  1,  2,  0,  2,  3,
        4,  5,  6,  4,  6,  7,
        8,  9, 10,  8, 10, 11,
       12, 13, 14, 12, 14, 15,
       16, 17, 18, 16, 18, 19,
       20, 21, 22, 20, 22, 23,
    };
    outIndices.assign(std::begin(faceIndices), std::end(faceIndices));
}

static void BuildPlaneData(
    std::vector<float>& outPositions,
    std::vector<float>& outUVs,
    std::vector<float>& outNormals,
    std::vector<float>& outTangents,
    std::vector<uint32_t>& outIndices)
{
    outPositions = {
        -1, 0, -1,
         1, 0, -1,
         1, 0,  1,
        -1, 0,  1,
    };

    outNormals = {
         0, 1, 0,
         0, 1, 0,
         0, 1, 0,
         0, 1, 0,
    };

    outUVs = {
        0, 0,
        1, 0,
        1, 1,
        0, 1,
    };

    outTangents = {
        1, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,
        1, 0, 0, 0,
    };

    outIndices = {0, 1, 2, 0, 2, 3};
}

static void BuildSphereData(
    std::vector<float>& outPositions,
    std::vector<float>& outUVs,
    std::vector<float>& outNormals,
    std::vector<float>& outTangents,
    std::vector<uint32_t>& outIndices,
    int latSegments = 16,
    int lonSegments = 16)
{
    const float pi = 3.14159265359f;
    const int latCount = latSegments + 1;
    const int lonCount = lonSegments + 1;

    outPositions.clear();
    outNormals.clear();
    outUVs.clear();
    outTangents.clear();
    outIndices.clear();

    outPositions.reserve(latCount * lonCount * 3);
    outNormals.reserve(latCount * lonCount * 3);
    outUVs.reserve(latCount * lonCount * 2);
    outTangents.reserve(latCount * lonCount * 4);

    for (int lat = 0; lat < latCount; ++lat) {
        const float theta = lat * pi / latSegments;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (int lon = 0; lon < lonCount; ++lon) {
            const float phi = lon * 2.0f * pi / lonSegments;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            const float x = sinTheta * cosPhi;
            const float y = cosTheta;
            const float z = sinTheta * sinPhi;

            outPositions.push_back(x);
            outPositions.push_back(y);
            outPositions.push_back(z);

            outNormals.push_back(x);
            outNormals.push_back(y);
            outNormals.push_back(z);

            outUVs.push_back(static_cast<float>(lon) / lonSegments);
            outUVs.push_back(1.0f - static_cast<float>(lat) / latSegments);

            outTangents.push_back(-sinPhi);
            outTangents.push_back(0.0f);
            outTangents.push_back(cosPhi);
            outTangents.push_back(0.0f);
        }
    }

    for (int lat = 0; lat < latSegments; ++lat) {
        for (int lon = 0; lon < lonSegments; ++lon) {
            const uint32_t first = static_cast<uint32_t>(lat * lonCount + lon);
            const uint32_t second = static_cast<uint32_t>((lat + 1) * lonCount + lon);

            outIndices.push_back(first);
            outIndices.push_back(second);
            outIndices.push_back(second + 1);

            outIndices.push_back(first);
            outIndices.push_back(second + 1);
            outIndices.push_back(first + 1);
        }
    }
}

static void BuildCylinderData(
    std::vector<float>& outPositions,
    std::vector<float>& outUVs,
    std::vector<float>& outNormals,
    std::vector<float>& outTangents,
    std::vector<uint32_t>& outIndices,
    int segments = 16)
{
    const float pi = 3.14159265359f;
    const float halfHeight = 1.0f;
    const int ringSize = segments + 1;

    outPositions.clear();
    outUVs.clear();
    outNormals.clear();
    outTangents.clear();
    outIndices.clear();

    outPositions.reserve((ringSize * 2 + ringSize * 2 + 2) * 3);
    outUVs.reserve((ringSize * 2 + ringSize * 2 + 2) * 2);
    outNormals.reserve((ringSize * 2 + ringSize * 2 + 2) * 3);
    outTangents.reserve((ringSize * 2 + ringSize * 2 + 2) * 4);

    // Side vertices
    for (int slice = 0; slice < ringSize; ++slice) {
        const float theta = slice * 2.0f * pi / segments;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        outPositions.push_back(cosTheta);
        outPositions.push_back(halfHeight);
        outPositions.push_back(sinTheta);
        outNormals.push_back(cosTheta);
        outNormals.push_back(0.0f);
        outNormals.push_back(sinTheta);
        outUVs.push_back(static_cast<float>(slice) / segments);
        outUVs.push_back(0.0f);
        outTangents.push_back(-sinTheta);
        outTangents.push_back(0.0f);
        outTangents.push_back(cosTheta);
        outTangents.push_back(0.0f);
    }

    for (int slice = 0; slice < ringSize; ++slice) {
        const float theta = slice * 2.0f * pi / segments;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        outPositions.push_back(cosTheta);
        outPositions.push_back(-halfHeight);
        outPositions.push_back(sinTheta);
        outNormals.push_back(cosTheta);
        outNormals.push_back(0.0f);
        outNormals.push_back(sinTheta);
        outUVs.push_back(static_cast<float>(slice) / segments);
        outUVs.push_back(1.0f);
        outTangents.push_back(-sinTheta);
        outTangents.push_back(0.0f);
        outTangents.push_back(cosTheta);
        outTangents.push_back(0.0f);
    }

    const uint32_t topCenterIndex = static_cast<uint32_t>(outPositions.size() / 3);
    outPositions.push_back(0.0f);
    outPositions.push_back(halfHeight);
    outPositions.push_back(0.0f);
    outNormals.push_back(0.0f);
    outNormals.push_back(1.0f);
    outNormals.push_back(0.0f);
    outUVs.push_back(0.5f);
    outUVs.push_back(0.5f);
    outTangents.push_back(1.0f);
    outTangents.push_back(0.0f);
    outTangents.push_back(0.0f);
    outTangents.push_back(0.0f);

    const uint32_t topRingIndex = static_cast<uint32_t>(outPositions.size() / 3);
    for (int slice = 0; slice < ringSize; ++slice) {
        const float theta = slice * 2.0f * pi / segments;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        outPositions.push_back(cosTheta);
        outPositions.push_back(halfHeight);
        outPositions.push_back(sinTheta);
        outNormals.push_back(0.0f);
        outNormals.push_back(1.0f);
        outNormals.push_back(0.0f);
        outUVs.push_back((cosTheta + 1.0f) * 0.5f);
        outUVs.push_back((sinTheta + 1.0f) * 0.5f);
        outTangents.push_back(-sinTheta);
        outTangents.push_back(0.0f);
        outTangents.push_back(cosTheta);
        outTangents.push_back(0.0f);
    }

    const uint32_t bottomCenterIndex = static_cast<uint32_t>(outPositions.size() / 3);
    outPositions.push_back(0.0f);
    outPositions.push_back(-halfHeight);
    outPositions.push_back(0.0f);
    outNormals.push_back(0.0f);
    outNormals.push_back(-1.0f);
    outNormals.push_back(0.0f);
    outUVs.push_back(0.5f);
    outUVs.push_back(0.5f);
    outTangents.push_back(1.0f);
    outTangents.push_back(0.0f);
    outTangents.push_back(0.0f);
    outTangents.push_back(0.0f);

    const uint32_t bottomRingIndex = static_cast<uint32_t>(outPositions.size() / 3);
    for (int slice = 0; slice < ringSize; ++slice) {
        const float theta = slice * 2.0f * pi / segments;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        outPositions.push_back(cosTheta);
        outPositions.push_back(-halfHeight);
        outPositions.push_back(sinTheta);
        outNormals.push_back(0.0f);
        outNormals.push_back(-1.0f);
        outNormals.push_back(0.0f);
        outUVs.push_back((cosTheta + 1.0f) * 0.5f);
        outUVs.push_back((sinTheta + 1.0f) * 0.5f);
        outTangents.push_back(-sinTheta);
        outTangents.push_back(0.0f);
        outTangents.push_back(cosTheta);
        outTangents.push_back(0.0f);
    }

    for (int slice = 0; slice < segments; ++slice) {
        const uint32_t top0 = static_cast<uint32_t>(slice);
        const uint32_t top1 = static_cast<uint32_t>(slice + 1);
        const uint32_t bottom0 = static_cast<uint32_t>(ringSize + slice);
        const uint32_t bottom1 = static_cast<uint32_t>(ringSize + slice + 1);

        outIndices.push_back(top0);
        outIndices.push_back(bottom0);
        outIndices.push_back(bottom1);
        outIndices.push_back(top0);
        outIndices.push_back(bottom1);
        outIndices.push_back(top1);

        outIndices.push_back(topCenterIndex);
        outIndices.push_back(topRingIndex + static_cast<uint32_t>(slice + 1));
        outIndices.push_back(topRingIndex + static_cast<uint32_t>(slice));

        outIndices.push_back(bottomCenterIndex);
        outIndices.push_back(bottomRingIndex + static_cast<uint32_t>(slice));
        outIndices.push_back(bottomRingIndex + static_cast<uint32_t>(slice + 1));
    }
}

static std::shared_ptr<StaticMesh> CreateCubeMesh()
{
    std::vector<float> positions;
    std::vector<float> uVs;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    BuildCubeData(positions, uVs, normals, tangents, indices);
    return CreateStaticMeshFromData(std::move(positions), std::move(uVs), std::move(normals), std::move(tangents), std::move(indices));
}

static std::shared_ptr<StaticMesh> CreatePlaneMesh()
{
    std::vector<float> positions;
    std::vector<float> uVs;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    BuildPlaneData(positions, uVs, normals, tangents, indices);
    return CreateStaticMeshFromData(std::move(positions), std::move(uVs), std::move(normals), std::move(tangents), std::move(indices));
}

static std::shared_ptr<StaticMesh> CreateSphereMesh()
{
    std::vector<float> positions;
    std::vector<float> uVs;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    BuildSphereData(positions, uVs, normals, tangents, indices);
    return CreateStaticMeshFromData(std::move(positions), std::move(uVs), std::move(normals), std::move(tangents), std::move(indices));
}

static std::shared_ptr<StaticMesh> CreateCylinderMesh()
{
    std::vector<float> positions;
    std::vector<float> uVs;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    BuildCylinderData(positions, uVs, normals, tangents, indices);
    return CreateStaticMeshFromData(std::move(positions), std::move(uVs), std::move(normals), std::move(tangents), std::move(indices));
}

} // namespace

StaticMeshSP GStaticMesh_Cube;
StaticMeshSP GStaticMesh_Sphere;
StaticMeshSP GStaticMesh_Plane;
StaticMeshSP GStaticMesh_Cylinder;

ENGINE_API bool InitializeShapeStaticMeshes()
{
    ReleaseShapeStaticMeshes();

    GStaticMesh_Cube = CreateCubeMesh();
    GStaticMesh_Plane = CreatePlaneMesh();
    GStaticMesh_Sphere = CreateSphereMesh();
    GStaticMesh_Cylinder = CreateCylinderMesh();

    return GStaticMesh_Cube && GStaticMesh_Plane && GStaticMesh_Sphere && GStaticMesh_Cylinder;
}

ENGINE_API void ReleaseShapeStaticMeshes()
{
    GStaticMesh_Cube.reset();
    GStaticMesh_Plane.reset();
    GStaticMesh_Sphere.reset();
    GStaticMesh_Cylinder.reset();
}

} // namespace Engine
