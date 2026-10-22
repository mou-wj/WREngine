#include "ApplicationBase.h"
namespace SlateCore {
	ApplicationBase* CurApplication = nullptr;
	ApplicationBase* ApplicationBase::GetApplication() {
		return CurApplication;
	}
	void ApplicationBase::SetApplication(ApplicationBase* inApplication) {
		CurApplication = inApplication;
	}





}