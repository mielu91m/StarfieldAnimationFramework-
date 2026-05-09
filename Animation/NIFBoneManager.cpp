#include "PCH.h"
#include "Animation/NIFBoneManager.h"
#include "Animation/GraphManager.h"
#include "Util/String.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/A/Actor.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiTransform.h"
#include <cstdlib>
#include <cstring>

namespace Animation
{
	// Static members initialization
	std::unordered_map<std::string, CustomBoneData> NIFBoneManager::s_customBones;
	std::unordered_map<std::uintptr_t, bool> NIFBoneManager::s_modifiedSkeletons;

	bool NIFBoneManager::BoneExists(RE::NiNode* skeleton, const std::string& boneName)
	{
		if (!skeleton) return false;
		
		RE::NiAVObject* node = FindNodeRecursive(skeleton, boneName);
		return node != nullptr;
	}

	RE::NiAVObject* NIFBoneManager::FindNodeRecursive(RE::NiNode* node, const std::string& name)
	{
		if (!node) return nullptr;
		
		// Check current node - use c_str() directly
		const char* nodeNameStr = node->name.c_str();
		if (nodeNameStr && std::strlen(nodeNameStr) > 0 && std::string(nodeNameStr) == name) {
			return node;
		}
		
		// Check children recursively - use CommonLibSF NiNode::children
		for (auto& child : node->children) {
			if (child) {
				RE::NiAVObject* childObj = child.get();
				if (childObj) {
					RE::NiNode* childNode = childObj->GetAsNiNode();
					if (childNode) {
						RE::NiAVObject* result = FindNodeRecursive(childNode, name);
						if (result) return result;
					}
				}
			}
		}
		
		return nullptr;
	}

	RE::NiNode* NIFBoneManager::CreateCustomBone(const CustomBoneData& boneData)
	{
		// Allocate memory without calling constructor (to avoid NiCollisionObject dependency)
		void* memory = std::malloc(sizeof(RE::NiNode));
		if (!memory) return nullptr;
		
		std::memset(memory, 0, sizeof(RE::NiNode));
		
		// Cast to NiNode - we will manually initialize critical fields
		RE::NiNode* newBone = static_cast<RE::NiNode*>(memory);
		
		// Set vtable pointer (copy from an existing NiNode if possible)
		// For now, we skip this as it's platform and version specific
		
		// Set name using BSFixedString
		RE::BSFixedString boneName(boneData.boneName.c_str());
		newBone->name = boneName;
		
		// Set transform fields directly
		// Note: This assumes local and world are at standard offsets
		// Accessing these fields may require raw pointer arithmetic
		
		SAF_LOG_INFO("Created custom bone (raw allocation): {}", boneData.boneName);
		return newBone;
	}

	bool NIFBoneManager::AttachBoneToSkeleton(RE::NiNode* skeleton, RE::NiNode* newBone, const std::string& parentBoneName)
	{
		if (!skeleton || !newBone) return false;
		
		RE::NiNode* parentNode = nullptr;
		
		// Find parent node if specified
		if (!parentBoneName.empty()) {
			RE::NiAVObject* parentObj = FindNodeRecursive(skeleton, parentBoneName);
			if (parentObj) {
				parentNode = parentObj->GetAsNiNode();
			}
		}
		
		// If no parent found or specified, attach to skeleton root
		if (!parentNode) {
			parentNode = skeleton;
		}
		
		// Attach the new bone using CommonLibSF NiNode::children
		// Get bone names for logging
		const char* boneNameStr = newBone->name.c_str();
		const char* parentNameStr = parentNode->name.c_str();
		
		// Use virtual AddChild method from NiNode
		parentNode->AddChild(newBone);
		
		SAF_LOG_INFO("Attached bone '{}' to parent '{}'", 
			std::strlen(boneNameStr) > 0 ? boneNameStr : "unnamed", 
			std::strlen(parentNameStr) > 0 ? parentNameStr : "root");
		
		return true;
	}

