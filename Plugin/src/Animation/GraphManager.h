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
		void LoadAndStartAnimation(RE::Actor* a_actor, std::string_view a_path);
		void DetachGenerator(RE::Actor* a_actor, float a_duration);
		void StopAnimation(RE::Actor* a_actor);
		void SyncGraphs(const std::vector<RE::Actor*>& a_actors);
		void StopSyncing(RE::Actor* a_actor);
		void StartSequence(RE::Actor* a_actor, std::vector<Sequencer::PhaseData>&& a_phases);
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