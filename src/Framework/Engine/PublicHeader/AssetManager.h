#pragma once

#include "Asset.h"
#include "AssetRegistry.h"
// Compatibility includes: existing callers may still include only AssetManager.h.
#include "TextureAsset.h"
#include "MaterialAsset.h"
#include "StaticMeshAsset.h"
#include "SkyLightAsset.h"
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Engine {

class ENGINE_API AssetManager
{
public:
    using LoadCallback = std::function<void(AssetSP)>;
    static AssetManager& Get();
    static std::string NormalizePath(const std::string& path);

    template<typename AssetType>
    std::shared_ptr<AssetType> LoadSync(const std::string& path)
    {
        return std::dynamic_pointer_cast<AssetType>(LoadSyncInternal(path, typeid(AssetType),
            [](const std::string& normalized) { return std::make_shared<AssetType>(normalized); }));
    }

    // Completion runs outside manager locks on the load worker, including cache hits.
    template<typename AssetType>
    void LoadAsync(const std::string& path, std::function<void(std::shared_ptr<AssetType>)> callback)
    {
        QueueLoad([this, path, callback = std::move(callback)] {
            std::shared_ptr<AssetType> asset;
            try { asset = LoadSync<AssetType>(path); }
            catch (...)
            {
                if (callback) callback(nullptr);
                throw;
            }
            if (callback) callback(std::move(asset));
        });
    }

    template<typename AssetType>
    std::shared_ptr<AssetType> GetAssetShared(const std::string& path) const
    {
        return std::dynamic_pointer_cast<AssetType>(FindLoaded(NormalizePath(path)));
    }

    // Borrowed access for compatibility; retain GetAssetShared when storing a reference.
    template<typename AssetType>
    const AssetType* GetAsset(const std::string& path) const
    {
        return GetAssetShared<AssetType>(path).get();
    }

    template<typename AssetType>
    const AssetType* GetByName(const std::string& name) const
    {
        for (const auto& info : AssetRegistry::Get().GetByName(name))
            if (info.Type == typeid(AssetType))
                if (auto asset = GetAsset<AssetType>(info.Path)) return asset;
        return nullptr;
    }

    // Release residency only; components and asset dependencies can keep assets alive.
    void Unload(const std::string& path);
    void Clear();
    void WaitForLoads();
    void Shutdown();

private:
    using AssetFactory = std::function<AssetSP(const std::string&)>;
    struct LoadRequest
    {
        explicit LoadRequest(std::type_index type) : Type(type), Future(Promise.get_future().share()) {}
        std::type_index Type;
        std::thread::id Owner;
        std::promise<AssetSP> Promise;
        std::shared_future<AssetSP> Future;
        bool bKeepResident = true;
    };

    AssetManager();
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    AssetSP LoadSyncInternal(const std::string& path, std::type_index type, const AssetFactory& factory);
    AssetSP FindLoaded(const std::string& path) const;
    void QueueLoad(std::function<void()> task);

    mutable std::mutex m_Mutex;
    std::unordered_map<std::string, AssetSP> m_ResidentAssets;
    std::unordered_map<std::string, std::weak_ptr<IAsset>> m_LoadedAssets;
    std::unordered_map<std::string, std::shared_ptr<LoadRequest>> m_Loading;
    std::vector<std::future<void>> m_Tasks;
    uint64_t m_ResidencyGeneration = 0;
    bool m_bStopping = false;
};

}
