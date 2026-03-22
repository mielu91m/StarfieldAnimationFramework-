#pragma once
#include "PCH.h"
#include <map>
#include <utility>

namespace Papyrus
{
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
			std::string typeName;
			std::string funcName;
		};

		static EventManager* GetSingleton()
		{
			static EventManager singleton;
			return &singleton;
		}

		/// Jedna rejestracja na (typ, handle) – jak w NAF (nadpisuje przy ponownym Register).
		void RegisterScript(EventType a_type, VMHandle a_handle, std::string a_typeName, std::string a_funcName);
		void UnregisterScript(EventType a_type, VMHandle a_handle);

		void DispatchPhaseBegin(RE::Actor* a_actor, int a_phaseIndex, const std::string& a_phaseName);
		void DispatchSequenceEnd(RE::Actor* a_actor, const std::string& a_sequenceName);

		void Reset();

	private:
		EventManager() = default;
		using Key = std::pair<EventType, VMHandle>;
		std::map<Key, RegisteredScript> _registrations;
		std::mutex _mutex;
	};
}