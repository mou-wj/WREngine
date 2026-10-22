#pragma once

#include "Asset.h"
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Engine {

// Metadata only. Query results are snapshots, independent of later registrations.
class ENGINE_API AssetRegistry
{
public:
    static AssetRegistry& Get();
    void Register(const AssetInfo& info);
    std::optional<AssetInfo> GetByPath(const std::string& path) const;
    std::vector<AssetInfo> GetByName(const std::string& name) const;

private:
    mutable std::mutex m_Mutex;
    std::unordered_map<std::string, AssetInfo> m_PathToInfo;
};

}
