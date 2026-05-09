#include "PCH.h"
#include "Papyrus/EventManager.h"
#include "RE/G/GameVM.h"
#include "RE/B/BSScriptUtil.h"
#include "RE/B/BSTArray.h"
#include "RE/I/IVirtualMachine.h"

namespace Papyrus
{
	void EventManager::RegisterScript(EventType a_type, VMHandle a_handle, std::string a_typeName, std::string a_funcName)
	{
		std::lock_guard lock(_mutex);
		_registrations[{ a_type, a_handle }] = RegisteredScript{ std::move(a_typeName), std::move(a_funcName) };
		SAF_LOG_INFO("Registered script for event type {} (handle={})", static_cast<int>(a_type), a_handle);
	}

	void EventManager::UnregisterScript(EventType a_type, VMHandle a_handle)
	{
		std::lock_guard lock(_mutex);
		_registrations.erase({ a_type, a_handle });
		SAF_LOG_INFO("Unregistered script for event type {}", static_cast<int>(a_type));
	}

	void EventManager::DispatchPhaseBegin(RE::Actor* a_actor, int a_phaseIndex, const std::string& a_phaseName)
	{
		std::vector<std::pair<VMHandle, RegisteredScript>> copy;
		{
			std::lock_guard lock(_mutex);
			for (const auto& [key, r] : _registrations)
				if (key.first == EventType::kPhaseBegin)
					copy.emplace_back(key.second, r);
		}
		if (copy.empty()) return;

		auto* gameVM = RE::GameVM::GetSingleton();
		if (!gameVM) return;
		RE::BSScript::IVirtualMachine* vm = gameVM->GetVM();
		if (!vm) return;

		RE::BSFixedString objName;
		RE::BSFixedString funcName;
		for (const auto& [handle, r] : copy) {
			objName = r.typeName.c_str();
			funcName = r.funcName.c_str();
			RE::Actor* actor = a_actor;
			int phaseIndex = a_phaseIndex;
			std::string phaseName = a_phaseName;
			RE::BSFixedString phaseNameStr(phaseName.c_str());
			vm->DispatchMethodCall(
				handle,
				objName,
				funcName,
				[actor, phaseIndex, phaseNameStr](RE::BSScrapArray<RE::BSScript::Variable>& a_args) -> bool {
					a_args.resize(3);
					RE::BSScript::PackVariable(a_args[0], actor);
					RE::BSScript::PackVariable(a_args[1], phaseIndex);
					RE::BSScript::PackVariable(a_args[2], phaseNameStr);
					return true;
				},
				nullptr,
				0);
		}
	}

	void EventManager::DispatchSequenceEnd(RE::Actor* a_actor, const std::string& a_sequenceName)
	{
		std::vector<std::pair<VMHandle, RegisteredScript>> copy;
		{
			std::lock_guard lock(_mutex);
			for (const auto& [key, r] : _registrations)
				if (key.first == EventType::kSequenceEnd)
					copy.emplace_back(key.second, r);
		}
		if (copy.empty()) return;

		auto* gameVM = RE::GameVM::GetSingleton();
		if (!gameVM) return;
		RE::BSScript::IVirtualMachine* vm = gameVM->GetVM();
		if (!vm) return;

		RE::BSFixedString objName;
		RE::BSFixedString funcName;
		for (const auto& [handle, r] : copy) {
			objName = r.typeName.c_str();
			funcName = r.funcName.c_str();
			RE::Actor* actor = a_actor;
			RE::BSFixedString sequenceNameStr(a_sequenceName.c_str());
			vm->DispatchMethodCall(
				handle,
				objName,
				funcName,
				[actor, sequenceNameStr](RE::BSScrapArray<RE::BSScript::Variable>& a_args) -> bool {
					a_args.resize(2);
					RE::BSScript::PackVariable(a_args[0], actor);
					RE::BSScript::PackVariable(a_args[1], sequenceNameStr);
					return true;
				},
				nullptr,
				0);
		}
	}

	void EventManager::Reset()
	{
		std::lock_guard lock(_mutex);
		_registrations.clear();
		SAF_LOG_INFO("EventManager::Reset - cleared registrations");
	}
}

