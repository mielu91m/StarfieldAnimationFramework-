#include "BoneMask.h"
#include "Util/String.h"
#include <algorithm>

namespace Animation
{
	namespace
	{
		std::string ToLower(std::string_view a_text)
		{
			std::string out{ a_text };
			for (auto& c : out) {
				if (c >= 'A' && c <= 'Z') {
					c = static_cast<char>(c - 'A' + 'a');
				}
			}
			return out;
		}

		void AddBone(BoneMask& a_mask, std::string_view a_bone, float a_weight = 1.0f)
		{
			a_mask.weights.emplace(ToLower(a_bone), a_weight);
			if (a_weight < 1.0f) {
				a_mask.feathered = true;
			}
		}

		// Wrist-down: fingers, cup, and the in-hand prop helpers.
		void AddHand(BoneMask& a_mask, std::string_view a_side)
		{
			static constexpr std::string_view kHand[] = {
				"wrist", "thumb", "thumb1", "thumb2", "cup",
				"pinky", "pinky1", "pinky2", "ring", "ring1", "ring2",
				"middle", "middle1", "middle2", "index", "index1", "index2",
				"animobject1", "animobject2", "animobject3"
			};
			for (const auto part : kHand) {
				AddBone(a_mask, std::string(a_side) + "_" + std::string(part));
			}
		}

		// Clavicle-down: the full arm chain including twists, helper, and hand.
		void AddArm(BoneMask& a_mask, std::string_view a_side)
		{
			static constexpr std::string_view kArm[] = {
				"clavicle", "biceps", "biceps_twist", "biceps_twist1",
				"forearm", "arm", "wrist_twist", "wrist_twist1", "wrist_twist2"
			};
			for (const auto part : kArm) {
				AddBone(a_mask, std::string(a_side) + "_" + std::string(part));
			}
			AddHand(a_mask, a_side);
		}

		void AddLeg(BoneMask& a_mask, std::string_view a_side)
		{
			static constexpr std::string_view kLeg[] = {
				"thigh", "thigh_twist", "thigh_twist1", "calf", "foot", "toe"
			};
			for (const auto part : kLeg) {
				AddBone(a_mask, std::string(a_side) + "_" + std::string(part));
			}
		}

		std::vector<BoneMask> BuildDefaultMasks()
		{
			std::vector<BoneMask> masks;

			// Chest-up + both arms at full weight over a feathered spine seam
			{
				BoneMask& m = masks.emplace_back();
				m.id = "upperBody";
				AddBone(m, "c_spine", 0.25f);
				AddBone(m, "c_spine1", 0.5f);
				AddBone(m, "c_spine2", 0.75f);
				AddBone(m, "c_chest");
				AddBone(m, "c_neck");
				AddBone(m, "c_neck1");
				AddBone(m, "c_neck_twist");
				AddBone(m, "c_head");
				AddBone(m, "weapon");
				AddBone(m, "weaponleft");
				AddBone(m, "l_handik");
				AddBone(m, "r_handik");
				AddArm(m, "l");
				AddArm(m, "r");
			}
			{
				BoneMask& m = masks.emplace_back();
				m.id = "arms";
				AddBone(m, "weapon");
				AddBone(m, "weaponleft");
				AddBone(m, "l_handik");
				AddBone(m, "r_handik");
				AddArm(m, "l");
				AddArm(m, "r");
			}
			{
				BoneMask& m = masks.emplace_back();
				m.id = "leftArm";
				AddBone(m, "weaponleft");
				AddBone(m, "l_handik");
				AddArm(m, "l");
			}
			{
				BoneMask& m = masks.emplace_back();
				m.id = "rightArm";
				AddBone(m, "weapon");
				AddBone(m, "r_handik");
				AddArm(m, "r");
			}
			{
				BoneMask& m = masks.emplace_back();
				m.id = "lowerBody";
				AddBone(m, "c_hips");
				AddBone(m, "c_waist");
				AddLeg(m, "l");
				AddLeg(m, "r");
			}
			{
				BoneMask& m = masks.emplace_back();
				m.id = "hands";
				AddHand(m, "l");
				AddHand(m, "r");
			}
			return masks;
		}
	}

	namespace BoneMaskRegistry
	{
		const BoneMask* GetMask(std::string_view a_name)
		{
			const auto lower = ToLower(a_name);
			for (const auto& mask : GetAllMasks()) {
				if (ToLower(mask.id) == lower) {
					return &mask;
				}
			}
			return nullptr;
		}

		const std::vector<BoneMask>& GetAllMasks()
		{
			static const std::vector<BoneMask> masks = BuildDefaultMasks();
			return masks;
		}

		std::string GetKnownMaskList()
		{
			std::string out;
			for (const auto& mask : GetAllMasks()) {
				if (!out.empty()) {
					out += ", ";
				}
				out += mask.id;
			}
			return out;
		}

		void RegisterDefaultMasks()
		{
			// Masks are statically built in GetAllMasks(), so this is mainly for initialization/logging
			const auto& masks = GetAllMasks();
			// Could log initialization here if needed
		}
	}
}
