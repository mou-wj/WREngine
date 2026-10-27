#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Editor
{
    struct MaterialLibraryEntry
    {
        std::filesystem::path Path;
        std::string Name;
        std::string Category;
        std::string ShadingModel;
        std::array<float, 4> BaseColor{1, 1, 1, 1};
    };

    // Project-file metadata and explicit import operations. Assets/GPU resources
    // remain owned and loaded by Engine::AssetManager, not by this catalog.
    class MATERIALEDITOR_API MaterialLibrary
    {
    public:
        explicit MaterialLibrary(std::filesystem::path root);
        void Refresh();
        void SetRoot(std::filesystem::path root);
        const std::filesystem::path& GetRoot() const { return m_Root; }
        const std::vector<MaterialLibraryEntry>& GetEntries() const { return m_Entries; }
        const std::vector<std::string>& GetCategories() const { return m_Categories; }
        const std::vector<std::string>& GetWarnings() const { return m_Warnings; }
        std::filesystem::path Create(const std::string& name, const std::string& category);
        std::filesystem::path Import(const std::filesystem::path& source, const std::string& category, const std::string& name = {});
        static bool IsImage(const std::filesystem::path& path);
        static std::string PathText(const std::filesystem::path& path);
    private:
        std::filesystem::path GetCategoryDirectory(const std::string& category) const;
        std::filesystem::path m_Root;
        std::vector<MaterialLibraryEntry> m_Entries;
        std::vector<std::string> m_Categories;
        std::vector<std::string> m_Warnings;
    };
}
