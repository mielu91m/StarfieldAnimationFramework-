#define BETTERAPI_IMPLEMENTATION
#include "betterapi.h"
#include "Util/String.h"
#include "Animation/GraphManager.h"
#include "Animation/Graph.h"
#include "Animation/FileManager.h"

namespace
{
	static const struct better_api_t* API = NULL;
	static const struct simple_draw_t* UI = NULL;

	template <typename T>
	struct ListTabData
	{
		std::vector<T>::iterator GetSelectedItem()
		{
			if (selectedIdx < list.size()) {
				return list.begin() + selectedIdx;
			} else {
				return list.end();
			}
		}

		std::vector<T> list;
		uint32_t selectedIdx = 0;
	};

	struct ActorsTabData :
		public ListTabData<std::pair<RE::TESObjectREFR*, std::weak_ptr<Animation::Graph>>>
	{
	};

	struct AnimationsTabData : 
		public ListTabData<std::pair<Animation::AnimID, std::weak_ptr<Animation::IAnimationFile>>>,
		public Util::Event::Listener<Animation::FileLoadUnloadEvent>
	{
		AnimationsTabData()
		{
			RegisterForEvent(Animation::FileManager::GetSingleton());
		}

		virtual ListenerStatus OnEvent(Animation::FileLoadUnloadEvent& a_event) {
			listNeedsUpdate = true;
			return ListenerStatus::kUnchanged;
		}

		std::atomic<bool> listNeedsUpdate = true;
	};

	AnimationsTabData animTab;
	ActorsTabData actorTab;

	inline void DrawActorGraphInfo(std::pair<RE::TESObjectREFR*, std::weak_ptr<Animation::Graph>>& a_actor) {
		UI->Text("Actor: %s (%08X)", a_actor.first->GetDisplayFullName(), a_actor.first->formID);

		if (UI->Button("Stop Animation")) {
			Animation::GraphManager::GetSingleton()->DetachGenerator(static_cast<RE::Actor*>(a_actor.first), 1.0f);
		}

		auto g = a_actor.second.lock();
		if (!g)
			return;

		std::unique_lock l{ g->lock };
		bool isLoaded = g->flags.none(Animation::Graph::FLAGS::kUnloaded3D);

		UI->Separator();
		UI->Text("Loaded: %s", isLoaded ? "True" : "False");
#ifdef ENABLE_PERFORMANCE_MONITORING
		UI->Text("Base Update Time: %.2f ms", g->baseUpdateMs);
		UI->Text("Update Time: %.2f ms", g->lastUpdateMs);
#endif

		UI->Text("Memory Usage: %.2f KB", static_cast<double>(g->GetSizeBytes()) / 1024.0);

		if (isLoaded) {
			UI->Separator();
			UI->Text("Generator");
			UI->Separator();

			auto gen = g->generator.get();
			if (gen) {
				UI->Text("Has Generator: True");
				UI->Checkbox("Paused", &gen->paused);

				if (gen->GetType() != Animation::GenType::kProcedural) {
					UI->DragFloat("Current Time Ratio", &gen->localTime, 0.0f, 1.0f);
				} else {
					UI->Text("Blend Graph Variables:");
					auto pGen = static_cast<Animation::ProceduralGenerator*>(gen);
					pGen->ForEachVariable([&](const std::string_view a_name, float& a_value) {
						UI->DragFloat(a_name.data(), &a_value, -10000.0f, 10000.0f);
					});
				}
				
			} else {
				UI->Text("Has Generator: False");
			}
		} else {
			static const char* cachedId{ nullptr };
			static std::array<char, 240> buffer;
			const char* curId = g->unloadedData->restoreFile.QPath().data();
			if (cachedId != curId) {
				std::strncpy(buffer.data(), curId, sizeof(buffer));
				buffer.back() = '/0';
				cachedId = curId;
			}
			if (UI->InputText("Animation File", buffer.data(), sizeof(buffer), true)) {
				g->unloadedData->restoreFile = Animation::FileID(buffer.data(), "");
			}
		}
		

		UI->Separator();
		UI->Text("Sequence");
		UI->Separator();

		if (g->sequencer) {
			UI->Text("Has Sequence: True");
			UI->DragInt("Loops Remaining in Phase", &g->sequencer->loopsRemaining, -1, 50);
			int curPhase = static_cast<int32_t>(std::distance(g->sequencer->phases.begin(), g->sequencer->currentPhase));
			if (UI->DragInt("Current Phase", &curPhase, 0, static_cast<int32_t>(g->sequencer->phases.size() - 1))) {
				g->sequencer->SetPhase(curPhase);
			}
			UI->Text("Preloading Next Animation: %s", g->sequencer->flags.any(Animation::Sequencer::FLAG::kLoadingNextAnim) ? "True" : "False");
			UI->Separator();
			UI->Text("Current Phase Data");
			UI->Separator();
			auto& phaseIter = g->sequencer->currentPhase;

			UI->Text("File Path: %s", phaseIter->file.QPath().data());
			UI->Text("Total Loops: %i", phaseIter->loopCount);
			UI->Text("Transition Time: %.2f", phaseIter->transitionTime);
		} else {
			UI->Text("Has Sequence: False");
		}
	}

