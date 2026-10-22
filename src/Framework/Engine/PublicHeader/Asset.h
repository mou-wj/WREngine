#pragma once

#include "EngineExport.h"
#include <memory>
#include <string>
#include <typeindex>

namespace Engine {

class ENGINE_API IAsset : public std::enable_shared_from_this<IAsset>
{
public:
    virtual ~IAsset() = default;
    virtual const std::string& GetName() const = 0;
    virtual const std::string& GetPath() const = 0;
    virtual bool Load() = 0;
};

using AssetSP = std::shared_ptr<IAsset>;

struct AssetInfo
{
    std::string Name;
    std::string Path;
    std::type_index Type = typeid(IAsset);
};

}
