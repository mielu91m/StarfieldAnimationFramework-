#pragma once
#include "PCH.h"

namespace Papyrus
{
	// W Starfield VMHandle to prawdopodobnie uint64_t
	// (nie ma osobnego typu RE::VMHandle)
	using VMHandle = std::uint64_t;
	
	enum class EventType
	{
		kPhaseBegin,
		kSequenceEnd
	};

	class EventManager
	{
	public:
		struct RegisteredScript
		{
			VMHandle handle;  // Używamy naszego aliasu
			std::string scriptName;
			std::string funcName;
		};

		static EventManager* GetSingleton()
		{
			static EventManager singleton;
			return &singleton;
		}

		// Rejestracja skryptu dla eventu
		void RegisterScript(EventType a_type, VMHandle a_handle, std::string a_funcName)
		{
			std::lock_guard lock(_mutex);
			_registrations.push_back({a_handle, "", a_funcName});
			SAF_LOG_INFO("Registered script for event type {}", static_cast<int>(a_type));
		}
		
		// Wyrejestrowanie skryptu
		void UnregisterScript(EventType a_type, VMHandle a_handle)
		{
			std::lock_guard lock(_mutex);
			_registrations.erase(
				std::remove_if(_registrations.begin(), _registrations.end(),
					[a_handle](const RegisteredScript& r) { return r.handle == a_handle; }),
				_registrations.end()
			);
			SAF_LOG_INFO("Unregistered script for event type {}", static_cast<int>(a_type));
		}
		
		// Uproszczona wersja bez IFunctionArguments (który nie istnieje w Starfield)
		void DispatchEvent(std::string_view a_eventName)
		{
			std::lock_guard lock(_mutex);
			SAF_LOG_DEBUG("Dispatching event: {}", a_eventName);
			
			// TODO: Po otrzymaniu IVirtualMachine.h z Starfield:
			// - Pobrać VM
			// - Wywołać funkcję na zarejestrowanych skryptach
			// - Przekazać argumenty (sprawdzić jak w Starfield API)
		}
		
		void Reset()
		{
			std::lock_guard lock(_mutex);
			_registrations.clear();
			SAF_LOG_INFO("EventManager::Reset - cleared {} registrations", _registrations.size());
		}

	private:
		EventManager() = default;
		std::vector<RegisteredScript> _registrations;
		std::mutex _mutex;
	};
}