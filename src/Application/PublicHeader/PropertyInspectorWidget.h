#pragma once

#include "ImWidget.h"

#include <string>
#include <utility>
#include <vector>

namespace App
{
    class APPLICATION_API PropertyInspectorWidget : public ImGUISlate::ImWidget
    {
    public:
        PropertyInspectorWidget();
        explicit PropertyInspectorWidget(std::vector<std::pair<std::string, std::string>> properties);

        void SetComponentProperties(std::vector<std::pair<std::string, std::string>> properties);
        void SetComponentName(const std::string& name);

        void Draw() override;

    private:
        std::string m_ComponentName = "No Selection";
        std::vector<std::pair<std::string, std::string>> m_Properties;
    };
}
