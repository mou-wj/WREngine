#include "AssetRegistry.h"
#include "AssetManager.h"
#include <algorithm>

namespace Engine {

AssetRegistry& AssetRegistry::Get()
{
    static AssetRegistry instance;
    return instance;
}

void AssetRegistry::Register(const AssetInfo& info)
{
    auto normalized = info;
    normalized.Path = AssetManager::NormalizePath(info.Path);
    const auto key = normalized.Path;
    std::lock_guard lock(Mutex);
    PathToInfo.insert_or_assign(key, std::move(normalized));
}

std::optional<AssetInfo> AssetRegistry::GetByPath(const std::string& path) const
{
    const auto key = AssetManager::NormalizePath(path);
    std::lock_guard lock(Mutex);
    const auto it = PathToInfo.find(key);
    return it == PathToInfo.end() ? std::nullopt : std::optional<AssetInfo>(it->second);
}

std::vector<AssetInfo> AssetRegistry::GetByName(const std::string& name) const
{
    std::vector<AssetInfo> result;
    std::lock_guard lock(Mutex);
    for (const auto& [path, info] : PathToInfo)
        if (info.Name == name)
            result.push_back(info);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.Path < b.Path; });
    return result;
}

}
