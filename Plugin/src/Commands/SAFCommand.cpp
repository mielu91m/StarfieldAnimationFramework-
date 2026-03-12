#include "PCH.h"
#include "SAFCommand.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESNPC.h"
#include "RE/T/TESForm.h"
#include "RE/U/UIMessageQueue.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiAVObject.h"
#include "Serialization/GLTFImport.h"
#include "Serialization/GLTFExport.h"
#include "Settings/Settings.h"
#include "Animation/GraphManager.h"
#include "Animation/Ozz.h"
#include "Util/OzzUtil.h"
#include "Util/String.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/skeleton_utils.h"
#include "zstr.hpp"
#include "Tasks/Input.h"
#include "SFSE/API.h"
#include "REL/Offset2ID.h"
#include <Windows.h>
#include <fstream>
#include <queue>
#include <mutex>
#include <thread>
#include <sstream>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <set>
#include <filesystem>

namespace Commands::SAFCommand
{
	MCF::ConsoleInterface* itfc;
	MCF::simple_array<MCF::simple_string_view> args;
	const char* fullStr = nullptr;
	RE::Actor* lastActor = nullptr;
	std::filesystem::path lastFile;

	// Kolejka odroczonych zleceń – wykonywana na głównym wątku
	struct PendingPlay
	{
		std::string path;   // np. "R1" lub "Data/SAF/Animations/MyAnim.saf"
		std::string actorId;  // pusty = player, inaczej FormID jako string
		int animIndex = 0;   // który klip w pliku (0-based)
	};
	static std::queue<PendingPlay> g_pendingPlay;
	static std::mutex g_pendingMutex;
	static std::atomic<bool> g_hasPendingCommands{ false };  // Flaga atomowa dla szybkiego sprawdzenia
	static std::atomic<bool> g_processRequested{ false };
	static std::atomic<bool> g_closeConsoleRequested{ false };

	struct PendingDump
	{
		std::string actorId;
	};
	static std::queue<PendingDump> g_pendingDump;
	static std::mutex g_dumpMutex;
	static std::atomic<bool> g_hasPendingDump{ false };
	static std::atomic<bool> g_dumpRequested{ false };
	static std::unordered_map<std::string, std::string> g_animOverrideMap;
	static std::mutex g_animOverrideMutex;
	static std::unordered_map<std::string, std::string> g_aliasToKey;
	static std::atomic<bool> g_aliasesInitialized{ false };
	static std::atomic<bool> g_overridesLoaded{ false };

	// (playscene soft lock removed – left only speed control)

	static std::string NormalizeKey(std::string_view a_key)
	{
		return Util::String::ToLower(a_key);
	}

	static std::string Trim(std::string_view a_text)
	{
		size_t start = 0;
		while (start < a_text.size() && std::isspace(static_cast<unsigned char>(a_text[start]))) {
			start++;
		}
		size_t end = a_text.size();
		while (end > start && std::isspace(static_cast<unsigned char>(a_text[end - 1]))) {
			end--;
		}
		return std::string(a_text.substr(start, end - start));
	}

	static std::filesystem::path ResolveAnimationPathWithFallback(std::string_view a_path)
	{
		auto resolved = Util::String::ResolveAnimationPath(a_path);
		if (resolved.has_extension()) {
			return resolved;
		}

		auto glb = resolved;
		glb += ".glb";
		if (std::filesystem::exists(glb)) {
			return glb;
		}

		auto saf = resolved;
		saf += ".saf";
		if (std::filesystem::exists(saf)) {
			return saf;
		}

		auto gltf = resolved;
		gltf += ".gltf";
		if (std::filesystem::exists(gltf)) {
			return gltf;
		}

		// Prosta nazwa (bez / i \) – szukaj po stem w podfolderach (np. cow1 -> mb/cow1.glb)
		const bool simpleName = (a_path.find('/') == std::string_view::npos && a_path.find('\\') == std::string_view::npos);
		if (simpleName && !a_path.empty()) {
			auto found = Util::String::FindAnimationByStem(a_path);
			if (found && std::filesystem::exists(*found)) {
				return *found;
			}
		}

		return resolved;
	}

