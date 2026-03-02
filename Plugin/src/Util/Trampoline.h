#pragma once

#include <SFSE/Trampoline.h>

namespace Util
{
	inline SFSE::Trampoline& GetTrampoline()
	{
		static SFSE::Trampoline trampoline;
		return trampoline;
	}

	inline void AllocTrampoline(size_t a_size)
	{
		auto& trampoline = GetTrampoline();
		SFSE::AllocTrampoline(a_size);
	}
}