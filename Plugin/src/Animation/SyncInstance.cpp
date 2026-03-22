#include "SyncInstance.h"
#include <algorithm>

namespace Animation
{
	bool SyncInstance::Synchronize(RE::TESFormID a_formId, VisitFunc a_visitFunc)
	{
		std::unique_lock<std::mutex> lock(_mutex);
		InstData& d = _data;

		if (d.ownerFormId == 0)
			return false;

		// Nowa klatka: jeśli ten sam formId zgłasza się drugi raz, czyścimy listę zaktualizowanych.
		auto it = std::find(d.updatedThisFrame.begin(), d.updatedThisFrame.end(), a_formId);
		if (it != d.updatedThisFrame.end()) {
			d.ownerUpdatedThisFrame = false;
			d.updatedThisFrame.clear();
		}
		d.updatedThisFrame.push_back(a_formId);

		if (a_formId == d.ownerFormId) {
			d.ownerUpdatedThisFrame = true;
			return true;
		}

		bool ownerUpdated = d.ownerUpdatedThisFrame;
		RE::TESFormID ownerId = d.ownerFormId;
		lock.unlock();

		if (a_visitFunc)
			a_visitFunc(ownerId, ownerUpdated);
		return true;
	}

	void SyncInstance::AddMember(RE::TESFormID a_formId, bool a_addAsOwner)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto& members = _data.members;
		if (std::find(members.begin(), members.end(), a_formId) == members.end())
			members.push_back(a_formId);
		if (a_addAsOwner)
			_data.ownerFormId = a_formId;
	}

	void SyncInstance::RemoveMember(RE::TESFormID a_formId)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto& members = _data.members;
		members.erase(std::remove(members.begin(), members.end(), a_formId), members.end());
		if (_data.ownerFormId == a_formId)
			_data.ownerFormId = 0;
	}
}
