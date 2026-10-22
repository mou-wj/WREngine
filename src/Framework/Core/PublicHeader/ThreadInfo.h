#pragma once
#include <cstdint>
namespace Core {

	//渲染线程中调用
	CORE_API void InitRenderThreadId();
	CORE_API bool IsInRenderThread();


}