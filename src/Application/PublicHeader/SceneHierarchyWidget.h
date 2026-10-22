#pragma once

#include "ImWidget.h"

#include <string>
#include <vector>

namespace App
{
    class APPLICATION_API SceneHierarchyWidget : public ImGUISlate::ImWidget
    {
    public:
        SceneHierarchyWidget();
        explicit SceneHierarchyWidget(std::vector<std::string> sceneObjects);

        void SetSceneObjects(std::vector<std::string> sceneObjects);
        void SetSelectedObject(const std::string& objectName);

        void Draw() override;

    private:
        std::vector<std::string> m_SceneObjects;
        std::string m_SelectedObject;
    };
}
