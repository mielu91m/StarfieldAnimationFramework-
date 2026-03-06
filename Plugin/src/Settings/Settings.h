#pragma once

#include "Animation/Ozz.h"
#include <map>
#include <vector>
#include <string>
#include <string_view>
#include <filesystem>
#include <memory>
#include "ozz/base/maths/transform.h"
#include "RE/Starfield.h" // Rozwiązuje błędy RE::Actor
#include <optional>

namespace Settings
{
	struct SkeletonDescriptor
	{
		struct Bone {
			std::string name;
			std::string parent;
			ozz::math::Transform restPose;
			std::vector<std::string> aliases;
			bool controlledByDefault = true;
			bool controlledByGame = false;
		};

		std::vector<Bone> bones;

		void AddBone(const std::string_view name, const std::string_view parent, const ozz::math::Transform& pose, int index = -1, bool def = true, bool game = false, std::vector<std::string> aliases = {}) {
			bones.push_back({ std::string(name), std::string(parent), pose, std::move(aliases), def, game });
		}

		std::unique_ptr<Animation::OzzSkeleton> BuildRuntime(std::string_view name);
	};

	using SkeletonMap = std::map<std::string, std::unique_ptr<Animation::OzzSkeleton>, std::less<>>;
	SkeletonMap& GetSkeletonMap();
	std::filesystem::path GetSkeletonsPath();
	void LoadBaseSkeletons();

	std::shared_ptr<Animation::OzzSkeleton> GetSkeleton(RE::Actor* a_actor);
	void LoadSkeletonForRace(RE::TESRace* a_race);  // ładowanie na żądanie z rasy aktora
	bool IsDefaultSkeleton(const std::shared_ptr<Animation::OzzSkeleton>& a_skele);

	/// Returns race skeleton model path (from TESRace::unk5E8) if any.
	std::optional<std::string> GetSkeletonModelPathFromRace(RE::TESRace* a_race);
	/// When set, GetSkeleton returns nullptr for actors whose race skeleton path contains any of these substrings (case-insensitive). Use for SFF Body Replacer / SF Extended Skeleton.
	void SetSkipSkeletonPathSubstrings(std::vector<std::string> a_substrings);
	/// When true, actors with path containing "extended" or "sff" are not skipped (SAF animations play on SFF/extended bodies even if they are in the skip list).
	void SetAllowAnimationsOnExtendedSkeleton(bool a_allow);
	/// Set by plugin when game is safe (player in world, post-load). When false, GetSkeleton returns nullptr for extended/SFF paths to avoid load crash.
	void SetSafeToUseExtendedSkeleton(bool a_safe);

	// Face Morphs
	void SetFaceMorphs(const std::vector<std::string>& a_morphs);
	const std::map<std::string, size_t>& GetFaceMorphIndexMap();
	const std::vector<std::string>& GetFaceMorphs();
}