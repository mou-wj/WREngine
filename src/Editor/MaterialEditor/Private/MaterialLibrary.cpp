#include "MaterialLibrary.h"
#include "Material.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace Editor
{
    namespace
    {
        using Json = nlohmann::json;
        namespace fs = std::filesystem;
        std::string Extension(const fs::path& path)
        {
            auto text = path.extension().string();
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return text;
        }
        std::string SafeStem(const std::string& text)
        {
            std::string result;
            for (unsigned char value : text)
                result += (std::isalnum(value) && value < 128) || value == '-' || value == '_' ? static_cast<char>(value) : '_';
            if (result.empty() || result.find_first_not_of('_') == std::string::npos) result = "Material";
            return result.substr(0, 64);
        }
        bool IsInside(const fs::path& child, const fs::path& root)
        {
            const auto relative = fs::weakly_canonical(child).lexically_relative(fs::weakly_canonical(root));
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }
        Json ReadMaterial(const fs::path& path)
        {
            std::ifstream input(path);
            if (!input) throw std::runtime_error("Cannot read material: " + MaterialLibrary::PathText(path));
            Json data;
            input >> data;
            if (!data.is_object() || (!data.contains("version") && !data.contains("shadingModel") && !data.contains("parameters") && !data.contains("scalarParameters")))
                throw std::runtime_error("Expected a project material JSON, not an arbitrary JSON document");
            if (data.value("version", 1) != 1) throw std::runtime_error("Unsupported material schema version");
            const auto shading = data.value("shadingModel", std::string("Unlit"));
            const auto blend = data.value("blendMode", std::string("Opaque"));
            const auto cull = data.value("cullMode", std::string("Back"));
            if (shading != "Lit" && shading != "Unlit") throw std::runtime_error("Unsupported shadingModel");
            if (blend != "Opaque" && blend != "Masked" && blend != "Translucent") throw std::runtime_error("Unsupported blendMode");
            if (cull != "None" && cull != "Front" && cull != "Back") throw std::runtime_error("Unsupported cullMode");
            // Reuse the runtime parameter contract without creating GPU resources.
            Engine::Material validation;
            if (data.contains("staticParameters"))
                for (const auto& [name, value] : data.at("staticParameters").items())
                    if (name == "twoSided") validation.SetTwoSided(value.get<bool>());
                    else if (value != false) throw std::runtime_error("Unsupported material static parameter: " + name);
            for (const auto* block : {"parameters", "scalarParameters", "vectorParameters"})
                if (data.contains(block))
                    for (const auto& [name, value] : data.at(block).items())
                        if (value.is_array()) validation.SetParameter(name, value.get<std::array<float, 4>>());
                        else validation.SetParameter(name, value.get<float>());
            if (data.contains("textureParameters"))
                for (const auto& [name, value] : data.at("textureParameters").items())
                {
                    Engine::FindMaterialParameterDefinition(name, Engine::EMaterialParameterType::Texture);
                    if (!value.is_null() && !value.is_string()) throw std::runtime_error("Texture dependencies must be file paths or null");
                }
            return data;
        }
        void WriteMaterial(const fs::path& path, const Json& data)
        {
            std::ofstream output(path);
            if (!output || !(output << data.dump(2) << '\n')) throw std::runtime_error("Cannot write material JSON");
            output.close();
            if (!output) throw std::runtime_error("Cannot flush material JSON");
        }
        fs::path UniqueDirectory(const fs::path& parent, const std::string& stem)
        {
            fs::create_directories(parent);
            for (uint32_t index = 0; ; ++index)
            {
                const auto path = parent / (stem + (index ? "_" + std::to_string(index) : ""));
                std::error_code error;
                if (fs::create_directory(path, error)) return path;
                if (error && !fs::exists(path)) throw fs::filesystem_error("Cannot create material directory", path, error);
            }
        }
    }

    std::string MaterialLibrary::PathText(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    }
    bool MaterialLibrary::IsImage(const std::filesystem::path& path)
    {
        const auto extension = Extension(path);
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga";
    }
    MaterialLibrary::MaterialLibrary(std::filesystem::path root) { SetRoot(std::move(root)); }
    void MaterialLibrary::SetRoot(std::filesystem::path root)
    {
        m_Root = root.empty() ? std::filesystem::path{} : std::filesystem::absolute(root).lexically_normal(); Refresh();
    }

    void MaterialLibrary::Refresh()
    {
        m_Entries.clear(); m_Categories.clear(); m_Warnings.clear();
        std::set<std::string> categories;
        if (m_Root.empty() || !std::filesystem::exists(m_Root)) return;
        for (const auto& item : std::filesystem::recursive_directory_iterator(m_Root, std::filesystem::directory_options::skip_permission_denied))
        {
            if (item.is_symlink()) continue;
            if (item.is_directory()) categories.insert(PathText(item.path().lexically_relative(m_Root)));
            if (!item.is_regular_file() || Extension(item.path()) != ".json") continue;
            try
            {
                const auto data = ReadMaterial(item.path());
                MaterialLibraryEntry entry;
                entry.Path = item.path();
                entry.Name = data.value("name", PathText(item.path().stem()));
                entry.Category = PathText(item.path().parent_path().lexically_relative(m_Root));
                if (entry.Category == ".") entry.Category.clear();
                entry.ShadingModel = data.value("shadingModel", std::string("Unlit"));
                for (const auto* block : {"parameters", "vectorParameters"})
                    if (data.contains(block))
                        for (const auto& [name, value] : data.at(block).items())
                            if (Engine::CanonicalMaterialParameterName(name) == "BaseColor") entry.BaseColor = value.get<std::array<float, 4>>();
                m_Entries.push_back(std::move(entry));
            }
            catch (const std::exception& error) { m_Warnings.push_back(PathText(item.path()) + ": " + error.what()); }
        }
        m_Categories.assign(categories.begin(), categories.end());
        std::sort(m_Entries.begin(), m_Entries.end(), [](const auto& a, const auto& b) { return a.Path < b.Path; });
    }
    std::filesystem::path MaterialLibrary::GetCategoryDirectory(const std::string& category) const
    {
        if (m_Root.empty()) throw std::runtime_error("No project content is open");
        const auto path = category.empty() ? m_Root : m_Root / std::filesystem::u8path(category);
        if (path != m_Root && !IsInside(path, m_Root)) throw std::invalid_argument("Category must stay inside the material library");
        return path;
    }
    std::filesystem::path MaterialLibrary::Create(const std::string& name, const std::string& category)
    {
        if (name.empty()) throw std::invalid_argument("Material name cannot be empty");
        const auto destination = UniqueDirectory(GetCategoryDirectory(category), SafeStem(name));
        try
        {
            const auto path = destination / "material.json";
            WriteMaterial(path, Json{{"version",1},{"name",name},{"blendMode","Opaque"},{"shadingModel","Lit"}});
            Refresh();
            return path;
        }
        catch (...)
        {
            if (IsInside(destination, m_Root)) std::filesystem::remove_all(destination);
            throw;
        }
    }
    std::filesystem::path MaterialLibrary::Import(const std::filesystem::path& source, const std::string& category, const std::string& name)
    {
        namespace fs = std::filesystem;
        if (!fs::is_regular_file(source)) throw std::invalid_argument("Drop material JSON files or image files; folders are not imported");
        const bool image = IsImage(source);
        if (!image && Extension(source) != ".json") throw std::invalid_argument("Supported imports: project material JSON, PNG/JPG/BMP/TGA images");
        Json data = image ? Json{{"version",1},{"name",PathText(source.stem())},{"blendMode","Opaque"},{"shadingModel","Lit"},
                                {"textureParameters",{{"BaseColor", "source"}}}}
                          : ReadMaterial(source);
        if (!name.empty()) data["name"] = name;
        const auto destination = UniqueDirectory(GetCategoryDirectory(category), SafeStem(data.value("name", PathText(source.stem()))));
        try
        {
            if (data.contains("textureParameters"))
            {
                uint32_t index = 0;
                for (auto& [name, value] : data["textureParameters"].items())
                {
                    if (value.is_null()) continue;
                    auto texture = image ? source : fs::u8path(value.get<std::string>());
                    if (!image && texture.is_relative()) texture = source.parent_path() / texture;
                    if (!fs::is_regular_file(texture)) throw std::runtime_error("Missing texture dependency: " + PathText(texture));
                    const auto fileName = "texture_" + std::to_string(index++) + Extension(texture);
                    fs::copy_file(texture, destination / fileName, fs::copy_options::none);
                    value = fileName; // Relative to the new JSON, independent of the desktop source.
                }
            }
            const auto path = destination / "material.json";
            WriteMaterial(path, data); // Publish the JSON only after its dependencies exist.
            Refresh();
            return path;
        }
        catch (...)
        {
            // destination was exclusively created by this operation; never remove a source.
            if (IsInside(destination, m_Root)) fs::remove_all(destination);
            throw;
        }
    }
}
