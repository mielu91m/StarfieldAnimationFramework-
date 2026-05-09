#include "Trampoline.h"

namespace Util::Trampoline
{
	struct HookData
	{
		size_t totalAlloc = 0;
		std::vector<std::function<void(REL::Trampoline&, uintptr_t)>> hookFuncs;
	};

	std::unique_ptr<HookData>& GetTempData()
	{
		static std::unique_ptr<HookData> tempData{ std::make_unique<HookData>() };
		return tempData;
	}

	void AddHook(size_t bytesAlloc, const std::function<void(REL::Trampoline&, uintptr_t)>& func)
	{
		auto& tempData = GetTempData();
		tempData->totalAlloc += bytesAlloc;
		tempData->hookFuncs.push_back(func);
	}

	void ProcessHooks()
	{
		auto& tempData = GetTempData();
		uintptr_t baseAddr = REX::FModule::GetExecutingModule().GetBaseAddress();

		if (tempData->totalAlloc > 0) {
			REL::GetTrampoline().create(tempData->totalAlloc);
		}
		auto& t = REL::GetTrampoline();
		for (auto& f : tempData->hookFuncs) {
			f(t, baseAddr);
		}
		tempData.reset();
	}
}
