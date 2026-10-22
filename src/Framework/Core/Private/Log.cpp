#include "Log.h"
#include <cstdarg>
namespace Core {
    void Logger::SetLogFile(const std::string& filename)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_File.is_open())
            m_File.close();
        if (!filename.empty())
            m_File.open(filename, std::ios::out | std::ios::app);
    }

    std::string Logger::GetTimeString()
    {
        using namespace std::chrono;
        auto now = system_clock::now();
        auto time = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tmTime;
#ifdef _WIN32
        localtime_s(&tmTime, &time);
#else
        localtime_r(&time, &tm_time);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tmTime, "%Y-%m-%d %H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms.count();
        return oss.str();
    }

    std::string Logger::LevelToString(Level level)
    {
        switch (level)
        {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
        default: return "UNKNOWN";
        }
    }

    void Logger::Log(Level level, const char* fmt, ...)
    {
        if (level < m_Level) return;

        std::lock_guard<std::mutex> lock(m_Mutex);

        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        std::ostringstream oss;
        oss << "[" << GetTimeString() << "] "
            << "[" << LevelToString(level) << "] "
            << buffer;

        std::string msg = oss.str();

        // 控制台输出
        if (level == Level::Error)
            std::cerr << msg << std::endl;
        else
            std::cout << msg << std::endl;

        // 文件输出
        if (m_File.is_open())
        {
            m_File << msg << std::endl;
            m_File.flush();
        }
    }

    void Logger::Debug(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Log(Level::Debug, "%s", buffer);
    }

    void Logger::Info(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Log(Level::Info, "%s", buffer);
    }

    void Logger::Warn(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Log(Level::Warn, "%s", buffer);
    }

    void Logger::Error(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Log(Level::Error, "%s", buffer);
    }



}