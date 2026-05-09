#pragma once

#include "PCH.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace RE
{
	class NiNode;
	class NiAVObject;
	struct NiPoint3;
	struct NiQuaternion;
	class Actor;
}

namespace Animation
{
	struct CustomBoneData
	{
		std::string boneName;
		RE::NiPoint3 position;
		RE::NiQuaternion rotation;
		float scale;
		
		CustomBoneData() = default;
		CustomBoneData(const std::string& name, const RE::NiPoint3& pos = {0,0,0}, 
			const RE::NiQuaternion& rot = {0,0,0,1}, float scl = 1.0f)
			: boneName(name), position(pos), rotation(rot), scale(scl) {}
	};

	class NIFBoneManager
	{
	private:
		static std::unordered_map<std::string, CustomBoneData> s_customBones;
		static std::unordered_map<std::uintptr_t, bool> s_modifiedSkeletons;
		
		// Sprawdza czy kość już istnieje w szkielecie
		static bool BoneExists(RE::NiNode* skeleton, const std::string& boneName);
		
		// Znajduje node po nazwie (rekurencyjnie)
		static RE::NiAVObject* FindNodeRecursive(RE::NiNode* node, const std::string& name);
		
		// Tworzy nowy NiNode dla kości
		static RE::NiNode* CreateCustomBone(const CustomBoneData& boneData);
		
		// Dołącza kość do hierarchii
		static bool AttachBoneToSkeleton(RE::NiNode* skeleton, RE::NiNode* newBone, 
			const std::string& parentBoneName = "");

	public:
		// Rejestracja niestandardowej kości
		static void RegisterCustomBone(const std::string& boneName, 
			const RE::NiPoint3& position = {0,0,0}, 
			const RE::NiQuaternion& rotation = {0,0,0,1}, 
			float scale = 1.0f);
		
		// Dodaje niestandardowe kości do szkieletu aktora
		static void AddCustomBonesToActor(RE::Actor* actor);
		
		// Sprawdza czy aktor ma już dodane kości
		static bool HasCustomBones(RE::Actor* actor);
		
		// Zwraca listę zarejestrowanych kości
		static std::vector<std::string> GetRegisteredBones();
		
		// Czyści rejestr kości
		static void ClearCustomBones();
		
		// Inicjalizacja domyślnych kości penisa (C_Penis_07+)
		static void InitializeDefaultPenisBones();
	};
}
