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
		/// Ustaw pozycję i rotację aktora a_target tak, aby odpowiadała a_source
		/// (pozycja z GetPositionForSceneSync, rotacja z GetAngle). Aktualizuje
		/// zarówno ref (data.location/angle), jak i stan grafu (positionX/Y/Z, angleX/Y/Z).
		/// Kopiuje też macierz rotacji root NiNode bezpośrednio (nie czeka na tick silnika).
		void MatchActorTransform(RE::Actor* a_target, RE::Actor* a_source);
		/// Ustawia obu aktorów na pozycji a_actor1 z rotacją a_actor2.
		/// Wywołaj przed LoadAndStartAnimation obu aktorów – zapewnia identyczny
		/// transform startowy, co gwarantuje spójne gameBaseRotations.
		/// Przygotowuje dwóch aktorów do sceny: blokuje AI, wyrównuje pozycję i rotację.
		/// Schemat: backup flag → blokada AI → ustawienie pos+ang → macierz NiNode.
		/// Wywołaj PRZED LoadAndStartAnimation. DetachGenerator odtworzy flagi po animacji.
		void PrepareActorsForScene(RE::Actor* a_actor1, RE::Actor* a_actor2);
		int GetSequencePhase(RE::Actor* a_actor) const;
		bool SetSequencePhase(RE::Actor* a_actor, int a_phase);
		bool SetBlendGraphVariable(RE::Actor* a_actor, const std::string& a_name, float a_value);
		float GetBlendGraphVariable(RE::Actor* a_actor, const std::string& a_name) const;
		void StartSequence(RE::Actor* a_actor, std::vector<Sequencer::PhaseData>&& a_phases, bool a_loop);
		void DetachGenerator(RE::Actor* a_actor, float a_duration);
		void StopAnimation(RE::Actor* a_actor);
		// Stops all active SAF animations on all actors (used by 'saf stop' with no args).
		void StopAllAnimations();
		void SyncGraphs(const std::vector<RE::Actor*>& a_actors);
		void StopSyncing(RE::Actor* a_actor);
		void AdvanceSequence(RE::Actor* a_actor, bool a_smooth);
		void SetGraphControlsPosition(RE::Actor* a_actor, bool a_lock);
		/// Pozycja (x,y,z) do użycia przy następnym AttachGenerator; odblokowanie przez UnlockActorAfterAnimation.
		void LockActorForAnimation(RE::Actor* a_actor, float a_x, float a_y, float a_z);
		void UnlockActorAfterAnimation(RE::Actor* a_actor);
		/// Prędkość odtwarzania w saf playscene (z INI PlaySceneSpeed). 1.0 = domyślna.
		static float GetPlaySceneSpeed();
		/// Pozycja aktora do synchronizacji (tylko początkowy teleport w PlayScene): z 3D root world jeśli dostępny, inaczej GetPosition().
		RE::NiPoint3 GetPositionForSceneSync(RE::Actor* a_actor) const;

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

		/// Zapis/odczyt stanu animacji (jak w NAF). CreateSaveData wypełnia a_data; LoadSaveData odtwarza grafy i sync.
		/// Pełna persystencja wymaga zapisu a_data do pliku zapisu (np. SFSE SerializationInterface) po stronie wywołującego.
		struct SaveData {
			struct BlendGraphVariable { std::string name; float value = 0.0f; };
			struct RefData {
				RE::TESFormID formId = 0;
				RE::TESFormID syncOwner = 0;
				std::string animFile;
				float localTime = 0.0f;
				float speedMult = 1.0f;
				std::vector<BlendGraphVariable> blendVars;
			};
			std::vector<RefData> refs;
		};
		void CreateSaveData(SaveData& a_data);
		void LoadSaveData(const SaveData& a_data);

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