	static void InitDefaultAliases()
	{
		if (g_aliasesInitialized.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		// Predefined aliases for common animation groups.
		g_aliasToKey.emplace("idle", "idle");
		g_aliasToKey.emplace("idledefault", "idle");
		g_aliasToKey.emplace("idle_default", "idle");
		g_aliasToKey.emplace("idle01", "idle");

		g_aliasToKey.emplace("walk", "walk");
		g_aliasToKey.emplace("walkforward", "walk");
		g_aliasToKey.emplace("walkfwd", "walk");

		g_aliasToKey.emplace("run", "run");
		g_aliasToKey.emplace("runforward", "run");
		g_aliasToKey.emplace("runfwd", "run");

		g_aliasToKey.emplace("sprint", "sprint");
		g_aliasToKey.emplace("sprintforward", "sprint");
		g_aliasToKey.emplace("sprintfwd", "sprint");

		g_aliasToKey.emplace("turnleft", "turn_left");
		g_aliasToKey.emplace("turnright", "turn_right");
	}

	static std::string ResolveOverridePath(const std::string& a_path)
	{
		const auto key = NormalizeKey(a_path);
		std::lock_guard<std::mutex> lock(g_animOverrideMutex);
		auto it = g_animOverrideMap.find(key);
		if (it != g_animOverrideMap.end()) {
			return it->second;
		}
		auto aliasIt = g_aliasToKey.find(key);
		if (aliasIt != g_aliasToKey.end()) {
			auto canonical = aliasIt->second;
			auto canonicalIt = g_animOverrideMap.find(canonical);
			if (canonicalIt != g_animOverrideMap.end()) {
				return canonicalIt->second;
			}
		}
		return a_path;
	}

	static void SetAnimOverride(const std::string& a_key, const std::string& a_value)
	{
		const auto key = NormalizeKey(a_key);
		std::lock_guard<std::mutex> lock(g_animOverrideMutex);
		g_animOverrideMap[key] = a_value;
		SAF_LOG_INFO("[OVERRIDE] '{}' -> '{}'", key, a_value);
	}

	static void ClearAnimOverride(const std::string& a_key)
	{
		const auto key = NormalizeKey(a_key);
		std::lock_guard<std::mutex> lock(g_animOverrideMutex);
		if (key.empty()) {
			g_animOverrideMap.clear();
			SAF_LOG_INFO("[OVERRIDE] cleared all");
			return;
		}
		auto erased = g_animOverrideMap.erase(key);
		SAF_LOG_INFO("[OVERRIDE] remove '{}' -> {}", key, erased ? "ok" : "not found");
	}

	static void ListAnimOverrides()
	{
		std::lock_guard<std::mutex> lock(g_animOverrideMutex);
		if (g_animOverrideMap.empty()) {
			SAF_LOG_INFO("[OVERRIDE] list: empty");
			return;
		}
		SAF_LOG_INFO("[OVERRIDE] list: {} entries", g_animOverrideMap.size());
		for (const auto& [key, value] : g_animOverrideMap) {
			SAF_LOG_INFO("[OVERRIDE] '{}' -> '{}'", key, value);
		}
	}

	static void ListAnimAliases()
	{
		if (g_aliasToKey.empty()) {
			SAF_LOG_INFO("[OVERRIDE] alias list: empty");
			return;
		}
		SAF_LOG_INFO("[OVERRIDE] alias list: {} entries", g_aliasToKey.size());
		for (const auto& [alias, key] : g_aliasToKey) {
			SAF_LOG_INFO("[OVERRIDE] alias '{}' -> '{}'", alias, key);
		}
	}

	static void LoadOverridesFromIni()
	{
		if (g_overridesLoaded.exchange(true, std::memory_order_acq_rel)) {
			return;
		}

		const auto iniPath = Util::String::GetDataPath() / "SAF" / "Overrides.ini";
		std::ifstream file(iniPath);
		if (!file.is_open()) {
			SAF_LOG_INFO("[OVERRIDE] no overrides file at '{}'", iniPath.string());
			return;
		}

		std::string line;
		while (std::getline(file, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			const auto pos = line.find('=');
			if (pos == std::string::npos) {
				continue;
			}
			auto key = Trim(std::string_view(line).substr(0, pos));
			auto value = Trim(std::string_view(line).substr(pos + 1));
			if (!key.empty() && !value.empty()) {
				SetAnimOverride(key, value);
			}
		}
	}

	// Forward declaration (używane w zadaniu)
	void SetLastAnimInfo(std::string_view fileOrFullPath, RE::Actor* actor);
	void RequestProcessPending();

	// Zamknięcie konsoli – wywoływać TYLKO z głównego wątku (AddMessage nie jest thread-safe z wątku konsoli).
	static void QueueCloseConsole()
	{
		SAF_LOG_INFO("[UI] QueueCloseConsole: hide console");
		if (auto* ui = RE::UIMessageQueue::GetSingleton()) {
			ui->AddMessage(RE::BSFixedString("Console"), RE::UI_MESSAGE_TYPE::kHide);
		}
	}

	static void QueueProcessPending()
	{
		SAF_LOG_INFO("[QUEUE] QueueProcessPending: scheduling main-thread request task");
		if (auto* taskInterface = SFSE::GetTaskInterface()) {
			taskInterface->AddTask([]() {
				SAF_LOG_INFO("[TASK] RequestProcessPending: ENTRY");
				RequestProcessPending();
				RequestCloseConsole();  // ustaw flagę; główny wątek wykona QueueCloseConsole()
				SAF_LOG_INFO("[TASK] RequestProcessPending: EXIT");
			});
			return;
		}
		SAF_LOG_WARN("[QUEUE] QueueProcessPending: TaskInterface not available, requesting directly");
		RequestProcessPending();
	}


	// Gdy domyślny cel to gracz: jeśli pod celownikiem jest Actor (NPC), zwróć go; inaczej gracza.
	// Wywoływać tylko z głównego wątku (odczyt crosshairRef).
	static RE::Actor* GetPlayerOrCrosshairActor()
	{
		RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
		if (!player) return nullptr;
		RE::TESObjectREFR* ref = player->crosshairRef;
		if (ref && ref->IsActor())
			return static_cast<RE::Actor*>(ref);
		return player;
	}

	// Parsuje ref ID (decimal lub 0xhex – np. po kliknięciu NPC w konsoli). Zwraca nullopt przy błędzie.
	static std::optional<RE::TESFormID> ParseRefID(const std::string& a_str)
	{
		if (a_str.empty()) return std::nullopt;
		try {
			unsigned long id = std::stoul(a_str, nullptr, 0);  // 0 = auto (decimal lub 0xhex)
			return static_cast<RE::TESFormID>(id & 0xFFFFFFFFu);
		} catch (...) {
			return std::nullopt;
		}
	}

	// Aktualnie wybrane odniesienie w konsoli (prid / klik na obiekt). Wersja: 1.15.222.
	// W try/catch – przy innej wersji gry lub zmodyfikowanej konsoli REL::ID mogą być nieaktualne.
	static RE::TESObjectREFR* GetConsoleReference()
	{
		try {
			static REL::Relocation<std::uint32_t*> handleReloc{ REL::ID(949615) };
			if (auto handle = *handleReloc; handle) {
				struct FormStruct {
					std::uintptr_t lambdaPTR;
					RE::TESForm*** formHolder;
				};
				using typeM = std::uintptr_t;
				using typeF = bool(typeM, std::uint32_t, FormStruct*);
				static REL::Relocation<typeM*> manager{ REL::ID(883285) };
				static REL::Relocation<typeF*> function{ REL::ID(139363) };
				static auto lambdaAddress = REL::ID(392398).address();
				RE::TESForm* result = nullptr;
				auto resultHolder = std::addressof(result);
				FormStruct argument{ lambdaAddress, &resultHolder };
				function(*manager, handle, &argument);
				if (result) {
					switch (result->GetFormType()) {
					case RE::FormType::kREFR:
					case RE::FormType::kACHR:
						return reinterpret_cast<RE::TESObjectREFR*>(result);
					default:
						break;
					}
				}
			}
		} catch (const std::exception& e) {
			SAF_LOG_WARN("[SAF] GetConsoleReference exception: {}", e.what());
		} catch (...) {
			SAF_LOG_WARN("[SAF] GetConsoleReference unknown exception");
		}
		return nullptr;
	}

	// Zwraca aktora po ref ID – jak NAF: HexStrToForm (ID w formacie konsoli), GetSelectedReference gdy brak ID.
	// Dla "player"/"0"/"14": najpierw ref wybrana w konsoli (prid / klik), potem crosshair lub gracz.
	static RE::Actor* GetActorByRefID(const std::string& a_actorId)
	{
		if (a_actorId.empty() || a_actorId == "player" || a_actorId == "0" || a_actorId == "14") {
			return GetPlayerOrCrosshairActor();
		}

		// NAF: jawny ref ID – rozwiązywanie przez HexStrToForm (format taki jak w konsoli).
		if (itfc) {
			try {
				MCF::simple_string_view sv(a_actorId.data(), a_actorId.size());
				RE::TESForm* form = itfc->HexStrToForm(sv);
				if (form) {
					RE::Actor* actor = form->As<RE::Actor>();
					if (actor) return actor;
				}
			} catch (const std::exception& e) {
				SAF_LOG_WARN("[SAF] HexStrToForm exception: {}", e.what());
			} catch (...) {
				SAF_LOG_WARN("[SAF] HexStrToForm unknown exception");
			}
		}

		// Fallback: surowy FormID (często nie znajdzie refów z konsoli w Starfieldzie).
		auto fid = ParseRefID(a_actorId);
		if (!fid) return nullptr;
		return RE::TESForm::LookupByID<RE::Actor>(*fid);
	}

	// Wykonaj komendę play na głównym wątku przez SFSE TaskInterface
	static void QueuePlayTask(std::string path, std::string actorId)
	{
		SAF_LOG_INFO("[TASK] QueuePlayTask: START - path='{}', actorId='{}'", path, actorId);
		const auto* taskInterface = SFSE::GetTaskInterface();
		if (!taskInterface) {
			SAF_LOG_ERROR("[TASK] QueuePlayTask: SFSE TaskInterface not available");
			return;
		}

		taskInterface->AddTask([path = std::move(path), actorId = std::move(actorId)]() mutable {
			std::stringstream ss;
			ss << std::this_thread::get_id();
			SAF_LOG_INFO("[TASK] ExecutePlayTask: ENTRY (thread_id={})", ss.str());

			RE::Actor* actor = GetActorByRefID(actorId);
			if (actorId.empty() || actorId == "player" || actorId == "0" || actorId == "14")
				SAF_LOG_INFO("[TASK] ExecutePlayTask: player/crosshair actor={}", static_cast<void*>(actor));
			else
				SAF_LOG_INFO("[TASK] ExecutePlayTask: refID '{}' -> actor={}", actorId, static_cast<void*>(actor));

			if (!actor) {
				SAF_LOG_WARN("[TASK] ExecutePlayTask: actor not found, skipping command");
				return;
			}

			SAF_LOG_INFO("[TASK] ExecutePlayTask: resolving path for '{}'", path);
			auto resolvedPath = ResolveAnimationPathWithFallback(path);
			std::string pathStr = resolvedPath.string();
			SAF_LOG_INFO("[TASK] ExecutePlayTask: resolved path='{}'", pathStr);

			SAF_LOG_INFO("[TASK] ExecutePlayTask: calling GraphManager::GetSingleton()");
			auto* mgr = Animation::GraphManager::GetSingleton();
			SAF_LOG_INFO("[TASK] ExecutePlayTask: GraphManager={}", static_cast<void*>(mgr));

			SAF_LOG_INFO("[TASK] ExecutePlayTask: calling LoadAndStartAnimation(actor={}, path='{}')", static_cast<void*>(actor), pathStr);
			(void)mgr->LoadAndStartAnimation(actor, pathStr);
			SAF_LOG_INFO("[TASK] ExecutePlayTask: LoadAndStartAnimation returned");

			SAF_LOG_INFO("[TASK] ExecutePlayTask: calling SetLastAnimInfo");
			SetLastAnimInfo(pathStr, actor);
			SAF_LOG_INFO("[TASK] ExecutePlayTask: SetLastAnimInfo returned");

			// NIE zamykaj konsoli z wątku taska – UIMessageQueue z worker thread może powodować crash.
			// Konsolę użytkownik zamyka ręcznie (np. ~).

			SAF_LOG_INFO("[TASK] ExecutePlayTask: EXIT");
		});

		SAF_LOG_INFO("[TASK] QueuePlayTask: DONE - task queued");
	}

	// Wykonaj komendę playscene na głównym wątku:
	// saf playscene <refId1> <refId2> <file1> <file2> [speed]
	// speed – opcjonalne, np. 1.0 (x1), 2.0 (x2). Gdy brak, używana jest wartość z INI (PlaySceneSpeed).
	static void QueuePlaySceneTask(
		std::string actorId1,
		std::string actorId2,
		std::string file1,
		std::string file2,
		std::optional<float> optSpeed)
	{
		SAF_LOG_INFO("[TASK] QueuePlaySceneTask: START - a1='{}', a2='{}', f1='{}', f2='{}', speedOverride={}",
			actorId1, actorId2, file1, file2, optSpeed ? std::to_string(*optSpeed) : "INI");

		const auto* taskInterface = SFSE::GetTaskInterface();
		if (!taskInterface) {
			SAF_LOG_ERROR("[TASK] QueuePlaySceneTask: SFSE TaskInterface not available");
			return;
		}

		taskInterface->AddTask(
			[actorId1 = std::move(actorId1),
			 actorId2 = std::move(actorId2),
			 file1 = std::move(file1),
			 file2 = std::move(file2),
			 optSpeed]() mutable {
				std::stringstream ss;
				ss << std::this_thread::get_id();
				SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: ENTRY (thread_id={})", ss.str());

				RE::Actor* a1 = GetActorByRefID(actorId1);
				RE::Actor* a2 = GetActorByRefID(actorId2);

				SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: a1Id='{}' -> {}", actorId1, static_cast<void*>(a1));
				SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: a2Id='{}' -> {}", actorId2, static_cast<void*>(a2));

				if (!a1 || !a2) {
					SAF_LOG_WARN("[TASK] ExecutePlaySceneTask: missing actor(s), aborting");
					return;
				}

				auto* mgr = Animation::GraphManager::GetSingleton();
				if (!mgr) {
					SAF_LOG_ERROR("[TASK] ExecutePlaySceneTask: GraphManager null");
					return;
				}

				// Alias/override -> klucz, potem rozwiąż ścieżkę jak w komendzie play
				auto key1 = ResolveOverridePath(file1);
				auto key2 = ResolveOverridePath(file2);

				SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: resolving paths '{}' / '{}'", key1, key2);
				const auto path1 = ResolveAnimationPathWithFallback(key1);
				const auto path2 = ResolveAnimationPathWithFallback(key2);
				const std::string pathStr1 = path1.string();
				const std::string pathStr2 = path2.string();
				SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: resolved paths '{}' / '{}'", pathStr1, pathStr2);

				// Aktor 1 zostaje w miejscu. Aktor 2 dostaje pozycję aktora 1 – odtwarzają w jednym miejscu.
				RE::NiPoint3 pos1 = a1->GetPosition();
				a2->SetPosition(pos1, false);
				SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: a2 moved to a1 position ({}, {}, {})", pos1.x, pos1.y, pos1.z);

				try {
					bool ok1 = mgr->LoadAndStartAnimation(a1, pathStr1, true, 0);
					bool ok2 = mgr->LoadAndStartAnimation(a2, pathStr2, true, 0);
					SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: LoadAndStartAnimation a1={} a2={} (ok1={}, ok2={})",
						static_cast<void*>(a1), static_cast<void*>(a2), ok1, ok2);

					// Nie blokujemy pozycji (SetGraphControlsPosition/SetActorPosition) – powodowało to crash w UpdateGraphs.
					// Aktor 2 jest już ustawiony na pozycję aktora 1 wyżej; animacje grają bez locku.

					// Prędkość: najpierw parametr z komendy (jeśli podany), inaczej INI (PlaySceneSpeed).
					float speed = optSpeed.has_value()
						? std::clamp(*optSpeed, 0.1f, 10.0f)
						: Animation::GraphManager::GetPlaySceneSpeed();
					if (ok1) mgr->SetAnimationSpeed(a1, speed);
					if (ok2) mgr->SetAnimationSpeed(a2, speed);
					SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: speed={}", speed);
				} catch (const std::exception& e) {
					SAF_LOG_ERROR("[TASK] ExecutePlaySceneTask: exception {}", e.what());
				} catch (...) {
					SAF_LOG_ERROR("[TASK] ExecutePlaySceneTask: unknown exception");
				}

				mgr->RequestGraphUpdate();

				// Zamknij konsolę natychmiast z głównego wątku dla playscene
				// (analogicznie do CloseConsoleMainThread wywoływanego z Input hooka).
				CloseConsoleMainThread();
				SAF_LOG_INFO("[TASK] ExecutePlaySceneTask: EXIT");
			});
	}

	// Helper do konwersji typów MCF na std::string_view (data/size są członkami, nie metodami)
	inline std::string_view ToSV(const MCF::simple_string_view& sv) {
		if (!sv.data || sv.size == 0) return std::string_view();
		return std::string_view(sv.data, sv.size);
	}

	// Bezpieczne wyjście do konsoli – unika crashy przy zmodyfikowanym MCF/INI lub .bat
	static void SafePrintLn(MCF::ConsoleInterface* a_intfc, const char* a_txt)
	{
		if (!a_txt) return;
		if (!a_intfc) return;
		try {
			a_intfc->PrintLn(MCF::simple_string_view(a_txt, std::strlen(a_txt)));
		} catch (const std::exception& e) {
			SAF_LOG_WARN("[SAF] SafePrintLn exception: {}", e.what());
		} catch (...) {
			SAF_LOG_WARN("[SAF] SafePrintLn unknown exception");
		}
	}
	static void SafePrintLn(MCF::ConsoleInterface* a_intfc, const std::string& a_txt)
	{
		if (a_txt.empty() || !a_intfc) return;
		try {
			a_intfc->PrintLn(MCF::simple_string_view(a_txt.c_str(), a_txt.size()));
		} catch (const std::exception& e) {
			SAF_LOG_WARN("[SAF] SafePrintLn exception: {}", e.what());
		} catch (...) {
			SAF_LOG_WARN("[SAF] SafePrintLn unknown exception");
		}
	}

	static void ListAnimations()
	{
		std::set<std::string> names;
		try {
			const auto dir = Util::String::GetAnimationsPath();
			if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
				if (itfc) SafePrintLn(itfc, "SAF: Animations folder not found: " + dir.string());
				return;
			}
			for (const auto& e : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied)) {
				if (!e.is_regular_file()) continue;
				std::string ext = e.path().extension().string();
				if (ext.size() >= 2) {
					for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (ext == ".glb" || ext == ".gltf" || ext == ".saf") {
						auto rel = std::filesystem::relative(e.path(), dir);
						rel.replace_extension("");
						names.insert(rel.generic_string());
					}
				}
			}
		} catch (const std::exception& ex) {
			if (itfc) SafePrintLn(itfc, std::string("SAF: Error listing animations: ") + ex.what());
			return;
		}
		if (itfc) {
			SafePrintLn(itfc, "SAF animations (Data/SAF/Animations and subfolders):");
			if (names.empty())
				SafePrintLn(itfc, "  (none)");
			else
				for (const auto& n : names)
					SafePrintLn(itfc, "  " + n);
		}
	}

	static void ValidateAnimationPath(const std::string& pathKey)
	{
		if (!itfc) return;
		auto resolvedPath = ResolveAnimationPathWithFallback(pathKey);
		if (!std::filesystem::exists(resolvedPath)) {
			SafePrintLn(itfc, "SAF validate: file not found: " + resolvedPath.string());
			return;
		}
		auto asset = Serialization::GLTFImport::LoadGLTF(resolvedPath);
		if (!asset) {
			SafePrintLn(itfc, "SAF validate: failed to load GLTF: " + resolvedPath.string());
			return;
		}
		RE::Actor* player = RE::PlayerCharacter::GetSingleton();
		auto skeleton = Settings::GetSkeleton(player);
		if (!skeleton || !skeleton->GetRawSkeleton()) {
			SafePrintLn(itfc, "SAF validate: no skeleton (player race).");
			return;
		}
		std::map<std::string, size_t> skeletonMap;
		for (size_t i = 0; i < skeleton->jointNames.size(); i++)
			skeletonMap[skeleton->jointNames[i]] = i;
		const auto& boneAliases = Settings::GetGLTFBoneAliases();
		size_t matched = 0;
		std::vector<std::string> missing;
		for (size_t i = 0; i < asset->asset.nodes.size(); i++) {
			std::string nodeName(asset->asset.nodes[i].name);
			if (auto it = asset->originalNames.find(i); it != asset->originalNames.end())
				nodeName = it->second;
			std::string lookupName = nodeName;
			auto it = skeletonMap.find(lookupName);
			if (it == skeletonMap.end() && !boneAliases.empty()) {
				auto ait = boneAliases.find(nodeName);
				if (ait != boneAliases.end()) lookupName = ait->second;
				it = skeletonMap.find(lookupName);
			}
			if (it != skeletonMap.end())
				matched++;
			else
				missing.push_back(nodeName);
		}
		SafePrintLn(itfc, "SAF validate: " + resolvedPath.filename().string());
		SafePrintLn(itfc, "  Skeleton joints: " + std::to_string(skeleton->jointNames.size()) + ", GLTF nodes: " + std::to_string(asset->asset.nodes.size()));
		SafePrintLn(itfc, "  Matched: " + std::to_string(matched));
		if (!missing.empty()) {
			SafePrintLn(itfc, "  Missing (add to GLTFBoneAliases.ini or re-export): " + std::to_string(missing.size()));
			for (const auto& m : missing)
				SafePrintLn(itfc, "    " + m);
		}
		if (asset->asset.animations.empty())
			SafePrintLn(itfc, "  Warning: no animations in file.");
		else
			SafePrintLn(itfc, "  Animations in file: " + std::to_string(asset->asset.animations.size()));
	}

	// Maks. liczba komend przetwarzanych w jednej klatce – zapobiega zawieszce przy .bat / wielu komendach
	static constexpr size_t kMaxPendingCommandsPerFrame = 5;

	static void CollectNodesIterative(RE::NiAVObject* a_root, std::vector<RE::NiAVObject*>& a_out)
	{
		if (!a_root) {
			return;
		}
		constexpr size_t kMaxNodes = 20000;
		std::vector<RE::NiAVObject*> stack;
		stack.reserve(512);
		stack.push_back(a_root);
		while (!stack.empty()) {
			auto* current = stack.back();
			stack.pop_back();
			if (!current) {
				continue;
			}
			a_out.push_back(current);
			if (a_out.size() >= kMaxNodes) {
				SAF_LOG_WARN("[DUMP] CollectNodesIterative: node limit reached ({}), stopping", kMaxNodes);
				break;
			}
			if (auto* node = current->GetAsNiNode()) {
				for (auto& child : node->children) {
					if (child) {
						stack.push_back(child.get());
					}
				}
			}
		}
	}

	void SetLastAnimInfo(std::string_view fileOrFullPath, RE::Actor* actor)
	{
		lastActor = actor;
		lastFile = std::filesystem::path(fileOrFullPath);
	}

	void ShowHelp()
	{
		if (!itfc) return;
		SafePrintLn(itfc, "SAF (Starfield Animation Framework) Commands:");
		SafePrintLn(itfc, "saf play <path> [actorID ...] - Odtwarza animację. Jedna osoba: saf play tw lub saf play tw <refID>.");
		SafePrintLn(itfc, "  Wiele osób (ta sama animacja): saf play tw player 0x123 0x456 (gracz + NPC + NPC).");
		SafePrintLn(itfc, "saf stop [actorID ...] - Zatrzymuje animację SAF. Bez argumentu: celownik/gracz. Wiele: saf stop player 0x123.");
		SafePrintLn(itfc, "saf list - Lists animation names in Data/SAF/Animations (.glb/.gltf/.saf).");
		SafePrintLn(itfc, "saf speed [actorID] <value> - Set playback speed (default: crosshair/player).");
		SafePrintLn(itfc, "saf loop [actorID] 0|1 - Set looping off/on (default: crosshair/player).");
		SafePrintLn(itfc, "saf validate <path> - Check GLB/GLTF bone names vs SAF skeleton (report missing).");
		SafePrintLn(itfc, "saf optimize <file> [compression_level] - Optimizes GLTF to SAF");
		SafePrintLn(itfc, "saf dumpbones [actorID] - Dumps actor 3D bone hierarchy to Data/SAF/Skeletons");
		SafePrintLn(itfc, "saf offset2id <hex_rva> - Address Library: convert RVA (offset in exe) to ID for this game version. Example: saf offset2id 1A2B3C4D");
	}

	RE::Actor* StrToActor(std::string_view a_str, bool a_verbose = true)
	{
		std::string s(a_str);
		if (s.empty()) {
			if (a_verbose) SafePrintLn(itfc, "Error: No actor ID given");
			return nullptr;
		}
		RE::Actor* actor = GetActorByRefID(s);
		if (!actor && a_verbose) SafePrintLn(itfc, "Error: Actor not found (use ref ID in decimal or 0xhex, or aim at NPC and use no ID)");
		return actor;
	}

	void DoOptimize(const std::filesystem::path& a_in, const std::filesystem::path& a_out, uint8_t a_compression, const Animation::OzzSkeleton* a_skeleton, bool a_ignoreBones)
	{
		auto assetData = Serialization::GLTFImport::LoadGLTF(a_in);
		if (!assetData) {
			SafePrintLn(itfc, "Error: Failed to load GLTF asset");
			return;
		}

		if (assetData->asset.animations.empty()) {
			SafePrintLn(itfc, "Error: Asset has no animations");
			return;
		}

		const ozz::animation::Skeleton* skel = a_skeleton ? a_skeleton->GetRawSkeleton() : nullptr;
		if (!skel) {
			SafePrintLn(itfc, "Error: No skeleton for optimization");
			return;
		}
		auto rawAnim = Serialization::GLTFImport::CreateRawAnimation(
			assetData.get(),
			&assetData->asset.animations[0],
			skel,
			a_skeleton ? &a_skeleton->jointNames : nullptr);
		if (!rawAnim || !rawAnim->data) {
			SafePrintLn(itfc, "Error: Failed to create raw animation");
			return;
		}

		auto bytes = Serialization::GLTFExport::CreateOptimizedAsset(rawAnim.get(), skel, a_compression);
		if (bytes.empty()) {
			SafePrintLn(itfc, "Error: Failed to create optimized asset");
			return;
		}
		std::ofstream ofs(a_out, std::ios::binary);
		if (ofs)
			ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SafePrintLn(itfc, "Optimization successful.");
	}

	// Dodaje zlecenie play do kolejki – BEZ wywołań RE (bezpieczne z wątku konsoli)
	void PushPendingPlay(std::string path, std::string actorId, int animIndex = 0)
	{
		SAF_LOG_INFO("[QUEUE] PushPendingPlay: START - path='{}', actorId='{}', animIndex={}", path, actorId, animIndex);
		std::lock_guard<std::mutex> lock(g_pendingMutex);
		g_pendingPlay.push(PendingPlay{ std::move(path), std::move(actorId), animIndex });
		g_hasPendingCommands.store(true, std::memory_order_release);
	}

	void ProcessPlayCommand(uint64_t idxStart = 1, bool verbose = true)
	{
		SAF_LOG_INFO("[CMD] ProcessPlayCommand: START - idxStart={}, args.size()={}", idxStart, args.size());
		for (size_t i = 0; i < args.size(); ++i) {
			std::string_view a = ToSV(args[i]);
			SAF_LOG_INFO("[CMD] ProcessPlayCommand: args[{}]='{}'", i, std::string(a));
		}

		if (args.size() < idxStart + 1) {
			SAF_LOG_WARN("[CMD] ProcessPlayCommand: Not enough args, returning");
			return;
		}

		std::string path(ToSV(args[idxStart]));
		std::vector<std::string> actorIds;
		int animIndex = 0;
		size_t actorStart = idxStart + 1;

		if (args.size() > idxStart + 1) {
			// Opcjonalnie pierwszy argument to indeks animacji w pliku (0–9), np. saf play kiss 1 player = drugi klip.
			// Tylko 0–9 traktujemy jako indeks, żeby "14" (FormID gracza) nie było zjadane jako animIndex.
			auto firstArg = std::string(ToSV(args[idxStart + 1]));
			auto parsed = Util::String::StrToInt(firstArg);
			if (parsed.has_value() && *parsed >= 0 && *parsed <= 9) {
				animIndex = *parsed;
				actorStart = idxStart + 2;
			}
			for (size_t i = actorStart; i < args.size(); ++i) {
				actorIds.push_back(std::string(ToSV(args[i])));
			}
			// Gdy podano tylko indeks bez aktorów (saf play kiss 1) – rozwiąż jednego aktora jak poniżej
			if (actorIds.empty()) {
				RE::Actor* one = nullptr;
				auto* mgrMain = Animation::GraphManager::GetSingleton();
				if (mgrMain) {
					if (auto* consoleRef = GetConsoleReference(); consoleRef && consoleRef->IsActor())
						one = static_cast<RE::Actor*>(consoleRef);
				}
				if (!one && itfc && mgrMain) {
					try {
						RE::NiPointer<RE::TESObjectREFR> sel = itfc->GetSelectedReference();
						if (sel && sel->IsActor()) one = static_cast<RE::Actor*>(sel.get());
					} catch (...) {}
				}
				if (!one && mgrMain) one = GetPlayerOrCrosshairActor();
				if (one) {
					char buf[16];
					std::snprintf(buf, sizeof(buf), "%08X", one->GetFormID());
					actorIds.push_back(buf);
				} else {
					actorIds.push_back("player");
				}
			}
			SAF_LOG_INFO("[CMD] ProcessPlayCommand: path='{}', animIndex={}, {} actor IDs", path, animIndex, actorIds.size());
		} else {
			// Jedna osoba: brak argumentu lub "path refID" w jednym tokenie.
			std::string actorId;
			size_t lastSpace = path.rfind(' ');
			if (lastSpace != std::string::npos && lastSpace + 1 < path.size()) {
				std::string maybeRefId(path.substr(lastSpace + 1));
				std::string pathOnly(path.substr(0, lastSpace));
				if (ParseRefID(maybeRefId)) {
					path = std::move(pathOnly);
					actorId = std::move(maybeRefId);
					SAF_LOG_INFO("[CMD] ProcessPlayCommand: split from single arg -> path='{}', actorId='{}'", path, actorId);
				}
			}
			if (actorId.empty()) {
				// 1) Native console ref (prid / klik na obiekt w konsoli).
				// 2) MCF GetSelectedReference.
				// 3) Fallback: crosshairRef – celownik na NPC.
				auto* mgrMain = Animation::GraphManager::GetSingleton();
				RE::Actor* actorToPlay = nullptr;
				if (mgrMain) {
					RE::TESObjectREFR* consoleRef = GetConsoleReference();
					if (consoleRef && consoleRef->IsActor()) {
						actorToPlay = static_cast<RE::Actor*>(consoleRef);
						SAF_LOG_INFO("[CMD] ProcessPlayCommand: using console selected ref");
					}
				}
				if (!actorToPlay && itfc && mgrMain) {
					try {
						RE::NiPointer<RE::TESObjectREFR> sel = itfc->GetSelectedReference();
						if (sel && sel->IsActor()) {
							actorToPlay = static_cast<RE::Actor*>(sel.get());
							SAF_LOG_INFO("[CMD] ProcessPlayCommand: using MCF selected ref");
						}
					} catch (const std::exception& e) {
						SAF_LOG_WARN("[SAF] GetSelectedReference exception: {}", e.what());
					} catch (...) {
						SAF_LOG_WARN("[SAF] GetSelectedReference unknown exception");
					}
				}
				if (!actorToPlay && mgrMain) {
					RE::Actor* crosshairActor = GetPlayerOrCrosshairActor();
					if (crosshairActor) {
						RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
						if (crosshairActor != player && crosshairActor->IsActor()) {
							actorToPlay = crosshairActor;
							SAF_LOG_INFO("[CMD] ProcessPlayCommand: using crosshair ref (MCF selection null)");
						}
					}
				}
				if (actorToPlay && mgrMain) {
					char refIdBuf[16];
					std::snprintf(refIdBuf, sizeof(refIdBuf), "%08X", actorToPlay->GetFormID());
					actorId = refIdBuf;
					SAF_LOG_INFO("[CMD] ProcessPlayCommand: queuing play for ref ID {} (console/crosshair)", actorId);
				} else {
					actorId = "player";
					SAF_LOG_INFO("[CMD] ProcessPlayCommand: no selection and no crosshair NPC, using actorId='player'");
					SafePrintLn(itfc, "SAF: Odtwarzanie na graczu (brak zaznaczenia w konsoli). Użyj: saf play <anim> <refID> lub skieruj celownik na NPC.");
				}
			}
			actorIds.push_back(std::move(actorId));
		}

		// Kolejkuj jedno odtwarzanie na aktora (ta sama ścieżka, ten sam indeks klipu).
		for (std::string& aid : actorIds) {
			PushPendingPlay(path, aid, animIndex);
		}
		SAF_LOG_INFO("[CMD] ProcessPlayCommand: queued {} play(s) for path='{}'", actorIds.size(), path);

		if (itfc) {
			if (actorIds.size() > 1) {
				SafePrintLn(itfc, "SAF: Odtwarzanie na " + std::to_string(actorIds.size()) + " aktorów. Zamknij konsolę (~).");
			} else if (actorIds.empty() == false) {
				const std::string& aid = actorIds[0];
				const bool byRefId = aid != "player" && aid != "0" && aid != "14";
				if (!byRefId)
					SafePrintLn(itfc, "SAF: Odtwarzanie na celownik/gracz. Zamknij konsole (~). Dla NPC: saf play <anim> <refID>.");
				else
					SafePrintLn(itfc, "SAF: Odtwarzanie na ref ID " + aid + ". Zamknij konsolę (~).");
			}
		}

		QueueProcessPending();
		RequestCloseConsole();

		SAF_LOG_INFO("[CMD] ProcessPlayCommand: DONE");
	}

	void ProcessOptimizeCommand(uint64_t idxStart = 1)
	{
		if (args.size() < idxStart + 1) return;
		std::filesystem::path filePath = Util::String::GetDataPath() / ToSV(args[idxStart]);
		
		RE::Actor* player = RE::PlayerCharacter::GetSingleton();
		RE::TESNPC* npc = player ? player->GetNPC() : nullptr;
		RE::TESRace* race = npc ? npc->GetRace() : nullptr;
		const Animation::OzzSkeleton* skele = nullptr;
		if (race) {
			const char* editorId = race->GetFormEditorID();
			auto& map = Settings::GetSkeletonMap();
			auto it = map.find(editorId ? std::string(editorId) : std::string());
			if (it != map.end())
				skele = it->second.get();
		}

		if (args.size() < idxStart + 2) {
			DoOptimize(filePath, filePath, 0, skele, true);
		} else {
			auto arg2Int = Util::String::StrToInt(std::string(ToSV(args[idxStart + 1])));
			int compressLevel = arg2Int.has_value() ? std::clamp(arg2Int.value(), 0, 255) : 0;
			DoOptimize(filePath, filePath, static_cast<uint8_t>(compressLevel), skele, true);
		}
	}

	void ProcessStartSeqCommand(uint64_t idxStart = 1, bool verbose = true)
	{
		if (args.size() < idxStart + 4) return;
		auto actor = StrToActor(ToSV(args[idxStart]), verbose);
		if (!actor) return;

		std::vector<Animation::Sequencer::PhaseData> phases;
		for (size_t i = (idxStart + 2); (i + 2) < args.size(); i += 3) {
			auto& p = phases.emplace_back();
			// Konwersja na string dla FileID
			p.file = std::string(ToSV(args[i])); 
			p.loopCount = Util::String::StrToInt(std::string(ToSV(args[i + 1]))).value_or(0);
			p.transitionTime = Util::String::StrToFloat(std::string(ToSV(args[i + 2]))).value_or(1.0f);
		}
		Animation::GraphManager::GetSingleton()->StartSequence(actor, std::move(phases), false);
	}

	// Maks. liczba argumentów – zabezpieczenie przed błędnym/zmodyfikowanym parserem (INI, .bat)
	static constexpr uint64_t kMaxArgsSize = 128;

	void Run(const MCF::simple_array<MCF::simple_string_view>& a_args, const char* a_fullString, MCF::ConsoleInterface* a_intfc)
	{
		try {
			InitDefaultAliases();
			LoadOverridesFromIni();
		} catch (const std::exception& e) {
			SAF_LOG_WARN("[SAF] Run init exception: {}", e.what());
		} catch (...) {
			SAF_LOG_WARN("[SAF] Run init unknown exception");
		}

		// Walidacja wejścia – unikaj crashy przy zmodyfikowanym MCF/INI lub innych modach
		if (!a_args.data || a_args.size() > kMaxArgsSize) {
			SAF_LOG_WARN("[SAF] Run: invalid args (data={}, size={}), ignoring", static_cast<void*>(a_args.data), a_args.size());
			return;
		}

		SAF_LOG_INFO("[MCF] Run: ENTRY - args.size()={}, fullString='{}'", a_args.size(), a_fullString ? a_fullString : "(null)");
		itfc = a_intfc;
		args = a_args;
		fullStr = a_fullString;
		SAF_LOG_INFO("[MCF] Run: variables set, itfc={}", static_cast<void*>(itfc));

		if (args.size() < 1) {
			SAF_LOG_INFO("[MCF] Run: no args, showing help");
			if (itfc) ShowHelp();
			RequestCloseConsole();
			SAF_LOG_INFO("[MCF] Run: EXIT (help shown)");
			return;
		}

		try {
			std::string_view cmd = ToSV(args[0]);
			SAF_LOG_INFO("[MCF] Run: command='{}'", cmd);

			// Komendy synchroniczne (help) - mogą używać itfc
			if (cmd == "help") {
				SAF_LOG_INFO("[MCF] Run: handling 'help' command");
				if (itfc) ShowHelp();
				RequestCloseConsole();
				SAF_LOG_INFO("[MCF] Run: EXIT (help)");
				return;
			}

			// Komendy asynchroniczne (play) - tylko dodaj do kolejki, NIE używaj itfc
			if (cmd == "play") {
				SAF_LOG_INFO("[MCF] Run: handling 'play' command");
				ProcessPlayCommand();
				SAF_LOG_INFO("[MCF] Run: EXIT (play queued)");
				return;
			}

			// saf playscene <refId1> <refId2> <file1> <file2> [speed]
			// speed – opcjonalne, np. 1.0 (x1), 2.0 (x2); gdy brak, używana jest wartość z INI (PlaySceneSpeed).
			if (cmd == "playscene") {
				SAF_LOG_INFO("[MCF] Run: handling 'playscene' command");
				if (args.size() < 5) {
					SAF_LOG_WARN("[MCF] playscene: requires 4 arguments: refId1 refId2 file1 file2 [speed]");
					if (itfc) {
						SafePrintLn(itfc, "Usage: saf playscene <refId1> <refId2> <file1> <file2> [speed]");
					}
					return;
				}

				std::string a1(ToSV(args[1]));
				std::string a2(ToSV(args[2]));
				std::string f1(ToSV(args[3]));
				std::string f2(ToSV(args[4]));

				std::optional<float> speed;
				if (args.size() >= 6) {
					auto speedOpt = Util::String::StrToFloat(std::string(ToSV(args[5])));
					if (!speedOpt) {
						SAF_LOG_WARN("[MCF] playscene: invalid speed value '{}'", std::string(ToSV(args[5])));
						if (itfc) {
							SafePrintLn(itfc, "SAF: playscene - invalid speed. Use a number like 1.0 or 2.0.");
						}
						return;
					}
					speed = *speedOpt;
				}

				QueuePlaySceneTask(a1, a2, f1, f2, speed);

				if (itfc) {
					SafePrintLn(itfc, "SAF: playscene queued. Close console (~) to see effect.");
				}
				RequestCloseConsole();
				SAF_LOG_INFO("[MCF] Run: EXIT (playscene queued)");
				return;
			}

			if (cmd == "stop") {
			std::vector<RE::Actor*> toStop;
			if (args.size() > 1) {
				// Jawna lista aktorów: saf stop player 0x123 0x456
				for (size_t i = 1; i < args.size(); ++i) {
					RE::Actor* a = StrToActor(ToSV(args[i]), itfc != nullptr);
					if (a) toStop.push_back(a);
				}
			} else {
				// Brak argumentu: jak wcześniej – console ref / MCF selected / crosshair lub gracz.
				RE::Actor* actor = nullptr;
				auto* mgrMain = Animation::GraphManager::GetSingleton();
				if (mgrMain) {
					if (auto* consoleRef = GetConsoleReference(); consoleRef && consoleRef->IsActor()) {
						actor = static_cast<RE::Actor*>(consoleRef);
						SAF_LOG_INFO("[CMD] stop: using console selected ref");
					}
				}
				if (!actor && itfc && mgrMain) {
					try {
						RE::NiPointer<RE::TESObjectREFR> sel = itfc->GetSelectedReference();
						if (sel && sel->IsActor()) {
							actor = static_cast<RE::Actor*>(sel.get());
							SAF_LOG_INFO("[CMD] stop: using MCF selected ref");
						}
					} catch (const std::exception& e) {
						SAF_LOG_WARN("[SAF] stop GetSelectedReference exception: {}", e.what());
					} catch (...) {
						SAF_LOG_WARN("[SAF] stop GetSelectedReference unknown exception");
					}
				}
				if (!actor) {
					actor = GetPlayerOrCrosshairActor();
					if (actor) SAF_LOG_INFO("[CMD] stop: using player/crosshair actor");
				}
				if (actor) toStop.push_back(actor);
			}

			if (auto* mgr = Animation::GraphManager::GetSingleton()) {
				for (RE::Actor* a : toStop)
					mgr->StopAnimation(a);
				if (!toStop.empty() && itfc)
					SafePrintLn(itfc, toStop.size() > 1 ? "Animation stopped on " + std::to_string(toStop.size()) + " actors." : "Animation stopped.");
			}

			RequestCloseConsole();
			return;
		}
		if (cmd == "override") {
			if (args.size() < 3) {
				SAF_LOG_WARN("[MCF] Run: override requires 2 args (key value)");
				return;
			}
			std::string key(ToSV(args[1]));
			std::string value(ToSV(args[2]));
			SetAnimOverride(key, value);
			RequestCloseConsole();
			return;
		}
		if (cmd == "clearoverride") {
			std::string key;
			if (args.size() > 1) {
				key = std::string(ToSV(args[1]));
			}
			ClearAnimOverride(key);
			RequestCloseConsole();
			return;
		}
		if (cmd == "listoverride") {
			ListAnimOverrides();
			RequestCloseConsole();
			return;
		}
		if (cmd == "listalias") {
			ListAnimAliases();
			RequestCloseConsole();
			return;
		}
		if (cmd == "list") {
			ListAnimations();
			RequestCloseConsole();
			return;
		}
		if (cmd == "speed") {
			RE::Actor* actor = nullptr;
			float value = 1.0f;
			if (args.size() >= 3) {
				actor = StrToActor(ToSV(args[1]), itfc != nullptr);
				auto v = Util::String::StrToFloat(std::string(ToSV(args[2])));
				if (v.has_value()) value = *v;
			} else if (args.size() == 2) {
				auto v = Util::String::StrToFloat(std::string(ToSV(args[1])));
				if (v.has_value()) { value = *v; actor = GetPlayerOrCrosshairActor(); }
				else { actor = StrToActor(ToSV(args[1]), itfc != nullptr); }
			} else {
				actor = GetPlayerOrCrosshairActor();
			}
			if (actor && Animation::GraphManager::GetSingleton()) {
				Animation::GraphManager::GetSingleton()->SetAnimationSpeed(actor, value);
				if (itfc) SafePrintLn(itfc, "SAF: Speed set to " + std::to_string(value) + ".");
			} else if (itfc) SafePrintLn(itfc, "SAF: No actor or no animation.");
			RequestCloseConsole();
			return;
		}
		if (cmd == "loop") {
			RE::Actor* actor = nullptr;
			bool loop = true;
			if (args.size() >= 3) {
				actor = StrToActor(ToSV(args[1]), itfc != nullptr);
				std::string v(ToSV(args[2]));
				loop = (v == "1" || v == "true" || v == "yes");
			} else if (args.size() == 2) {
				std::string v(ToSV(args[1]));
				if (v == "0" || v == "1" || v == "true" || v == "false" || v == "yes" || v == "no") {
					loop = (v == "1" || v == "true" || v == "yes");
					actor = GetPlayerOrCrosshairActor();
				} else {
					actor = StrToActor(ToSV(args[1]), itfc != nullptr);
				}
			} else {
				actor = GetPlayerOrCrosshairActor();
			}
			if (actor && Animation::GraphManager::GetSingleton()) {
				Animation::GraphManager::GetSingleton()->SetAnimationLooping(actor, loop);
				if (itfc) SafePrintLn(itfc, std::string("SAF: Loop ") + (loop ? "on." : "off."));
			} else if (itfc) SafePrintLn(itfc, "SAF: No actor or no animation.");
			RequestCloseConsole();
			return;
		}
		if (cmd == "validate") {
			if (args.size() < 2) {
				if (itfc) SafePrintLn(itfc, "Usage: saf validate <path>   (path = animation name in Data/SAF/Animations)");
				RequestCloseConsole();
				return;
			}
			ValidateAnimationPath(std::string(ToSV(args[1])));
			RequestCloseConsole();
			return;
		}
		if (cmd == "dumpbones") {
			SAF_LOG_INFO("[MCF] Run: handling 'dumpbones' command");
			std::string actorId;
			if (args.size() > 1) {
				actorId = std::string(ToSV(args[1]));
			} else {
				actorId = "player";
			}
			{
				std::lock_guard<std::mutex> lock(g_dumpMutex);
				g_pendingDump.push(PendingDump{ std::move(actorId) });
				g_hasPendingDump.store(true, std::memory_order_release);
			}
			g_dumpRequested.store(true, std::memory_order_release);
			SAF_LOG_INFO("[DUMP] dumpbones queued");
			RequestCloseConsole();
			SAF_LOG_INFO("[MCF] Run: EXIT (dumpbones queued)");
			return;
		}

		// Address Library: offset (RVA) → ID dla aktualnej wersji gry (np. 1.15.222)
		if (cmd == "offset2id") {
			if (args.size() < 2) {
				SafePrintLn(itfc, "Usage: saf offset2id <hex_rva>   (RVA = offset in Starfield.exe, from IDA)");
				RequestCloseConsole();
				return;
			}
			std::string hexStr(Trim(ToSV(args[1])));
			if (hexStr.size() >= 2 && (hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')))
				hexStr = hexStr.substr(2);
			std::size_t offset = 0;
			try {
				offset = std::stoull(hexStr, nullptr, 16);
			} catch (...) {
				SafePrintLn(itfc, "Error: invalid hex value");
				RequestCloseConsole();
				return;
			}
			try {
				auto* o2id = REL::Offset2ID::GetSingleton();
				o2id->load_v2();
				if (o2id->size() == 0) {
					o2id->load_v5();
				}
				std::uint64_t id = o2id->get_id(offset);
				std::string msg = std::format("Offset 0x{:X} -> Address Library ID: {}", offset, id);
				SafePrintLn(itfc, msg);
				SAF_LOG_INFO("offset2id: RVA 0x{:X} -> ID {}", offset, id);
			} catch (const std::exception& e) {
				std::string errMsg = std::string("Error: ") + e.what() + " (offset not in Address Library?)";
				SafePrintLn(itfc, errMsg);
				SAF_LOG_ERROR("offset2id failed: {}", e.what());
			}
			RequestCloseConsole();
			return;
		}

		// Inne komendy mogą używać itfc (optimize, startseq)
		if (cmd == "optimize") {
			SAF_LOG_INFO("[MCF] Run: handling 'optimize' command");
			ProcessOptimizeCommand();
			SAF_LOG_INFO("[MCF] Run: EXIT (optimize)");
		} else if (cmd == "startseq") {
			SAF_LOG_INFO("[MCF] Run: handling 'startseq' command");
			ProcessStartSeqCommand();
			SAF_LOG_INFO("[MCF] Run: EXIT (startseq)");
		} else {
			SAF_LOG_WARN("[MCF] Run: unknown command '{}'", cmd);
			SAF_LOG_INFO("[MCF] Run: EXIT (unknown command)");
		}
		RequestCloseConsole();
		} catch (const std::exception& e) {
			SAF_LOG_WARN("[SAF] Run command exception: {}", e.what());
		} catch (...) {
			SAF_LOG_WARN("[SAF] Run command unknown exception");
		}
	}

	void ProcessPendingCommands()
	{
		if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && !mgr->IsMainThread()) {
			static std::atomic<uint32_t> wrongThreadCount{ 0 };
			uint32_t count = ++wrongThreadCount;
			if (count <= 10 || count % 60 == 0) {
				SAF_LOG_WARN("[PROCESS] ProcessPendingCommands: running on non-main thread (call #{})", count);
			}
		}

		// Szybkie sprawdzenie atomowej flagi - jeśli false, nie ma zleceń, wyjdź szybko
		if (!g_hasPendingCommands.load(std::memory_order_acquire)) {
			g_processRequested.store(false, std::memory_order_release);
			static std::atomic<uint32_t> emptyCount{ 0 };
			uint32_t emptyNum = ++emptyCount;
			if (emptyNum <= 10 || emptyNum % 60 == 0) {
				SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: no pending flag set, returning (call #{})", emptyNum);
			}
			return;  // Brak zleceń, nie blokuj mutexa
		}

		static std::atomic<uint32_t> callCount{ 0 };
		uint32_t callNum = ++callCount;
		
		// Log ID wątku dla debugowania
		std::stringstream ss;
		ss << std::this_thread::get_id();
		std::string threadIdStr = ss.str();
		
		// Log pierwsze 10 wywołań, potem co 60
		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: ENTRY (call #{}, thread_id={})", callNum, threadIdStr);
		}
		
		std::queue<PendingPlay> batch;
		size_t queueSizeBefore = 0;
		{
			if (callNum <= 10 || callNum % 60 == 0) {
				SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: acquiring lock (call #{}, thread_id={})", callNum, threadIdStr);
			}
			std::lock_guard<std::mutex> lock(g_pendingMutex);
			queueSizeBefore = g_pendingPlay.size();
			// Ograniczenie na jedną klatkę – zapobiega zawieszce przy .bat / wielu komendach
			size_t toTake = (std::min)(g_pendingPlay.size(), kMaxPendingCommandsPerFrame);
			for (size_t i = 0; i < toTake && !g_pendingPlay.empty(); ++i) {
				batch.push(std::move(g_pendingPlay.front()));
				g_pendingPlay.pop();
			}
			if (g_pendingPlay.empty()) {
				g_hasPendingCommands.store(false, std::memory_order_release);
			}
			if (callNum <= 10 || callNum % 60 == 0 || batch.size() > 0) {
				SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: took {} from queue ({} left), batch size: {} (call #{})", toTake, g_pendingPlay.size(), batch.size(), callNum);
			}
		}
		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: lock released (call #{})", callNum);
		}

		if (batch.empty()) {
			if (callNum <= 10 || callNum % 60 == 0) {
				SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: no commands in local batch to process (call #{})", callNum);
			}
			return;
		}

		// Przy pierwszej komendzie przeładuj szkelety – przy PostDataLoad lista ras bywa pusta w Starfield
		static std::once_flag s_skeletonsReloadedForForms;
		std::call_once(s_skeletonsReloadedForForms, []() {
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: reloading skeletons (form list now available for ModelDB)");
			Settings::LoadBaseSkeletons();
		});

		size_t batchSize = batch.size();
		SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: processing {} commands from local batch (call #{})", batchSize, callNum);

		size_t processed = 0;
		while (!batch.empty()) {
			processed++;
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: processing command {}/{} from batch (call #{})", processed, batchSize, callNum);
			
			auto cmd = std::move(batch.front());
			batch.pop();
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: cmd.path='{}', cmd.actorId='{}'", cmd.path, cmd.actorId);

			RE::Actor* actor = GetActorByRefID(cmd.actorId);
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: actorId='{}' -> actor={}", cmd.actorId, static_cast<void*>(actor));

			if (!actor) {
				SAF_LOG_WARN("[PROCESS] ProcessPendingCommands: actor not found, skipping command");
				const bool byRefId = (cmd.actorId != "player" && cmd.actorId != "0" && cmd.actorId != "14");
				if (byRefId) {
					SafePrintLn(itfc, "SAF: Nie znaleziono aktora o ref ID " + cmd.actorId + ". Upewnij się, że ID jest poprawne (np. prid <id> w konsoli).");
				}
				continue;
			}

			auto resolvedKey = ResolveOverridePath(cmd.path);
			if (resolvedKey != cmd.path) {
				SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: override '{}' -> '{}'", cmd.path, resolvedKey);
			}
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: resolving path for '{}'", resolvedKey);
			auto resolvedPath = ResolveAnimationPathWithFallback(resolvedKey);
			std::string pathStr = resolvedPath.string();
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: resolved path='{}', animIndex={}", pathStr, cmd.animIndex);

			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: calling GraphManager::GetSingleton()");
			auto* mgr = Animation::GraphManager::GetSingleton();
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: GraphManager={}", static_cast<void*>(mgr));
			
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: calling LoadAndStartAnimation(actor={}, path='{}', animIndex={})", static_cast<void*>(actor), pathStr, cmd.animIndex);
			bool ok = false;
			try {
				ok = mgr->LoadAndStartAnimation(actor, pathStr, true, cmd.animIndex);
			} catch (const std::exception& e) {
				SAF_LOG_ERROR("[PROCESS] ProcessPendingCommands: exception {}", e.what());
			} catch (...) {
				SAF_LOG_ERROR("[PROCESS] ProcessPendingCommands: unknown exception");
			}
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: LoadAndStartAnimation returned");
			if (!ok && itfc) {
				const std::string& err = Animation::GraphManager::GetLastLoadError();
				if (!err.empty())
					SafePrintLn(itfc, "SAF: " + err);
			}
			if (mgr) {
				SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: requesting graph update");
				mgr->RequestGraphUpdate();
			}
			
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: calling SetLastAnimInfo");
			SetLastAnimInfo(pathStr, actor);
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: SetLastAnimInfo returned");
			
			SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: command {}/{} completed successfully", processed, batchSize);
		}
		
		// Wyczyść flagę jeśli wszystkie zlecenia zostały przetworzone
		g_hasPendingCommands.store(false, std::memory_order_release);
		g_processRequested.store(false, std::memory_order_release);
		
		SAF_LOG_INFO("[PROCESS] ProcessPendingCommands: EXIT - processed {}/{} commands", processed, batchSize);

		// Zamknij konsolę po wykonaniu komendy (jesteśmy na głównym wątku).
		if (processed > 0) {
			if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && mgr->IsMainThread()) {
				RequestCloseConsole();
			}
		}
	}

	bool HasPendingCommands()
	{
		return g_hasPendingCommands.load(std::memory_order_acquire);
	}

	void RequestProcessPending()
	{
		g_processRequested.store(true, std::memory_order_release);
		SAF_LOG_INFO("[PROCESS] RequestProcessPending: flag set");
	}

	void RequestCloseConsole()
	{
		g_closeConsoleRequested.store(true, std::memory_order_release);
		SAF_LOG_INFO("[UI] RequestCloseConsole: flag set");
	}

	bool ConsumeProcessRequest()
	{
		return g_processRequested.exchange(false, std::memory_order_acq_rel);
	}

	bool HasProcessRequest()
	{
		return g_processRequested.load(std::memory_order_acquire);
	}

	bool ConsumeCloseConsole()
	{
		return g_closeConsoleRequested.exchange(false, std::memory_order_acq_rel);
	}

	void CloseConsoleMainThread()
	{
		QueueCloseConsole();  // faktyczne zamknięcie konsoli (tylko z głównego wątku)
	}

	bool HasPendingDump()
	{
		return g_hasPendingDump.load(std::memory_order_acquire);
	}

	bool ConsumeDumpRequest()
	{
		return g_dumpRequested.exchange(false, std::memory_order_acq_rel);
	}

	void ClearPendingDump()
	{
		std::lock_guard<std::mutex> lock(g_dumpMutex);
		while (!g_pendingDump.empty()) {
			g_pendingDump.pop();
		}
		g_hasPendingDump.store(false, std::memory_order_release);
		g_dumpRequested.store(false, std::memory_order_release);
	}

	void ProcessPendingDump()
	{
		try {
			if (auto* mgr = Animation::GraphManager::GetSingleton(); mgr && !mgr->IsMainThread()) {
				SAF_LOG_WARN("[DUMP] ProcessPendingDump: called from non-main thread, deferring");
				g_dumpRequested.store(true, std::memory_order_release);
				return;
			}

			SAF_LOG_INFO("[DUMP] ProcessPendingDump: ENTRY");
			PendingDump job;
			{
				std::lock_guard<std::mutex> lock(g_dumpMutex);
				if (g_pendingDump.empty()) {
					g_hasPendingDump.store(false, std::memory_order_release);
					return;
				}
				job = std::move(g_pendingDump.front());
				g_pendingDump.pop();
				if (g_pendingDump.empty()) {
					g_hasPendingDump.store(false, std::memory_order_release);
				}
			}

			RE::Actor* actor = GetActorByRefID(job.actorId);

			if (!actor) {
				SAF_LOG_WARN("[DUMP] ProcessPendingDump: actor not found");
				return;
			}

			auto loaded = actor->loadedData.LockRead();
			auto loadedPtr = *loaded;
			auto* root = loadedPtr ? loadedPtr->data3D.get() : nullptr;
			if (!root) {
				SAF_LOG_WARN("[DUMP] ProcessPendingDump: actor has no 3D");
				return;
			}

			auto skeleton = Settings::GetSkeleton(actor);
			if (!skeleton) {
				SAF_LOG_WARN("[DUMP] ProcessPendingDump: skeleton not found for actor");
				return;
			}

			std::vector<std::pair<std::string, std::string>> entries;
			entries.reserve(skeleton->jointNames.size());
			size_t missing = 0;
			std::vector<std::string> missingNames;
			missingNames.reserve(20);
			for (const auto& jointName : skeleton->jointNames) {
				auto* node = root->GetObjectByName(jointName.c_str());
				if (!node) {
					++missing;
					if (missingNames.size() < 20) {
						missingNames.emplace_back(jointName);
					}
					continue;
				}
				std::string parentName;
				if (node->parent) {
					const char* p = node->parent->name.c_str();
					if (p && p[0]) {
						parentName = p;
					}
				}
				entries.emplace_back(jointName, std::move(parentName));
			}

			if (missing > 0) {
				std::string sample;
				for (size_t i = 0; i < missingNames.size(); ++i) {
					if (i > 0) {
						sample += ", ";
					}
					sample += missingNames[i];
				}
				SAF_LOG_WARN("[DUMP] ProcessPendingDump: missing {}/{} joints (first {}): {}",
					missing, skeleton->jointNames.size(), missingNames.size(), sample);
			}

			std::string raceName = "Dumped";
			if (auto* npc = actor->GetNPC()) {
				if (auto* race = npc->GetRace()) {
					if (const char* rid = race->GetFormEditorID(); rid && rid[0]) {
						raceName = rid;
					}
				}
			}

			auto outDir = Util::String::GetDataPath() / "SAF" / "Skeletons";
			std::error_code ec;
			std::filesystem::create_directories(outDir, ec);
			if (ec) {
				SAF_LOG_ERROR("[DUMP] ProcessPendingDump: failed to create {} ({})", outDir.string(), ec.message());
				return;
			}
			auto outPath = outDir / ("Dumped_" + raceName + ".json");
			std::ofstream ofs(outPath);
			if (!ofs) {
				SAF_LOG_ERROR("[DUMP] ProcessPendingDump: failed to open {}", outPath.string());
				return;
			}

			ofs << "{\n\t\"nodes\":[\n";
			for (size_t i = 0; i < entries.size(); ++i) {
				const auto& [name, parent] = entries[i];
				ofs << "\t\t{ \"name\": \"" << name << "\", \"parent\": \"" << parent << "\" }";
				if (i + 1 < entries.size()) {
					ofs << ",";
				}
				ofs << "\n";
			}
			ofs << "\t]\n}\n";

			SAF_LOG_INFO("[DUMP] ProcessPendingDump: wrote {} bones to {}", entries.size(), outPath.string());
			SAF_LOG_INFO("[DUMP] ProcessPendingDump: EXIT");
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("[DUMP] ProcessPendingDump: exception {}", e.what());
		} catch (...) {
			SAF_LOG_ERROR("[DUMP] ProcessPendingDump: unknown exception");
		}
	}

	void RegisterKeybinds()
	{
		auto input = Tasks::Input::GetSingleton();
		using ButtonCallback = Tasks::Input::ButtonCallback;

		input->RegisterForKey(Tasks::Input::BS_BUTTON_CODE::kDown, ButtonCallback([](Tasks::Input::BS_BUTTON_CODE a_key, bool a_down) {
			if (!a_down || !lastActor) return;
			// Logika scrollowania plików...
		}));

		input->RegisterForKey(Tasks::Input::BS_BUTTON_CODE::kUp, ButtonCallback([](Tasks::Input::BS_BUTTON_CODE a_key, bool a_down) {
			if (!a_down || !lastActor) return;
			// Logika scrollowania plików...
		}));
	}
}