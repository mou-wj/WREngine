#pragma once
#include <string>
#include <mutex>
#include <iostream>
#include <ostream>
#include <fstream>
#include <sstream>
#include <iomanip>   // std::put_time, std::setw, std::setfill
#include <chrono>    // std::chrono::milliseconds
#include <ctime>   
namespace Core {
    enum class Level
    {
        Debug,
        Info,
        Warn,
        Error
    };

    class CORE_API Logger
    {
    public:
        static Logger& Get()
        {
            static Logger instance;
            return instance;
        }

        // 设置日志输出文件，如果为空则只输出控制台
        void SetLogFile(const std::string& filename);

        // 设置日志等级
        void SetLevel(Level level) { m_Level = level; }

        // 核心打印函数
        void Log(Level level, const char* fmt, ...);

        // 便捷宏函数
        void Debug(const char* fmt, ...);
        void Info(const char* fmt, ...);
        void Warn(const char* fmt, ...);
        void Error(const char* fmt, ...);

    private:
        Logger() = default;
        ~Logger() { if (m_File.is_open()) m_File.close(); }

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        std::string GetTimeString();
        std::string LevelToString(Level level);

        std::mutex m_Mutex;
        std::ofstream m_File;
        Level m_Level = Level::Debug;
    };



}

// 核心宏
#define LOG_INTERNAL(level, fmt, ...) \
    ::Core::Logger::Get().Log(::Core::Level::level, fmt, ##__VA_ARGS__)

// 便捷宏
#define LOG_DEBUG(fmt, ...) LOG_INTERNAL(Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  LOG_INTERNAL(Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG_INTERNAL(Warn, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_INTERNAL(Error, fmt, ##__VA_ARGS__)

// DEBUG 日志可开关
#ifdef ENABLE_DEBUG_LOG
#define DLOG(fmt, ...) LOG_DEBUG(fmt, ##__VA_ARGS__)
#else
#define DLOG(fmt, ...) ((void)0)
#endif
