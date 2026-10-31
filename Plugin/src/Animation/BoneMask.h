#pragma once

// Named driven-bone masks for partial-body playback: a role with a mask drives ONLY the mask's
// rig bones, every other bone stays fully engine-driven. This is the layered "gesture" path the
// vanilla-style equip/unequip-while-moving look needs.
// Bone names are matched case-insensitively against live rig node names.

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Animation
{
	struct BoneMask
	{
		std::string id;  // canonical display id ("upperBody"); lookups are case-insensitive
		// lowercased rig bone name -> weight in (0, 1]
		std::unordered_map<std::string, float> weights;
		bool feathered = false;  // any weight < 1 (needs the live-base blend path)
	};

	namespace BoneMaskRegistry
	{
		// Get mask by name (case-insensitive), nullptr if not found
		const BoneMask* GetMask(std::string_view a_name);

		// Get all available masks
		const std::vector<BoneMask>& GetAllMasks();

		// Get comma-separated list of known mask names for diagnostics
		std::string GetKnownMaskList();

		// Register default masks (upperBody, arms, hands, lowerBody, etc.)
		void RegisterDefaultMasks();
	}
}
