#pragma once
#include "PCH.h"

#include "Animation/Sequencer.h"
#include "Animation/Graph.h"
#include "Animation/Generator.h"  // ← DODANE: brakujący include
#include "Util/Event.h"

namespace Animation
{
	class GraphManager
	{
	public:
		static GraphManager* GetSingleton()
		{
			static GraphManager singleton;
			return &singleton;
		}

		// Instaluje GraphUpdateHook - musi być wywołane przy starcie
		void InstallHooks();

		// Metody sterujące wywoływane przez konsolę
		/// Returns true on success. On false, use GetLastLoadError() for a short message (e.g. for console).
		/// a_animIndex: which animation in the file (0-based; if out of range, uses first).
		bool LoadAndStartAnimation(RE::Actor* a_actor, std::string_view a_path, bool a_looping = true, int a_animIndex = 0);
		static const std::string& GetLastLoadError();
		std::string GetCurrentAnimation(RE::Actor* a_actor) const;
		void SetAnimationSpeed(RE::Actor* a_actor, float a_speed);
		float GetAnimationSpeed(RE::Actor* a_actor) const;
		void SetAnimationLooping(RE::Actor* a_actor, bool a_loop);
		bool GetAnimationLooping(RE::Actor* a_actor) const;
		void SetActorPosition(RE::Actor* a_actor, float a_x, float a_y, float a_z);
		int GetSequencePhase(RE::Actor* a_actor) const;
		bool SetSequencePhase(RE::Actor* a_actor, int a_phase);
		bool SetBlendGraphVariable(RE::Actor* a_actor, const std::string& a_name, float a_value);
		float GetBlendGraphVariable(RE::Actor* a_actor, const std::string& a_name) const;
		void StartSequence(RE::Actor* a_actor, std::vector<Sequencer::PhaseData>&& a_phases, bool a_loop);
		void DetachGenerator(RE::Actor* a_actor, float a_duration);
		void StopAnimation(RE::Actor* a_actor);
		void SyncGraphs(const std::vector<RE::Actor*>& a_actors);
		void StopSyncing(RE::Actor* a_actor);
		void AdvanceSequence(RE::Actor* a_actor, bool a_smooth);
		void SetGraphControlsPosition(RE::Actor* a_actor, bool a_lock);
		
		template <typename T, typename U>
		void RegisterForEvent(U* a_listener)
		{
			SAF_LOG_INFO("Registered listener for animation event");
		}

		void AttachGenerator(RE::Actor* a_actor, std::unique_ptr<Animation::Generator> a_generator, float a_transitionTime);
		void UpdateGraphs(float a_deltaSeconds);
		void RequestGraphUpdate();
		bool HasActiveGraphs() const;
		void EnableAnimationVtableHooks();
		bool ShouldDeferHookInstall() const;
		void SetMainThreadId(std::uint32_t a_threadId);
		std::uint32_t GetMainThreadId() const;
		bool IsMainThread() const;

		Graph* GetGraphForActor(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) return nullptr;
			// TODO: Implementacja mapy actor->graph
			return &_internalGraph;
		}

		void Reset();

		/// Czy używać ModelDB::GetEntry do ładowania rest pose z NIF rasy.
		static bool IsModelDBForRestPoseEnabled();
		/// Address Library ID dla GetModelDBEntry (z INI ModelDBGetEntryID; domyślnie 949826).
		static std::uint64_t GetModelDBGetEntryID();
		/// Address Library ID dla DecRef (z INI ModelDBDecRefID; domyślnie 36741).
		static std::uint64_t GetModelDBDecRefID();
		/// Gdy nie 0: DecRef = base+RVA (z IDA); wtedy drugie ID nie jest potrzebne (z INI ModelDBDecRefRVA).
		static std::uint32_t GetModelDBDecRefRVA();

		// Event wywoływany gdy zmienia się faza sekwencji
		Util::Event::Event<SequencePhaseChangeEvent&> OnSequencePhaseChange;

	private:
		GraphManager() = default;
		Graph _internalGraph;
		// TODO: std::unordered_map<RE::FormID, std::unique_ptr<Graph>> _graphs;
	};
}