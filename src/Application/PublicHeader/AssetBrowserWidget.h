#pragma once

#include "ImWidget.h"

#include <string>
#include <vector>

namespace App
{
    class APPLICATION_API AssetBrowserWidget : public ImGUISlate::ImWidget
    {
    public:
        AssetBrowserWidget();
        explicit AssetBrowserWidget(std::vector<std::string> assetPaths);

        void SetAssets(std::vector<std::string> assetPaths);
        void Draw() override;

    private:
        std::vector<std::string> m_AssetPaths;
    };
}
