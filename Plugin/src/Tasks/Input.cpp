#include "PCH.h"
#include "Tasks/Input.h"
#include "Commands/SAFCommand.h"
#include "Animation/GraphManager.h"
#include <Windows.h>
#include <chrono>

namespace Tasks
{
	Input* Input::GetSingleton()
	{
		static Input singleton;
		return &singleton;
	}

	void Input::RegisterForKey(BS_BUTTON_CODE a_key, InputCallback* a_callback)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_regKeys[a_key].push_back(a_callback);
	}

	void Input::RegisterForKey(BS_BUTTON_CODE a_key, ButtonCallback a_callback)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		callbacks[static_cast<uint32_t>(a_key)] = a_callback;
	}

	using InputProcessFunc = void (*)(const RE::PlayerCamera*, const RE::InputEvent*);

	static InputProcessFunc g_originalInputProcessing = nullptr;
	static bool g_inputHookInstalled = false;

	static void WriteAbsoluteJmp(uintptr_t from, uintptr_t to)
	{
		uint8_t jmp[14]{};
		jmp[0] = 0xFF;
		jmp[1] = 0x25;
		*reinterpret_cast<uint32_t*>(jmp + 2) = 0;
		*reinterpret_cast<uint64_t*>(jmp + 6) = to;

		DWORD oldProtect;
		VirtualProtect(reinterpret_cast<void*>(from), 14, PAGE_EXECUTE_READWRITE, &oldProtect);
		memcpy(reinterpret_cast<void*>(from), jmp, 14);
		VirtualProtect(reinterpret_cast<void*>(from), 14, oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(from), 14);
	}

	void Input::SimulateOpenConsoleKey()
	{
		INPUT inputs[2] = {};
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = static_cast<WORD>(VK_OEM_3);
		inputs[0].ki.dwFlags = 0;
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = static_cast<WORD>(VK_OEM_3);
		inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(2, inputs, sizeof(INPUT));
	}

	static void PerformInputProcessingHookImpl(const RE::PlayerCamera* a_camera, const RE::InputEvent* a_queueHead)
	{
		// Run game input processing FIRST so activation (E), docking, menus etc. respond immediately.
		// Previously we ran ProcessPendingCommands + UpdateGraphs before the game, causing noticeable
		// input lag and broken docking when UpdateGraphs was slow.
		if (g_originalInputProcessing) {
			g_originalInputProcessing(a_camera, a_queueHead);
		}

		static std::atomic<uint32_t> callCount{ 0 };
		static auto lastTick = std::chrono::steady_clock::now();
		uint32_t count = ++callCount;
		if (count % 120 == 0) {
			SAF_LOG_INFO("[INPUT] PerformInputProcessingHook: called (count={})", count);
		}

		const bool hasPending = Commands::SAFCommand::HasPendingCommands();
		const bool requested = Commands::SAFCommand::HasProcessRequest();
		if (hasPending || requested) {
			if (requested) {
				Commands::SAFCommand::ConsumeProcessRequest();
			}
			Commands::SAFCommand::ProcessPendingCommands();
			if (Commands::SAFCommand::ConsumeCloseConsole()) {
				Commands::SAFCommand::CloseConsoleMainThread();
			}
		}

		if (auto* mgr = Animation::GraphManager::GetSingleton()) {
			if (mgr->GetMainThreadId() == 0) {
				mgr->SetMainThreadId(GetCurrentThreadId());
			}
			const auto now = std::chrono::steady_clock::now();
			const std::chrono::duration<float> dt = now - lastTick;
			lastTick = now;
			mgr->UpdateGraphs(dt.count());
		}

		auto* m = Input::GetSingleton();
		for (auto curEvent = a_queueHead; curEvent != nullptr; curEvent = curEvent->next) {
			if (curEvent->eventType != RE::InputEvent::EventType::kButton) {
				continue;
			}
			auto btnEvent = static_cast<const RE::ButtonEvent*>(curEvent);
			if (!btnEvent) {
				continue;
			}
			auto code = static_cast<Input::BS_BUTTON_CODE>(btnEvent->idCode);
			bool isDown = btnEvent->value > 0.0f;
			if (auto iter = m->callbacks.find(btnEvent->idCode); iter != m->callbacks.end()) {
				iter->second(code, isDown);
			}
		}
	}

	bool Input::InstallHook()
	{
		if (g_inputHookInstalled) {
			SAF_LOG_INFO("[INPUT] InstallHook: already installed");
			return true;
		}

		try {
			REL::Relocation<uintptr_t> target{ REL::ID(459729) };
			const uintptr_t targetAddr = target.address();
			SAF_LOG_INFO("[INPUT] InstallHook: target address = {:X}", targetAddr);

			constexpr size_t bytesToCopy = 14;
			uintptr_t trampolineAddr = 0;
			for (int delta = -2000; delta <= 2000; delta += 64) {
				uintptr_t candidate = targetAddr + (delta * 1024 * 1024);
				candidate &= ~0xFFFF;
				void* result = VirtualAlloc(reinterpret_cast<void*>(candidate), 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
				if (result) {
					trampolineAddr = reinterpret_cast<uintptr_t>(result);
					break;
				}
			}

			if (!trampolineAddr) {
				SAF_LOG_ERROR("[INPUT] InstallHook: failed to allocate trampoline");
				return false;
			}

			memcpy(reinterpret_cast<void*>(trampolineAddr), reinterpret_cast<void*>(targetAddr), bytesToCopy);
			WriteAbsoluteJmp(trampolineAddr + bytesToCopy, targetAddr + bytesToCopy);

			g_originalInputProcessing = reinterpret_cast<InputProcessFunc>(trampolineAddr);
			WriteAbsoluteJmp(targetAddr, reinterpret_cast<uintptr_t>(PerformInputProcessingHookImpl));
			g_inputHookInstalled = true;
			SAF_LOG_INFO("[INPUT] InstallHook: OK (trampoline {:X})", trampolineAddr);
			return true;
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("[INPUT] InstallHook: exception {}", e.what());
			return false;
		} catch (...) {
			SAF_LOG_ERROR("[INPUT] InstallHook: unknown exception");
			return false;
		}
	}
}