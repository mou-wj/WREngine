#pragma once

// 跨平台 DLL 导出/导入宏
#if defined(_WIN32) || defined(_WIN64)
#ifdef ENGINE_EXPORTS          // Engine 自己编译时定义
#define ENGINE_API __declspec(dllexport)
#else
#define ENGINE_API __declspec(dllimport)
#endif
#else
#ifdef ENGINE_EXPORTS
#define ENGINE_API __attribute__((visibility("default")))
#else
#define ENGINE_API
#endif
#endif