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
			EventType type;
			VMHandle handle;
			std::string typeName;
			std::string funcName;
		};

		static EventManager* GetSingleton()
		{
			static EventManager singleton;
			return &singleton;
		}

		void RegisterScript(EventType a_type, VMHandle a_handle, std::string a_typeName, std::string a_funcName);
		void UnregisterScript(EventType a_type, VMHandle a_handle);

		/// PhaseBegin: (ObjectReference akTarget, Int iPhase, String sName)
		void DispatchPhaseBegin(RE::Actor* a_actor, int a_phaseIndex, const std::string& a_phaseName);
		/// SequenceEnd: (ObjectReference akTarget, String sName)
		void DispatchSequenceEnd(RE::Actor* a_actor, const std::string& a_sequenceName);

		void Reset();

	private:
		EventManager() = default;
		std::vector<RegisteredScript> _registrations;
		std::mutex _mutex;
	};
}