	inline void DrawActorsTab()
	{
		auto* mgr = Animation::GraphManager::GetSingleton();
		if (!mgr) {
			UI->Text("SAF GraphManager not ready.");
			return;
		}
		mgr->GetAllGraphs(actorTab.list);

		UI->VboxTop(1.0f, 0.0f);
		UI->HBoxLeft(0.3f, 20.0f);
		if (actorTab.list.empty()) {
			UI->Text("No actors with active graphs.");
			UI->Text("Play an animation with: saf play <name>");
		} else {
			UI->SelectionList(&actorTab.selectedIdx, nullptr, static_cast<uint32_t>(actorTab.list.size()),
				[](const void* userdata, uint32_t index, char* out_buffer, uint32_t out_buffer_size) -> const char* {
					auto& ele = actorTab.list[index];
					std::snprintf(out_buffer, out_buffer_size, "%s (%08X)", ele.first->GetDisplayFullName(), ele.first->formID);
					return out_buffer;
				});
		}
		UI->HBoxRight();

		if (auto iter = actorTab.GetSelectedItem(); iter != actorTab.list.end()) {
			DrawActorGraphInfo(*iter);
		} else if (!actorTab.list.empty()) {
			UI->Text("Select an actor in the list.");
		}
		UI->HBoxEnd();
		UI->VBoxEnd();
	}

	inline void DrawAnimInfo(const std::pair<Animation::AnimID, std::weak_ptr<Animation::IAnimationFile>>& a_anim) {
		UI->Text("Animation: %s (%s)", a_anim.first.file.QPath().data(), a_anim.first.skeleton.c_str());

		auto animPtr = a_anim.second.lock();
		if (!animPtr)
			return;

		bool hasFaceAnim = false;
		if (auto basicAnim = dynamic_cast<Animation::IBasicAnimation*>(animPtr.get()); basicAnim) {
			hasFaceAnim = basicAnim->HasFaceAnimation();
		}

		UI->Text("In-Memory Size: %.2f KB", static_cast<double>(animPtr->GetSizeBytes()) / 1024.0);
		UI->Text("Time-to-Load: %.3f ms", animPtr->extra.loadTime);
		UI->Text("Has Face Animation: %s", hasFaceAnim ? "True" : "False");
		UI->Text("Use Count: %i", animPtr.use_count() - 1);
	}

	inline void DrawAnimationsTab()
	{
		auto* fileMgr = Animation::FileManager::GetSingleton();
		if (!fileMgr) {
			UI->Text("SAF FileManager not ready.");
			return;
		}
		if (animTab.listNeedsUpdate) {
			std::optional<Animation::AnimID> selectedId = std::nullopt;
			if (auto iter = animTab.GetSelectedItem(); iter != animTab.list.end()) {
				selectedId = iter->first;
			}
			fileMgr->GetAllLoadedAnimations(animTab.list);
			if (selectedId.has_value()) {
				for (auto iter = animTab.list.begin(); iter != animTab.list.end(); iter++) {
					if (iter->first == selectedId.value()) {
						animTab.selectedIdx = static_cast<uint32_t>(std::distance(animTab.list.begin(), iter));
						break;
					}
				}
			}
			animTab.listNeedsUpdate = false;
		}

		UI->VboxTop(1.0f, 0.0f);
		UI->HBoxLeft(0.5f, 20.0f);
		if (animTab.list.empty()) {
			UI->Text("No animations loaded yet.");
			UI->Text("Play one with: saf play <name>");
			UI->Text("(e.g. saf play cow1)");
		} else {
			UI->SelectionList(&animTab.selectedIdx, nullptr, static_cast<uint32_t>(animTab.list.size()),
				[](const void* userdata, uint32_t index, char* out_buffer, uint32_t out_buffer_size) -> const char* {
					auto& ele = animTab.list[index];
					std::snprintf(out_buffer, out_buffer_size, "%s (%s)", ele.first.file.QPath().data(), ele.first.skeleton.c_str());
					return out_buffer;
				});
		}
		UI->HBoxRight();

		if (auto iter = animTab.GetSelectedItem(); iter != animTab.list.end()) {
			DrawAnimInfo(*iter);
		} else if (!animTab.list.empty()) {
			UI->Text("Select an animation in the list.");
		}
		UI->HBoxEnd();
		UI->VBoxEnd();
	}

	void DrawPhysicsTab()
	{
		
	}

	static std::array<const char*, 3> tabNames{ "Managed Actors", "Loaded Animations", "Physics" };
	static int activeTab = 0;

	void OnUIDraw(void*)
	{
		if (!UI) return;
		UI->Separator();
		UI->Text("SAF Companion - Open console with tilde (`) and select this tab.");
		UI->Separator();
		UI->TabBar(tabNames.data(), tabNames.size(), &activeTab);
		switch (activeTab) {
		case 0:
			DrawActorsTab();
			break;
		case 1:
			DrawAnimationsTab();
			break;
		case 2:
			DrawPhysicsTab();
			break;
		}
	}
}

static void OnHotkeyOpenCompanion(uintptr_t)
{
	// User assigned this hotkey (e.g. F2) in Better Console -> Mod Menu -> Hotkeys.
	// Opening the console is done by the game/Better Console; we just register the action.
	if (API && API->Console && API->Console->RunCommand) {
		API->Console->RunCommand("");  // Empty command can focus the console in some setups
	}
}

static int OnBetterConsoleLoad(const struct better_api_t* better_api)
{
	if (!better_api || !better_api->Callback || !better_api->SimpleDraw)
		return -1;
	API = better_api;
	UI = API->SimpleDraw;
	RegistrationHandle handle = API->Callback->RegisterMod("StarfieldAnimationFrameworkSF");
	API->Callback->RegisterDrawCallback(handle, &OnUIDraw);
	API->Callback->RegisterHotkeyCallback(handle, &OnHotkeyOpenCompanion);
	API->Callback->RequestHotkey(handle, "Open SAF Companion", 0);
	return 0;
}