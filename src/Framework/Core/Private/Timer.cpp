#include "Timer.h"
namespace Core {
    Timer::Timer() : m_LastTime(std::chrono::steady_clock::now()) {}

    float Timer::GetDelta() {
        auto now = std::chrono::steady_clock::now();
        auto delta = now - m_LastTime;
        m_LastTime = now;
        return static_cast<float>(std::chrono::duration<double>(delta).count());
    }

    Timer& Timer::GetGlobalInstance() {
        static Timer instance;
        return instance;
    }
}