	void NIFBoneManager::RegisterCustomBone(const std::string& boneName, 
		const RE::NiPoint3& position, 
		const RE::NiQuaternion& rotation, 
		float scale)
	{
		CustomBoneData boneData(boneName, position, rotation, scale);
		s_customBones[boneName] = boneData;
		
		SAF_LOG_INFO("Registered custom bone: {} at ({:.2f}, {:.2f}, {:.2f})", 
			boneName, position.x, position.y, position.z);
	}

	void NIFBoneManager::AddCustomBonesToActor(RE::Actor* actor)
	{
		// DISABLED: Kości C_Penis_07..15 działają przez transformacje matematyczne
		// transformacje C_Penis_* obsługuje GraphManager (NAF) – nie wymagają fizycznego dodawania do skeletonu
		// Poprzednia implementacja powodowała crash w FindNodeRecursive przy dostępie do node->children
		return;
		
		/* ORYGINALNY KOD - WYŁĄCZONY ZE WZGLĘDU NA CRASH
		if (!actor) return;
		
		// Get actor's 3D root - use GraphManager approach
		RE::NiAVObject* rootObj = GetActor3DRootRaw(actor);
		if (!rootObj) {
			SAF_LOG_WARN("Failed to get 3D root for actor {:X}", actor->GetFormID());
			return;
		}
		
		if (!rootObj) return;
		
		RE::NiNode* skeleton = rootObj->GetAsNiNode();
		if (!skeleton) return;
		
		// Check if we already modified this skeleton
		std::uintptr_t skeletonPtr = reinterpret_cast<std::uintptr_t>(skeleton);
		if (s_modifiedSkeletons[skeletonPtr]) {
			SAF_LOG_DEBUG("Skeleton already modified for actor {:X}", actor->GetFormID());
			return;
		}
		
		int addedBones = 0;
		
		// Add all registered custom bones
		for (const auto& [boneName, boneData] : s_customBones) {
			// Check if bone already exists
			if (BoneExists(skeleton, boneName)) {
				SAF_LOG_DEBUG("Bone '{}' already exists in skeleton", boneName);
				continue;
			}
			
			// Create and attach the bone
			RE::NiNode* newBone = CreateCustomBone(boneData);
			if (newBone) {
				// Try to attach to C_Penis_01 as parent for penis bones
				std::string parentName = "";
				if (boneName.find("C_Penis_") == 0) {
					parentName = "C_Penis_01"; // Attach all penis bones to main penis bone
				}
				
				if (AttachBoneToSkeleton(skeleton, newBone, parentName)) {
					addedBones++;
				}
			}
		}
		
		if (addedBones > 0) {
			s_modifiedSkeletons[skeletonPtr] = true;
			SAF_LOG_INFO("Added {} custom bones to actor {:X}", addedBones, actor->GetFormID());
		}
		*/
	}

	bool NIFBoneManager::HasCustomBones(RE::Actor* actor)
	{
		if (!actor) return false;

		// AddCustomBonesToActor is currently disabled, so we do not have a reliable
		// cross-module way to resolve actor 3D root here (GetActor3DRootRaw is local
		// to GraphManager.cpp). Keep this query conservative and non-crashing.
		return false;
	}

	std::vector<std::string> NIFBoneManager::GetRegisteredBones()
	{
		std::vector<std::string> bones;
		bones.reserve(s_customBones.size());
		
		for (const auto& [boneName, _] : s_customBones) {
			bones.push_back(boneName);
		}
		
		return bones;
	}

	void NIFBoneManager::ClearCustomBones()
	{
		s_customBones.clear();
		s_modifiedSkeletons.clear();
		SAF_LOG_INFO("Cleared all custom bones");
	}

	void NIFBoneManager::InitializeDefaultPenisBones()
	{
		// Dodatkowe kości C_Penis_07..15 usunięte na prośbę użytkownika
		// Kości te nie są potrzebne - wystarczające są C_Penis_01..06
		SAF_LOG_INFO("NIFBoneManager initialized (no additional penis bones)");
	}
}
