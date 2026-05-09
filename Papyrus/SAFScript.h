#pragma once
#include "PCH.h"
#include <atomic>

namespace Papyrus::SAFScript
{
	constexpr const char* SCRIPT_NAME{ "SAFScript" };
	
	// Globalny wskaźnik ostatnio zboundowanej VM - deklaracja zewnętrzna
	extern std::atomic<RE::BSScript::IVirtualMachine*> g_lastBoundVM;
	
	bool Bind(RE::BSScript::IVirtualMachine* a_vm);

	// Wywołaj po wczytaniu zapisu (LoadGame) żeby przywrócić bindingi
	// native functions w nowej instancji VM.
	void RebindAfterLoad();
	
	// Sprawdza czy trzeba rebindować (VM się zmieniła)
	bool NeedsRebind();
}