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
        std::filesystem::path ResolveAnimationPathWithFallback(std::string_view a_path)
        {
            auto resolved = Util::String::ResolveAnimationPath(a_path);
            if (resolved.has_extension()) return resolved;

            auto glb = resolved; glb += ".glb";
            if (std::filesystem::exists(glb)) return glb;
            auto saf = resolved; saf += ".saf";
            if (std::filesystem::exists(saf)) return saf;
            auto gltf = resolved; gltf += ".gltf";
            if (std::filesystem::exists(gltf)) return gltf;

            const bool simpleName = (a_path.find('/') == std::string_view::npos &&
                                     a_path.find('\\') == std::string_view::npos);
            if (simpleName && !a_path.empty()) {
                auto found = Util::String::FindAnimationByStem(a_path);
                if (found && std::filesystem::exists(*found)) return *found;
            }
            return resolved;
        }

		bool PlayOnActorImpl(RE::Actor* a_actor, std::string_view a_animId,
                             float a_speed = 1.0f, int a_animIndex = 0)
        {
            if (!a_actor) { SAF_LOG_WARN("[Papyrus] PlayOnActorImpl: actor is null"); return false; }
            auto* mgr = Animation::GraphManager::GetSingleton();
            if (!mgr) { SAF_LOG_ERROR("[Papyrus] PlayOnActorImpl: GraphManager is null"); return false; }

            auto resolvedKey  = Util::String::ToLower(a_animId);
            auto resolvedPath = ResolveAnimationPathWithFallback(resolvedKey);
            std::string pathStr = resolvedPath.string();

            try {
                if (a_speed <= 0.0f) a_speed = 1.0f;
                SAF_LOG_INFO("[Papyrus] PlayOnActorImpl: playing '{}' index {} on actor {:08X} (speed={})",
                             pathStr, a_animIndex, a_actor->GetFormID(), a_speed);
                bool ok = mgr->LoadAndStartAnimation(a_actor, pathStr, true, a_animIndex);
                if (ok) { mgr->RequestGraphUpdate(); mgr->SetAnimationSpeed(a_actor, a_speed); }
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

	// Zwraca referencję pod celownikiem (player->crosshairRef), albo none, jeśli brak.
	RE::TESObjectREFR* GetCrosshairRef(
	    RE::BSScript::IVirtualMachine& /*a_vm*/,
	    std::uint32_t /*a_stackID*/,
	    std::monostate)
	{
	    auto* player = RE::PlayerCharacter::GetSingleton();
	    if (!player) {
	        return nullptr;
	    }
	    return player->crosshairRef;
	}

    // =========================================================================
    // REJESTRACJA EVENTÓW – Global z parametrem ScriptObject (jak w NAF)
    // Papyrus: RegisterForPhaseBegin(akScript, "NazwaFunkcji")
    // =========================================================================

    void RegisterForPhaseBegin(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::BSTSmartPointer<RE::BSScript::Object> a_script,
        RE::BSFixedString a_funcName)
    {
        if (!a_script) return;
        Papyrus::VMHandle handle   = static_cast<Papyrus::VMHandle>(a_script->GetHandle());
        std::string       typeName = a_script->type ? a_script->type->name.c_str() : "";
        Papyrus::EventManager::GetSingleton()->RegisterScript(
            Papyrus::EventType::kPhaseBegin, handle, typeName, a_funcName.c_str());
        SAF_LOG_INFO("RegisterForPhaseBegin: handle={} type={}", handle, typeName);
    }

    void RegisterForSequenceEnd(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::BSTSmartPointer<RE::BSScript::Object> a_script,
        RE::BSFixedString a_funcName)
    {
        if (!a_script) return;
        Papyrus::VMHandle handle   = static_cast<Papyrus::VMHandle>(a_script->GetHandle());
        std::string       typeName = a_script->type ? a_script->type->name.c_str() : "";
        Papyrus::EventManager::GetSingleton()->RegisterScript(
            Papyrus::EventType::kSequenceEnd, handle, typeName, a_funcName.c_str());
        SAF_LOG_INFO("RegisterForSequenceEnd: handle={} type={}", handle, typeName);
    }

    void UnregisterForPhaseBegin(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::BSTSmartPointer<RE::BSScript::Object> a_script)
    {
        if (!a_script) return;
        Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script->GetHandle());
        Papyrus::EventManager::GetSingleton()->UnregisterScript(
            Papyrus::EventType::kPhaseBegin, handle);
    }

    void UnregisterForSequenceEnd(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::BSTSmartPointer<RE::BSScript::Object> a_script)
    {
        if (!a_script) return;
        Papyrus::VMHandle handle = static_cast<Papyrus::VMHandle>(a_script->GetHandle());
        Papyrus::EventManager::GetSingleton()->UnregisterScript(
            Papyrus::EventType::kSequenceEnd, handle);
    }

    // =========================================================================
    // Ping – minimalny test bindowania (cgf "SAFScript.Ping")
    //
    // UWAGA: w Starfield 1.15.222 uproszczona sygnatura R(std::monostate)
    // potrafi crashować podczas marshallingu w CommonLibSF (Variable::operator=).
    // Używamy więc pełnej sygnatury jak w NAF:
    //   R(IVirtualMachine&, uint32_t, std::monostate)
    // =========================================================================
    bool Ping(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate)
    {
        return true;
    }

    // =========================================================================
    // FUNKCJE ANIMACJI – Native Global
    // =========================================================================

    bool PlayOnActor(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::Actor* a_actor,
        RE::BSFixedString a_animId,
        float a_speed,
        int a_animIndex)
    {
        if (!a_actor)         { SAF_LOG_WARN("[Papyrus] PlayOnActor: actor none");  return false; }
        if (!a_animId.data()) { SAF_LOG_WARN("[Papyrus] PlayOnActor: animId empty"); return false; }
        return PlayOnActorImpl(a_actor, a_animId.c_str(), a_speed, a_animIndex >= 0 ? a_animIndex : 0);
    }

    bool PlayOnPlayer(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::BSFixedString a_animId,
        float a_speed,
        int a_animIndex)
    {
        if (!a_animId.data()) { SAF_LOG_WARN("[Papyrus] PlayOnPlayer: animId empty"); return false; }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) { SAF_LOG_ERROR("[Papyrus] PlayOnPlayer: PlayerCharacter null"); return false; }
        return PlayOnActorImpl(player, a_animId.c_str(), a_speed, a_animIndex >= 0 ? a_animIndex : 0);
    }

    bool PlayOnActors(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        std::vector<RE::Actor*> a_actors,
        RE::BSFixedString a_animId,
        int a_animIndex)
    {
        if (!a_animId.data()) { SAF_LOG_WARN("[Papyrus] PlayOnActors: animId empty"); return false; }
        if (a_actors.empty()) { SAF_LOG_WARN("[Papyrus] PlayOnActors: array empty");  return false; }
        int  idx   = a_animIndex >= 0 ? a_animIndex : 0;
        bool anyOk = false;
        for (RE::Actor* a : a_actors)
            if (a && PlayOnActorImpl(a, a_animId.c_str(), 1.0f, idx)) anyOk = true;
        if (anyOk)
            if (auto* mgr = Animation::GraphManager::GetSingleton()) mgr->RequestGraphUpdate();
        return anyOk;
    }

    bool StopAnimation(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::Actor* a_actor)
    {
        if (!a_actor) { SAF_LOG_WARN("[Papyrus] StopAnimation: actor none"); return false; }
        auto* mgr = Animation::GraphManager::GetSingleton();
        if (!mgr) { SAF_LOG_ERROR("[Papyrus] StopAnimation: GraphManager null"); return false; }
        try { mgr->StopAnimation(a_actor); return true; } catch (...) { return false; }
    }

    RE::BSFixedString GetCurrentAnimation(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor)
    {
        auto* mgr = Animation::GraphManager::GetSingleton();
        return (mgr && a_actor) ? RE::BSFixedString(mgr->GetCurrentAnimation(a_actor).c_str())
                                : RE::BSFixedString("");
    }

    void SetAnimationSpeed(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor, float a_speed)
    {
        if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor)
            mgr->SetAnimationSpeed(a_actor, a_speed);
    }

    float GetAnimationSpeed(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor)
    {
        auto* mgr = Animation::GraphManager::GetSingleton();
        return (mgr && a_actor) ? mgr->GetAnimationSpeed(a_actor) : 0.0f;
    }

    void SetGraphControlsPosition(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor, bool a_lock)
    {
        if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor)
            mgr->SetGraphControlsPosition(a_actor, a_lock);
    }

    void SetActorPosition(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate,
        RE::Actor* a_actor, float a_x, float a_y, float a_z)
    {
        if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor)
            mgr->SetActorPosition(a_actor, a_x, a_y, a_z);
    }

    void LockActorForAnimation(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate,
        RE::Actor* a_actor, float a_x, float a_y, float a_z, bool a_isPlayer)
    {
        (void)a_isPlayer;
        if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor)
            mgr->LockActorForAnimation(a_actor, a_x, a_y, a_z);
    }

    void UnlockActorAfterAnimation(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate,
        RE::Actor* a_actor, bool a_isPlayer)
    {
        (void)a_isPlayer;
        if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor)
            mgr->UnlockActorAfterAnimation(a_actor);
    }

    int GetSequencePhase(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor)
    {
        auto* mgr = Animation::GraphManager::GetSingleton();
        return (mgr && a_actor) ? mgr->GetSequencePhase(a_actor) : -1;
    }

    bool SetSequencePhase(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor, int a_phase)
    {
        auto* mgr = Animation::GraphManager::GetSingleton();
        return mgr && a_actor && mgr->SetSequencePhase(a_actor, a_phase);
    }

    bool AdvanceSequence(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor, bool a_smooth)
    {
        if (!a_actor) return false;
        auto* mgr = Animation::GraphManager::GetSingleton();
        if (!mgr) return false;
        mgr->AdvanceSequence(a_actor, a_smooth);
        return mgr->GetSequencePhase(a_actor) >= 0;
    }

    void SyncGraphs(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, std::vector<RE::Actor*> a_actors)
    {
        if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && !a_actors.empty())
            mgr->SyncGraphs(a_actors);
    }

    void StopSyncing(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate, RE::Actor* a_actor)
    {
        if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_actor)
            mgr->StopSyncing(a_actor);
    }

    bool SetBlendGraphVariable(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate,
        RE::Actor* a_actor, RE::BSFixedString a_name, float a_value)
    {
        auto* mgr = Animation::GraphManager::GetSingleton();
        return mgr && a_actor && mgr->SetBlendGraphVariable(a_actor, a_name.c_str(), a_value);
    }

    float GetBlendGraphVariable(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate,
        RE::Actor* a_actor, RE::BSFixedString a_name)
    {
        auto* mgr = Animation::GraphManager::GetSingleton();
        return (mgr && a_actor) ? mgr->GetBlendGraphVariable(a_actor, a_name.c_str()) : 0.0f;
    }

    void StartSequence(
        RE::BSScript::IVirtualMachine&, std::uint32_t,
        std::monostate,
        RE::Actor* a_actor, std::vector<RE::BSFixedString> a_paths, bool a_loop)
    {
        auto* mgr = Animation::GraphManager::GetSingleton();
        if (!mgr || !a_actor || a_paths.empty()) return;
        std::vector<Animation::Sequencer::PhaseData> phases;
        phases.reserve(a_paths.size());
        for (const auto& p : a_paths) {
            Animation::Sequencer::PhaseData pd;
            pd.file           = p.c_str();
            pd.loopCount      = 0;
            pd.transitionTime = 1.0f;
            phases.push_back(std::move(pd));
        }
        mgr->StartSequence(a_actor, std::move(phases), a_loop);
    }

    // =========================================================================
		bool Bind(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm) { SAF_LOG_ERROR("Cannot bind SAFScript: VM null"); return false; }
        const char* N = "SAFScript";
        try {
            // Ping: nie ustawiamy callable-from-tasklets (std::nullopt) – maksymalnie prosta ścieżka wywołania
            a_vm->BindNativeMethod(N, "Ping",                    Ping,                    std::nullopt, false);
            a_vm->BindNativeMethod(N, "RegisterForPhaseBegin",   RegisterForPhaseBegin,   true, false);
            a_vm->BindNativeMethod(N, "RegisterForSequenceEnd",   RegisterForSequenceEnd,   true, false);
            a_vm->BindNativeMethod(N, "UnregisterForPhaseBegin",  UnregisterForPhaseBegin,  true, false);
            a_vm->BindNativeMethod(N, "UnregisterForSequenceEnd", UnregisterForSequenceEnd, true, false);

			a_vm->BindNativeMethod(N, "PlayOnActor",              PlayOnActor,              true, false);
            a_vm->BindNativeMethod(N, "PlayOnPlayer",             PlayOnPlayer,             true, false);
            a_vm->BindNativeMethod(N, "PlayOnActors",             PlayOnActors,             true, false);
            a_vm->BindNativeMethod(N, "StopAnimation",            StopAnimation,            true, false);
            a_vm->BindNativeMethod(N, "GetCurrentAnimation",      GetCurrentAnimation,      true, false);
            a_vm->BindNativeMethod(N, "SetAnimationSpeed",        SetAnimationSpeed,        true, false);
            a_vm->BindNativeMethod(N, "GetAnimationSpeed",        GetAnimationSpeed,        true, false);
            a_vm->BindNativeMethod(N, "SetGraphControlsPosition", SetGraphControlsPosition, true, false);
            a_vm->BindNativeMethod(N, "SetActorPosition",         SetActorPosition,         true, false);
            a_vm->BindNativeMethod(N, "LockActorForAnimation",   LockActorForAnimation,   true, false);
            a_vm->BindNativeMethod(N, "UnlockActorAfterAnimation", UnlockActorAfterAnimation, true, false);
            a_vm->BindNativeMethod(N, "GetSequencePhase",         GetSequencePhase,         true, false);
            a_vm->BindNativeMethod(N, "SetSequencePhase",         SetSequencePhase,         true, false);
            a_vm->BindNativeMethod(N, "AdvanceSequence",          AdvanceSequence,          true, false);
            a_vm->BindNativeMethod(N, "SyncGraphs",               SyncGraphs,               true, false);
            a_vm->BindNativeMethod(N, "StopSyncing",              StopSyncing,              true, false);
            a_vm->BindNativeMethod(N, "SetBlendGraphVariable",    SetBlendGraphVariable,    true, false);
            a_vm->BindNativeMethod(N, "GetBlendGraphVariable",    GetBlendGraphVariable,    true, false);
            a_vm->BindNativeMethod(N, "StartSequence",            StartSequence,            true, false);
			a_vm->BindNativeMethod(N, "GetCrosshairRef",          GetCrosshairRef,          true, false);

            SAF_LOG_INFO("SAFScript Papyrus functions bound successfully");
            return true;
        } catch (const std::exception& e) {
            SAF_LOG_ERROR("Failed to bind SAFScript: {}", e.what());
            return false;
        }
    }
}
