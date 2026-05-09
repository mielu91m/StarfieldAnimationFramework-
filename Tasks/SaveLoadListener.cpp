#include "PCH.h"
#include "SaveLoadListener.h"
#include "Animation/GraphManager.h"
#include "Animation/Face/Manager.h"
#include "Papyrus/EventManager.h"
#include <memory>
#include <vector>

namespace Tasks::SaveLoadListener
{
	namespace
	{
		// VirtualMachine: destructor @0, then overrides in declaration order through DropAllRunningData
		// (see RE::BSScript::Internal::VirtualMachine — IVMSaveLoadInterface section).
		constexpr std::size_t kDropAllRunningDataVtableIndex = 83;

		using DropHook = REL::THookVFT<void*(RE::BSScript::Internal::VirtualMachine*)>;

		static std::vector<std::unique_ptr<DropHook>> g_dropHooks;

		static void* DropAllRunningDataHook(RE::BSScript::Internal::VirtualMachine* a_this)
		{
			if (g_dropHooks.empty()) {
				SAF_LOG_ERROR("SaveLoadListener: DropAllRunningData — brak aktywnych hooków");
				return nullptr;
			}

			// Gra może wywołać tę funkcję przez wskaźnik z przestawieniem (MI) — wtedy vptr
			// w *a_this wskazuje na WTÓRNĄ tablicę metod, nie na tę z BSScript__Internal__VirtualMachine.
			// Wcześniejsze dopasowanie wyłącznie po vtable kończyło się return nullptr bez oryginału → CTD „bez crashloga”.
			DropHook* const hook = g_dropHooks.front().get();
			const auto vtbl = a_this ? *reinterpret_cast<const std::uintptr_t*>(a_this) : 0;

			SAF_LOG_INFO(
				"SaveLoadListener: DropAllRunningData (caller vtbl={:X}, this={:p}) — Reset(false)",
				vtbl,
				static_cast<void*>(a_this));

			Animation::GraphManager::GetSingleton()->ApplyRevertLoadSkipGuard();
			Animation::GraphManager::GetSingleton()->SetSaveLoadInProgress(true);

			try {
				Animation::GraphManager::GetSingleton()->Reset(false);
				Animation::Face::Manager::GetSingleton()->Reset();
				Papyrus::EventManager::GetSingleton()->Reset();
			} catch (...) {
				SAF_LOG_ERROR("SaveLoadListener: wyjątek podczas resetu");
			}

			// Oryginał ma this = pełny obiekt VirtualMachine (canonical), nie przyrostek interfejsu.
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (vm) {
				return (*hook)(vm);
			}
			SAF_LOG_WARN("SaveLoadListener: VirtualMachine::GetSingleton()==null — wołanie oryginału z przekazanym this");
			return (*hook)(a_this);
		}
	}

	static std::atomic<bool> g_hookInstalled{ false };

	bool HasPendingRebind()
	{
		return false;
	}

	bool ConsumePendingRebind()
	{
		return false;
	}

	void Install()
	{
		if (g_hookInstalled.load()) {
			return;
		}

		try {
			g_dropHooks.clear();

			for (const auto id : RE::VTABLE::BSScript__Internal__VirtualMachine) {
				auto hook = std::make_unique<DropHook>(id, kDropAllRunningDataVtableIndex, &DropAllRunningDataHook);
				if (hook->Enable()) {
					g_dropHooks.push_back(std::move(hook));
					SAF_LOG_INFO(
						"SaveLoadListener: DropAllRunningData VFT OK (vtable ID {}, idx {})",
						id.id(),
						kDropAllRunningDataVtableIndex);
				} else {
					SAF_LOG_WARN("SaveLoadListener: DropAllRunningData VFT FAILED (vtable ID {})", id.id());
				}
			}

			if (!g_dropHooks.empty()) {
				g_hookInstalled.store(true);
			} else {
				SAF_LOG_ERROR("SaveLoadListener: brak hooków DropAllRunningData");
			}
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("SaveLoadListener: instalacja hooka — {}", e.what());
		} catch (...) {
			SAF_LOG_ERROR("SaveLoadListener: instalacja hooka — nieznany wyjątek");
		}
	}
}
