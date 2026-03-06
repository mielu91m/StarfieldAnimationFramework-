#include "PCH.h"
#include "Settings/Settings.h"
#include "Animation/GraphManager.h"
#include "simdjson.h"
#include "Util/String.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiAVObject.h"
#include "RE/ModelDB.h"
#include "RE/T/TESDataHandler.h"
#include <REX/FModule.h>
#include "RE/T/TESRace.h"
#include "RE/F/FormTypes.h"
#include "ozz/base/maths/simd_math.h"

#ifdef _WIN32
#	include <excpt.h>
#endif

namespace Settings
{
	// Opakowanie ModelDB::GetEntry w SEH. ID z INI; gdy ModelDBDecRefRVA ustawione – drugie ID niepotrzebne.
	static RE::ModelDB::Entry* SafeGetModelDBEntry(const char* filename)
	{
		const std::uint64_t id1 = Animation::GraphManager::GetModelDBGetEntryID();
		const std::uint32_t decRefRva = Animation::GraphManager::GetModelDBDecRefRVA();
#ifdef _MSC_VER
		__try {
			if (decRefRva != 0) {
				std::uintptr_t base = REX::FModule::GetExecutingModule().GetBaseAddress();
				return RE::ModelDB::GetEntryWithDecRefRVA(filename, id1, decRefRva, base);
			}
			const std::uint64_t id2 = Animation::GraphManager::GetModelDBDecRefID();
			return RE::ModelDB::GetEntry(filename, id1, id2);
		}
		__except (GetExceptionCode() == 0xC0000005 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
			return nullptr;  // EXCEPTION_ACCESS_VIOLATION – wrong ID/RVA for this game version
		}
#else
		if (decRefRva != 0) {
			std::uintptr_t base = REX::FModule::GetExecutingModule().GetBaseAddress();
			return RE::ModelDB::GetEntryWithDecRefRVA(filename, id1, decRefRva, base);
		}
		return RE::ModelDB::GetEntry(filename, id1, Animation::GraphManager::GetModelDBDecRefID());
#endif
	}
	// Ścieżka NIF szkieletu rasy (z TESRace::unk5E8). Zwraca std::nullopt jeśli rasy nie ma lub model jest pusty.
	// Próbuje: dokładne raceName, potem raceName + "Race" (np. Human -> HumanRace).
	static std::optional<std::string> GetSkeletonModelPath(const std::string_view raceName)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			SAF_LOG_DEBUG("GetSkeletonModelPath: TESDataHandler null");
			return std::nullopt;
		}

		const auto formType = std::to_underlying(RE::FormType::kRACE);
		auto& formArray = dh->formArrays[formType];
		const auto arrSize = formArray.formArray.size();
		if (!formArray.formArray.data() || arrSize == 0) {
			SAF_LOG_DEBUG("GetSkeletonModelPath: RACE form array empty (size={})", arrSize);
			return std::nullopt;
		}

		// Kolejno próbuj: dokładna nazwa, potem z sufiksem "Race"
		std::array<std::string_view, 2> toTry = { raceName, {} };
		std::string raceWithSuffix;
		if (raceName.size() < 4 || raceName.substr(raceName.size() - 4) != "Race"sv) {
			raceWithSuffix = std::string(raceName) + "Race";
			toTry[1] = raceWithSuffix;
		}

		for (std::string_view lookup : toTry) {
			if (lookup.empty()) continue;
			for (auto& form : formArray.formArray) {
				auto* race = form ? form->As<RE::TESRace>() : nullptr;
				if (!race)
					continue;
				if (race->formEditorID != lookup)
					continue;
				for (std::size_t i = 0; i < 4; ++i) {
					auto& model = race->unk5E8[i];
					if (model.model.empty())
						continue;
					std::string path(model.model.c_str());
					SAF_LOG_INFO("GetSkeletonModelPath: race '{}' (lookup '{}') -> '{}'", raceName, lookup, path);
					return path;
				}
				SAF_LOG_WARN("GetSkeletonModelPath: race '{}' found but all unk5E8 models empty", lookup);
				return std::nullopt;
			}
		}
		// Pomoc diagnostyczna: rozmiar tablicy, ile form rzutuje się na TESRace, i pierwsze Editor ID
		{
			int raceCount = 0;
			std::string list;
			const int maxLog = 20;
			for (auto& form : formArray.formArray) {
				auto* race = form ? form->As<RE::TESRace>() : nullptr;
				if (!race) continue;
				++raceCount;
				if (static_cast<int>(list.size()) < 400) {  // limit długości
					if (!list.empty()) list += ", ";
					list += race->formEditorID.c_str();
				}
				if (raceCount >= maxLog) break;
			}
			SAF_LOG_WARN("GetSkeletonModelPath: no race matching '{}'. RACE array size={}, TESRace count={}. EditorIDs: [{}]",
				raceName, arrSize, raceCount, list.empty() ? "(none)" : list);
		}
		return std::nullopt;
	}

	// Pobiera ścieżkę NIF bezpośrednio z TESRace (gdy mamy wskaźnik na rasę z aktora).
	std::optional<std::string> GetSkeletonModelPathFromRace(RE::TESRace* a_race)
	{
		if (!a_race) return std::nullopt;
		for (std::size_t i = 0; i < 4; ++i) {
			auto& model = a_race->unk5E8[i];
			if (model.model.empty()) continue;
			return std::string(model.model.c_str());
		}
		return std::nullopt;
	}

	// Zwraca root NIF szkieletu rasy przez ModelDB::GetEntry (NAF: ID 183072 + 36741). nullptr gdy brak/crash.
	static RE::NiNode* GetSkeletonModel(const std::string_view raceName)
	{
		auto pathOpt = GetSkeletonModelPath(raceName);
		if (!pathOpt || pathOpt->empty())
			return nullptr;
		auto* entry = SafeGetModelDBEntry(pathOpt->c_str());
		return entry && entry->node ? entry->node : nullptr;
	}

	// Zwraca root NIF dla rasy (ścieżka z a_race->unk5E8) przez ModelDB::GetEntry. nullptr gdy brak/crash.
	static RE::NiNode* GetSkeletonModelFromRace(RE::TESRace* a_race)
	{
		auto pathOpt = GetSkeletonModelPathFromRace(a_race);
		if (!pathOpt || pathOpt->empty())
			return nullptr;
		auto* entry = SafeGetModelDBEntry(pathOpt->c_str());
		return entry && entry->node ? entry->node : nullptr;
	}

	bool FillInSkeletonNIFDataFromRace(SkeletonDescriptor& a_desc, RE::TESRace* a_race)
	{
		if (!Animation::GraphManager::IsModelDBForRestPoseEnabled()) {
			return false;  // UseModelDBForRestPose=0 – wyłączone w INI
		}
		RE::NiNode* m = GetSkeletonModelFromRace(a_race);
		if (!m) {
			SAF_LOG_WARN("FillInSkeletonNIFDataFromRace: ModelDB::GetEntry failed or no path in race, leaving restPose from JSON");
			return false;
		}
		for (auto& bone : a_desc.bones) {
			RE::NiAVObject* gameNode = m->GetObjectByName(RE::BSFixedString(bone.name.c_str()));
			if (!gameNode && !bone.name.empty()) {
				std::string npcName = "NPC " + bone.name;
				gameNode = m->GetObjectByName(RE::BSFixedString(npcName.c_str()));
			}
			if (!gameNode) continue;
			if (gameNode->parent) bone.parent = gameNode->parent->name.c_str();
			RE::NiQuaternion rotQuat(gameNode->local.rotate);
			bone.restPose.rotation.x = rotQuat.x;
			bone.restPose.rotation.y = rotQuat.y;
			bone.restPose.rotation.z = rotQuat.z;
			bone.restPose.rotation.w = rotQuat.w;
			bone.restPose.translation.x = gameNode->local.translate.x;
			bone.restPose.translation.y = gameNode->local.translate.y;
			bone.restPose.translation.z = gameNode->local.translate.z;
		}
		SAF_LOG_INFO("FillInSkeletonNIFDataFromRace: filled restPose for {} bones from race NIF", a_desc.bones.size());
		return true;
	}

	bool FillInSkeletonNIFData(SkeletonDescriptor& a_desc, const std::string_view raceName)
	{
		if (!Animation::GraphManager::IsModelDBForRestPoseEnabled()) {
			return false;  // UseModelDBForRestPose=0 – wyłączone w INI
		}
		RE::NiNode* m = GetSkeletonModel(raceName);
		if (!m) {
			SAF_LOG_WARN("FillInSkeletonNIFData: no skeleton model for race '{}' (ModelDB::GetEntry failed or no path), leaving restPose from JSON", raceName);
			return false;
		}
		for (auto& bone : a_desc.bones) {
			RE::NiAVObject* gameNode = m->GetObjectByName(RE::BSFixedString(bone.name.c_str()));
			if (!gameNode && !bone.name.empty()) {
				std::string npcName = "NPC " + bone.name;
				gameNode = m->GetObjectByName(RE::BSFixedString(npcName.c_str()));
			}
			if (!gameNode)
				continue;

			if (gameNode->parent) {
				bone.parent = gameNode->parent->name.c_str();
			}

			RE::NiQuaternion rotQuat(gameNode->local.rotate);
			bone.restPose.rotation.x = rotQuat.x;
			bone.restPose.rotation.y = rotQuat.y;
			bone.restPose.rotation.z = rotQuat.z;
			bone.restPose.rotation.w = rotQuat.w;

			bone.restPose.translation.x = gameNode->local.translate.x;
			bone.restPose.translation.y = gameNode->local.translate.y;
			bone.restPose.translation.z = gameNode->local.translate.z;
		}

		SAF_LOG_INFO("FillInSkeletonNIFData: filled restPose for {} bones of race '{}'", a_desc.bones.size(), raceName);
		return true;
	}

	void LoadBaseSkeletons()
	{
		simdjson::ondemand::parser parser;
		auto& skeletons = Settings::GetSkeletonMap();
		auto path = Settings::GetSkeletonsPath();

		if (!std::filesystem::exists(path)) {
			SAF_LOG_WARN("Skeletons directory does not exist: {}", path.string());
			return;
		}

		for (auto& f : std::filesystem::directory_iterator(path)) {
			if (auto p = f.path(); f.exists() && !f.is_directory() && p.extension() == ".json") {
				Settings::SkeletonDescriptor skele;
				try {
					auto res = simdjson::padded_string::load(p.string());
					if (res.error() != simdjson::error_code::SUCCESS) {
						SAF_LOG_ERROR("Failed to load skeleton file: {}", p.string());
						continue;
					}

					simdjson::ondemand::document doc = parser.iterate(res.value());
					auto nodes = doc["nodes"].get_array();

					for (auto n : nodes) {
						if (n.type().value() == simdjson::fallback::ondemand::json_type::string) {
							skele.AddBone(n.get_string().value(), "", ozz::math::Transform::identity());
						} else {
							std::string_view name = n["name"].get_string();
							std::string_view parent = "";
							auto parentField = n["parent"];
							if (parentField.error() == simdjson::error_code::SUCCESS) {
								parent = parentField.get_string();
							}
							std::vector<std::string> aliases;
							auto aliasesField = n["aliases"];
							if (aliasesField.error() == simdjson::error_code::SUCCESS) {
								auto aliasesArray = aliasesField.get_array();
								for (auto a : aliasesArray) {
									if (a.type().value() == simdjson::fallback::ondemand::json_type::string) {
										aliases.emplace_back(a.get_string().value());
									}
								}
							}

							bool controlledByGame = false;
							auto field = n["gameControlled"];
							if (field.error() == simdjson::error_code::SUCCESS) {
								controlledByGame = field.get_bool();
							}

							skele.AddBone(name, parent, ozz::math::Transform::identity(), -1, true, controlledByGame, std::move(aliases));
						}
					}

					// Nie wywołuj ModelDB::GetEntry tutaj – w PostDataLoad preładowanie NIF rasy (np. vanilla) może spowodować, że gra użyje tego modelu dla gracza i korpus SFF w ogóle się nie pojawi. Rest pose z NIF uzupełniamy dopiero w LoadSkeletonForRace (na żądanie, gdy gracz jest już w świecie).
					const auto raceName = p.stem().string();

					skeletons[raceName] = skele.BuildRuntime(raceName);
					SAF_LOG_INFO("Loaded skeleton: {} (rest from NIF on first use via LoadSkeletonForRace)", raceName);

				} catch (const std::exception& e) {
					SAF_LOG_ERROR("Failed to parse skeleton {}: {}", p.string(), e.what());
				}
			}
		}
		
		SAF_LOG_INFO("Loaded {} skeleton(s) from JSON files", skeletons.size());
	}

	void LoadSkeletonForRace(RE::TESRace* a_race)
	{
		if (!a_race) return;
		auto& skeletons = Settings::GetSkeletonMap();
		const char* editorId = a_race->formEditorID.c_str();
		if (!editorId || !*editorId) return;
		std::string raceKey(editorId);
		static std::unordered_set<std::string> s_raceNifTried;
		if (s_raceNifTried.count(raceKey)) {
			SAF_LOG_DEBUG("LoadSkeletonForRace: race '{}' already tried, skipping", raceKey);
			return;  // już próbowano załadować z NIF dla tej rasy
		}
		SAF_LOG_INFO("LoadSkeletonForRace: loading skeleton for race '{}'", raceKey);

		auto dir = Settings::GetSkeletonsPath();
		auto jsonPath = dir / (raceKey + ".json");
		if (!std::filesystem::exists(jsonPath)) {
			// Fallback: Human -> HumanRace.json (gdy editor ID to "Human")
			if (raceKey.size() < 4 || raceKey.substr(raceKey.size() - 4) != "Race"sv) {
				jsonPath = dir / (raceKey + "Race.json");
			}
			if (!std::filesystem::exists(jsonPath)) {
				SAF_LOG_DEBUG("LoadSkeletonForRace: no JSON for race '{}'", raceKey);
				return;
			}
		}

		simdjson::ondemand::parser parser;
		SkeletonDescriptor skele;
		try {
			auto res = simdjson::padded_string::load(jsonPath.string());
			if (res.error() != simdjson::error_code::SUCCESS) {
				SAF_LOG_ERROR("LoadSkeletonForRace: failed to load {}", jsonPath.string());
				return;
			}
			simdjson::ondemand::document doc = parser.iterate(res.value());
			auto nodes = doc["nodes"].get_array();
			for (auto n : nodes) {
				if (n.type().value() == simdjson::fallback::ondemand::json_type::string) {
					skele.AddBone(n.get_string().value(), "", ozz::math::Transform::identity());
				} else {
					std::string_view name = n["name"].get_string();
					std::string_view parent = "";
					auto parentField = n["parent"];
					if (parentField.error() == simdjson::error_code::SUCCESS)
						parent = parentField.get_string();
					std::vector<std::string> aliases;
					auto aliasesField = n["aliases"];
					if (aliasesField.error() == simdjson::error_code::SUCCESS) {
						for (auto a : aliasesField.get_array()) {
							if (a.type().value() == simdjson::fallback::ondemand::json_type::string)
								aliases.emplace_back(a.get_string().value());
						}
					}
					bool controlledByGame = false;
					auto field = n["gameControlled"];
					if (field.error() == simdjson::error_code::SUCCESS)
						controlledByGame = field.get_bool();
					skele.AddBone(name, parent, ozz::math::Transform::identity(), -1, true, controlledByGame, std::move(aliases));
				}
			}
			FillInSkeletonNIFDataFromRace(skele, a_race);
			skeletons[raceKey] = skele.BuildRuntime(raceKey);
			s_raceNifTried.insert(raceKey);
			SAF_LOG_INFO("LoadSkeletonForRace: loaded '{}' with rest from race NIF", raceKey);
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("LoadSkeletonForRace: {} - {}", jsonPath.string(), e.what());
		}
	}
}