#pragma once
#include <string>
#include <cstdint>

namespace RHI {

// 可扩展的内容捕获辅助类，当前实现基于 RenderDoc
class RHI_API RHICaptureHelper {
public:
	// 单例获取
	static RHICaptureHelper& GetInstance();

	void Init();
	void Shutdown();
	void CaptureBegin();
	void CaptureEnd();
	void SetCaptureOutPath(const std::string& outPath);
	void SetNumCaptures(uint32_t numCapture);
	void WriteCaptureOut();
	void SetDevice(void* device);
	void SetWindow(void* window);
	bool IsCapturing();

private:
	void Load();
	RHICaptureHelper();
	
	bool m_bLoaded = false;
	bool m_bInited = false;
	std::string m_CaptureOutPath;
	void* m_DevicePointer = nullptr;
	void* m_WindowPointer = nullptr;
	void* m_RdocApi = nullptr; // RenderDoc API 指针，实际类型在cpp中处理
	uint32_t m_NumCaptures = 1;
	void* m_LibHandle = nullptr;
};

} // namespace RHI

// 宏定义，便于调用
#define RHICapture_SetOutPath(x) RHI::RHICaptureHelper::GetInstance().SetCaptureOutPath(x)
#define RHICapture_Begin() RHI::RHICaptureHelper::GetInstance().CaptureBegin()
#define RHICapture_End() RHI::RHICaptureHelper::GetInstance().CaptureEnd()
#define RHICapture_IsCapturing() RHI::RHICaptureHelper::GetInstance().IsCapturing()
#define RHICapture_SetNum(num) RHI::RHICaptureHelper::GetInstance().SetNumCaptures(num)
#define RHICapture_SetDevice(device) RHI::RHICaptureHelper::GetInstance().SetDevice(device)
#define RHICapture_SetWindow(window) RHI::RHICaptureHelper::GetInstance().SetWindow(window)