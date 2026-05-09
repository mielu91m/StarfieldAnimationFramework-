#pragma once

namespace Tasks::SaveLoadListener
{
	// Hook VirtualMachine::DropAllRunningData (VFT) — przed kolejnym wczytaniem/revertem.
	// Wywołaj raz w PostDataLoad.
	void Install();

	// Zwraca true gdy jest zaplanowany rebind Papyrusa (sprawdzane co klatkę).
	// Używane przez HasPendingCommands() w SAFCommand.cpp.
	bool HasPendingRebind();

	// Konsumuje flagę rebindu (zwraca true i kasuje flagę atomowo).
	// Wywołaj z ProcessPendingCommands() na main thread.
	bool ConsumePendingRebind();
}