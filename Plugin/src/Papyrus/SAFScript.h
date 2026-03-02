#pragma once
#include "PCH.h"

namespace Papyrus::SAFScript
{
	constexpr const char* SCRIPT_NAME{ "SAFScript" };
	
	// Funkcja bindująca native metody do VM
	bool Bind(RE::BSScript::IVirtualMachine* a_vm);
}