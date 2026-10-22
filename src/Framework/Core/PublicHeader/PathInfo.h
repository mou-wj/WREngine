#pragma once
#include <string>
namespace Core {

	CORE_API std::string GetProjectDir();

	CORE_API std::string GetExecutableDir();

    CORE_API std::string GetShaderFilesRootDir();

    // =========================
// Path 基础拆分
// =========================

// 获取文件名（含后缀）
    CORE_API std::string GetFileName(const std::string& path);

    // 获取文件名（不含后缀）
    CORE_API std::string GetBaseName(const std::string& path);

    // 获取扩展名（不含点）
    CORE_API std::string GetExtension(const std::string& path);

    // 获取目录路径（去掉文件名）
    CORE_API std::string GetDirectory(const std::string& path);

    // =========================
    // 路径拼接 / 规范化
    // =========================

    // 拼接路径
    CORE_API std::string JoinPath(const std::string& a, const std::string& b);

    CORE_API std::string JoinPath(const std::string& a,
        const std::string& b,
        const std::string& c);

    // 规范化路径（处理 ../ ./ 以及 / \ 混用）
    CORE_API std::string NormalizePath(const std::string& path);

    // =========================
    // 路径判断
    // =========================

    // 是否是绝对路径
    CORE_API bool IsAbsolutePath(const std::string& path);

    // 是否存在扩展名
    CORE_API bool HasExtension(const std::string& path);

    // 是否是某个扩展名
    CORE_API bool IsExtension(const std::string& path, const std::string& ext);

    // =========================
    // 便捷工具
    // =========================

    // 替换扩展名
    CORE_API std::string ReplaceExtension(const std::string& path, const std::string& newExt);

    // 只保留目录（确保末尾带 / 或 \ 统一）
    CORE_API std::string EnsureTrailingSlash(const std::string& path);

    // 相对路径转绝对路径（基于 project 或 exe）
    CORE_API std::string AbsoluteFromProject(const std::string& relativePath);

}
