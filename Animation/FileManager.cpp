#include "FileManager.h"
#include "Animation/Procedural/PGraph.h"
#include "Animation/IBasicAnimation.h"
#include "Util/String.h"
#include "Ozz.h"
#include <SFSE/SFSE.h>

namespace Animation
{
	static bool FileIsBlendGraph(const FileID&) { return false; }

	FileManager::FileManager()
		: _workerThread([this]() { DoProcessRequests(); })
	{}

	FileManager* FileManager::GetSingleton()
	{
		static FileManager instance;
		return &instance;
	}

	void FileManager::RequestAnimation(const FileID& a_id, const std::string& a_skeleton, std::weak_ptr<FileRequesterBase> a_requester)
	{
		AnimID aID{ .file = a_id, .skeleton = a_skeleton };
		if (auto anim = GetLoadedAnimation(aID)) {
			NotifyAnimationRequested(a_requester, a_id, false);
			NotifyAnimationReady(a_requester, a_id, anim, false);
			return;
		}
		{
			std::lock_guard lk(_reqMutex);
			for (auto it = _requests.begin(); it != _requests.end(); ++it) {
				if (it->anim == aID && it->requester.lock() == a_requester.lock())
					return;
			}
			NotifyAnimationRequested(a_requester, a_id, false);
			_requests.push_back(RequestData{ .requester = a_requester, .anim = aID });
		}
		ProcessRequests();
	}

	bool FileManager::CancelAnimationRequest(const FileID& a_id, std::weak_ptr<FileRequesterBase> a_requester)
	{
		auto req_lock = a_requester.lock();
		std::lock_guard lk(_reqMutex);
		for (auto it = _requests.begin(); it != _requests.end(); ++it) {
			if (it->anim.file == a_id && it->requester.lock() == req_lock) {
				_requests.erase(it);
				return true;
			}
		}
		return false;
	}

	std::shared_ptr<IAnimationFile> FileManager::DemandAnimation(const FileID& a_id, std::string_view a_skeleton, bool a_noBlendGraphs)
	{
		if (FileIsBlendGraph(a_id) && a_noBlendGraphs)
			return nullptr;
		AnimID aID{ .file = a_id, .skeleton = std::string(a_skeleton) };
		if (auto anim = GetLoadedAnimation(aID))
			return anim;
		std::shared_ptr<IAnimationFile> result;
		if (FileIsBlendGraph(a_id))
			result = std::static_pointer_cast<IAnimationFile>(DoLoadBlendGraph(aID));
		else
			result = std::static_pointer_cast<IAnimationFile>(DoLoadAnimation(aID));
		if (result)
			return InsertLoadedAnimation(aID, result);
		return nullptr;
	}

	void FileManager::OnAnimationDestroyed(IAnimationFile* a_anim)
	{
		std::lock_guard lk(_loadedMutex);
		for (auto it = _loadedAnimations.begin(); it != _loadedAnimations.end(); ++it) {
			if (it->second.raw == a_anim) {
				AnimID id = it->first;
				_loadedAnimations.erase(it);
				if (auto* ti = SFSE::GetTaskInterface()) {
					ti->AddTask([this, id]() {
						SendEvent(FileLoadUnloadEvent{ .file = id.file, .skeleton = id.skeleton, .loaded = false });
					});
				}
				return;
			}
		}
	}

	void FileManager::GetAllLoadedAnimations(std::vector<std::pair<AnimID, std::weak_ptr<IAnimationFile>>>& a_animsOut)
	{
		a_animsOut.clear();
		std::lock_guard lk(_loadedMutex);
		for (auto& [id, data] : _loadedAnimations)
			a_animsOut.emplace_back(id, data.shared_handle);
	}

	void FileManager::ProcessRequests() { _workCV.notify_one(); }

