#include "PCH.h"
#include "Papyrus/SAFScript.h"
#include "Papyrus/EventManager.h"
#include "Animation/GraphManager.h"
#include "Animation/Sequencer.h"
#include "RE/P/PlayerCharacter.h"
#include "Util/String.h"
#include <filesystem>
#include <vector>

namespace Papyrus::SAFScript
{
	namespace
	{
		// Lokalny resolver ścieżek animacji – duplikat logiki z Commands::SAFCommand,
		// żeby Papyrus nie musiał przechodzić przez konsolowy parser.
		std::filesystem::path ResolveAnimationPathWithFallback(std::string_view a_path)
		{
			auto resolved = Util::String::ResolveAnimationPath(a_path);
			if (resolved.has_extension()) {
				return resolved;
			}

			auto glb = resolved;
			glb += ".glb";
			if (std::filesystem::exists(glb)) {
				return glb;
			}

			auto saf = resolved;
			saf += ".saf";
			if (std::filesystem::exists(saf)) {
				return saf;
			}

			auto gltf = resolved;
			gltf += ".gltf";
			if (std::filesystem::exists(gltf)) {
				return gltf;
			}

			return resolved;
		}

		bool PlayOnActorImpl(RE::Actor* a_actor, std::string_view a_animId, int a_animIndex = 0)
		{
			if (!a_actor) {
				SAF_LOG_WARN("[Papyrus] PlayOnActorImpl: actor is null");
				return false;
			}

			auto* mgr = Animation::GraphManager::GetSingleton();
			if (!mgr) {
				SAF_LOG_ERROR("[Papyrus] PlayOnActorImpl: GraphManager singleton is null");
				return false;
			}

			auto resolvedKey = Util::String::ToLower(a_animId);
			auto resolvedPath = ResolveAnimationPathWithFallback(resolvedKey);
			std::string pathStr = resolvedPath.string();

			try {
				SAF_LOG_INFO("[Papyrus] PlayOnActorImpl: playing '{}' index {} on actor {:08X}", pathStr, a_animIndex, a_actor->GetFormID());
				bool ok = mgr->LoadAndStartAnimation(a_actor, pathStr, true, a_animIndex);
				if (ok) mgr->RequestGraphUpdate();
				return ok;
			} catch (const std::exception& e) {
				SAF_LOG_ERROR("[Papyrus] PlayOnActorImpl: exception '{}'", e.what());
				return false;
			} catch (...) {
				SAF_LOG_ERROR("[Papyrus] PlayOnActorImpl: unknown exception");
				return false;
			}
		}
	}

	void RegisterForSAFEvent(
		RE::BSScript::IVirtualMachine& /* a_vm */,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script,
		std::uint32_t a_eventType,
		RE::BSFixedString a_funcName)
	{
		auto type = static_cast<Papyrus::EventType>(a_eventType);
		Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script.GetHandle());
		std::string typeName = a_script.type ? a_script.type->name.c_str() : "";

