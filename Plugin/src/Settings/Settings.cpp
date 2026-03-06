#include "PCH.h"
#include "Settings.h"
#include "Util/String.h"
#include "Animation/Ozz.h"
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace Settings
{
	std::map<std::string, size_t> idxMap;
	std::vector<std::string> morphs;

	static std::vector<std::string> g_skipSkeletonPathSubstrings;
	/// When true, actors with path containing "extended" or "sff" are not skipped. Safe to default true now (extended only loaded after SetSafeToUseExtendedSkeleton).
	static bool g_allowAnimationsOnExtendedSkeleton = true;
	/// True only after player is in world (set from GraphUpdateHook). When false, we never load extended/SFF skeleton to avoid load crash.
	static bool g_safeToUseExtendedSkeleton = false;

	void SetSkipSkeletonPathSubstrings(std::vector<std::string> a_substrings)
	{
		g_skipSkeletonPathSubstrings = std::move(a_substrings);
	}

	void SetAllowAnimationsOnExtendedSkeleton(bool a_allow)
	{
		g_allowAnimationsOnExtendedSkeleton = a_allow;
	}

	void SetSafeToUseExtendedSkeleton(bool a_safe)
	{
		g_safeToUseExtendedSkeleton = a_safe;
	}

	static SkeletonMap& GetSkeletonMapImpl()
	{
		static SkeletonMap s_map;
		return s_map;
	}

	SkeletonMap& GetSkeletonMap()
	{
		return GetSkeletonMapImpl();
	}

	std::filesystem::path GetSkeletonsPath()
	{
		return Util::String::GetDataPath() / "SAF" / "Skeletons";
	}

	std::unique_ptr<Animation::OzzSkeleton> SkeletonDescriptor::BuildRuntime(std::string_view name)
	{
		ozz::animation::offline::RawSkeleton raw;
		raw.roots.resize(1);
		raw.roots[0].name = name.data();
		raw.roots[0].transform = ozz::math::Transform::identity();

		std::unordered_map<std::string, const Bone*> boneByName;
		boneByName.reserve(bones.size());
		for (const auto& bone : bones) {
			boneByName.emplace(bone.name, &bone);
		}

		std::unordered_map<std::string, std::vector<std::string>> children;
		children.reserve(bones.size());
		std::vector<std::string> roots;
		roots.reserve(bones.size());
		for (const auto& bone : bones) {
			if (!bone.parent.empty() && boneByName.find(bone.parent) != boneByName.end()) {
				children[bone.parent].push_back(bone.name);
			} else {
				roots.push_back(bone.name);
			}
		}

		auto buildJoint = [&](auto&& self, const std::string& jointName)
			-> ozz::animation::offline::RawSkeleton::Joint {
			ozz::animation::offline::RawSkeleton::Joint j;
			if (auto it = boneByName.find(jointName); it != boneByName.end()) {
				j.name = it->second->name.c_str();
				j.transform = it->second->restPose;
			} else {
				j.name = jointName.c_str();
				j.transform = ozz::math::Transform::identity();
			}
			auto childIt = children.find(jointName);
			if (childIt != children.end()) {
				for (const auto& childName : childIt->second) {
					j.children.push_back(self(self, childName));
				}
			}
			return j;
		};

		for (const auto& rootName : roots) {
			raw.roots[0].children.push_back(buildJoint(buildJoint, rootName));
		}

		if (!raw.Validate()) return nullptr;
		ozz::animation::offline::SkeletonBuilder builder;
		auto built = builder(raw);
		if (!built) return nullptr;
		auto skel = std::make_unique<Animation::OzzSkeleton>();
		skel->name.assign(name.data(), name.size());
		skel->data = std::move(built);
		// Populate joint names and controlledByGame flags
		auto joints = skel->data->joint_names();
		skel->jointNames.reserve(joints.size());
		skel->controlledByGame.assign(joints.size(), false);
		skel->jointAliases.resize(joints.size());
		for (auto& j : joints) {
			skel->jointNames.emplace_back(j);
		}
		for (const auto& bone : bones) {
			for (size_t i = 0; i < skel->jointNames.size(); ++i) {
				if (skel->jointNames[i] == bone.name) {
					skel->controlledByGame[i] = bone.controlledByGame;
					skel->jointAliases[i] = bone.aliases;
					break;
				}
			}
		}
		return skel;
	}

	void SetFaceMorphs(const std::vector<std::string>& a_morphs)
	{
		morphs = a_morphs;
		idxMap.clear();
		for (size_t i = 0; i < morphs.size(); i++) {
			idxMap[a_morphs[i]] = i;
		}
	}

	const std::map<std::string, size_t>& GetFaceMorphIndexMap()
	{
		return idxMap;
	}

	const std::vector<std::string>& GetFaceMorphs()
	{
		return morphs;
	}

	static std::shared_ptr<Animation::OzzSkeleton> MakeSharedView(Animation::OzzSkeleton* a_ptr)
	{
		return std::shared_ptr<Animation::OzzSkeleton>(a_ptr, [](Animation::OzzSkeleton*) {});
	}

	std::shared_ptr<Animation::OzzSkeleton> GetSkeleton(RE::Actor* a_actor)
	{
		auto& map = GetSkeletonMap();
		if (!a_actor) return nullptr;
		// Nie ładuj szkieletów w PostDataLoad – tylko przy pierwszym użyciu w grze (korpus SFF/body replacery muszą się załadować wcześniej).
		if (map.empty()) {
			LoadBaseSkeletons();
		}

		auto* npc = a_actor->GetNPC();
		auto* race = npc ? npc->GetRace() : nullptr;
		std::optional<std::string> pathOpt;
		if (race)
			pathOpt = GetSkeletonModelPathFromRace(race);

		// Optional: skip actors whose race skeleton path contains substrings from SkipSkeletonPathsContaining (AllowAnimationsOnExtendedSkeleton=1 still allows extended/SFF).
		if (race && !g_skipSkeletonPathSubstrings.empty() && pathOpt && !pathOpt->empty()) {
			std::string pathLower = *pathOpt;
			std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			for (const auto& sub : g_skipSkeletonPathSubstrings) {
				if (sub.empty()) continue;
				std::string subLower = sub;
				std::transform(subLower.begin(), subLower.end(), subLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (pathLower.find(subLower) != std::string::npos) {
					if (g_allowAnimationsOnExtendedSkeleton && (subLower == "extended" || subLower == "sff")) {
						SAF_LOG_DEBUG("GetSkeleton: not skipping (AllowAnimationsOnExtendedSkeleton, path contains '{}')", sub);
						break;
					}
					SAF_LOG_INFO("GetSkeleton: skipping actor (race skeleton path contains '{}')", sub);
					return nullptr;
				}
			}
		}
		const char* raceId = race ? race->formEditorID.c_str() : nullptr;
		if (raceId && *raceId) {
			// Zawsze próbuj załadować z NIF rasy (race->unk5E8) – działa gdy form list jest pusty przy PostDataLoad
			SAF_LOG_INFO("GetSkeleton: actor race editorID='{}', calling LoadSkeletonForRace", raceId);
			LoadSkeletonForRace(race);
			if (auto it = map.find(raceId); it != map.end() && it->second) {
				return MakeSharedView(it->second.get());
			}
		} else {
			SAF_LOG_WARN("GetSkeleton: actor has no NPC or race (raceId null), using first skeleton in map");
		}

		if (map.empty()) return nullptr;
		auto it = map.begin();
		return it != map.end() && it->second ? MakeSharedView(it->second.get()) : nullptr;
	}

	bool IsDefaultSkeleton(const std::shared_ptr<Animation::OzzSkeleton>& a_skele)
	{
		if (!a_skele) return false;
		return a_skele->name == "default" || a_skele->name == "Default";
	}
}