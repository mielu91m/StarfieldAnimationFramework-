#include "Composer.h"
#include "GraphManager.h"

namespace Animation
{
	void Composer::Process()
	{
		auto* agm = Animation::GraphManager::GetSingleton();

		// Jeśli są dokładnie 2 aktorzy, użyj PrepareActorsForScene:
		// blokada AI → wyrównanie pos+rot → gwarantowany state.angleZ przed animacją.
		// Dla 1 lub 3+ aktorów: standardowe LoadAndStartAnimation (brak pos/rot sync).
		if (members.size() == 2 && members[0]->actor && members[1]->actor) {
			agm->PrepareActorsForScene(members[0]->actor.get(), members[1]->actor.get());
		}

		for (auto& m : members) {
			agm->LoadAndStartAnimation(m->actor.get(), m->animFile);
		}

		std::vector<RE::Actor*> syncList;
		syncList.reserve(members.size());
		for (auto& m : members) {
			syncList.push_back(m->actor.get());
		}
		agm->SyncGraphs(syncList);
	}

	void Composer::StopAll()
	{
		auto* agm = GraphManager::GetSingleton();
		for (auto& m : members) {
			agm->DetachGenerator(m->actor.get(), 1.0f);
		}
	}

	Composer::ActorData* Composer::AddActor(RE::Actor* a_actor)
	{
		auto& elem = members.emplace_back(std::make_unique<ActorData>());
		elem->actor.reset(a_actor);
		return elem.get();
	}

	bool Composer::RemoveActor(RE::Actor* a_actor)
	{
		return std::erase_if(members, [&](const std::unique_ptr<ActorData>& elem) {
			return elem->actor.get() == a_actor;
		}) > 0;
	}
}