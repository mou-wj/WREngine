#pragma once
#include "SceneComponent.h"
#include "Asset.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace Engine
{
    // The scene owns components; their assets continue to own actual resources.
    struct ProjectNode
    {
        std::string Name;
        std::unique_ptr<SceneComponent> Component;
        uint64_t Id = 0;
    };

    struct ProjectSnapshot
    {
        std::vector<ProjectNode> Nodes;
        // Explicit project content, including resources not instantiated in the scene.
        std::vector<AssetSP> Assets;
    };

    class ENGINE_API ProjectArchive
    {
    public:
        // Synchronous, on the scene control thread, before recording GUI draw data.
        // Publishes a new generation only after all of its files are closed.
        static void Save(const std::filesystem::path& directory, const std::vector<ProjectNode>& nodes,
            const std::vector<AssetSP>& assets = {});
        // Reconstructs unregistered components. Failure leaves the live scene alone.
        // Version-1 projects without a snapshot load as an empty scene.
        static ProjectSnapshot Load(const std::filesystem::path& directory);
    };
}
