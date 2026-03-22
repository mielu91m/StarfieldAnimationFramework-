#include "PCH.h"
#include "Papyrus/SAFScript.h"
#include "Papyrus/EventManager.h"
#include "Animation/GraphManager.h"
#include "Animation/Sequencer.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/E/Events.h"
#include "RE/B/BSTEvent.h"
#include "Util/String.h"
#include <filesystem>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

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


    // =========================================================================
    // SELECTION BUFFER oparty na TESHitEvent
    //
    // Problem crosshairRef: wskazuje na obiekt POD celownikiem w chwili sprawdzenia,
    // nie na faktycznie trafiony obiekt. Pocisk leci chwilę - w tym czasie
    // crosshairRef może zmienić się na innego NPC lub obiekt.
    //
    // Rozwiązanie: TESHitEvent::target = FAKTYCZNIE trafiony obiekt przez pocisk gracza.
    // =========================================================================
    static std::vector<RE::Actor*>                g_selectionBuffer;
    static std::mutex                              g_selectionMutex;
    static RE::Actor*                              g_lastHitActor    = nullptr;
    static std::mutex                              g_lastHitMutex;
    static std::chrono::steady_clock::time_point  g_lastHitTime;
    static bool                                    g_hitSinkInstalled = false;
    constexpr size_t                               kSelBufMax         = 8;
    constexpr float                                kHitTimeoutSec     = 2.0f;

    class SAFHitEventSink : public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESHitEvent& a_event,
            RE::BSTEventSource<RE::TESHitEvent>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return RE::BSEventNotifyControl::kContinue;
            // Tylko strzały gracza
            if (a_event.cause.get() != static_cast<RE::TESObjectREFR*>(player))
                return RE::BSEventNotifyControl::kContinue;
            auto* target = a_event.target.get();
            if (!target) return RE::BSEventNotifyControl::kContinue;
            RE::Actor* hitActor = target->As<RE::Actor>();
            if (!hitActor || hitActor == player) return RE::BSEventNotifyControl::kContinue;

            std::lock_guard<std::mutex> lk(g_lastHitMutex);
            g_lastHitActor = hitActor;
            g_lastHitTime  = std::chrono::steady_clock::now();
            SAF_LOG_INFO("[HitSink] Player hit actor {:08X}", hitActor->GetFormID());
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    static SAFHitEventSink g_hitSink;

    static void EnsureHitSinkInstalled()
    {
        if (g_hitSinkInstalled) return;
        auto* src = RE::TESHitEvent::GetEventSource();
        if (src) {
            src->RegisterSink(&g_hitSink);
            g_hitSinkInstalled = true;
            SAF_LOG_INFO("[SAFScript] TESHitEvent sink registered for selection buffer");
        } else {
            SAF_LOG_WARN("[SAFScript] TESHitEvent source not available");
        }
    }

    static RE::Actor* GetLastHitActor()
    {
        std::lock_guard<std::mutex> lk(g_lastHitMutex);
        if (!g_lastHitActor) return nullptr;
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - g_lastHitTime).count();
        if (elapsed > kHitTimeoutSec) {
            g_lastHitActor = nullptr;
            return nullptr;
        }
        return g_lastHitActor;
    }

    RE::Actor* GetCrosshairActor(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate)
    {
        // Preferuj ostatnio trafionego przez TESHitEvent (niezawodne przy strzałach)
        if (auto* hit = GetLastHitActor()) return hit;
        // Fallback: crosshairRef (działa gdy nie strzelamy)
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        auto* ref = player->crosshairRef;
        return ref ? ref->As<RE::Actor>() : nullptr;
    }

    RE::Actor* FindActorNearCrosshair(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
        float /*maxAngle*/, float /*maxDist*/)
    {
        if (auto* hit = GetLastHitActor()) return hit;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        if (auto* ref = player->crosshairRef) return ref->As<RE::Actor>();
        return nullptr;
    }

    int AddActorToSelectionBuffer(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
        float /*maxAngle*/, float /*maxDist*/)
    {
        EnsureHitSinkInstalled();
        // Użyj FAKTYCZNIE trafionego aktora (TESHitEvent), nie crosshairRef
        RE::Actor* found = GetLastHitActor();
        if (!found) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) if (auto* ref = player->crosshairRef) found = ref->As<RE::Actor>();
        }
        if (!found) {
            SAF_LOG_WARN("[Papyrus] AddActorToSelectionBuffer: no hit actor");
            return -1;
        }
        std::lock_guard<std::mutex> lk(g_selectionMutex);
        for (auto* a : g_selectionBuffer) if (a == found) return -1; // brak duplikatów
        if (g_selectionBuffer.size() >= kSelBufMax) return -1;
        int idx = static_cast<int>(g_selectionBuffer.size());
        g_selectionBuffer.push_back(found);
        { std::lock_guard<std::mutex> lk2(g_lastHitMutex); g_lastHitActor = nullptr; }
        SAF_LOG_INFO("[Papyrus] AddActorToSelectionBuffer: {:08X} idx={}", found->GetFormID(), idx);
        return idx;
    }

    std::vector<RE::Actor*> GetSelectionBuffer(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate)
    {
        std::lock_guard<std::mutex> lk(g_selectionMutex);
        return g_selectionBuffer;
    }

    int GetSelectionBufferSize(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate)
    {
        std::lock_guard<std::mutex> lk(g_selectionMutex);
        return static_cast<int>(g_selectionBuffer.size());
    }

    void ClearSelectionBuffer(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate)
    {
        std::lock_guard<std::mutex> lk(g_selectionMutex);
        g_selectionBuffer.clear();
        SAF_LOG_INFO("[Papyrus] ClearSelectionBuffer");
    }

    int SelectActor(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
        RE::Actor* a_actor)
    {
        if (!a_actor) return -1;
        std::lock_guard<std::mutex> lk(g_selectionMutex);
        for (auto* a : g_selectionBuffer) if (a == a_actor) return -1;
        if (g_selectionBuffer.size() >= kSelBufMax) return -1;
        int idx = static_cast<int>(g_selectionBuffer.size());
        g_selectionBuffer.push_back(a_actor);
        SAF_LOG_INFO("[Papyrus] SelectActor: {:08X} idx={}", a_actor->GetFormID(), idx);
        return idx;
    }


    // =========================================================================
    // PlaySceneSeparate – scena z dwoma aktorami i pełną synchronizacją
    // =========================================================================
    bool PlaySceneSeparate(
        RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate,
        RE::Actor* a_actor1, RE::Actor* a_actor2,
        RE::BSFixedString a_animId1, RE::BSFixedString a_animId2, float a_speed)
    {
        if (!a_actor1 || !a_actor2) { SAF_LOG_WARN("[Papyrus] PlaySceneSeparate: actor none"); return false; }
        if (!a_animId1.data() || !a_animId2.data()) { SAF_LOG_WARN("[Papyrus] PlaySceneSeparate: anim empty"); return false; }
        auto* mgr = Animation::GraphManager::GetSingleton();
        if (!mgr) return false;
        auto resolve = [](std::string_view id) {
            return ResolveAnimationPathWithFallback(Util::String::ToLower(id)).string();
        };
        std::string p1 = resolve(a_animId1.c_str()), p2 = resolve(a_animId2.c_str());
        if (a_speed <= 0.0f) a_speed = 1.0f;
        // Krok 1: odśwież kapsułę havok w bieżącej pozycji
        { RE::NiPoint3 pos = a_actor1->GetPosition(); a_actor1->SetPosition(pos, true); }
        { RE::NiPoint3 pos = a_actor2->GetPosition(); a_actor2->SetPosition(pos, true); }
        // Krok 2: przesuń actor2 na pozycję actor1 + zablokuj AI
        mgr->PrepareActorsForScene(a_actor1, a_actor2);
        // Krok 3: zakotwicz pozycje
        { RE::NiPoint3 pos = a_actor1->GetPosition(); mgr->LockActorForAnimation(a_actor1, pos.x, pos.y, pos.z); }
        { RE::NiPoint3 pos = a_actor2->GetPosition(); mgr->LockActorForAnimation(a_actor2, pos.x, pos.y, pos.z); }
        // Krok 4: animacje + sync
        bool ok1 = mgr->LoadAndStartAnimation(a_actor1, p1, true, 0);
        bool ok2 = mgr->LoadAndStartAnimation(a_actor2, p2, true, 0);
        if (ok1) mgr->SetAnimationSpeed(a_actor1, a_speed);
        if (ok2) mgr->SetAnimationSpeed(a_actor2, a_speed);
        if (ok1 && ok2) mgr->SyncGraphs(std::vector<RE::Actor*>{a_actor1, a_actor2});
        mgr->RequestGraphUpdate();
        SAF_LOG_INFO("[Papyrus] PlaySceneSeparate: ok1={} ok2={}", ok1, ok2);
        return ok1 && ok2;
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

    /// Jedna faza, bez pętli (jak NAF PlayAnimationOnce). transitionTime w sekundach.
    bool PlayAnimationOnce(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::Actor* a_actor,
        RE::BSFixedString a_animId,
        float a_transitionTime)
    {
        if (!a_actor) { SAF_LOG_WARN("[Papyrus] PlayAnimationOnce: actor none"); return false; }
        if (!a_animId.data()) { SAF_LOG_WARN("[Papyrus] PlayAnimationOnce: animId empty"); return false; }
        auto* mgr = Animation::GraphManager::GetSingleton();
        if (!mgr) return false;
        std::vector<Animation::Sequencer::PhaseData> phases;
        phases.push_back(Animation::Sequencer::PhaseData{
            .file = a_animId.c_str(),
            .loopCount = 0,
            .transitionTime = a_transitionTime > 0.f ? a_transitionTime : 1.f
        });
        mgr->StartSequence(a_actor, std::move(phases), false);
        return true;
    }

    /// Dwie osoby: aktor 2 dostaje pozycję aktora 1, potem obie animacje startują (jak saf playscene z konsoli).
    bool PlayScene(
        RE::BSScript::IVirtualMachine& /*a_vm*/,
        std::uint32_t /*a_stackID*/,
        std::monostate,
        RE::Actor* a_actor1,
        RE::Actor* a_actor2,
        RE::BSFixedString a_animId1,
        RE::BSFixedString a_animId2,
        float a_speed)
    {
        if (!a_actor1 || !a_actor2) {
            SAF_LOG_WARN("[Papyrus] PlayScene: actor1 or actor2 is none");
            return false;
        }
        if (!a_animId1.data() || !a_animId2.data()) {
            SAF_LOG_WARN("[Papyrus] PlayScene: animId1 or animId2 empty");
            return false;
        }
        auto* mgr = Animation::GraphManager::GetSingleton();
        if (!mgr) return false;

        auto resolve = [](std::string_view id) {
            auto key = Util::String::ToLower(id);
            return ResolveAnimationPathWithFallback(key).string();
        };
        std::string path1 = resolve(a_animId1.c_str());
        std::string path2 = resolve(a_animId2.c_str());
        if (a_speed <= 0.0f) a_speed = 1.0f;

        // Schemat: blokada AI → wyrównanie pos+rot → animacja → odblokowanie po animacji.
        // PrepareActorsForScene: zapisuje backup flag, blokuje AI obu NPC, ustawia
        // pos+kąt z actor1, wpisuje macierz yaw do root NiNode.
        // Po tej funkcji AI nie może nadpisać data.angle zanim AttachGenerator go odczyta.
        mgr->PrepareActorsForScene(a_actor1, a_actor2);

        bool ok1 = mgr->LoadAndStartAnimation(a_actor1, path1, true, 0);
        bool ok2 = mgr->LoadAndStartAnimation(a_actor2, path2, true, 0);
        if (ok1) mgr->SetAnimationSpeed(a_actor1, a_speed);
        if (ok2) mgr->SetAnimationSpeed(a_actor2, a_speed);
        if (ok1 && ok2)
            mgr->SyncGraphs(std::vector<RE::Actor*>{ a_actor1, a_actor2 });
        mgr->RequestGraphUpdate();
        SAF_LOG_INFO("[Papyrus] PlayScene: ok1={} ok2={}, SyncGraphs(actor1,actor2)", ok1, ok2);
        return ok1 && ok2;
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

	void MatchActorTransform(
		RE::BSScript::IVirtualMachine&, std::uint32_t,
		std::monostate,
		RE::Actor* a_target, RE::Actor* a_source)
	{
		if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && a_target && a_source)
			mgr->MatchActorTransform(a_target, a_source);
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
            a_vm->BindNativeMethod(N, "PlayAnimationOnce",        PlayAnimationOnce,        true, false);
            a_vm->BindNativeMethod(N, "PlayScene",                 PlayScene,                 true, false);
            a_vm->BindNativeMethod(N, "PlaySceneSeparate",        PlaySceneSeparate,        true, false);
            a_vm->BindNativeMethod(N, "PlayOnPlayer",             PlayOnPlayer,             true, false);
            a_vm->BindNativeMethod(N, "PlayOnActors",             PlayOnActors,             true, false);
            a_vm->BindNativeMethod(N, "StopAnimation",            StopAnimation,            true, false);
            a_vm->BindNativeMethod(N, "GetCurrentAnimation",      GetCurrentAnimation,      true, false);
            a_vm->BindNativeMethod(N, "SetAnimationSpeed",        SetAnimationSpeed,        true, false);
            a_vm->BindNativeMethod(N, "GetAnimationSpeed",        GetAnimationSpeed,        true, false);
            a_vm->BindNativeMethod(N, "SetGraphControlsPosition", SetGraphControlsPosition, true, false);
            a_vm->BindNativeMethod(N, "SetActorPosition",         SetActorPosition,         true, false);
			a_vm->BindNativeMethod(N, "MatchActorTransform",      MatchActorTransform,      true, false);
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
			a_vm->BindNativeMethod(N, "GetCrosshairActor",        GetCrosshairActor,        true, false);
			a_vm->BindNativeMethod(N, "FindActorNearCrosshair",   FindActorNearCrosshair,   true, false);
			a_vm->BindNativeMethod(N, "AddActorToSelectionBuffer",AddActorToSelectionBuffer,true, false);
			a_vm->BindNativeMethod(N, "GetSelectionBuffer",       GetSelectionBuffer,       true, false);
			a_vm->BindNativeMethod(N, "GetSelectionBufferSize",   GetSelectionBufferSize,   true, false);
			a_vm->BindNativeMethod(N, "ClearSelectionBuffer",     ClearSelectionBuffer,     true, false);
			a_vm->BindNativeMethod(N, "SelectActor",              SelectActor,              true, false);

            SAF_LOG_INFO("SAFScript Papyrus functions bound successfully");
            return true;
        } catch (const std::exception& e) {
            SAF_LOG_ERROR("Failed to bind SAFScript: {}", e.what());
            return false;
        }
    }
}