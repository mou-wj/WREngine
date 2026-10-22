#include "RHICaptureHelper.h"
#include <iostream>
#if defined(_WIN32)
#  include <Windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#  include <dlfcn.h>
#endif
#include "PathInfo.h"

#ifdef ENABLE_RENDERDOC_CAPTURE
#include <renderdoc_app.h>
#endif

namespace RHI {

#ifdef ENABLE_RENDERDOC_CAPTURE
typedef RENDERDOC_API_1_6_0 RenderDocAPIType;
#else
typedef void RenderDocAPIType;
#endif

RHICaptureHelper& RHICaptureHelper::GetInstance() {
	static RHICaptureHelper instance;
	return instance;
}

RHICaptureHelper::RHICaptureHelper() {
	Load();
}

void RHICaptureHelper::Load()
{
	if (m_bLoaded) return;
#ifdef ENABLE_RENDERDOC_CAPTURE
	std::string libName;
#if defined(_WIN32)
	libName = std::string(Core::GetExecutableDir()) + "/renderdoc.dll";
#elif defined(__linux__)
	libName = std::string(Core::GetExecutableDir()) + "/librenderdoc.so";
#elif defined(__APPLE__)
	libName = std::string(Core::GetExecutableDir()) + "/librenderdoc.dylib";
#else
#   error "Unsupported platform for RenderDoc capture!"
#endif

#if defined(_WIN32)
	libHandle = (void*)LoadLibraryA(libName.c_str());
#elif defined(__linux__) || defined(__APPLE__)
	libHandle = dlopen(libName.c_str(), RTLD_LAZY);
#endif
	loaded = true;

}
void RHICaptureHelper::Shutdown() {
	if (rdoc_api) reinterpret_cast<RenderDocAPIType*>(rdoc_api)->Shutdown();
}
void RHICaptureHelper::CaptureBegin() {
	if (numCaptures == 0) return;
#ifdef ENABLE_RENDERDOC_CAPTURE
	if (rdoc_api) reinterpret_cast<RenderDocAPIType*>(rdoc_api)->StartFrameCapture(devicePointer, windowPointer);
#endif
	numCaptures--;
}

void RHICaptureHelper::CaptureEnd() {
	if (numCaptures == 0) return;
#ifdef ENABLE_RENDERDOC_CAPTURE
	if (rdoc_api)
	{
		auto res = reinterpret_cast<RenderDocAPIType*>(rdoc_api)->EndFrameCapture(devicePointer, windowPointer);
		if (res == 0) {
			printf("Failed to end capture!\n");
			// 获取当前 RenderDoc 正在写入的日志文件路径
			std::string logPath = reinterpret_cast<RenderDocAPIType*>(rdoc_api)->GetLogFilePathTemplate();
			printf("RenderDoc Log Path: %s\n", logPath);
		}
		uint32_t num = reinterpret_cast<RenderDocAPIType*>(rdoc_api)->GetNumCaptures();
		printf("Total Captures: %u\n", num);
	}
#endif
	WriteCaptureOut();
}

void RHICaptureHelper::SetCaptureOutPath(const std::string& outPath) {
	captureOutPath = outPath;
#ifdef ENABLE_RENDERDOC_CAPTURE
	if (rdoc_api) reinterpret_cast<RenderDocAPIType*>(rdoc_api)->SetCaptureFilePathTemplate(captureOutPath.c_str());
#endif
}

void RHICaptureHelper::SetNumCaptures(uint32_t numCapture) {
	numCaptures = numCapture;
}

void RHICaptureHelper::WriteCaptureOut() {
#ifdef ENABLE_RENDERDOC_CAPTURE
	if (rdoc_api) reinterpret_cast<RenderDocAPIType*>(rdoc_api)->TriggerCapture();
#endif
}

void RHICaptureHelper::SetDevice(void* device) {
	devicePointer = device;
}
void RHICaptureHelper::SetWindow(void* window) {
	windowPointer = window;
}
bool RHICaptureHelper::IsCapturing() {
#ifdef ENABLE_RENDERDOC_CAPTURE
	if (rdoc_api) return reinterpret_cast<RenderDocAPIType*>(rdoc_api)->IsFrameCapturing();
#endif
	return false;
}

void RHICaptureHelper::Init() {
	if (inited) return;

	if (libHandle != nullptr) {
		typedef int (*pRENDERDOC_GetAPI)(int, void**);
		pRENDERDOC_GetAPI RENDERDOC_GetAPI = nullptr;
#if defined(_WIN32)
		RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress((HMODULE)libHandle, "RENDERDOC_GetAPI");
#elif defined(__linux__) || defined(__APPLE__)
		RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(libHandle, "RENDERDOC_GetAPI");
#endif
		if (RENDERDOC_GetAPI != nullptr) {
			RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, &rdoc_api);
			int major = 0, minor = 0, match = 0;
			reinterpret_cast<RenderDocAPIType*>(rdoc_api)->GetAPIVersion(&major, &minor, &match);
			std::cout << "RenderDoc API Version: " << major << "." << minor << "." << match << std::endl;
			// 1. 开启 "Ref All Resources"
			// 这个选项会强制 RenderDoc 包含所有已创建的资源，即使在当前捕获帧内没被显式引用。
			// 这对于纯 Compute 任务非常关键，因为很多 Buffer 是在初始化阶段创建的。
			reinterpret_cast<RenderDocAPIType*>(rdoc_api)->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources, 1);

			// 2. 开启 "Capture All Cmd Lists"
			// 这个选项会告诉 RenderDoc 记录所有提交到队列的命令列表，无论它是否关联到 Present。
			reinterpret_cast<RenderDocAPIType*>(rdoc_api)->SetCaptureOptionU32(eRENDERDOC_Option_CaptureAllCmdLists, 1);

			int active = reinterpret_cast<RenderDocAPIType*>(rdoc_api)->IsTargetControlConnected();
			printf("RenderDoc Connected: %s\n", active ? "YES" : "NO");

			// 获取当前正在捕获的配置，确认你的 SetCaptureOption 是否真的生效了
			uint32_t refAll = reinterpret_cast<RenderDocAPIType*>(rdoc_api)->GetCaptureOptionU32(eRENDERDOC_Option_RefAllResources);
			printf("RefAllResources: %u\n", refAll);


			// 设置捕获
			SetCaptureOutPath(Core::GetExecutableDir() + "/test.rdc");
            SetNumCaptures(40);
			inited = true;
		}
	}
#endif
}

} // namespace RHI