	void FileManager::DoProcessRequests()
	{
		RequestData nextReq;
		for (;;) {
			{
				std::unique_lock lk(_reqMutex);
				_workCV.wait(lk, [this]() { return !_requests.empty(); });
				nextReq = _requests.front();
				_requests.pop_front();
			}
			std::shared_ptr<IAnimationFile> loadedFile;
			if (FileIsBlendGraph(nextReq.anim.file))
				loadedFile = std::static_pointer_cast<IAnimationFile>(DoLoadBlendGraph(nextReq.anim));
			else
				loadedFile = std::static_pointer_cast<IAnimationFile>(DoLoadAnimation(nextReq.anim));
			if (!loadedFile) {
				ReportFailedToLoadAnimation(nextReq);
				continue;
			}
			ReportAnimationLoaded(nextReq, loadedFile);
		}
	}

	std::shared_ptr<Procedural::PGraph> FileManager::DoLoadBlendGraph(const AnimID&) { return nullptr; }

	std::shared_ptr<IBasicAnimation> FileManager::DoLoadAnimation(const AnimID&)
	{
		return nullptr;
	}

	std::shared_ptr<IAnimationFile> FileManager::GetLoadedAnimation(const AnimID& a_id)
	{
		std::lock_guard lk(_loadedMutex);
		auto it = _loadedAnimations.find(a_id);
		if (it != _loadedAnimations.end())
			return it->second.shared_handle.lock();
		return nullptr;
	}

	std::shared_ptr<IAnimationFile> FileManager::InsertLoadedAnimation(const AnimID& a_id, std::shared_ptr<IAnimationFile> a_anim)
	{
		std::shared_ptr<IAnimationFile> inserted;
		{
			std::lock_guard lk(_loadedMutex);
			auto [it, ok] = _loadedAnimations.insert({ a_id, LoadedAnimData{ .raw = a_anim.get(), .shared_handle = a_anim } });
			inserted = it->second.shared_handle.lock();
		}
		{
			std::lock_guard lk(_reqMutex);
			for (auto it = _requests.begin(); it != _requests.end();) {
				if (it->anim == a_id) {
					NotifyAnimationReady(it->requester, a_id.file, inserted, false);
					it = _requests.erase(it);
				} else
					++it;
			}
		}
		if (auto* ti = SFSE::GetTaskInterface()) {
			ti->AddTask([this, a_id]() {
				SendEvent(FileLoadUnloadEvent{ .file = a_id.file, .skeleton = a_id.skeleton, .loaded = true });
			});
		}
		return inserted;
	}

	void FileManager::ReportAnimationLoaded(RequestData& a_req, std::shared_ptr<IAnimationFile> a_anim)
	{
		auto inserted = InsertLoadedAnimation(a_req.anim, a_anim);
		NotifyAnimationReady(a_req.requester, a_req.anim.file, inserted, false);
	}

	void FileManager::ReportFailedToLoadAnimation(const RequestData& a_req)
	{
		NotifyAnimationReady(a_req.requester, a_req.anim.file, nullptr, false);
	}

	static void DoNotifyAnimationReady(std::weak_ptr<FileRequesterBase> a_requester, const FileID& a_id, std::shared_ptr<IAnimationFile> a_anim)
	{
		if (auto r = a_requester.lock())
			r->OnAnimationReady(a_id, a_anim);
	}

	static void DoNotifyAnimationRequested(std::weak_ptr<FileRequesterBase> a_requester, const FileID& a_id)
	{
		if (auto r = a_requester.lock())
			r->OnAnimationRequested(a_id);
	}

	void FileManager::NotifyAnimationReady(std::weak_ptr<FileRequesterBase> a_requester, const FileID& a_id, std::shared_ptr<IAnimationFile> a_anim, bool a_queue)
	{
		if (a_queue && SFSE::GetTaskInterface())
			SFSE::GetTaskInterface()->AddTask([a_requester, a_id, a_anim]() { DoNotifyAnimationReady(a_requester, a_id, a_anim); });
		else
			DoNotifyAnimationReady(a_requester, a_id, a_anim);
	}

	void FileManager::NotifyAnimationRequested(std::weak_ptr<FileRequesterBase> a_requester, const FileID& a_id, bool a_queue)
	{
		if (a_queue && SFSE::GetTaskInterface())
			SFSE::GetTaskInterface()->AddTask([a_requester, a_id]() { DoNotifyAnimationRequested(a_requester, a_id); });
		else
			DoNotifyAnimationRequested(a_requester, a_id);
	}
}
