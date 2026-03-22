#pragma once

#include <REL/Trampoline.h>

namespace Util
{
	inline REL::Trampoline& GetTrampoline()
	{
		static REL::Trampoline trampoline;
		return trampoline;
	}

	inline void AllocTrampoline(size_t a_size)
	{
		auto& trampoline = GetTrampoline();
		trampoline.allocate(a_size);
	}
}