#include "PathInfo.h"
#include "Math.hpp"
#include "Module.h"
#include "BoxSphereBounds.h"
#include <filesystem>
#include <algorithm>
namespace Core {
	std::string GetProjectDir()
	{
		return PROJECT_DIR;
	}
	std::string GetExecutableDir() {
        return EXECUTABLE_DIR;
	}
	std::string GetShaderFilesRootDir() {
		return GetProjectDir() + "/shaders";
	}


    static std::filesystem::path ToFs(const std::string& p)
    {
        return std::filesystem::path(p);
    }

    static std::string ToStr(const std::filesystem::path& p)
    {
        return p.generic_string(); // 统一用 /
    }

    // =========================
    // 基础路径
    // =========================

    std::string GetFileName(const std::string& path)
    {
        return ToFs(path).filename().string();
    }

    std::string GetBaseName(const std::string& path)
    {
        return ToFs(path).stem().string();
    }

    std::string GetExtension(const std::string& path)
    {
        std::string ext = ToFs(path).extension().string();
        if (!ext.empty() && ext[0] == '.')
            ext.erase(0, 1);
        return ext;
    }

    std::string GetDirectory(const std::string& path)
    {
        return ToStr(ToFs(path).parent_path());
    }

    // =========================
    // 拼接路径
    // =========================

    std::string JoinPath(const std::string& a, const std::string& b)
    {
        return ToStr(ToFs(a) / b);
    }

    std::string JoinPath(const std::string& a,
        const std::string& b,
        const std::string& c)
    {
        return ToStr(ToFs(a) / b / c);
    }

    // =========================
    // 规范化路径
    // =========================

    std::string NormalizePath(const std::string& path)
    {
        try {
            return ToStr(std::filesystem::weakly_canonical(ToFs(path)));
        }
        catch (...) {
            // fallback：避免非法路径崩溃
            return ToStr(ToFs(path));
        }
    }

    // =========================
    // 判断类
    // =========================

    bool IsAbsolutePath(const std::string& path)
    {
        return ToFs(path).is_absolute();
    }

    bool HasExtension(const std::string& path)
    {
        return !ToFs(path).extension().empty();
    }

    bool IsExtension(const std::string& path, const std::string& ext)
    {
        std::string e = GetExtension(path);

        auto toLower = [](std::string s)
            {
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                return s;
            };

        return toLower(e) == toLower(ext);
    }

    // =========================
    // 替换后缀
    // =========================

    std::string ReplaceExtension(const std::string& path, const std::string& newExt)
    {
        std::filesystem::path p = ToFs(path);

        std::string ext = newExt;
        if (!ext.empty() && ext[0] != '.')
            ext = "." + ext;

        p.replace_extension(ext);
        return ToStr(p);
    }

    // =========================
    // 结尾斜杠
    // =========================

    std::string EnsureTrailingSlash(const std::string& path)
    {
        std::string p = ToStr(ToFs(path));
        if (p.empty()) return p;

        if (p.back() != '/')
            p.push_back('/');

        return p;
    }

    // =========================
    // 绝对路径（Project）
    // =========================

    std::string AbsoluteFromProject(const std::string& relativePath)
    {
        return NormalizePath(ToStr(ToFs(GetProjectDir()) / relativePath));
    }
}
