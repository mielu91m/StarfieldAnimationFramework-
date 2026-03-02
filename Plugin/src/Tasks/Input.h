#pragma once
#include "Util/General.h"
#include <functional>
#include <map>
#include <vector>
#include <mutex>

namespace Tasks
{
	class Input
	{
	public:
		// Kody przycisków Starfielda
		enum BS_BUTTON_CODE : uint32_t
		{
			kBackspace = 0x08, kTab = 0x09, kEnter = 0x0D, kEscape = 0x1B, kSpace = 0x20,
			kLeft = 0x25, kUp = 0x26, kRight = 0x27, kDown = 0x28,
			kA = 0x41, kD = 0x44, kS = 0x53, kW = 0x57,
			// Gamepad
			kDPAD_Up = 0x10001, kDPAD_Down = 0x10002, kDPAD_Left = 0x10004, kDPAD_Right = 0x10008
		};

		using InputCallback = void(BS_BUTTON_CODE a_key, bool a_down);
		using ButtonCallback = std::function<void(BS_BUTTON_CODE a_key, bool a_down)>;

		static Input* GetSingleton();
		static bool InstallHook();
		
		void RegisterForKey(BS_BUTTON_CODE a_key, InputCallback* a_callback);
		void RegisterForKey(BS_BUTTON_CODE a_key, ButtonCallback a_callback);

		std::map<uint32_t, ButtonCallback> callbacks;

	private:
		Input() = default;
		std::mutex _mutex;
		std::map<BS_BUTTON_CODE, std::vector<InputCallback*>> _regKeys;
	};
}