		Papyrus::EventManager::GetSingleton()->RegisterScript(type, handle, typeName, a_funcName.c_str());
		SAF_LOG_INFO("Registered for SAF event type {} (handle={}, type={})", a_eventType, handle, typeName);
	}

	void UnregisterFromSAFEvent(
		RE::BSScript::IVirtualMachine& /* a_vm */,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script,
		std::uint32_t a_eventType)
	{
		auto type = static_cast<Papyrus::EventType>(a_eventType);
		Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script.GetHandle());

		Papyrus::EventManager::GetSingleton()->UnregisterScript(type, handle);
		SAF_LOG_INFO("Unregistered from SAF event type {}", a_eventType);
	}

	// NAF-style event registration (ScriptObject = script that will receive the event)
	void RegisterForPhaseBegin(
		RE::BSScript::IVirtualMachine& /* a_vm */,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script,
		RE::BSFixedString a_funcName)
	{
		Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script.GetHandle());
		std::string typeName = a_script.type ? a_script.type->name.c_str() : "";
		Papyrus::EventManager::GetSingleton()->RegisterScript(Papyrus::EventType::kPhaseBegin, handle, typeName, a_funcName.c_str());
	}

	void RegisterForSequenceEnd(
		RE::BSScript::IVirtualMachine& /* a_vm */,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script,
		RE::BSFixedString a_funcName)
	{
		Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script.GetHandle());
		std::string typeName = a_script.type ? a_script.type->name.c_str() : "";
		Papyrus::EventManager::GetSingleton()->RegisterScript(Papyrus::EventType::kSequenceEnd, handle, typeName, a_funcName.c_str());
	}

	void UnregisterForPhaseBegin(
		RE::BSScript::IVirtualMachine& /* a_vm */,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script)
	{
		Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script.GetHandle());
		Papyrus::EventManager::GetSingleton()->UnregisterScript(Papyrus::EventType::kPhaseBegin, handle);
	}

	void UnregisterForSequenceEnd(
		RE::BSScript::IVirtualMachine& /* a_vm */,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script)
	{
		Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script.GetHandle());
		Papyrus::EventManager::GetSingleton()->UnregisterScript(Papyrus::EventType::kSequenceEnd, handle);
	}

	// Papyrus: SAFScript.PlayOnActor(Actor akActor, string animId, int animIndex=0) -> bool
	bool PlayOnActor(
		RE::BSScript::IVirtualMachine& /*a_vm*/,
		std::uint32_t /*a_stackID*/,
		RE::BSScript::Object& /*a_script*/,
		RE::Actor* a_actor,
		RE::BSFixedString a_animId,
		int a_animIndex)
	{
		if (!a_actor) {
			SAF_LOG_WARN("[Papyrus] PlayOnActor: actor is none");
			return false;
		}
		if (!a_animId.data()) {
			SAF_LOG_WARN("[Papyrus] PlayOnActor: animId is empty");
			return false;
		}
		return PlayOnActorImpl(a_actor, a_animId.c_str(), a_animIndex >= 0 ? a_animIndex : 0);
	}

	// Papyrus: SAFScript.PlayOnPlayer(string animId, int animIndex=0) -> bool
	bool PlayOnPlayer(
		RE::BSScript::IVirtualMachine& /*a_vm*/,
		std::uint32_t /*a_stackID*/,
		RE::BSScript::Object& /*a_script*/,
		RE::BSFixedString a_animId,
		int a_animIndex)
	{
		if (!a_animId.data()) {
			SAF_LOG_WARN("[Papyrus] PlayOnPlayer: animId is empty");
			return false;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			SAF_LOG_ERROR("[Papyrus] PlayOnPlayer: PlayerCharacter singleton is null");
			return false;
		}

		return PlayOnActorImpl(player, a_animId.c_str(), a_animIndex >= 0 ? a_animIndex : 0);
	}

	// Papyrus: SAFScript.PlayOnActors(Actor[] akActors, string animId, int animIndex=0) -> bool
	bool PlayOnActors(
		RE::BSScript::IVirtualMachine& /*a_vm*/,
		std::uint32_t /*a_stackID*/,
		RE::BSScript::Object& /*a_script*/,
		std::vector<RE::Actor*> a_actors,
		RE::BSFixedString a_animId,
		int a_animIndex)
	{
		if (!a_animId.data()) {
			SAF_LOG_WARN("[Papyrus] PlayOnActors: animId is empty");
			return false;
		}
		if (a_actors.empty()) {
			SAF_LOG_WARN("[Papyrus] PlayOnActors: actor array is empty");
			return false;
		}
		int idx = a_animIndex >= 0 ? a_animIndex : 0;
		bool anyOk = false;
		for (RE::Actor* a : a_actors) {
			if (a && PlayOnActorImpl(a, a_animId.c_str(), idx))
				anyOk = true;
		}
		if (anyOk && Animation::GraphManager::GetSingleton())
			Animation::GraphManager::GetSingleton()->RequestGraphUpdate();
		return anyOk;
	}

	// Papyrus: SAFScript.StopAnimation(Actor akActor) -> bool
	bool StopAnimation(
		RE::BSScript::IVirtualMachine& /*a_vm*/,
		std::uint32_t /*a_stackID*/,
		RE::BSScript::Object& /*a_script*/,
		RE::Actor* a_actor)
	{
		if (!a_actor) {
			SAF_LOG_WARN("[Papyrus] StopAnimation: actor is none");
			return false;
		}
		auto* mgr = Animation::GraphManager::GetSingleton();
		if (!mgr) {
			SAF_LOG_ERROR("[Papyrus] StopAnimation: GraphManager is null");
			return false;
		}
		try {
			mgr->StopAnimation(a_actor);
			return true;
		} catch (...) {
			return false;
		}
	}

	RE::BSFixedString GetCurrentAnimation(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor)
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		if (!mgr || !a_actor) return RE::BSFixedString("");
		return RE::BSFixedString(mgr->GetCurrentAnimation(a_actor).c_str());
	}
	void SetAnimationSpeed(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, float a_speed)
	{
		if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor) mgr->SetAnimationSpeed(a_actor, a_speed);
	}
	float GetAnimationSpeed(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor)
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		return mgr && a_actor ? mgr->GetAnimationSpeed(a_actor) : 0.0f;
	}
	void SetGraphControlsPosition(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, bool a_lock)
	{
		if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor) mgr->SetGraphControlsPosition(a_actor, a_lock);
	}
	void SetActorPosition(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, float a_x, float a_y, float a_z)
	{
		if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor) mgr->SetActorPosition(a_actor, a_x, a_y, a_z);
	}
	int GetSequencePhase(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor)
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		return mgr && a_actor ? mgr->GetSequencePhase(a_actor) : -1;
	}
	bool SetSequencePhase(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, int a_phase)
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		return mgr && a_actor && mgr->SetSequencePhase(a_actor, a_phase);
	}
	bool AdvanceSequence(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, bool a_smooth)
	{
		if (!a_actor) return false;
		auto* mgr = Animation::GraphManager::GetSingleton();
		if (!mgr) return false;
		mgr->AdvanceSequence(a_actor, a_smooth);
		return mgr->GetSequencePhase(a_actor) >= 0;
	}
	void SyncGraphs(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, std::vector<RE::Actor*> a_actors)
	{
		if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && !a_actors.empty())
			mgr->SyncGraphs(a_actors);
	}
	void StopSyncing(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor)
	{
		if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor) mgr->StopSyncing(a_actor);
	}
	bool SetBlendGraphVariable(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, RE::BSFixedString a_name, float a_value)
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		return mgr && a_actor && mgr->SetBlendGraphVariable(a_actor, a_name.c_str(), a_value);
	}
	float GetBlendGraphVariable(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, RE::BSFixedString a_name)
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		return mgr && a_actor ? mgr->GetBlendGraphVariable(a_actor, a_name.c_str()) : 0.0f;
	}

	void StartSequence(RE::BSScript::IVirtualMachine&, std::uint32_t, RE::BSScript::Object&, RE::Actor* a_actor, std::vector<RE::BSFixedString> a_paths, bool a_loop)
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		if (!mgr || !a_actor || a_paths.empty()) return;
		std::vector<Animation::Sequencer::PhaseData> phases;
		phases.reserve(a_paths.size());
		for (const auto& p : a_paths) {
			Animation::Sequencer::PhaseData pd;
			pd.file = p.c_str();
			pd.loopCount = 0;
			pd.transitionTime = 1.0f;
			phases.push_back(std::move(pd));
		}
		mgr->StartSequence(a_actor, std::move(phases), a_loop);
	}

	bool Bind(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			SAF_LOG_ERROR("Cannot bind SAFScript: VM is null");
			return false;
		}

		const char* className = "SAFScript";

		try {
			// STARFIELD API: BindNativeMethod wymaga 5 argumentów
			// Sygnatura: BindNativeMethod(className, methodName, function, stateful, latent)
			
			a_vm->BindNativeMethod(
				className,                    // std::string_view - nazwa klasy
				"RegisterForSAFEvent",        // std::string_view - nazwa metody
				RegisterForSAFEvent,          // F - wskaźnik do funkcji
				std::nullopt,                 // std::optional<bool> - czy stateful?
				false                         // bool - czy latent (asynchroniczna)?
			);
			
			a_vm->BindNativeMethod(
				className,
				"UnregisterFromSAFEvent",
				UnregisterFromSAFEvent,
				std::nullopt,
				false
			);

			a_vm->BindNativeMethod(className, "RegisterForPhaseBegin", RegisterForPhaseBegin, std::nullopt, false);
			a_vm->BindNativeMethod(className, "RegisterForSequenceEnd", RegisterForSequenceEnd, std::nullopt, false);
			a_vm->BindNativeMethod(className, "UnregisterForPhaseBegin", UnregisterForPhaseBegin, std::nullopt, false);
			a_vm->BindNativeMethod(className, "UnregisterForSequenceEnd", UnregisterForSequenceEnd, std::nullopt, false);

			// Funkcje odtwarzania animacji
			a_vm->BindNativeMethod(
				className,
				"PlayOnActor",
				PlayOnActor,
				std::nullopt,
				false
			);

			a_vm->BindNativeMethod(
				className,
				"PlayOnPlayer",
				PlayOnPlayer,
				std::nullopt,
				false
			);

			a_vm->BindNativeMethod(
				className,
				"PlayOnActors",
				PlayOnActors,
				std::nullopt,
				false
			);

			a_vm->BindNativeMethod(
				className,
				"StopAnimation",
				StopAnimation,
				std::nullopt,
				false
			);
			a_vm->BindNativeMethod(className, "GetCurrentAnimation", GetCurrentAnimation, std::nullopt, false);
			a_vm->BindNativeMethod(className, "SetAnimationSpeed", SetAnimationSpeed, std::nullopt, false);
			a_vm->BindNativeMethod(className, "GetAnimationSpeed", GetAnimationSpeed, std::nullopt, false);
			a_vm->BindNativeMethod(className, "SetGraphControlsPosition", SetGraphControlsPosition, std::nullopt, false);
			a_vm->BindNativeMethod(className, "SetActorPosition", SetActorPosition, std::nullopt, false);
			a_vm->BindNativeMethod(className, "GetSequencePhase", GetSequencePhase, std::nullopt, false);
			a_vm->BindNativeMethod(className, "SetSequencePhase", SetSequencePhase, std::nullopt, false);
			a_vm->BindNativeMethod(className, "AdvanceSequence", AdvanceSequence, std::nullopt, false);
			a_vm->BindNativeMethod(className, "SyncGraphs", SyncGraphs, std::nullopt, false);
			a_vm->BindNativeMethod(className, "StopSyncing", StopSyncing, std::nullopt, false);
			a_vm->BindNativeMethod(className, "SetBlendGraphVariable", SetBlendGraphVariable, std::nullopt, false);
			a_vm->BindNativeMethod(className, "GetBlendGraphVariable", GetBlendGraphVariable, std::nullopt, false);
			a_vm->BindNativeMethod(className, "StartSequence", StartSequence, std::nullopt, false);

			SAF_LOG_INFO("Successfully bound SAFScript Papyrus functions");
			return true;
			
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("Failed to bind SAFScript: {}", e.what());
			return false;
		}
	}
}