#include "AssetManager.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace Engine {

AssetManager& AssetManager::Get()
{
    static AssetManager instance;
    return instance;
}

AssetManager::AssetManager()
{
    // Registry must outlive this manager's pending tasks and destructor.
    AssetRegistry::Get();
}

AssetManager::~AssetManager() { Shutdown(); }

std::string AssetManager::NormalizePath(const std::string& path)
{
    if (path.empty()) throw std::invalid_argument("Asset path cannot be empty");
    return std::filesystem::absolute(std::filesystem::path(path)).lexically_normal().generic_string();
}

AssetSP AssetManager::FindLoaded(const std::string& path) const
{
    std::lock_guard lock(m_Mutex);
    const auto it = m_LoadedAssets.find(path);
    return it == m_LoadedAssets.end() ? nullptr : it->second.lock();
}

AssetSP AssetManager::LoadSyncInternal(const std::string& path, std::type_index type, const AssetFactory& factory)
{
    const auto key = NormalizePath(path);
    std::shared_ptr<LoadRequest> request;
    uint64_t generation = 0;
    bool ownsRequest = false;
    {
        std::lock_guard lock(m_Mutex);
        if (m_bStopping) throw std::runtime_error("AssetManager is shutting down");
        if (auto asset = m_LoadedAssets[key].lock())
        {
            if (std::type_index(typeid(*asset)) != type)
                throw std::invalid_argument("Asset type mismatch: " + key);
            m_ResidentAssets[key] = asset;
            return asset;
        }
        const auto it = m_Loading.find(key);
        if (it != m_Loading.end())
        {
            request = it->second;
            if (request->Type != type) throw std::invalid_argument("Asset type mismatch: " + key);
            if (request->Owner == std::this_thread::get_id())
                throw std::runtime_error("Cyclic asset dependency: " + key);
        }
        else
        {
            request = std::make_shared<LoadRequest>(type);
            request->Owner = std::this_thread::get_id();
            m_Loading.emplace(key, request);
            generation = m_ResidencyGeneration;
            ownsRequest = true;
        }
    }
    if (!ownsRequest) return request->Future.get();

    try
    {
        auto asset = factory(key);
        if (!asset->Load())
        {
            std::cerr << "Failed to load asset: " << key << std::endl;
            asset.reset();
        }
        if (asset) AssetRegistry::Get().Register({asset->GetName(), key, type});
        {
            std::lock_guard lock(m_Mutex);
            if (asset)
            {
                m_LoadedAssets[key] = asset;
                if (generation == m_ResidencyGeneration && request->bKeepResident && !m_bStopping) m_ResidentAssets[key] = asset;
            }
            request->Promise.set_value(asset);
            m_Loading.erase(key);
        }
        return asset;
    }
    catch (...)
    {
        std::lock_guard lock(m_Mutex);
        request->Promise.set_exception(std::current_exception());
        m_Loading.erase(key);
        throw;
    }
}

void AssetManager::QueueLoad(std::function<void()> task)
{
    std::vector<std::future<void>> finished;
    {
        std::lock_guard lock(m_Mutex);
        if (m_bStopping) throw std::runtime_error("AssetManager is shutting down");
        for (auto it = m_Tasks.begin(); it != m_Tasks.end();)
        {
            if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                finished.push_back(std::move(*it));
                it = m_Tasks.erase(it);
            }
            else ++it;
        }
        m_Tasks.push_back(std::async(std::launch::async, std::move(task)));
    }
    for (auto& result : finished)
        try { result.get(); }
        catch (const std::exception& ex) { std::cerr << "Asset load task failed: " << ex.what() << std::endl; }
        catch (...) { std::cerr << "Asset load task failed with an unknown exception" << std::endl; }
}

void AssetManager::Unload(const std::string& path)
{
    const auto key = NormalizePath(path);
    AssetSP removed;
    {
        std::lock_guard lock(m_Mutex);
        if (auto loading = m_Loading.find(key); loading != m_Loading.end()) loading->second->bKeepResident = false;
        auto it = m_ResidentAssets.find(key);
        if (it != m_ResidentAssets.end()) { removed = std::move(it->second); m_ResidentAssets.erase(it); }
    }
}

void AssetManager::Clear()
{
    std::unordered_map<std::string, AssetSP> removed;
    {
        std::lock_guard lock(m_Mutex);
        ++m_ResidencyGeneration;
        removed.swap(m_ResidentAssets);
    }
}

void AssetManager::WaitForLoads()
{
    // Called by the controlling thread, never from a load callback.
    for (;;)
    {
        std::vector<std::future<void>> tasks;
        std::vector<std::shared_future<AssetSP>> requests;
        {
            std::lock_guard lock(m_Mutex);
            tasks.swap(m_Tasks);
            for (const auto& [path, request] : m_Loading) requests.push_back(request->Future);
        }
        if (tasks.empty() && requests.empty()) break;
        for (auto& task : tasks)
            try { task.get(); }
            catch (const std::exception& ex) { std::cerr << "Asset load task failed: " << ex.what() << std::endl; }
            catch (...) { std::cerr << "Asset load task failed with an unknown exception" << std::endl; }
        for (auto& request : requests) request.wait();
    }
}

void AssetManager::Shutdown()
{
    {
        std::lock_guard lock(m_Mutex);
        m_bStopping = true;
    }
    WaitForLoads();
    Clear();
}

}
