#pragma once

#include "PCH.h"
#include <functional>
#include <vector>

namespace Animation
{
	/// Synchronizacja grup aktorów (owner + członkowie) – wzór z NAF, bez zależności od Graph.
	/// Przechowuje owner FormID i listę członków; wywołujący (np. GraphManager) może sprawdzić,
	/// czy dany aktor jest w grupie i kto jest ownerem.
	class SyncInstance
	{
	public:
		struct InstData
		{
			RE::TESFormID ownerFormId = 0;
			std::vector<RE::TESFormID> members;
			std::vector<RE::TESFormID> updatedThisFrame;
			bool ownerUpdatedThisFrame = false;
		};

		/// visitFunc(ownerFormId, ownerUpdatedThisFrame) – wywoływane dla slave’a, gdy trzeba zsynchronizować z ownerem.
		using VisitFunc = std::function<void(RE::TESFormID ownerFormId, bool ownerUpdatedThisFrame)>;

		/// Zwraca true, jeśli a_formId jest w tej instancji i visitFunc zostało wywołane (dla slave’a).
		/// Dla ownera tylko aktualizuje flagę ownerUpdatedThisFrame.
		bool Synchronize(RE::TESFormID a_formId, VisitFunc a_visitFunc);

		void AddMember(RE::TESFormID a_formId, bool a_addAsOwner);
		void RemoveMember(RE::TESFormID a_formId);
		RE::TESFormID GetOwnerFormID() const;

	private:
		mutable std::mutex _mutex;
		InstData _data;
	};

	inline RE::TESFormID SyncInstance::GetOwnerFormID() const
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return _data.ownerFormId;
	}
}
