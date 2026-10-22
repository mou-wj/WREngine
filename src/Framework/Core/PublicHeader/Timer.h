#pragma once 
#include <chrono>
namespace Core {
    class CORE_API Timer {
    public:
        Timer();

        float GetDelta();
        inline static Timer& GetGlobalInstance();

    private:
        std::chrono::steady_clock::time_point m_LastTime;

    };
} // namespace Core
