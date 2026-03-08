#include "PCH.h"
#include <array>
#include "Animation/GraphManager.h"
#include "Commands/SAFCommand.h"
#include "Papyrus/EventManager.h"
#include "Tasks/Input.h"
#include "Serialization/GLTFImport.h"
#include "Settings/Settings.h"
#include "Animation/ClipGenerator.h"
#include "Util/String.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiTransform.h"
#include "RE/N/NiUpdateData.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/B/BSAnimationGraph.h"
#include "RE/IDs_VTABLE.h"
#include "RE/E/Events.h"
#include "RE/IDs.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/I/IMenu.h"
#include "RE/B/BSTEvent.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UIMessageQueue.h"
#include "RE/U/UI.h"
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/base/maths/simd_math.h>
#include <SFSE/API.h>
#include <cstddef>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <REL/Relocation.h>
#include <REL/THook.h>
#include <REX/FModule.h>
#include <Windows.h>
#include <memory>
#include <vector>

namespace Animation
{
	static std::string s_lastLoadError;

	const std::string& GraphManager::GetLastLoadError()
	{
		return s_lastLoadError;
	}

	// ============================================================================
	// GraphUpdateHook - główny punkt wejścia logiki animacji (jak w NAF)
	// Hook podpięty do funkcji gry wywoływanej co klatkę - pozwala nadpisać
	// pozycję, obrót i skalę stawów szkieletu przed wysłaniem do GPU.
	//
	// UZALEŻNIENIE OD ADDRESS LIBRARY:
	// REL::ID to identyfikator z Address Library. CommonLibSF przy starcie ładuje
	// plik Address Library i zamienia ID na adres dla aktualnej wersji Starfield.exe.
	// Wystarczy, że gracz ma aktualną Address Library dla swojej wersji gry – nie
	// trzeba nic dodatkowo linkować; adres jest rozwiązywany w runtime.
	//
	// WERYFIKACJA ID W RE (reverse engineering):
	// 1. W IDA/Ghidra znajdź funkcję (np. caller BSAnimationGraphManager, wywołanie
	//    co klatkę). Zanotuj OFFSET od bazy .exe (RVA), np. 0xEF3CE0.
	// 2. Po załadowaniu gry Address Library jest załadowana. Aby dostać ID dla
	//    offsetu: REL::Offset2ID::GetSingleton()->load_v2() lub load_v5() (zależnie
	//    od wersji AL), potem get_id(offset) – wynik to numer do REL::ID(...).
	// 3. Alternatywnie: sprawdź listę/CSV Address Library dla twojej wersji gry.
	// ============================================================================

	// Address Library ID (dla wersji gry w xmake: SF_VERSION). Sprawdź w RE jeśli crash.
	// Przykłady: 73213 (sub_140EF3CE0), 73235 – zweryfikuj który pasuje do twojej AL.
	// Fallback ID gdy RE::ID::...::GetEventSource == 0 w CommonLibSF (numery z komentarzy w RE/IDs.h).
	namespace ID
	{
		inline constexpr REL::ID GraphUpdateHook{ 73213 };
		inline constexpr REL::ID UpdateSceneRectEventGetEventSource{ 187249 };  // fallback gdy RE::ID ma 0
		inline constexpr REL::ID PlayerUpdateEventGetEventSource{ 134924 };     // fallback gdy RE::ID ma 0
		inline constexpr REL::ID AnimGraphManagerCall{ 118488 };               // call wewnątrz IAnimationGraphManagerHolder::UpdateAnimationGraphManager (+0x61)
	}

	// Ustaw na false, żeby wyłączyć GraphUpdateHook (crash przy load / zawieszanie).
	// UWAGA: Hook jest używany do przetwarzania kolejki zleceń SAF - jeśli wyłączony,
	// komendy mogą nie działać gdy konsola jest otwarta (Input hook nie działa wtedy).
	inline constexpr bool g_enableGraphUpdateHook = true;

	static bool g_graphUpdateHookInstalled = false;
	static std::atomic<std::uint32_t> g_hookOverrideRva{ 0 };
static std::atomic<bool> g_hookOverrideLoaded{ false };
static std::atomic<bool> g_taskLoopEnabled{ false };
static std::atomic<bool> g_taskLoopStarted{ false };
static std::atomic<bool> g_taskLoopAssumeMainThread{ false };
static std::atomic<bool> g_taskLoopAllowDump{ false };
static std::atomic<bool> g_taskLoopAllowUpdateGraphs{ false };
static std::atomic<bool> g_playerUpdateEventEnabled{ true };
static std::atomic<std::uint32_t> g_updateSceneRectEventRva{ 0 };
static std::atomic<std::uint32_t> g_playerUpdateEventRva{ 0 };
static std::atomic<std::uint32_t> g_animGraphUpdateRva{ 0 };
static std::atomic<std::uint32_t> g_animGraphManagerCallRva{ 0 };
static std::atomic<bool> g_inputHookEnabled{ false };
static std::atomic<bool> g_cameraHookEnabled{ false };
static std::atomic<bool> g_menuHookEnabled{ true };
	/// Skip our logic for the first N hook calls after install, or while player not in world (avoids crash when loading save). 0 = disabled.
	static std::atomic<uint32_t> g_skipFirstNHookCalls{ 300 };
	static std::atomic<uint32_t> g_skipFirstNHookCallsConfig{ 300 };
	// Apply Y-up (GLTF) to Z-up (Creation Engine). 1=apply ±90° X to delta; 0=no conversion.
	static std::atomic<bool> g_applyYUpToZUpConversion{ true };
	// 1 = use +90° X instead of -90° X for Y->Z. Try if pose is still distorted.
	static std::atomic<bool> g_yUpToZUpConversionFlip{ false };
	// 1 = similarity transform C×delta×C⁻¹ (default): T-pose stays I, delta in Z-up. 0 = old M'=C*M.
	static std::atomic<bool> g_yUpToZUpConversionConjugate{ true };
	// 1 = multiply in Y-up: base_zup→Y, result_yup = delta*base (or base*delta), then result→Z. Delta and base same space.
	static std::atomic<bool> g_unifiedYUpSpace{ true };
	// 1 = result = delta * game_base (correct orientation); 0 = result = game_base * delta (character upside down).
	static std::atomic<bool> g_rotationOrderDeltaThenBase{ true };
	// 1 = write anim rotation only (Y->Z), no game_base/delta. Test if mix-up is from base+delta.
	static std::atomic<bool> g_applyAnimRotationOnly{ false };
	// 1 = NAF-like apply: ignoruj rest/game_base, użyj bezpośrednio macierzy z animacji (jak Graph::PushAnimationOutput w NAF).
	// Zakładamy, że animacja jest już w tym samym układzie co szkielet gry (Z-up). Domyślnie bez YUpToZUpConversion.
	static std::atomic<bool> g_useNAFApplyMode{ false };
	// 1 = pozwól animacji sterować kośćmi twarzy (Eye/Jaw/faceBone_*). 0 = zostaw pod grą (FaceGen/morphy).
	static std::atomic<bool> g_animateFaceJoints{ true };  // default on: face bones animated with blend so they don't disappear
	// Siła miksu twarzy: 0.0 = tylko gra, 1.0 = tylko animacja, wartości pośrednie = blend.
	static std::atomic<float> g_faceAnimStrength{ 0.5f };  // 0.5 = 50% anim / 50% game for face rotation
	// 0 = off, 1..4 = prosta poprawka osi dla kręgosłupa i nóg (Spine/Legs) po wyliczeniu macierzy:
	// 1: R*Rx(+90°), 2: R*Rx(-90°), 3: R*Ry(+90°), 4: R*Ry(-90°).
	static std::atomic<int> g_spineLegsSimpleAxisFix{ 0 };
	// 0 = off. Yaw 180° fix dla kręgosłupa i nóg (obrót wokół osi pionowej kości po wyliczeniu macierzy, żeby zdjąć „obrót w prawo/lewo”).
	// 1 = Spine+Legs, Z-owa oś yaw (R*Rz(180°)). 2 = Spine only, Z-yaw. 3 = Legs only, Z-yaw.
	static std::atomic<int> g_spineLegsYawFlip{ 0 };
	// 1 = użyj jako rest pose transformów odczytanych z aktora przy budowaniu joint map (bez ModelDB).
	static std::atomic<bool> g_useActorRestPose{ false };
	static std::atomic<bool> g_useModelDBForRestPose{ false };
	static std::atomic<std::uint64_t> g_modelDBGetEntryID{ 949826 };  // Address Library ID GetEntry
		static std::atomic<std::uint64_t> g_modelDBDecRefID{ 36741 };    // Address Library ID DecRef (albo użyj ModelDBDecRefRVA)
		static std::atomic<std::uint32_t> g_modelDBDecRefRVA{ 0 };       // gdy nie 0: adres DecRef = base+RVA (IDA); wtedy drugie ID niepotrzebne
	// 1 = only apply to joints where anim != rest (skip identity delta). Reduces deformation from overwriting all bones.
	static std::atomic<bool> g_applyOnlyToAnimatedJoints{ true };
	// If true, call UpdateTransformAndBounds so animation is visible. OnRoot=true was the setting that made animation play (but can cause deformation/disappearance).
	static std::atomic<bool> g_useUpdateTransformAndBounds{ true };
	static std::atomic<bool> g_useUpdateTransformAndBoundsOnRoot{ true };
	// If non-empty, bone lookup is restricted to this node's subtree (avoids matching bones from weapons/attachments). E.g. "NPC" for body-only.
	static std::string g_skeletonRootName;
	// If true, write rotation matrix transposed (column-major). Try if engine expects column-major.
	static std::atomic<bool> g_transposeOutputRotation{ false };
	// If true, use NiNode::SetLocalTransform() (CommonLibSF API). Can crash on some builds; then use 0 (raw write).
	static std::atomic<bool> g_useNiNodeSetLocalTransform{ false };
	// If true, flip rotation (180° X) for arm+hand joints (Clavicle/Biceps/Forearm/Arm/Wrist/fingers/Deltoid/Elbow). Fixes "arms wrong direction".
	static std::atomic<bool> g_flipHandRotation{ false };
	// If true, flip 180° around Y for arm+hand joints (fixes "arms inside body" -> arms in front of model).
	static std::atomic<bool> g_armsInFrontFix{ false };
	// If true, rotate arm+hand joints -90° around Y (both arms same) – ręce z "w prawo" na "do przodu".
	static std::atomic<bool> g_armsForwardFix{ false };
	// If true, rotate arm+hand joints 90° around Z – ręce z "w prawo" na "do przodu" (gdy Y nie pomogło).
	static std::atomic<bool> g_armsForwardFixZ{ false };
	// Zamiana osi dla rąk (może wektor X jest przesunięty o 90° w grze): 0=off, 1=swap X↔Y, 2=cykl X→Y→Z→X.
	static std::atomic<int> g_armsAxisFix{ 0 };
	// If true, transpose final 3x3 rotation for arm joints only (R→R^T). Try if engine expects opposite row/column for limbs.
	static std::atomic<bool> g_transposeArmsOnly{ false };
	// For arm joints only: 0=use global RotationOrderDeltaThenBase, 1=force delta*base, 2=force base*delta.
	static std::atomic<int> g_armsRotationOrderOverride{ 0 };
	// For arm joints: 0=off, 1=R*Rz(-90°), 2=R*Rz(+90°), 3=R*Rz(180°), 4=R*Rx(180°) "przeciwny" obrót – ręce z przodu.
	static std::atomic<int> g_armsCorrectDirection{ 4 };
	// Align upper arm "forward" (col1) to torso (C_Chest) "forward" so arms follow body direction.
	static std::atomic<bool> g_armsAlignToTorso{ true };
	// Per-frame cache: rotation of C_Chest (9 floats row-major), valid until next frame.
	static float g_torsoRefRotation[9];
	static bool g_torsoRefValid{ false };

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

	static bool ParseBool(std::string_view a_text, bool a_default)
	{
		auto v = Trim(a_text);
		std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (v == "1" || v == "true" || v == "yes" || v == "on") {
			return true;
		}
		if (v == "0" || v == "false" || v == "no" || v == "off") {
			return false;
		}
		return a_default;
	}

	static void LoadHookOverrideFromIni()
	{
		if (g_hookOverrideLoaded.load(std::memory_order_acquire)) {
			return;
		}
		const auto iniPath = Util::String::GetDataPath() / "SFSE" / "Plugins" / "StarfieldAnimationFramework.ini";
		std::ifstream file(iniPath);
		if (!file.is_open()) {
			SAF_LOG_WARN("StarfieldAnimationFramework.ini not found at {} (will retry on next call)", iniPath.string());
			return;
		}
		if (g_hookOverrideLoaded.exchange(true, std::memory_order_acq_rel)) {
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
			if (key == "GraphUpdateHookRVA") {
				try {
					std::uint32_t rva = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
					g_hookOverrideRva.store(rva, std::memory_order_release);
					SAF_LOG_INFO("GraphUpdateHook RVA override set from ini: {:X}", rva);
				} catch (...) {
					SAF_LOG_WARN("GraphUpdateHook RVA override invalid in ini: {}", value);
				}
			} else if (key == "EnableTaskUpdateLoop") {
				const bool enabled = ParseBool(value, false);
				g_taskLoopEnabled.store(enabled, std::memory_order_release);
				SAF_LOG_INFO("TaskUpdateLoop enabled: {}", enabled ? "true" : "false");
		} else if (key == "TaskUpdateLoopAssumeMainThread") {
			const bool enabled = ParseBool(value, false);
			g_taskLoopAssumeMainThread.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("TaskUpdateLoop assume main thread: {}", enabled ? "true" : "false");
		} else if (key == "TaskUpdateLoopAllowDump") {
			const bool enabled = ParseBool(value, false);
			g_taskLoopAllowDump.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("TaskUpdateLoop allow dump: {}", enabled ? "true" : "false");
		} else if (key == "TaskUpdateLoopAllowUpdateGraphs") {
			const bool enabled = ParseBool(value, false);
			g_taskLoopAllowUpdateGraphs.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("TaskUpdateLoop allow UpdateGraphs: {}", enabled ? "true" : "false");
		} else if (key == "EnablePlayerUpdateEvent") {
			const bool enabled = ParseBool(value, true);
			g_playerUpdateEventEnabled.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("PlayerUpdateEvent enabled: {}", enabled ? "true" : "false");
		} else if (key == "UpdateSceneRectEventRVA") {
			try {
				std::uint32_t rva = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
				g_updateSceneRectEventRva.store(rva, std::memory_order_release);
				SAF_LOG_INFO("UpdateSceneRectEvent RVA override set from ini: {:X}", rva);
			} catch (...) {
				SAF_LOG_WARN("UpdateSceneRectEvent RVA override invalid in ini: {}", value);
			}
		} else if (key == "PlayerUpdateEventRVA") {
			try {
				std::uint32_t rva = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
				g_playerUpdateEventRva.store(rva, std::memory_order_release);
				SAF_LOG_INFO("PlayerUpdateEvent RVA override set from ini: {:X}", rva);
			} catch (...) {
				SAF_LOG_WARN("PlayerUpdateEvent RVA override invalid in ini: {}", value);
			}
		} else if (key == "BSAnimationGraphUpdateRVA") {
			try {
				std::uint32_t rva = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
				g_animGraphUpdateRva.store(rva, std::memory_order_release);
				SAF_LOG_INFO("BSAnimationGraph::Update RVA override set from ini: {:X}", rva);
			} catch (...) {
				SAF_LOG_WARN("BSAnimationGraph::Update RVA override invalid in ini: {}", value);
			}
		} else if (key == "AnimGraphManagerCallRVA") {
			try {
				std::uint32_t rva = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
				g_animGraphManagerCallRva.store(rva, std::memory_order_release);
				SAF_LOG_INFO("IAnimationGraphManagerHolder::UpdateAnimationGraphManager call RVA override set from ini: {:X}", rva);
			} catch (...) {
				SAF_LOG_WARN("AnimGraphManagerCallRVA invalid in ini: {}", value);
			}
		} else if (key == "EnableInputHook") {
				const bool enabled = ParseBool(value, false);
				g_inputHookEnabled.store(enabled, std::memory_order_release);
				SAF_LOG_INFO("InputHook enabled: {}", enabled ? "true" : "false");
			} else if (key == "SkipFirstNHookCalls") {
				try {
					uint32_t n = static_cast<uint32_t>(std::stoul(std::string(Trim(value)), nullptr, 0));
					g_skipFirstNHookCallsConfig.store(n, std::memory_order_release);
					g_skipFirstNHookCalls.store(n, std::memory_order_release);
					SAF_LOG_INFO("SkipFirstNHookCalls: {} (skip our logic until player in world / first N calls; 0=off)", n);
				} catch (...) {
					SAF_LOG_WARN("SkipFirstNHookCalls invalid in ini: {}", value);
				}
			} else if (key == "EnableCameraHook") {
				const bool enabled = ParseBool(value, false);
				g_cameraHookEnabled.store(enabled, std::memory_order_release);
				SAF_LOG_INFO("CameraHook enabled: {}", enabled ? "true" : "false");
		} else if (key == "EnableMenuHook") {
			const bool enabled = ParseBool(value, true);
			g_menuHookEnabled.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("MenuHook enabled: {}", enabled ? "true" : "false");
		} else if (key == "AnimateFaceJoints") {
			const bool enabled = ParseBool(value, false);
			g_animateFaceJoints.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("AnimateFaceJoints: {}", enabled ? "true" : "false");
		} else if (key == "FaceAnimationStrength") {
			try {
				float v = std::stof(std::string(Trim(value)));
				if (v < 0.0f) v = 0.0f;
				if (v > 1.0f) v = 1.0f;
				g_faceAnimStrength.store(v, std::memory_order_release);
				SAF_LOG_INFO("FaceAnimationStrength: {}", v);
			} catch (...) {
				SAF_LOG_WARN("FaceAnimationStrength invalid in ini: {}", value);
			}
		} else if (key == "ApplyYUpToZUpConversion") {
			const bool enabled = ParseBool(value, true);
			g_applyYUpToZUpConversion.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("ApplyYUpToZUpConversion: {} (1=Y-up to Z-up, 0=no conversion)", enabled ? "true" : "false");
		} else if (key == "YUpToZUpConversionFlip") {
			const bool enabled = ParseBool(value, false);
			g_yUpToZUpConversionFlip.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("YUpToZUpConversionFlip: {} (1=+90°X instead of -90°X)", enabled ? "true" : "false");
		} else if (key == "RotationOrderDeltaThenBase") {
			const bool enabled = ParseBool(value, true);
			g_rotationOrderDeltaThenBase.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("RotationOrderDeltaThenBase: {} (1=delta*base ok, 0=base*delta upside down)", enabled ? "true" : "false");
		} else if (key == "ApplyAnimRotationOnly") {
			const bool enabled = ParseBool(value, false);
			g_applyAnimRotationOnly.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("ApplyAnimRotationOnly: {} (1=anim rot only)", enabled ? "true" : "false");
		} else if (key == "UseNAFApplyMode") {
			const bool enabled = ParseBool(value, false);
			g_useNAFApplyMode.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("UseNAFApplyMode: {} (1=NAF-like: bez rest/game_base, czysta macierz z animacji jak w NAF Graph::PushAnimationOutput; GLTF musi być w Z-up)", enabled ? "true" : "false");
		} else if (key == "SpineLegsSimpleAxisFix") {
			try {
				int v = std::stoi(std::string(Trim(value)));
				v = (v < 0) ? 0 : (v > 8) ? 8 : v;
				g_spineLegsSimpleAxisFix.store(v, std::memory_order_release);
				SAF_LOG_INFO("SpineLegsSimpleAxisFix: {} (0=off, 1–4=wszystko Rx/Ry, 5–6=tylko kręgosłup Rx±90°, 7–8=tylko nogi Rx±90°)", v);
			} catch (...) {
				SAF_LOG_WARN("SpineLegsSimpleAxisFix: invalid value '{}'", value);
			}
		} else if (key == "SpineLegsYawFlip") {
			try {
				int v = std::stoi(std::string(Trim(value)));
				v = (v < 0) ? 0 : (v > 3) ? 3 : v;
				g_spineLegsYawFlip.store(v, std::memory_order_release);
				SAF_LOG_INFO("SpineLegsYawFlip: {} (0=off, 1=Spine+Legs Z-180°, 2=Spine-only Z-180°, 3=Legs-only Z-180°)", v);
			} catch (...) {
				SAF_LOG_WARN("SpineLegsYawFlip: invalid value '{}'", value);
			}
		} else if (key == "UseActorRestPose") {
			const bool enabled = ParseBool(value, false);
			g_useActorRestPose.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("UseActorRestPose: {} (1=rest z kości aktora przy BuildJointMap, bez ModelDB)", enabled ? "true" : "false");
		} else if (key == "UseModelDBForRestPose") {
			const bool enabled = ParseBool(value, false);
			g_useModelDBForRestPose.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("UseModelDBForRestPose: {} (0=wyłączone, unika crash; 1=rest z NIF przez ModelDB)", enabled ? "true" : "false");
		} else if (key == "ModelDBGetEntryID") {
			try {
				std::uint64_t id = static_cast<std::uint64_t>(std::stoull(std::string(Trim(value)), nullptr, 0));
				g_modelDBGetEntryID.store(id, std::memory_order_release);
				SAF_LOG_INFO("ModelDBGetEntryID: {} (0=użyj domyślnego 949826)", id);
			} catch (...) {
				SAF_LOG_WARN("ModelDBGetEntryID: invalid value '{}'", value);
			}
		} else if (key == "ModelDBDecRefID") {
			try {
				std::uint64_t id = static_cast<std::uint64_t>(std::stoull(std::string(Trim(value)), nullptr, 0));
				g_modelDBDecRefID.store(id, std::memory_order_release);
				SAF_LOG_INFO("ModelDBDecRefID: {} (0=domyślny 36741; albo ustaw ModelDBDecRefRVA z IDA)", id);
			} catch (...) {
				SAF_LOG_WARN("ModelDBDecRefID: invalid value '{}'", value);
			}
		} else if (key == "ModelDBDecRefRVA") {
			try {
				std::uint32_t rva = static_cast<std::uint32_t>(std::stoul(std::string(Trim(value)), nullptr, 0));
				g_modelDBDecRefRVA.store(rva, std::memory_order_release);
				SAF_LOG_INFO("ModelDBDecRefRVA: 0x{:X} (gdy ustawione, używany zamiast ModelDBDecRefID – wystarczy jedno ID)", rva);
			} catch (...) {
				SAF_LOG_WARN("ModelDBDecRefRVA: invalid value '{}'", value);
			}
		} else if (key == "ApplyOnlyToAnimatedJoints") {
			const bool enabled = ParseBool(value, true);
			g_applyOnlyToAnimatedJoints.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("ApplyOnlyToAnimatedJoints: {} (1=skip joints with anim=rest, reduces deformation)", enabled ? "true" : "false");
		} else if (key == "UseUpdateTransformAndBounds") {
			const bool enabled = ParseBool(value, true);
			g_useUpdateTransformAndBounds.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("UseUpdateTransformAndBounds: {} (1=update so animation visible, 0=only UpdateWorldData)", enabled ? "true" : "false");
		} else if (key == "UseUpdateTransformAndBoundsOnRoot") {
			const bool enabled = ParseBool(value, true);
			g_useUpdateTransformAndBoundsOnRoot.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("UseUpdateTransformAndBoundsOnRoot: {} (1=animation plays, may deform; 0=no root update)", enabled ? "true" : "false");
		} else if (key == "SkeletonRootName") {
			g_skeletonRootName = Trim(value);
			SAF_LOG_INFO("SkeletonRootName: '{}' (empty=search from actor root; set to e.g. NPC to restrict bones to body subtree, fixes leg/arm mix-up)", g_skeletonRootName.empty() ? "" : g_skeletonRootName);
		} else if (key == "SkipSkeletonPathsContaining") {
			std::vector<std::string> substrings;
			std::string v(Trim(value));
			if (!v.empty()) {
				for (size_t start = 0; start < v.size(); ) {
					size_t comma = v.find(',', start);
					std::string part = (comma == std::string::npos) ? v.substr(start) : v.substr(start, comma - start);
					part = Trim(part);
					if (!part.empty()) substrings.push_back(part);
					start = (comma == std::string::npos) ? v.size() : comma + 1;
				}
			}
			Settings::SetSkipSkeletonPathSubstrings(std::move(substrings));
			SAF_LOG_INFO("SkipSkeletonPathsContaining: {} (SAF will skip actors whose race skeleton path contains any of these; use for SFF Body Replacer / SF Extended Skeleton)", value.empty() ? "(empty)" : value.data());
		} else if (key == "AllowAnimationsOnExtendedSkeleton") {
			const bool enabled = ParseBool(value, false);
			Settings::SetAllowAnimationsOnExtendedSkeleton(enabled);
			SAF_LOG_INFO("AllowAnimationsOnExtendedSkeleton: {} (1=animations on SFF/Extended; 0=skip when in skip list; set 1 in INI for universal)", enabled ? "true" : "false");
		} else if (key == "TransposeOutputRotation") {
			const bool enabled = ParseBool(value, false);
			g_transposeOutputRotation.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("TransposeOutputRotation: {} (1=write column-major; try if character stays distorted)", enabled ? "true" : "false");
		} else if (key == "YUpToZUpConversionConjugate") {
			const bool enabled = ParseBool(value, true);
			g_yUpToZUpConversionConjugate.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("YUpToZUpConversionConjugate: {} (1=C×delta×C⁻¹, T-pose intact; 0=old C*M)", enabled ? "true" : "false");
		} else if (key == "UnifiedYUpSpace") {
			const bool enabled = ParseBool(value, true);
			g_unifiedYUpSpace.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("UnifiedYUpSpace: {} (1=multiply in Y-up: base→Y, delta*base in Y, result→Z)", enabled ? "true" : "false");
		} else if (key == "UseNiNodeSetLocalTransform") {
			const bool enabled = ParseBool(value, false);
			g_useNiNodeSetLocalTransform.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("UseNiNodeSetLocalTransform: {} (1=SetLocalTransform API, can crash; 0=raw write)", enabled ? "true" : "false");
		} else if (key == "FlipHandRotation") {
			const bool enabled = ParseBool(value, false);
			g_flipHandRotation.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("FlipHandRotation: {} (1=flip 180°X for arm+hand joints)", enabled ? "true" : "false");
		} else if (key == "ArmsInFrontFix") {
			const bool enabled = ParseBool(value, false);
			g_armsInFrontFix.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("ArmsInFrontFix: {} (1=flip 180°Y for arm+hand joints, hands in front of model)", enabled ? "true" : "false");
		} else if (key == "ArmsForwardFix") {
			const bool enabled = ParseBool(value, false);
			g_armsForwardFix.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("ArmsForwardFix: {} (1=rotate -90°Y arms: right->forward)", enabled ? "true" : "false");
		} else if (key == "ArmsForwardFixZ") {
			const bool enabled = ParseBool(value, false);
			g_armsForwardFixZ.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("ArmsForwardFixZ: {} (1=rotate 90°Z arms: right->forward)", enabled ? "true" : "false");
		} else if (key == "ArmsAxisFix") {
			try {
				int v = std::stoi(std::string(Trim(value)));
				v = (v < 0) ? 0 : (v > 5) ? 5 : v;
				g_armsAxisFix.store(v, std::memory_order_release);
				SAF_LOG_INFO("ArmsAxisFix: {} (0=off, 1=X↔Y, 2=cykl X→Y→Z, 3=X↔Z, 4=Y↔Z, 5=cykl X→Z→Y)", v);
			} catch (...) {
				SAF_LOG_WARN("ArmsAxisFix invalid in ini: {}", value);
			}
		} else if (key == "TransposeArmsOnly") {
			const bool enabled = ParseBool(value, false);
			g_transposeArmsOnly.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("TransposeArmsOnly: {} (1=transpose 3x3 for arm joints only; try if limbs expect R^T)", enabled ? "true" : "false");
		} else if (key == "ArmsRotationOrderOverride") {
			try {
				int v = std::stoi(std::string(Trim(value)));
				v = (v < 0) ? 0 : (v > 2) ? 2 : v;
				g_armsRotationOrderOverride.store(v, std::memory_order_release);
				SAF_LOG_INFO("ArmsRotationOrderOverride: {} (0=global, 1=delta*base, 2=base*delta for arms)", v);
			} catch (...) {
				SAF_LOG_WARN("ArmsRotationOrderOverride invalid in ini: {}", value);
			}
		} else if (key == "ArmsCorrectDirection") {
			try {
				int v = std::stoi(std::string(Trim(value)));
				v = (v < 0) ? 0 : (v > 4) ? 4 : v;
				g_armsCorrectDirection.store(v, std::memory_order_release);
				SAF_LOG_INFO("ArmsCorrectDirection: {} (0=off, 1=Rz(-90°), 2=Rz(+90°), 3=Rz(180°), 4=Rx(180°) ręce z przodu)", v);
			} catch (...) {
				SAF_LOG_WARN("ArmsCorrectDirection invalid in ini: {}", value);
			}
		} else if (key == "ArmsAlignToTorso") {
			const bool enabled = ParseBool(value, true);
			g_armsAlignToTorso.store(enabled, std::memory_order_release);
			SAF_LOG_INFO("ArmsAlignToTorso: {} (1=align upper arm forward to C_Chest/Spine2/Neck)", enabled ? "true" : "false");
		}
		}
	}

	// Weryfikacja ID: loguje adres, offset (RVA) i pierwsze bajty. Porównaj z IDA (offset w exe).
	// Jeśli "first bytes" to np. 00 00 00 lub FF FF – ID jest błędne lub nie ta wersja gry.
	static uintptr_t ResolveGraphUpdateHookAddress()
	{
		const auto overrideRva = g_hookOverrideRva.load(std::memory_order_acquire);
		if (overrideRva != 0) {
			const uintptr_t base = REX::FModule::GetExecutingModule().GetBaseAddress();
			return base + overrideRva;
		}
		REL::Relocation<uintptr_t> target{ ID::GraphUpdateHook };
		return target.address();
	}

static uintptr_t ResolveAnimGraphUpdateHookAddress()
{
	const auto overrideRva = g_animGraphUpdateRva.load(std::memory_order_acquire);
	if (overrideRva != 0) {
		const uintptr_t base = REX::FModule::GetExecutingModule().GetBaseAddress();
		return base + overrideRva;
	}
	return 0;
}

static uintptr_t ResolveAnimGraphManagerCallAddress()
{
	const auto overrideRva = g_animGraphManagerCallRva.load(std::memory_order_acquire);
	if (overrideRva != 0) {
		const uintptr_t base = REX::FModule::GetExecutingModule().GetBaseAddress();
		return base + overrideRva;
	}
	try {
		return ID::AnimGraphManagerCall.address() + 0x61;
	} catch (...) {
		return 0;
	}
}

	static void ValidateGraphUpdateHookID()
	{
		try {
			const uintptr_t addr = ResolveGraphUpdateHookAddress();
			const uintptr_t base = REX::FModule::GetExecutingModule().GetBaseAddress();
			const size_t offsetRVA = addr - base;

			uint8_t prologue[14];
			memcpy(prologue, reinterpret_cast<const void*>(addr), sizeof(prologue));

			// Format bajtów do logu (hex)
			char hex[64];
			int n = 0;
			for (size_t i = 0; i < sizeof(prologue) && n < (int)sizeof(hex) - 3; i++)
				n += std::sprintf(hex + n, "%02X ", prologue[i]);
			if (n > 0) hex[n - 1] = '\0';

			const auto overrideRva = g_hookOverrideRva.load(std::memory_order_acquire);
			if (overrideRva != 0) {
				SAF_LOG_INFO(
					"GraphUpdateHook RVA override {:X} -> address {:X}, offset (RVA) {:X}. First bytes: {}",
					overrideRva, addr, static_cast<unsigned>(offsetRVA), hex);
			} else {
				SAF_LOG_INFO(
					"GraphUpdateHook ID {} -> address {:X}, offset (RVA) {:X}. First bytes: {}",
					ID::GraphUpdateHook.id(), addr, static_cast<unsigned>(offsetRVA), hex);
			}

			// Szybka heurystyka: typowy prolog x64 to np. 40 53, 48 89 5C 24, 48 83 EC, 55...
			bool looksLikeCode = (prologue[0] != 0x00 && prologue[0] != 0xFF) ||
			                    (prologue[1] != 0x00 && prologue[1] != 0xFF);
			if (!looksLikeCode)
				SAF_LOG_WARN("GraphUpdateHook: first bytes look like data/null – ID may be wrong for this game version");
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("GraphUpdateHook validate failed: {} – invalid hook address",
				e.what());
		} catch (...) {
			SAF_LOG_ERROR("GraphUpdateHook validate failed – invalid hook address");
		}
	}

using GraphUpdateFunc = int64_t (*)(void* a_this, void* a_param2, void* a_param3);
static GraphUpdateFunc g_originalGraphUpdate = nullptr;
static DWORD g_mainThreadId = 0;
static std::atomic<uint32_t> g_debugHookLogs{ 0 };
static std::atomic<bool> g_rebindOnNextHook{ false };
static std::atomic_flag g_updateGuard = ATOMIC_FLAG_INIT;
static std::atomic<bool> g_forceUpdate{ false };
static std::atomic<bool> g_hookSeen{ false };
// VTable hooks currently unstable; keep disabled for now.
static std::atomic<bool> g_allowVtableHooks{ false };

// Offsets based on NAF CommonMisc.h (Starfield).
// Used to read timeDelta/modelCulled without a full struct definition in commonlib.
static bool ReadAnimUpdateData(void* a_updateData, float& a_outDelta, bool& a_outModelCulled)
{
	if (!a_updateData) {
		return false;
	}
	constexpr std::ptrdiff_t kTimeDeltaOffset = 0x60;
	constexpr std::ptrdiff_t kModelCulledOffset = 0x6B;
	const auto* base = reinterpret_cast<const std::byte*>(a_updateData);
	float dt = 0.0f;
	std::memcpy(&dt, base + kTimeDeltaOffset, sizeof(float));
	const uint8_t culledByte = static_cast<uint8_t>(*(base + kModelCulledOffset));
	a_outDelta = dt;
	a_outModelCulled = (culledByte != 0);
	return true;
}

using BSAnimationGraphUpdateFunc = void (*)(RE::BSAnimationGraph*, RE::BSAnimationUpdateData&);
static REL::THookVFT<void(RE::BSAnimationGraph*, RE::BSAnimationUpdateData&)>* g_animGraphVtablePrimaryHook = nullptr;
static std::vector<std::unique_ptr<REL::THookVFT<void(RE::BSAnimationGraph*, RE::BSAnimationUpdateData&)>>> g_animGraphVtableHooks;
static bool g_animGraphVtableHookInstalled = false;
static bool g_animGraphUpdateHookInstalled = false;
static BSAnimationGraphUpdateFunc g_originalAnimGraphUpdate = nullptr;
using AnimGraphManagerUpdateFunc = void (*)(RE::IAnimationGraphManagerHolder*, void*, void*);
static bool g_animGraphManagerCallHookInstalled = false;
static AnimGraphManagerUpdateFunc g_originalAnimGraphManagerUpdate = nullptr;
static REL::THookVFT<void(RE::PlayerCamera*)>* g_playerCameraUpdateHookPrimary = nullptr;
static std::vector<std::unique_ptr<REL::THookVFT<void(RE::PlayerCamera*)>>> g_playerCameraUpdateHooks;
static bool g_playerCameraHookInstalled = false;
static REL::THookVFT<RE::UI_MESSAGE_RESULT(RE::IMenu*, RE::UIMessageData&)>* g_menuProcessMessageHookPrimary = nullptr;
static std::vector<std::unique_ptr<REL::THookVFT<RE::UI_MESSAGE_RESULT(RE::IMenu*, RE::UIMessageData&)>>> g_menuProcessMessageHooks;
// Map vtable address -> hook; wywołanie oryginału tylko dla właściwego menu (unika CTD przy help / innych komendach).
static std::unordered_map<uintptr_t, REL::THookVFT<RE::UI_MESSAGE_RESULT(RE::IMenu*, RE::UIMessageData&)>*> g_menuVtableToHook;
static bool g_menuProcessMessageHookInstalled = false;
static bool g_updateSceneRectSinkInstalled = false;
static bool g_playerUpdateSinkInstalled = false;

class UpdateSceneRectEventSink : public RE::BSTEventSink<RE::UpdateSceneRectEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::UpdateSceneRectEvent&,
		RE::BSTEventSource<RE::UpdateSceneRectEvent>*) override
	{
		static auto lastTick = std::chrono::steady_clock::now();
		static std::atomic<uint32_t> callCount{ 0 };
		const uint32_t callNum = ++callCount;

		// If the original graph hook is ticking, don't double-run.
		if (g_hookSeen.load(std::memory_order_acquire)) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* mgr = GraphManager::GetSingleton();
		const DWORD curThread = GetCurrentThreadId();
		if (g_mainThreadId == 0) {
			g_mainThreadId = curThread;
			SAF_LOG_INFO("GraphManager: main thread id set to {} (UpdateSceneRectEvent)", g_mainThreadId);
		}

		const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);
		if (!isMainThread) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const bool hasPending = Commands::SAFCommand::HasPendingCommands();
		const bool requested = Commands::SAFCommand::HasProcessRequest();
		const bool hasDump = Commands::SAFCommand::HasPendingDump();
		const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();

		if (requested || hasPending) {
			if (requested) {
				Commands::SAFCommand::ConsumeProcessRequest();
			}
			if (Commands::SAFCommand::ConsumeCloseConsole()) {
				Commands::SAFCommand::CloseConsoleMainThread();
			}
			Commands::SAFCommand::ProcessPendingCommands();
		}

		if (dumpRequested || hasDump) {
			Commands::SAFCommand::ProcessPendingDump();
		}

		if (mgr) {
			if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
				const auto now = std::chrono::steady_clock::now();
				const std::chrono::duration<float> dt = now - lastTick;
				lastTick = now;

				if (callNum <= 10 || callNum % 300 == 0) {
					SAF_LOG_INFO("[HOOK] UpdateSceneRectEvent tick (dt={})", dt.count());
				}

				mgr->UpdateGraphs(dt.count());
				g_updateGuard.clear(std::memory_order_release);
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}
};

static UpdateSceneRectEventSink g_updateSceneRectSink;

class PlayerUpdateEventSink : public RE::BSTEventSink<RE::PlayerUpdateEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::PlayerUpdateEvent&,
		RE::BSTEventSource<RE::PlayerUpdateEvent>*) override
	{
		static auto lastTick = std::chrono::steady_clock::now();
		static std::atomic<uint32_t> callCount{ 0 };
		const uint32_t callNum = ++callCount;

		if (g_hookSeen.load(std::memory_order_acquire)) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* mgr = GraphManager::GetSingleton();
		const DWORD curThread = GetCurrentThreadId();
		if (g_mainThreadId == 0) {
			g_mainThreadId = curThread;
			SAF_LOG_INFO("GraphManager: main thread id set to {} (PlayerUpdateEvent)", g_mainThreadId);
		}

		const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);
		if (!isMainThread) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const bool hasPending = Commands::SAFCommand::HasPendingCommands();
		const bool requested = Commands::SAFCommand::HasProcessRequest();
		const bool hasDump = Commands::SAFCommand::HasPendingDump();
		const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();

		if (requested || hasPending) {
			if (requested) {
				Commands::SAFCommand::ConsumeProcessRequest();
			}
			if (Commands::SAFCommand::ConsumeCloseConsole()) {
				Commands::SAFCommand::CloseConsoleMainThread();
			}
			Commands::SAFCommand::ProcessPendingCommands();
		}

		if (dumpRequested || hasDump) {
			Commands::SAFCommand::ProcessPendingDump();
		}

		if (mgr) {
			if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
				const auto now = std::chrono::steady_clock::now();
				const std::chrono::duration<float> dt = now - lastTick;
				lastTick = now;

				if (callNum <= 10 || callNum % 300 == 0) {
					SAF_LOG_INFO("[HOOK] PlayerUpdateEvent tick (dt={})", dt.count());
				}

				mgr->UpdateGraphs(dt.count());
				g_updateGuard.clear(std::memory_order_release);
			}
		}

		return RE::BSEventNotifyControl::kContinue;
	}
};

static PlayerUpdateEventSink g_playerUpdateSink;

template <class TEvent>
static RE::BSTEventSource<TEvent>* ResolveEventSourceByIdOrRva(
	REL::ID a_id,
	REL::ID a_fallbackId,
	std::atomic<std::uint32_t>& a_rvaOverride,
	const char* a_name,
	RE::BSTEventSource<TEvent>* (*a_idGetter)())
{
	if (a_id.id() != 0) {
		return a_idGetter();
	}
	// Gdy RE::ID ma 0 (brak w CommonLibSF), spróbuj ID z Address Library (fallback z RE/IDs.h).
	// SEH (__try/__except) – wywołanie func() pod złym adresem powoduje AV; try/catch tego nie łapie.
	if (a_fallbackId.id() != 0) {
		RE::BSTEventSource<TEvent>* fallbackSource = nullptr;
		uintptr_t fallbackAddr = 0;
		__try {
			fallbackAddr = a_fallbackId.address();
			using func_t = RE::BSTEventSource<TEvent>* (*)();
			auto* func = reinterpret_cast<func_t>(fallbackAddr);
			fallbackSource = func();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			SAF_LOG_WARN("{} GetEventSource fallback ID {} caused access violation (wrong for this game version)", a_name, a_fallbackId.id());
		}
		if (fallbackSource) {
			SAF_LOG_INFO("{} GetEventSource using Address Library ID {} (addr {:X})", a_name, a_fallbackId.id(), fallbackAddr);
			return fallbackSource;
		}
	}
	const auto rva = a_rvaOverride.load(std::memory_order_acquire);
	if (rva == 0) {
		SAF_LOG_WARN("{} GetEventSource ID missing in Address Library; skip", a_name);
		return nullptr;
	}
	const uintptr_t base = REX::FModule::GetExecutingModule().GetBaseAddress();
	const uintptr_t addr = base + rva;
	using func_t = RE::BSTEventSource<TEvent>* (*)();
	auto* func = reinterpret_cast<func_t>(addr);
	__try {
		RE::BSTEventSource<TEvent>* source = func();
		if (source) {
			SAF_LOG_INFO("{} GetEventSource using RVA {:X} (addr {:X})", a_name, rva, addr);
			return source;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		SAF_LOG_WARN("{} GetEventSource RVA {:X} caused access violation", a_name, rva);
		return nullptr;
	}
	return nullptr;
}

	struct ActorGraphState
	{
		std::shared_ptr<Animation::OzzSkeleton> skeleton;
		std::unique_ptr<Animation::Generator> generator;
		std::vector<RE::NiAVObject*> jointNodes;
		// 1 if the current clip contains at least one TRS channel for this joint; 0 otherwise.
		// Used to keep face bones stable when the animation doesn't animate them.
		std::vector<std::uint8_t> jointHasChannel;
		std::vector<Animation::Transform> localTransforms;
		std::vector<Animation::Transform> restTransforms;     // OZZ bind-pose, or actor rest when UseActorRestPose
		std::vector<Animation::Transform> actorRestTransforms; // captured from actor when building joint map (if UseActorRestPose)
		std::vector<std::array<float,9>> gameBaseRotations;   // game's original bone matrices, captured once
		bool jointMapBuilt = false;
		bool loggedFirstUpdate = false;
		bool jointMapBuildFailed = false;
		uint32_t animFrameCount = 0;
		// NAF-style API state
		std::string currentAnimationPath;
		float playbackSpeed = 1.0f;
		bool positionLocked = false;
		float positionX = 0.0f, positionY = 0.0f, positionZ = 0.0f;
		std::unordered_map<std::string, float> blendGraphVariables;
	};

	struct ActorSequenceState
	{
		std::vector<Sequencer::PhaseData> phases;
		int currentPhaseIndex = 0;
		bool loop = false;
	};

	static std::unordered_map<RE::TESFormID, ActorGraphState> g_actorGraphs;
	static std::unordered_map<RE::TESFormID, ActorSequenceState> g_actorSequenceState;
	static std::unordered_map<RE::TESFormID, RE::TESFormID> g_syncOwner;  // actor formId -> owner formId (owner points to self)
	static std::mutex g_graphMutex;
	// Set of actors we already logged access-violation for (SafeBuildJointMap); file scope to avoid C2712 with __try.
	static std::unordered_set<RE::Actor*> g_loggedAvActorsBuildJointMap;

	static std::string NormalizeBoneName(std::string_view a_name)
	{
		std::string out;
		out.reserve(a_name.size());
		for (char c : a_name) {
			if (c >= 'A' && c <= 'Z') {
				out.push_back(static_cast<char>(c - 'A' + 'a'));
			} else if (c != ' ' && c != '\t') {
				out.push_back(c);
			}
		}
		const std::string npcPrefix = "npc";
		if (out.rfind(npcPrefix, 0) == 0) {
			out.erase(0, npcPrefix.size());
		}
		return out;
	}

	static bool Validate3DRoot(RE::NiAVObject* p);

	// Safe name read (node->name can AV if layout differs). No C++ objects in __try.
	static bool GetNodeNameSafe(RE::NiAVObject* a_node, char* a_buf, size_t a_bufSize)
	{
		if (!a_buf || a_bufSize == 0) return false;
		a_buf[0] = '\0';
		__try {
			const char* n = a_node->name.c_str();
			if (!n) return false;
			size_t len = std::strlen(n);
			if (len >= a_bufSize) len = a_bufSize - 1;
			std::memcpy(a_buf, n, len);
			a_buf[len] = '\0';
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// NiNode layout (CommonLibSF): NiAVObject at 0x0, children (BSTArray) at 0x130. BSTArray: _size at +0, _capacity +4, _data +8.
	// Read children via raw offsets to avoid AV if game layout differs; no C++ objects in __try (C2712).
	static int GetNodeChildrenRaw(RE::NiAVObject* a_node, RE::NiAVObject** a_out, int a_maxOut)
	{
		int count = 0;
		__try {
			if (!a_node || !Validate3DRoot(a_node)) return 0;
			RE::NiNode* node = a_node->GetAsNiNode();
			if (!node) return 0;
			const auto* base = reinterpret_cast<const std::uint8_t*>(node);
			constexpr std::uintptr_t kChildrenOffset = 0x130;
			std::uint32_t size = 0;
			void* data = nullptr;
			std::memcpy(&size, base + kChildrenOffset + 0, sizeof(size));
			std::memcpy(&data, base + kChildrenOffset + 8, sizeof(data));
			if (!data) return 0;
			if (size > static_cast<std::uint32_t>(a_maxOut)) size = static_cast<std::uint32_t>(a_maxOut);
			for (std::uint32_t i = 0; i < size; ++i) {
				RE::NiAVObject* child = nullptr;
				std::memcpy(&child, static_cast<const std::uint8_t*>(data) + i * sizeof(void*), sizeof(child));
				if (child) a_out[count++] = child;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			count = 0;
		}
		return count;
	}

	static void CollectNodesIterative(RE::NiAVObject* a_root, std::vector<RE::NiAVObject*>& a_out)
	{
		if (!a_root) return;
		constexpr size_t kMaxNodes = 20000;
		constexpr int kChildBufSize = 256;
		std::vector<RE::NiAVObject*> stack;
		stack.reserve(512);
		stack.push_back(a_root);
		RE::NiAVObject* childBuf[kChildBufSize];
		while (!stack.empty()) {
			RE::NiAVObject* current = stack.back();
			stack.pop_back();
			if (!current) continue;
			a_out.push_back(current);
			if (a_out.size() >= kMaxNodes) {
				SAF_LOG_WARN("CollectNodesIterative: node limit reached ({}), stopping", kMaxNodes);
				break;
			}
			int n = GetNodeChildrenRaw(current, childBuf, kChildBufSize);
			for (int i = 0; i < n; ++i)
				stack.push_back(childBuf[i]);
		}
	}

	// ── NiAVObject::local offset probe ───────────────────────────────────────────
	// CommonLibSF's NiAVObject::local offset may be wrong for Starfield 1.15.222.0.
	// We probe the node's raw memory to find the actual NiTransform location.
	//
	// NiTransform layout (48 bytes / 0x30):
	//   NiMatrix3 rotate  : 9 floats (row-major 3x3)
	//   NiPoint3  translate: 3 floats
	//   float     scale   : 1 float
	//
	// An identity rotation matrix contains:  [1,0,0, 0,1,0, 0,0,1]
	// We scan for 9 floats matching this pattern starting every 4 bytes.
	// The first match (with a plausible scale ~0.5..2.0) is the transform offset.
	//
	// The offset is cached after first detection and used for ALL subsequent writes.
	// ─────────────────────────────────────────────────────────────────────────────
	static std::atomic<std::uintptr_t> g_niTransformOffset{ 0 };  // 0 = not yet found
	static std::atomic<bool> g_niTransformSearchDone{ false };

	// Safely read a float from raw memory. Returns false + 0 on AV.
	// No C++ objects allowed in __try scope (C2712).
	__declspec(noinline) static bool SafeReadFloat(const void* addr, float& out)
	{
		__try {
			out = *static_cast<const float*>(addr);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			out = 0.0f;
			return false;
		}
	}

	// Safely write a float to raw memory. Returns false on AV.
	__declspec(noinline) static bool SafeWriteFloat(void* addr, float val)
	{
		__try {
			*static_cast<float*>(addr) = val;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Find NiTransform offset in NiAVObject by scanning for rotation matrix pattern.
	// Logs raw memory and detected offset. Called once per session.
	static std::uintptr_t ProbeNiTransformOffset(RE::NiAVObject* a_node)
	{
		const auto* base = reinterpret_cast<const std::uint8_t*>(a_node);

		// Dump raw floats [0x20..0xC0] for manual inspection
		{
			std::string dump;
			dump.reserve(512);
			char buf[32];
			for (std::uintptr_t off = 0x20; off < 0xC0; off += 4) {
				float f = 0.0f;
				SafeReadFloat(base + off, f);
				std::snprintf(buf, sizeof(buf), "+0x%02llX:%.4f ", (unsigned long long)off, f);
				dump += buf;
				if ((off - 0x20) % 0x30 == 0x2C) dump += '\n';
			}
			SAF_LOG_INFO("[OFFSET] NiAVObject raw floats:\n{}", dump);
		}

		// Scan for identity-like NiTransform. Two layouts:
		// (1) CommonLibSF: NiMatrix3 (3x4 floats) at +0, NiPoint3 translate at +0x30, float scale at +0x3C → scale at m[15]
		// (2) Packed (legacy): 9 floats rot, 3 trans, 1 scale → scale at m[12]
		constexpr float tol = 0.15f;
		for (std::uintptr_t off = 0x20; off <= 0xA0; off += 4) {
			float m[16] = {};
			bool ok = true;
			for (int k = 0; k < 16 && ok; ++k) {
				ok = SafeReadFloat(base + off + k * 4, m[k]);
			}
			if (!ok) continue;
			// Identity rotation (with optional padding at m[3], m[7], m[11] for NiMatrix3)
			const bool rotId =
				std::abs(m[0] - 1.f) < tol && std::abs(m[1]) < tol && std::abs(m[2]) < tol &&
				std::abs(m[4]) < tol && std::abs(m[5] - 1.f) < tol && std::abs(m[6]) < tol &&
				std::abs(m[8]) < tol && std::abs(m[9]) < tol && std::abs(m[10] - 1.f) < tol;
			// CommonLibSF layout: scale at +0x3C = 16th float
			const bool scaleAt15 = m[15] > 0.5f && m[15] < 2.0f;
			if (rotId && scaleAt15) {
				SAF_LOG_INFO("[OFFSET] NiTransform found at +0x{:X} (CommonLibSF layout, scale={:.4f})", off, m[15]);
				return off;
			}
			// CommonLibSF with non-identity rotation (e.g. root node): any 3x3 + scale at m[15]
			// Rows should have length ~1 (rotation), scale 0.5..2
			const float r0 = m[0]*m[0]+m[1]*m[1]+m[2]*m[2];
			const float r1 = m[4]*m[4]+m[5]*m[5]+m[6]*m[6];
			const float r2 = m[8]*m[8]+m[9]*m[9]+m[10]*m[10];
			const bool validRot = (r0 > 0.7f && r0 < 1.3f && r1 > 0.7f && r1 < 1.3f && r2 > 0.7f && r2 < 1.3f);
			if (validRot && scaleAt15) {
				SAF_LOG_INFO("[OFFSET] NiTransform found at +0x{:X} (CommonLibSF, non-identity, scale={:.4f})", off, m[15]);
				return off;
			}
			// Packed layout: scale at 13th float
			const bool scaleAt12 = m[12] > 0.5f && m[12] < 2.0f;
			if (rotId && scaleAt12) {
				SAF_LOG_INFO("[OFFSET] NiTransform found at +0x{:X} (packed layout, scale={:.4f})", off, m[12]);
				return off;
			}
		}
		// Fallback: use CommonLibSF declared offset (local at 0x40)
		const auto defaultOff = reinterpret_cast<std::uintptr_t>(&a_node->local) -
			reinterpret_cast<std::uintptr_t>(a_node);
		SAF_LOG_WARN("[OFFSET] NiTransform scan failed - using CommonLibSF default offset +0x{:X}", defaultOff);
		return defaultOff;
	}

	// ── Quaternion helpers (plain structs, safe inside __try) ────────────────────
	struct Quat { float x, y, z, w; };
	static Quat QuatMul(Quat a, Quat b) {
		return { a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
		         a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
		         a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
		         a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z };
	}
	static Quat QuatConj(Quat q) { return { -q.x, -q.y, -q.z, q.w }; }
	static void QuatToMat(Quat q, float m[9]) {
		const float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
		const float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
		const float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
		m[0]=1.f-2.f*(yy+zz); m[1]=2.f*(xy-wz);    m[2]=2.f*(xz+wy);
		m[3]=2.f*(xy+wz);     m[4]=1.f-2.f*(xx+zz); m[5]=2.f*(yz-wx);
		m[6]=2.f*(xz-wy);     m[7]=2.f*(yz+wx);    m[8]=1.f-2.f*(xx+yy);
	}

	// NAF-style apply: write full local 4x4 (rotation + translation + scale) to NiNode::local,
	// like NAF Graph::PushAnimationOutput. No rest/game_base, no Y-up conversion (bez konwersji = postać się nie łamie).
	__declspec(noinline) static void ApplyTransformNAFFull(
		RE::NiAVObject* a_node,
		const Animation::Transform& a_anim,
		std::uintptr_t a_localOffset)
	{
		Quat q{ a_anim.rotation.x, a_anim.rotation.y, a_anim.rotation.z, a_anim.rotation.w };
		float rm[9];
		QuatToMat(q, rm);
		const float s = (a_anim.scale > 0.f && a_anim.scale < 1e6f) ? a_anim.scale : 1.f;
		__try {
			float* p = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(a_node) + a_localOffset);
			// Column-major 4x4 (ozz Float4x4 layout): col0, col1, col2, col3
			p[0] = rm[0] * s;  p[1] = rm[3] * s;  p[2] = rm[6] * s;  p[3] = 0.f;
			p[4] = rm[1] * s;  p[5] = rm[4] * s;  p[6] = rm[7] * s;  p[7] = 0.f;
			p[8] = rm[2] * s;  p[9] = rm[5] * s;  p[10] = rm[8] * s; p[11] = 0.f;
			p[12] = a_anim.translation.x; p[13] = a_anim.translation.y; p[14] = a_anim.translation.z; p[15] = 1.f;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			static std::atomic<uint32_t> s_c{ 0 };
			if (s_c++ < 3)
				SAF_LOG_WARN("[APPLY] NAF full 4x4 write AV node={:p} off=+0x{:X}", static_cast<void*>(a_node), a_localOffset);
		}
	}

	// Read 3×3 game bone rotation (row-major, stride-4) into a flat 9-float array.
	// Returns false on AV. No C++ objects in __try (C2712).
	__declspec(noinline) static bool ReadBoneRotation(RE::NiAVObject* a_node, std::uintptr_t a_off, float out[9])
	{
		__try {
			const float* p = reinterpret_cast<const float*>(
				reinterpret_cast<const std::uint8_t*>(a_node) + a_off);
			out[0]=p[0]; out[1]=p[1]; out[2]=p[2];
			out[3]=p[4]; out[4]=p[5]; out[5]=p[6];
			out[6]=p[8]; out[7]=p[9]; out[8]=p[10];
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Convert 3×3 row-major rotation matrix to quaternion (x,y,z,w).
	static void Mat3ToQuat(const float m[9], float& qx, float& qy, float& qz, float& qw)
	{
		const float t = m[0] + m[4] + m[8];
		if (t > 0.0f) {
			float s = 0.5f / std::sqrt(t + 1.0f);
			qw = 0.25f / s;
			qx = (m[7] - m[5]) * s;
			qy = (m[2] - m[6]) * s;
			qz = (m[3] - m[1]) * s;
		} else if (m[0] > m[4] && m[0] > m[8]) {
			float s = 2.0f * std::sqrt(1.0f + m[0] - m[4] - m[8]);
			qw = (m[7] - m[5]) / s;
			qx = 0.25f * s;
			qy = (m[1] + m[3]) / s;
			qz = (m[2] + m[6]) / s;
		} else if (m[4] > m[8]) {
			float s = 2.0f * std::sqrt(1.0f + m[4] - m[0] - m[8]);
			qw = (m[2] - m[6]) / s;
			qx = (m[1] + m[3]) / s;
			qy = 0.25f * s;
			qz = (m[5] + m[7]) / s;
		} else {
			float s = 2.0f * std::sqrt(1.0f + m[8] - m[0] - m[4]);
			qw = (m[3] - m[1]) / s;
			qx = (m[2] + m[6]) / s;
			qy = (m[5] + m[7]) / s;
			qz = 0.25f * s;
		}
	}

	// Read full NiTransform (rotation + translation) from node. NiTransform: rotate at a_off, translate at a_off+0x30.
	// Returns false on AV. Fills Animation::Transform (rotation as quat, translation, scale=1).
	__declspec(noinline) static bool ReadBoneFullTransform(RE::NiAVObject* a_node, std::uintptr_t a_off, Animation::Transform& out)
	{
		float rot9[9];
		if (!ReadBoneRotation(a_node, a_off, rot9))
			return false;
		__try {
			const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(a_node) + a_off;
			constexpr std::uintptr_t kTranslateOffset = 0x30;
			const float* trans = reinterpret_cast<const float*>(base + kTranslateOffset);
			Mat3ToQuat(rot9, out.rotation.x, out.rotation.y, out.rotation.z, out.rotation.w);
			out.translation.x = trans[0];
			out.translation.y = trans[1];
			out.translation.z = trans[2];
			out.scale = 1.0f;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// Restore 9-float rotation to node (NiMatrix3 layout). No C++ objects – safe for __try (C2712).
	__declspec(noinline) static void RestoreBoneRotationRaw(RE::NiAVObject* a_node, std::uintptr_t a_off, const float* a_rot9)
	{
		__try {
			float* p = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(a_node) + a_off);
			p[0]=a_rot9[0]; p[1]=a_rot9[1]; p[2]=a_rot9[2];  p[3]=0.f;
			p[4]=a_rot9[3]; p[5]=a_rot9[4]; p[6]=a_rot9[5];  p[7]=0.f;
			p[8]=a_rot9[6]; p[9]=a_rot9[7]; p[10]=a_rot9[8]; p[11]=0.f;
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}

	// True if joint name starts with L_ or R_ (for left/right arm fixes).
	static bool IsLeftArmJoint(const char* a_name) {
		if (!a_name || !a_name[0]) return false;
		return (a_name[0] == 'L' || a_name[0] == 'l') && (a_name[1] == '_' || a_name[1] == ' ');
	}
	static bool IsRightArmJoint(const char* a_name) {
		if (!a_name || !a_name[0]) return false;
		return (a_name[0] == 'R' || a_name[0] == 'r') && (a_name[1] == '_' || a_name[1] == ' ');
	}
	// True if joint is upper arm only (Clavicle, Biceps, Deltoid, Arm). Used for ArmsCorrectDirection so we don't rotate forearm/wrist/fingers (avoids deformation).
	static bool IsUpperArmOnlyJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* upper[] = { "Clavicle", "Biceps", "Deltoid", "Arm", nullptr };
		for (int h = 0; upper[h]; ++h) {
			const char* sub = upper[h];
			const char* p = a_name;
			while (*p) {
				const char* q = p; const char* s = sub;
				while (*q && *s && std::tolower(static_cast<unsigned char>(*q)) == std::tolower(static_cast<unsigned char>(*s))) { ++q; ++s; }
				if (!*s) return true;
				++p;
			}
		}
		return false;
	}
	// True if joint is used as reference for arm alignment (torso/chest). We store its rotation and align arms to its "forward".
	static bool IsTorsoRefJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* ref[] = { "C_Chest", "C_Spine2", "C_Neck", nullptr };
		for (int h = 0; ref[h]; ++h) {
			const char* sub = ref[h];
			const char* p = a_name;
			while (*p) {
				const char* q = p; const char* s = sub;
				while (*q && *s && std::tolower(static_cast<unsigned char>(*q)) == std::tolower(static_cast<unsigned char>(*s))) { ++q; ++s; }
				if (!*s) return true;
				++p;
			}
		}
		return false;
	}
	static void ClearTorsoRefForFrame() { g_torsoRefValid = false; }
	// True if joint name looks like arm or hand (for FlipHandRotation). Case-insensitive substring match. Whole arm: Clavicle, Biceps, Forearm, Arm, Wrist, fingers.
	static bool IsArmOrHandJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* arm[] = { "Clavicle", "Biceps", "Forearm", "Arm", "Wrist", "Cup", "Thumb", "Index", "Middle", "Ring", "Pinky", "Deltoid", "Elbow", nullptr };
		for (int h = 0; arm[h]; ++h) {
			const char* sub = arm[h];
			const char* p = a_name;
			while (*p) {
				const char* q = p; const char* s = sub;
				while (*q && *s && std::tolower(static_cast<unsigned char>(*q)) == std::tolower(static_cast<unsigned char>(*s))) { ++q; ++s; }
				if (!*s) return true;
				++p;
			}
		}
		return false;
	}
	// Tułów i nogi – gdy te kości „kręcą się wokół własnej osi”.
	static bool IsSpineOrLegJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* ref[] = {
			"C_Spine", "C_Spine1", "C_Spine2", "C_Chest", "C_Neck", "C_Head", "C_Hips", "C_Waist", "COM",
			"L_Thigh", "R_Thigh", "L_Calf", "R_Calf", "L_Foot", "R_Foot", "L_Toe", "R_Toe",
			nullptr
		};
		for (int h = 0; ref[h]; ++h) {
			if (std::strcmp(a_name, ref[h]) == 0) return true;
		}
		return false;
	}
	// Tylko łydki i stopy – gdy chcemy poprawkę tylko dla dolnych segmentów nóg.
	static bool IsCalfOrFootJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* ref[] = { "L_Calf", "R_Calf", "L_Foot", "R_Foot", "L_Toe", "R_Toe", nullptr };
		for (int h = 0; ref[h]; ++h) {
			if (std::strcmp(a_name, ref[h]) == 0) return true;
		}
		return false;
	}
	// Tylko kręgosłup/biodra (bez uda/łydka/stopa) – do SpineLegsSimpleAxisFix 5–6.
	static bool IsSpineOnlyJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* ref[] = { "C_Spine", "C_Spine1", "C_Spine2", "C_Chest", "C_Neck", "C_Head", "C_Hips", "C_Waist", "COM", nullptr };
		for (int h = 0; ref[h]; ++h) {
			if (std::strcmp(a_name, ref[h]) == 0) return true;
		}
		return false;
	}
	// Tylko nogi (uda, łydki, stopy) – do SpineLegsSimpleAxisFix 7–8.
	static bool IsLegOnlyJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* ref[] = { "L_Thigh", "R_Thigh", "L_Calf", "R_Calf", "L_Foot", "R_Foot", "L_Toe", "R_Toe", nullptr };
		for (int h = 0; ref[h]; ++h) {
			if (std::strcmp(a_name, ref[h]) == 0) return true;
		}
		return false;
	}
	// Kości dłoni – w trybie NAF nie nadpisujemy, żeby gra sterowała (IK). Zostają pod grą niezależnie od AnimateFaceJoints.
	static bool IsHandJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* sub[] = { "Cup", "Thumb", "Index", "Middle", "Ring", "Pinky", "Wrist", nullptr };
		for (int h = 0; sub[h]; ++h) {
			const char* subStr = sub[h];
			const char* p = a_name;
			while (*p) {
				const char* q = p, * s = subStr;
				while (*q && *s && std::tolower(static_cast<unsigned char>(*q)) == std::tolower(static_cast<unsigned char>(*s))) { ++q; ++s; }
				if (!*s) return true;
				++p;
			}
		}
		return false;
	}
	// Kości twarzy – Eye, Jaw, faceBone_* (usta/policzki/broda). Możemy nimi sterować z animacji, jeśli AnimateFaceJoints=1.
	static bool IsFaceJoint(const char* a_name)
	{
		if (!a_name || !*a_name) return false;
		const char* sub[] = { "Eye", "Jaw", "faceBone", nullptr };
		for (int h = 0; sub[h]; ++h) {
			const char* subStr = sub[h];
			const char* p = a_name;
			while (*p) {
				const char* q = p, * s = subStr;
				while (*q && *s && std::tolower(static_cast<unsigned char>(*q)) == std::tolower(static_cast<unsigned char>(*s))) { ++q; ++s; }
				if (!*s) return true;
				++p;
			}
		}
		return false;
	}

	// Apply animation to a bone using delta-rotation:
	//   delta  = inv(ozz_rest_rot) * ozz_anim_rot
	//   result = game_base_rot * delta   (game_base captured once at joint-map build time)
	//
	// This avoids accumulation (each frame we start from the ORIGINAL game rotation,
	// not the one we wrote last frame) and avoids the Y-up / Z-up mismatch
	// (delta is coordinate-system agnostic – it's just the movement from rest pose).
	// Translation and scale are never touched (owned by Havok/IK).
	__declspec(noinline) static void ApplyTransformRaw(
		RE::NiAVObject* a_node,
		const Animation::Transform& a_anim,
		const Animation::Transform& a_rest,
		const float a_gameBase[9],
		std::uintptr_t a_off,
		const char* a_jointName = nullptr)
	{
		float r00, r01, r02, r10, r11, r12, r20, r21, r22;

		// Y-up (GLTF) → Z-up (Creation Engine). Similarity C×delta×C⁻¹: T-pose I → C×I×C⁻¹ = I ✓; movement correctly in Z-up.
		auto ApplyYUpToZUp = [](const float m[9], float out[9]) {
			const bool flip = g_yUpToZUpConversionFlip.load(std::memory_order_acquire);
			const bool useSimilarity = g_yUpToZUpConversionConjugate.load(std::memory_order_acquire);
			if (useSimilarity) {
				// C × delta × C⁻¹ (C⁻¹ = C^T for rotation). C = -90° (flip=0) or +90° (flip=1) around X.
				if (flip) {
					out[0]=m[0]; out[1]=-m[2]; out[2]=m[1];
					out[3]=-m[6]; out[4]=m[8]; out[5]=-m[7];
					out[6]=m[3]; out[7]=-m[5]; out[8]=m[4];
				} else {
					out[0]=m[0]; out[1]=m[2]; out[2]=-m[1];
					out[3]=m[6]; out[4]=m[8]; out[5]=-m[7];
					out[6]=-m[3]; out[7]=-m[5]; out[8]=m[4];
				}
			} else {
				out[0]=m[0]; out[1]=m[1]; out[2]=m[2];
				if (flip) {
					out[3]=-m[6]; out[4]=-m[7]; out[5]=-m[8];
					out[6]=m[3]; out[7]=m[4]; out[8]=m[5];
				} else {
					out[3]=m[6]; out[4]=m[7]; out[5]=m[8];
					out[6]=-m[3]; out[7]=-m[4]; out[8]=-m[5];
				}
			}
		};
		// Z-up → Y-up (inverse of ApplyYUpToZUp). C^T × M × C. Same formula for both flip (inverse of C*M*C^T).
		auto ApplyZUpToYUp = [](const float m[9], float out[9]) {
			out[0]=m[0]; out[1]=-m[2]; out[2]=m[1];
			out[3]=-m[6]; out[4]=m[8]; out[5]=-m[7];
			out[6]=m[3]; out[7]=-m[5]; out[8]=m[4];
		};

		if (g_useNAFApplyMode.load(std::memory_order_acquire)) {
			// NAF-style apply: full 4x4 local (rotation + translation + scale) to NiNode::local,
			// like NAF PushAnimationOutput. No rest/game_base, no Y-up conversion, no spine/arm fixes.
			ApplyTransformNAFFull(a_node, a_anim, a_off);
			return;
		}
		if (g_applyAnimRotationOnly.load(std::memory_order_acquire)) {
			// Replace: write anim rotation only (with optional Y->Z). No game_base, no delta.
			const Quat aq{ a_anim.rotation.x, a_anim.rotation.y, a_anim.rotation.z, a_anim.rotation.w };
			float am[9];
			QuatToMat(aq, am);
			const float* d;
			float amConv[9];
			if (g_applyYUpToZUpConversion.load(std::memory_order_acquire)) {
				ApplyYUpToZUp(am, amConv);
				d = amConv;
			} else {
				d = am;
			}
			r00=d[0]; r01=d[1]; r02=d[2];
			r10=d[3]; r11=d[4]; r12=d[5];
			r20=d[6]; r21=d[7]; r22=d[8];
		} else {
			// delta = inv(ozz_rest) * ozz_anim  (rotation from rest to anim in GLTF/Y-up space)
			const Quat rq{ a_rest.rotation.x, a_rest.rotation.y, a_rest.rotation.z, a_rest.rotation.w };
			const Quat aq{ a_anim.rotation.x, a_anim.rotation.y, a_anim.rotation.z, a_anim.rotation.w };
			float dm[9];
			QuatToMat(QuatMul(QuatConj(rq), aq), dm);

			const bool doConversion = g_applyYUpToZUpConversion.load(std::memory_order_acquire);
			const bool unifiedYUp = g_unifiedYUpSpace.load(std::memory_order_acquire) && doConversion;
			// For arm joints, optional override of rotation order (0=global, 1=delta*base, 2=base*delta).
			bool orderDeltaThenBase = g_rotationOrderDeltaThenBase.load(std::memory_order_acquire);
			if (IsArmOrHandJoint(a_jointName)) {
				const int armOrder = g_armsRotationOrderOverride.load(std::memory_order_acquire);
				if (armOrder == 1) orderDeltaThenBase = true;
				else if (armOrder == 2) orderDeltaThenBase = false;
			}

			if (unifiedYUp) {
				// Multiply in Y-up: base_zup → Y, result_yup = delta*base (or base*delta), then result_yup → Z.
				float baseY[9], resY[9], resZ[9];
				ApplyZUpToYUp(a_gameBase, baseY);
				const float* d = dm;
				const float* b = baseY;
				if (orderDeltaThenBase) {
					resY[0]=d[0]*b[0]+d[1]*b[3]+d[2]*b[6]; resY[1]=d[0]*b[1]+d[1]*b[4]+d[2]*b[7]; resY[2]=d[0]*b[2]+d[1]*b[5]+d[2]*b[8];
					resY[3]=d[3]*b[0]+d[4]*b[3]+d[5]*b[6]; resY[4]=d[3]*b[1]+d[4]*b[4]+d[5]*b[7]; resY[5]=d[3]*b[2]+d[4]*b[5]+d[5]*b[8];
					resY[6]=d[6]*b[0]+d[7]*b[3]+d[8]*b[6]; resY[7]=d[6]*b[1]+d[7]*b[4]+d[8]*b[7]; resY[8]=d[6]*b[2]+d[7]*b[5]+d[8]*b[8];
				} else {
					resY[0]=b[0]*d[0]+b[1]*d[3]+b[2]*d[6]; resY[1]=b[0]*d[1]+b[1]*d[4]+b[2]*d[7]; resY[2]=b[0]*d[2]+b[1]*d[5]+b[2]*d[8];
					resY[3]=b[3]*d[0]+b[4]*d[3]+b[5]*d[6]; resY[4]=b[3]*d[1]+b[4]*d[4]+b[5]*d[7]; resY[5]=b[3]*d[2]+b[4]*d[5]+b[5]*d[8];
					resY[6]=b[6]*d[0]+b[7]*d[3]+b[8]*d[6]; resY[7]=b[6]*d[1]+b[7]*d[4]+b[8]*d[7]; resY[8]=b[6]*d[2]+b[7]*d[5]+b[8]*d[8];
				}
				ApplyYUpToZUp(resY, resZ);
				r00=resZ[0]; r01=resZ[1]; r02=resZ[2];
				r10=resZ[3]; r11=resZ[4]; r12=resZ[5];
				r20=resZ[6]; r21=resZ[7]; r22=resZ[8];
			} else {
				// Legacy: convert delta to Z-up, multiply with game_base (Z-up).
				const float* d;
				float dmConv[9];
				if (doConversion) {
					ApplyYUpToZUp(dm, dmConv);
					d = dmConv;
				} else {
					d = dm;
				}
				const float* b = a_gameBase;
				if (orderDeltaThenBase) {
					r00=d[0]*b[0]+d[1]*b[3]+d[2]*b[6]; r01=d[0]*b[1]+d[1]*b[4]+d[2]*b[7]; r02=d[0]*b[2]+d[1]*b[5]+d[2]*b[8];
					r10=d[3]*b[0]+d[4]*b[3]+d[5]*b[6]; r11=d[3]*b[1]+d[4]*b[4]+d[5]*b[7]; r12=d[3]*b[2]+d[4]*b[5]+d[5]*b[8];
					r20=d[6]*b[0]+d[7]*b[3]+d[8]*b[6]; r21=d[6]*b[1]+d[7]*b[4]+d[8]*b[7]; r22=d[6]*b[2]+d[7]*b[5]+d[8]*b[8];
				} else {
					r00=b[0]*d[0]+b[1]*d[3]+b[2]*d[6]; r01=b[0]*d[1]+b[1]*d[4]+b[2]*d[7]; r02=b[0]*d[2]+b[1]*d[5]+b[2]*d[8];
					r10=b[3]*d[0]+b[4]*d[3]+b[5]*d[6]; r11=b[3]*d[1]+b[4]*d[4]+b[5]*d[7]; r12=b[3]*d[2]+b[4]*d[5]+b[5]*d[8];
					r20=b[6]*d[0]+b[7]*d[3]+b[8]*d[6]; r21=b[6]*d[1]+b[7]*d[4]+b[8]*d[7]; r22=b[6]*d[2]+b[7]*d[5]+b[8]*d[8];
				}
			}
		}

		// Optional: flip 180° around X for arm+hand joints (fixes "arms/hands animate wrong direction").
		if (g_flipHandRotation.load(std::memory_order_acquire) && IsArmOrHandJoint(a_jointName)) {
			r01 = -r01; r02 = -r02;
			r11 = -r11; r12 = -r12;
			r21 = -r21; r22 = -r22;
		}
		// Optional: flip 180° around Y for arm+hand joints (fixes "arms inside body" -> arms in front of model).
		if (g_armsInFrontFix.load(std::memory_order_acquire) && IsArmOrHandJoint(a_jointName)) {
			r00 = -r00; r02 = -r02;
			r10 = -r10; r12 = -r12;
			r20 = -r20; r22 = -r22;
		}
		// Optional: rotate -90° around Y for arm+hand joints – ręce z "w prawo" na "do przodu" (obie ręce tak samo).
		if (g_armsForwardFix.load(std::memory_order_acquire) && IsArmOrHandJoint(a_jointName)) {
			// -90° around Y: col0'=col2, col1'=col1, col2'=-col0
			float t00 = r02, t01 = r01, t02 = -r00;
			float t10 = r12, t11 = r11, t12 = -r10;
			float t20 = r22, t21 = r21, t22 = -r20;
			r00 = t00; r01 = t01; r02 = t02;
			r10 = t10; r11 = t11; r12 = t12;
			r20 = t20; r21 = t21; r22 = t22;
		}
		// Optional: rotate -90° around Z for arm+hand joints – ręce z "w prawo" na "do przodu".
		if (g_armsForwardFixZ.load(std::memory_order_acquire) && IsArmOrHandJoint(a_jointName)) {
			// -90° around Z: col0'=-col1, col1'=col0, col2'=col2
			float t00 = -r01, t01 = r00, t02 = r02;
			float t10 = -r11, t11 = r10, t12 = r12;
			float t20 = -r21, t21 = r20, t22 = r22;
			r00 = t00; r01 = t01; r02 = t02;
			r10 = t10; r11 = t11; r12 = t12;
			r20 = t20; r21 = t21; r22 = t22;
		}
		// Prosta poprawka osi: 0=off, 1–4=kręgosłup+nogi Rx/Ry±90°, 5–6=tylko kręgosłup, 7–8=tylko nogi.
		const int spineFix = g_spineLegsSimpleAxisFix.load(std::memory_order_acquire);
		if (spineFix != 0) {
			bool apply = false;
			if (spineFix >= 1 && spineFix <= 4)
				apply = IsSpineOrLegJoint(a_jointName);
			else if (spineFix >= 5 && spineFix <= 6)
				apply = IsSpineOnlyJoint(a_jointName);
			else if (spineFix >= 7 && spineFix <= 8)
				apply = IsLegOnlyJoint(a_jointName);
			if (apply) {
				float t00, t01, t02, t10, t11, t12, t20, t21, t22;
				switch (spineFix) {
				case 1:
					t00 = r00; t01 = -r02; t02 = r01;
					t10 = r10; t11 = -r12; t12 = r11;
					t20 = r20; t21 = -r22; t22 = r21;
					break;
				case 2:
					t00 = r00; t01 = r02; t02 = -r01;
					t10 = r10; t11 = r12; t12 = -r11;
					t20 = r20; t21 = r22; t22 = -r21;
					break;
				case 3:
					t00 = -r02; t01 = r01; t02 = r00;
					t10 = -r12; t11 = r11; t12 = r10;
					t20 = -r22; t21 = r21; t22 = r20;
					break;
				case 4:
					t00 = r02; t01 = r01; t02 = -r00;
					t10 = r12; t11 = r11; t12 = -r10;
					t20 = r22; t21 = r21; t22 = -r20;
					break;
				case 5:
					t00 = r00; t01 = -r02; t02 = r01;
					t10 = r10; t11 = -r12; t12 = r11;
					t20 = r20; t21 = -r22; t22 = r21;
					break;
				case 6:
					t00 = r00; t01 = r02; t02 = -r01;
					t10 = r10; t11 = r12; t12 = -r11;
					t20 = r20; t21 = r22; t22 = -r21;
					break;
				case 7:
					t00 = r00; t01 = -r02; t02 = r01;
					t10 = r10; t11 = -r12; t12 = r11;
					t20 = r20; t21 = -r22; t22 = r21;
					break;
				case 8:
					t00 = r00; t01 = r02; t02 = -r01;
					t10 = r10; t11 = r12; t12 = -r11;
					t20 = r20; t21 = r22; t22 = -r21;
					break;
				default:
					t00 = r00; t01 = r01; t02 = r02;
					t10 = r10; t11 = r11; t12 = r12;
					t20 = r20; t21 = r21; t22 = r22;
					break;
				}
				r00 = t00; r01 = t01; r02 = t02;
				r10 = t10; r11 = t11; r12 = t12;
				r20 = t20; r21 = t21; r22 = t22;
			}
		}

		// Yaw flip 180° dla kręgosłupa/nóg (obrót „w prawo/lewo” zdejmowany po wszystkich innych poprawkach).
		const int spineYaw = g_spineLegsYawFlip.load(std::memory_order_acquire);
		if (spineYaw != 0 && a_jointName) {
			bool applyYaw = false;
			if (spineYaw == 1) {
				applyYaw = IsSpineOrLegJoint(a_jointName);
			} else if (spineYaw == 2) {
				applyYaw = IsSpineOnlyJoint(a_jointName);
			} else if (spineYaw == 3) {
				applyYaw = IsLegOnlyJoint(a_jointName);
			}
			if (applyYaw) {
				// R * Rz(180°): yaw wokół osi Z – odwraca „przód/tył” kości.
				const float t00 = -r00, t01 = -r01, t02 =  r02;
				const float t10 = -r10, t11 = -r11, t12 =  r12;
				const float t20 = -r20, t21 = -r21, t22 =  r22;
				r00 = t00; r01 = t01; r02 = t02;
				r10 = t10; r11 = t11; r12 = t12;
				r20 = t20; r21 = t21; r22 = t22;
			}
		}
		// Zamiana osi – może wektor X w grze jest przesunięty o 90° względem naszego.
		const int axisFix = g_armsAxisFix.load(std::memory_order_acquire);
		if (axisFix != 0 && IsArmOrHandJoint(a_jointName)) {
			float t00, t01, t02, t10, t11, t12, t20, t21, t22;
			switch (axisFix) {
			case 1:
				// Swap X↔Y (col0 <-> col1)
				t00 = r01; t01 = r00; t02 = r02;
				t10 = r11; t11 = r10; t12 = r12;
				t20 = r21; t21 = r20; t22 = r22;
				break;
			case 2:
				// Cycle X→Y→Z→X
				t00 = r01; t01 = r02; t02 = r00;
				t10 = r11; t11 = r12; t12 = r10;
				t20 = r21; t21 = r22; t22 = r20;
				break;
			case 3:
				// Swap X↔Z (col0 <-> col2)
				t00 = r02; t01 = r01; t02 = r00;
				t10 = r12; t11 = r11; t12 = r10;
				t20 = r22; t21 = r21; t22 = r20;
				break;
			case 4:
				// Swap Y↔Z (col1 <-> col2)
				t00 = r00; t01 = r02; t02 = r01;
				t10 = r10; t11 = r12; t12 = r11;
				t20 = r20; t21 = r22; t22 = r21;
				break;
			case 5:
				// Cycle X→Z→Y→X (col0'=col2, col1'=col0, col2'=col1)
				t00 = r02; t01 = r00; t02 = r01;
				t10 = r12; t11 = r10; t12 = r11;
				t20 = r22; t21 = r20; t22 = r21;
				break;
			default:
				t00 = r00; t01 = r01; t02 = r02;
				t10 = r10; t11 = r11; t12 = r12;
				t20 = r20; t21 = r21; t22 = r22;
				break;
			}
			r00 = t00; r01 = t01; r02 = t02;
			r10 = t10; r11 = t11; r12 = t12;
			r20 = t20; r21 = t21; r22 = t22;
		}

		// ArmsCorrectDirection: R*Rz(±90° or 180°) only for upper arm (Clavicle/Biceps/Deltoid/Arm) so direction matches; forearm/wrist/fingers unchanged to avoid deformation.
		const int armsCorrect = g_armsCorrectDirection.load(std::memory_order_acquire);
		if (armsCorrect != 0 && IsUpperArmOnlyJoint(a_jointName)) {
			float t00, t01, t02, t10, t11, t12, t20, t21, t22;
			switch (armsCorrect) {
			case 1:
				// R * Rz(-90°): col0'=col1, col1'=-col0, col2'=col2
				t00 = r01; t01 = -r00; t02 = r02;
				t10 = r11; t11 = -r10; t12 = r12;
				t20 = r21; t21 = -r20; t22 = r22;
				break;
			case 2:
				// R * Rz(+90°): col0'=-col1, col1'=col0, col2'=col2
				t00 = -r01; t01 = r00; t02 = r02;
				t10 = -r11; t11 = r10; t12 = r12;
				t20 = -r21; t21 = r20; t22 = r22;
				break;
			case 3:
				// R * Rz(180°): col0'=-col0, col1'=-col1, col2'=col2
				t00 = -r00; t01 = -r01; t02 = r02;
				t10 = -r10; t11 = -r11; t12 = r12;
				t20 = -r20; t21 = -r21; t22 = r22;
				break;
			case 4:
				// R * Rx(180°): col0'=col0, col1'=-col1, col2'=-col2 – przeciwny obrót, ręce przed modelem
				t00 = r00; t01 = -r01; t02 = -r02;
				t10 = r10; t11 = -r11; t12 = -r12;
				t20 = r20; t21 = -r21; t22 = -r22;
				break;
			default:
				t00 = r00; t01 = r01; t02 = r02;
				t10 = r10; t11 = r11; t12 = r12;
				t20 = r20; t21 = r21; t22 = r22;
				break;
			}
			r00 = t00; r01 = t01; r02 = t02;
			r10 = t10; r11 = t11; r12 = t12;
			r20 = t20; r21 = t21; r22 = t22;
		}

		// Align upper arm "forward" (column 1) to torso reference (C_Chest etc.) so arms follow body direction.
		if (g_armsAlignToTorso.load(std::memory_order_acquire) && g_torsoRefValid && IsUpperArmOnlyJoint(a_jointName)) {
			const float* tr = g_torsoRefRotation;
			float vax = r01, vay = r11, vaz = r21;
			float vtx = tr[1], vty = tr[4], vtz = tr[7];
			float la = std::sqrt(vax*vax + vay*vay + vaz*vaz);
			float lt = std::sqrt(vtx*vtx + vty*vty + vtz*vtz);
			if (la > 1e-6f && lt > 1e-6f) {
				vax /= la; vay /= la; vaz /= la;
				vtx /= lt; vty /= lt; vtz /= lt;
				float kx = vay*vtz - vaz*vty, ky = vaz*vtx - vax*vtz, kz = vax*vty - vay*vtx;
				float kl = std::sqrt(kx*kx + ky*ky + kz*kz);
				if (kl > 1e-6f) {
					kx /= kl; ky /= kl; kz /= kl;
					float dot = vax*vtx + vay*vty + vaz*vtz;
					float angle = std::acos((dot < -1.f) ? -1.f : (dot > 1.f) ? 1.f : dot);
					float s = std::sin(angle), c = 1.f - std::cos(angle);
					// Rodrigues: R_align = I + s*K + c*K^2
					float R00 = 1.f - c*(ky*ky + kz*kz), R01 = c*kx*ky - s*kz,     R02 = c*kx*kz + s*ky;
					float R10 = c*kx*ky + s*kz,     R11 = 1.f - c*(kx*kx + kz*kz), R12 = c*ky*kz - s*kx;
					float R20 = c*kx*kz - s*ky,     R21 = c*ky*kz + s*kx,     R22 = 1.f - c*(kx*kx + ky*ky);
					float n00 = R00*r00 + R01*r10 + R02*r20, n01 = R00*r01 + R01*r11 + R02*r21, n02 = R00*r02 + R01*r12 + R02*r22;
					float n10 = R10*r00 + R11*r10 + R12*r20, n11 = R10*r01 + R11*r11 + R12*r21, n12 = R10*r02 + R11*r12 + R12*r22;
					float n20 = R20*r00 + R21*r10 + R22*r20, n21 = R20*r01 + R21*r11 + R22*r21, n22 = R20*r02 + R21*r12 + R22*r22;
					r00 = n00; r01 = n01; r02 = n02;
					r10 = n10; r11 = n11; r12 = n12;
					r20 = n20; r21 = n21; r22 = n22;
				}
			}
		}

		// Store torso rotation for this frame (so arms can align to it). Do this after all corrections, before write.
		if (IsTorsoRefJoint(a_jointName)) {
			g_torsoRefRotation[0]=r00; g_torsoRefRotation[1]=r01; g_torsoRefRotation[2]=r02;
			g_torsoRefRotation[3]=r10; g_torsoRefRotation[4]=r11; g_torsoRefRotation[5]=r12;
			g_torsoRefRotation[6]=r20; g_torsoRefRotation[7]=r21; g_torsoRefRotation[8]=r22;
			g_torsoRefValid = true;
		}

		// Optional: transpose 3x3 for arm joints only (R→R^T). Try if engine expects opposite row/column for limbs.
		if (g_transposeArmsOnly.load(std::memory_order_acquire) && IsArmOrHandJoint(a_jointName)) {
			float t01 = r01, t02 = r02, t10 = r10, t12 = r12, t20 = r20, t21 = r21;
			r01 = t10; r02 = t20; r10 = t01; r12 = t21; r20 = t02; r21 = t12;
		}

		const bool transpose = g_transposeOutputRotation.load(std::memory_order_acquire);
		const bool useApi = g_useNiNodeSetLocalTransform.load(std::memory_order_acquire);

		if (useApi) {
			RE::NiNode* niNode = a_node->GetAsNiNode();
			if (niNode) {
				__try {
					RE::NiTransform local = niNode->local;
					if (transpose) {
						local.rotate[0][0]=r00; local.rotate[0][1]=r10; local.rotate[0][2]=r20;
						local.rotate[1][0]=r01; local.rotate[1][1]=r11; local.rotate[1][2]=r21;
						local.rotate[2][0]=r02; local.rotate[2][1]=r12; local.rotate[2][2]=r22;
					} else {
						local.rotate[0][0]=r00; local.rotate[0][1]=r01; local.rotate[0][2]=r02;
						local.rotate[1][0]=r10; local.rotate[1][1]=r11; local.rotate[1][2]=r12;
						local.rotate[2][0]=r20; local.rotate[2][1]=r21; local.rotate[2][2]=r22;
					}
					niNode->SetLocalTransform(local);
					return;
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					static std::atomic<uint32_t> s_apiFallback{0};
					if (s_apiFallback++ == 0)
						SAF_LOG_WARN("[APPLY] SetLocalTransform AV, using raw write for this session");
				}
			}
		}

		__try {
			float* p = reinterpret_cast<float*>(
				reinterpret_cast<std::uint8_t*>(a_node) + a_off);
			if (transpose) {
				p[0]=r00; p[1]=r10; p[2]=r20; p[3]=0.f;
				p[4]=r01; p[5]=r11; p[6]=r21; p[7]=0.f;
				p[8]=r02; p[9]=r12; p[10]=r22; p[11]=0.f;
			} else {
				p[0]=r00; p[1]=r01; p[2]=r02; p[3]=0.f;
				p[4]=r10; p[5]=r11; p[6]=r12; p[7]=0.f;
				p[8]=r20; p[9]=r21; p[10]=r22; p[11]=0.f;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			static std::atomic<uint32_t> s_c{0};
			if (s_c++ < 3)
				SAF_LOG_WARN("[APPLY] AV writing rotation to node={:p} off=+0x{:X}", static_cast<void*>(a_node), a_off);
		}
	}

	static void ApplyTransform(RE::NiAVObject* a_node,
		const Animation::Transform& a_anim,
		const Animation::Transform& a_rest,
		const float a_gameBase[9],
		const char* a_jointName = nullptr)
	{
		if (!a_node) return;
		if (!g_niTransformSearchDone.load(std::memory_order_acquire)) {
			if (!g_niTransformSearchDone.exchange(true, std::memory_order_acq_rel)) {
				g_niTransformOffset.store(ProbeNiTransformOffset(a_node), std::memory_order_release);
			}
		}
		ApplyTransformRaw(a_node, a_anim, a_rest, a_gameBase, g_niTransformOffset.load(std::memory_order_acquire), a_jointName);
	}

	/// Returns true if p looks like a valid NiAVObject (GetAsNiNode works). Used to reject wrong raw-read ptrs (e.g. handleList). No C++ objects with destructors.
	static bool Validate3DRoot(RE::NiAVObject* p)
	{
		if (!p) return false;
		__try {
			return p->GetAsNiNode() != nullptr;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	/// Try to get actor 3D root. Prefer game's Get3D vfunc (Unk_AC); then raw loadedData->data3D.
	/// Never use 0xC8 – that is extraDataList, not 3D (causes AV on traverse).
	static RE::NiAVObject* GetActor3DRootRaw(RE::Actor* a_actor)
	{
		if (!a_actor) return nullptr;
		const auto* base = reinterpret_cast<const std::uint8_t*>(a_actor);
		RE::NiAVObject* root = nullptr;

		// 1) Call game's Get3D vfunc (Unk_AC). No C++ objects in __try (C2712). NiPointer is just one pointer; pass buffer.
		__try {
			void** vtable = *reinterpret_cast<void***>(a_actor);
			constexpr std::size_t kGet3DIndex = 0xAC;
			using Get3DFn = void (*)(RE::TESObjectREFR* self, void* outNiPointer);
			Get3DFn fn = reinterpret_cast<Get3DFn>(vtable[kGet3DIndex]);
			std::uintptr_t outPtr = 0;
			fn(static_cast<RE::TESObjectREFR*>(a_actor), &outPtr);
			root = reinterpret_cast<RE::NiAVObject*>(outPtr);
			if (root != nullptr && Validate3DRoot(root)) {
				static std::atomic<bool> s_logged{ false };
				if (s_logged.exchange(true) == false)
					SAF_LOG_INFO("[3D] GetActor3DRootRaw: using Get3D vfunc (Unk_AC)");
				return root;
			}
			root = nullptr;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			root = nullptr;
		}

		// 2) Raw read: BSGuarded may put LOADED_REF_DATA* at 0xC0 (lock first) or 0xB8 (pointer first). data3D at *ptr+0x08.
		const std::uintptr_t kLoadedDataPtrOffsets[] = { 0xC0, 0xB8 };
		for (std::uintptr_t ptrOff : kLoadedDataPtrOffsets) {
			__try {
				void* loadedDataPtr = nullptr;
				std::memcpy(&loadedDataPtr, base + ptrOff, sizeof(loadedDataPtr));
				if (loadedDataPtr != nullptr) {
					std::memcpy(&root, static_cast<const std::uint8_t*>(loadedDataPtr) + 0x08, sizeof(root));
					if (root != nullptr && Validate3DRoot(root)) {
						static std::atomic<bool> s_logged{ false };
						if (s_logged.exchange(true) == false)
							SAF_LOG_INFO("[3D] GetActor3DRootRaw: using loadedData->data3D (actor+0x{:X} -> +0x08)", ptrOff);
						return root;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				root = nullptr;
			}
		}

		// 3) Direct offsets (some builds).
		static const std::uintptr_t kDirectOffsets[] = { 0xB8 + 0x08, 0xB8 + 0x00 };
		for (std::uintptr_t off : kDirectOffsets) {
			root = nullptr;
			std::memcpy(&root, base + off, sizeof(root));
			if (root != nullptr && Validate3DRoot(root))
				return root;
		}

		static std::atomic<bool> s_loggedRawReadFailure{ false };
		if (s_loggedRawReadFailure.exchange(true) == false) {
			SAF_LOG_WARN("[3D] GetActor3DRootRaw: no 3D root. Dumping actor+0xB8={:p}, +0xC0={:p}.",
				*reinterpret_cast<const void* const*>(base + 0xB8),
				*reinterpret_cast<const void* const*>(base + 0xC0));
		}
		return nullptr;
	}

	static void BuildJointMap(RE::Actor* a_actor, const Animation::OzzSkeleton& a_skeleton, std::vector<RE::NiAVObject*>& a_out)
	{
		a_out.clear();
		a_out.resize(a_skeleton.jointNames.size(), nullptr);
		if (!a_actor) { SAF_LOG_WARN("BuildJointMap: actor is null"); return; }

		RE::NiAVObject* root = GetActor3DRootRaw(a_actor);
		if (!root) {
			static std::atomic<uint32_t> s_c{0};
			if (s_c++ < 3) SAF_LOG_WARN("BuildJointMap: actor has no 3D");
			return;
		}

		// Restrict bone search to a subtree (e.g. "NPC" = body only), so we don't match bones from weapons/attachments (fixes leg/arm mix-up).
		RE::NiAVObject* searchRoot = root;
		if (!g_skeletonRootName.empty()) {
			if (auto* skelRoot = root->GetObjectByName(RE::BSFixedString(g_skeletonRootName.c_str()))) {
				searchRoot = skelRoot;
			} else {
				static std::atomic<bool> s_loggedSkelRootMissing{ false };
				if (!s_loggedSkelRootMissing.exchange(true))
					SAF_LOG_WARN("BuildJointMap: SkeletonRootName '{}' not found, searching from actor root", g_skeletonRootName);
			}
		}

		SAF_LOG_INFO("BuildJointMap: start actor={}, joints={}, searchRoot={}", static_cast<void*>(a_actor), a_skeleton.jointNames.size(), searchRoot == root ? "actor root" : "skeleton subtree");

		// Use game's GetObjectByName so bones are found only under searchRoot (avoids matching same name from weapon/attachment).
		size_t found = 0, aliasHits = 0, missing = 0;
		std::vector<std::string> missingNames;
		missingNames.reserve(16);

		for (size_t i = 0; i < a_skeleton.jointNames.size(); ++i) {
			const auto& name = a_skeleton.jointNames[i];
			// Try 1: exact name
			if (!name.empty()) {
				RE::BSFixedString bs(name.c_str());
				if (auto* n = searchRoot->GetObjectByName(bs)) { a_out[i] = n; ++found; continue; }
			}
			// Try 2: aliases exact
			if (i < a_skeleton.jointAliases.size()) {
				for (const auto& alias : a_skeleton.jointAliases[i]) {
					if (alias.empty()) continue;
					RE::BSFixedString bs(alias.c_str());
					if (auto* n = searchRoot->GetObjectByName(bs)) { a_out[i] = n; ++found; ++aliasHits; break; }
				}
				if (a_out[i]) continue;
			}
			// Try 2b: "NPC Name With Spaces" (e.g. L_Clavicle -> "NPC L Clavicle") – common in Creation Engine
			if (!name.empty()) {
				std::string withSpaces;
				withSpaces.reserve(name.size() + 4);
				for (char c : name) withSpaces.push_back(c == '_' ? ' ' : c);
				std::string npcName = "NPC " + withSpaces;
				if (auto* n = searchRoot->GetObjectByName(RE::BSFixedString(npcName.c_str()))) { a_out[i] = n; ++found; continue; }
			}
			// Try 2c: "NPC " + original name (keep underscores) – SFF / extended skeleton often uses e.g. "NPC L_Clavicle"
			if (!name.empty()) {
				std::string npcOrig = "NPC " + name;
				if (auto* n = searchRoot->GetObjectByName(RE::BSFixedString(npcOrig.c_str()))) { a_out[i] = n; ++found; continue; }
			}
			// Try 3: normalized (strip "NPC ", lowercase)
			{
				std::string norm = NormalizeBoneName(name);
				if (!norm.empty() && norm != name) {
					if (auto* n = searchRoot->GetObjectByName(RE::BSFixedString(norm.c_str()))) { a_out[i] = n; ++found; continue; }
					std::string npc = "NPC " + norm;
					if (auto* n = searchRoot->GetObjectByName(RE::BSFixedString(npc.c_str()))) { a_out[i] = n; ++found; continue; }
				}
			}
			// Try 4: aliases normalized
			if (i < a_skeleton.jointAliases.size()) {
				for (const auto& alias : a_skeleton.jointAliases[i]) {
					if (alias.empty()) continue;
					std::string norm = NormalizeBoneName(alias);
					if (norm.empty()) continue;
					if (auto* n = searchRoot->GetObjectByName(RE::BSFixedString(norm.c_str()))) { a_out[i] = n; ++found; ++aliasHits; break; }
				}
				if (a_out[i]) continue;
			}
			++missing;
			if (missingNames.size() < 20) missingNames.emplace_back(name);
		}

		// Map missing root joint (index 0) to actor 3D root so root-only animations (e.g. R_ArmUp on joint 0) apply.
		// Game often has no node named "HumanRace" or "Root_", so joint 0 would otherwise stay nullptr.
		if (root && a_skeleton.jointNames.size() > 0 && !a_out[0]) {
			a_out[0] = root;
			++found;
			static std::atomic<bool> s_loggedRootFallback{ false };
			if (!s_loggedRootFallback.exchange(true)) {
				SAF_LOG_INFO("[3D] BuildJointMap: joint 0 missing in game -> mapped to actor 3D root (root animations will apply)");
			}
		}
		// HumanRace.json: joint 0 = "HumanRace" (file stem), joint 1 = "Root_". Game often has neither; if "Root_" still missing and joint 0 mapped to root, use same node.
		if (a_skeleton.jointNames.size() > 1 && !a_out[1] && a_out[0] &&
		    a_skeleton.jointNames[1] == "Root_") {
			a_out[1] = a_out[0];
			++found;
			static std::atomic<bool> s_loggedRoot1Fallback{ false };
			if (!s_loggedRoot1Fallback.exchange(true)) {
				SAF_LOG_INFO("[3D] BuildJointMap: joint 1 'Root_' missing -> mapped to same node as joint 0 (HumanRace.json)");
			}
		}

		SAF_LOG_INFO("BuildJointMap: DONE found={}/{}, aliasHits={}, missing={}",
			found, a_skeleton.jointNames.size(), aliasHits, missing);
		if (missing > 0) {
			std::string s;
			for (size_t i = 0; i < missingNames.size(); ++i) { if (i) s += ", "; s += missingNames[i]; }
			SAF_LOG_WARN("BuildJointMap: missing joints: {}", s);
			// If any missing joint is arm-related, hint that this can cause "arms wrong direction"
			const auto isArmRelated = [](const std::string& n) {
				if (n.find("Clavicle") != std::string::npos) return true;
				if (n.find("Biceps") != std::string::npos) return true;
				if (n.find("Deltoid") != std::string::npos) return true;
				if (n.find("Forearm") != std::string::npos) return true;
				if (n.find("Arm") != std::string::npos) return true;
				if (n.find("Wrist") != std::string::npos) return true;
				if (n.find("Elbow") != std::string::npos) return true;
				return false;
			};
			for (const auto& mn : missingNames) {
				if (isArmRelated(mn)) {
					SAF_LOG_WARN("BuildJointMap: missing arm-related joint '{}' – add aliases in skeleton JSON or fix name; unmapped arm bones can cause arms to point wrong (e.g. right instead of forward)", mn);
					break;
				}
			}
		}
	}

	/// SEH wrapper so UpdateGraphs (which has lock_guard) can avoid C2712. No locals with destructors in this function.
	static bool SafeBuildJointMap(RE::Actor* a_actor, const Animation::OzzSkeleton& a_skeleton, std::vector<RE::NiAVObject*>& a_out)
	{
		__try {
			BuildJointMap(a_actor, a_skeleton, a_out);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			if (g_loggedAvActorsBuildJointMap.insert(a_actor).second) {
				SAF_LOG_WARN("[UPDATE] UpdateGraphs: access violation building joint map (loadedData?) for actor {:X}, will not retry. Animation will not play until 3D access is fixed for this game build.", a_actor ? a_actor->GetFormID() : 0);
			}
			return false;
		}
	}

	/// Call UpdateWorldData on root; if a_modifiedBones is non-empty and UseUpdateTransformAndBounds is true,
	/// call UpdateTransformAndBounds on each modified bone (never on root) so the renderer sees animation without the character disappearing.
	static void SafeUpdateWorldData(RE::Actor* a_actor, RE::NiAVObject* a_actorRoot, const std::vector<RE::NiAVObject*>* a_modifiedBones)
	{
		if (!a_actor) return;
		RE::NiAVObject* root = a_actorRoot ? a_actorRoot : GetActor3DRootRaw(a_actor);
		if (root) {
			RE::NiUpdateData ud;
			ud.flags = 0x1;
			root->UpdateWorldData(&ud);
			if (g_useUpdateTransformAndBounds.load(std::memory_order_acquire)) {
				if (a_modifiedBones && !a_modifiedBones->empty()) {
					for (RE::NiAVObject* node : *a_modifiedBones) {
						if (node && node != root)
							node->UpdateTransformAndBounds(&ud);
					}
				}
				// Update on root can make animation visible but may cause full-body inversion (e.g. R_ArmUp flips character). INI: UseUpdateTransformAndBoundsOnRoot=0 to disable.
				if (g_useUpdateTransformAndBoundsOnRoot.load(std::memory_order_acquire))
					root->UpdateTransformAndBounds(&ud);
			}
		}
	}

	static void UnpackSoaTransforms(std::span<const ozz::math::SoaTransform> a_soa, std::vector<Animation::Transform>& a_out, size_t a_jointCount)
	{
		a_out.resize(a_jointCount);
		for (size_t soaIdx = 0; soaIdx < a_soa.size(); ++soaIdx) {
			const auto& s = a_soa[soaIdx];
			float tx[4], ty[4], tz[4];
			float rx[4], ry[4], rz[4], rw[4];
			float sx[4], sy[4], sz[4];

			ozz::math::StorePtr(s.translation.x, tx);
			ozz::math::StorePtr(s.translation.y, ty);
			ozz::math::StorePtr(s.translation.z, tz);

			ozz::math::StorePtr(s.rotation.x, rx);
			ozz::math::StorePtr(s.rotation.y, ry);
			ozz::math::StorePtr(s.rotation.z, rz);
			ozz::math::StorePtr(s.rotation.w, rw);

			ozz::math::StorePtr(s.scale.x, sx);
			ozz::math::StorePtr(s.scale.y, sy);
			ozz::math::StorePtr(s.scale.z, sz);

			for (int lane = 0; lane < 4; ++lane) {
				size_t idx = soaIdx * 4 + static_cast<size_t>(lane);
				if (idx >= a_jointCount) break;
				auto& t = a_out[idx];
				t.translation = { tx[lane], ty[lane], tz[lane] };
				t.rotation = { rx[lane], ry[lane], rz[lane], rw[lane] };
				t.scale = sx[lane];
			}
		}
	}

	static int64_t GraphUpdateHookImpl(void* a_this, void* a_param2, void* a_param3)
	{
		static auto lastTick = std::chrono::steady_clock::now();
		static std::atomic<uint32_t> hookCallCount{ 0 };
		uint32_t callNum = ++hookCallCount;

		// Skip our logic until player is in world (avoids crash when loading save - world/actor 3D not ready yet).
		const uint32_t skipConfig = g_skipFirstNHookCallsConfig.load(std::memory_order_acquire);
		auto* player = skipConfig > 0 ? RE::PlayerCharacter::GetSingleton() : nullptr;
		bool playerInWorld = player && GetActor3DRootRaw(player) != nullptr;
		if (skipConfig > 0 && !playerInWorld) {
			g_skipFirstNHookCalls.store(skipConfig, std::memory_order_release);
			int64_t result = g_originalGraphUpdate ? g_originalGraphUpdate(a_this, a_param2, a_param3) : 0;
			return result;
		}
		uint32_t skipRemaining = g_skipFirstNHookCalls.load(std::memory_order_acquire);
		if (skipRemaining > 0) {
			g_skipFirstNHookCalls.store(skipRemaining - 1, std::memory_order_release);
			int64_t result = g_originalGraphUpdate ? g_originalGraphUpdate(a_this, a_param2, a_param3) : 0;
			return result;
		}

	if (auto remaining = g_debugHookLogs.load(std::memory_order_relaxed); remaining > 0) {
		g_debugHookLogs.fetch_sub(1, std::memory_order_relaxed);
		SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: debug tick callNum={}, thread={}", callNum, GetCurrentThreadId());
	}
	g_hookSeen.store(true, std::memory_order_release);
		{
			static std::atomic<bool> s_safeExtendedSet{ false };
			if (!s_safeExtendedSet.exchange(true)) {
				Settings::SetSafeToUseExtendedSkeleton(true);
			}
		}
		// Log pierwsze 10 wywołań, potem co 60 (co ~1 sekundę przy 60 FPS)
		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: ENTRY (call #{})", callNum);
		}
		
		// ProcessPendingCommands wywoływane na main thread (GraphUpdateHook)

		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: calling original function");
		}
		int64_t result = g_originalGraphUpdate ? g_originalGraphUpdate(a_this, a_param2, a_param3) : 0;
		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: original function returned, result={}", result);
		}

	auto* mgr = GraphManager::GetSingleton();
	const DWORD curThread = GetCurrentThreadId();
	if (g_mainThreadId == 0) {
		g_mainThreadId = curThread;
		SAF_LOG_INFO("GraphManager: main thread id set to {}", g_mainThreadId);
	}
	if (g_rebindOnNextHook.exchange(false, std::memory_order_acq_rel)) {
		SAF_LOG_INFO("GraphManager: main thread id rebind to {} (after attach)", curThread);
		g_mainThreadId = curThread;
	}
	if (g_forceUpdate.exchange(false, std::memory_order_acq_rel)) {
		SAF_LOG_INFO("GraphManager: force update on thread {} (after request)", curThread);
		g_mainThreadId = curThread;
	}

	const bool hasPending = Commands::SAFCommand::HasPendingCommands();
	const bool requested = Commands::SAFCommand::HasProcessRequest();
	const bool hasDump = Commands::SAFCommand::HasPendingDump();
	const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();
	if (requested || hasPending || dumpRequested || hasDump) {
		SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: pending flags (req={}, pending={}, dumpReq={}, dumpPending={}) thread={}",
			requested, hasPending, dumpRequested, hasDump, curThread);
		if (g_mainThreadId != curThread) {
			SAF_LOG_INFO("GraphManager: main thread id rebind to {} (pending flags)", curThread);
			g_mainThreadId = curThread;
		}
	}
	const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);

	if (requested || hasPending) {
		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: calling ProcessPendingCommands");
		}
		if (requested) {
			Commands::SAFCommand::ConsumeProcessRequest();
		}
		if (Commands::SAFCommand::ConsumeCloseConsole()) {
			Commands::SAFCommand::CloseConsoleMainThread();
		}
		if (!isMainThread && (callNum <= 10 || callNum % 60 == 0)) {
			SAF_LOG_WARN("[HOOK] GraphUpdateHookImpl: running ProcessPendingCommands on non-main thread {}", curThread);
		}
		Commands::SAFCommand::ProcessPendingCommands();
	}
	if (dumpRequested || hasDump) {
		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: calling ProcessPendingDump");
		}
		if (isMainThread) {
			Commands::SAFCommand::ProcessPendingDump();
		} else if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: skip ProcessPendingDump (non-main thread {})", curThread);
		}
	}

	if (mgr && isMainThread) {
		if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
			float dtSeconds = 0.0f;
			bool modelCulled = false;
			const bool hasUpdateData = ReadAnimUpdateData(a_param2, dtSeconds, modelCulled);
			if (!hasUpdateData || !std::isfinite(dtSeconds) || dtSeconds <= 0.0f) {
				auto now = std::chrono::steady_clock::now();
				std::chrono::duration<float> dt = now - lastTick;
				lastTick = now;
				dtSeconds = dt.count();
			}
			if (!g_actorGraphs.empty()) {
				if (callNum <= 10 || callNum % 60 == 0) {
					SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: UpdateGraphs (graphs={}, dt={}, culled={})",
						g_actorGraphs.size(), dtSeconds, modelCulled ? "true" : "false");
				}
			}
			mgr->UpdateGraphs(dtSeconds);
			g_updateGuard.clear(std::memory_order_release);
		} else if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: UpdateGraphs skipped (guarded)");
		}
	}
		
		if (callNum <= 10 || callNum % 60 == 0) {
			SAF_LOG_INFO("[HOOK] GraphUpdateHookImpl: EXIT");
		}
		return result;
	}

static void BSAnimationGraphUpdateHook(RE::BSAnimationGraph* a_graph, RE::BSAnimationUpdateData& a_updateData)
{
	static auto lastTick = std::chrono::steady_clock::now();
	static std::atomic<uint32_t> callCount{ 0 };
	const uint32_t callNum = ++callCount;

	if (g_originalAnimGraphUpdate) {
		g_originalAnimGraphUpdate(a_graph, a_updateData);
	} else if (g_animGraphVtablePrimaryHook) {
		(*g_animGraphVtablePrimaryHook)(a_graph, a_updateData);
	}

	g_hookSeen.store(true, std::memory_order_release);

	auto* mgr = GraphManager::GetSingleton();
	const DWORD curThread = GetCurrentThreadId();
	if (g_mainThreadId == 0) {
		g_mainThreadId = curThread;
		SAF_LOG_INFO("GraphManager: main thread id set to {} (BSAnimationGraph::Update)", g_mainThreadId);
	}

	const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);
	const bool hasPending = Commands::SAFCommand::HasPendingCommands();
	const bool requested = Commands::SAFCommand::HasProcessRequest();
	const bool hasDump = Commands::SAFCommand::HasPendingDump();
	const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();

	if ((requested || hasPending) && isMainThread) {
		if (requested) {
			Commands::SAFCommand::ConsumeProcessRequest();
		}
		if (Commands::SAFCommand::ConsumeCloseConsole()) {
			Commands::SAFCommand::CloseConsoleMainThread();
		}
		Commands::SAFCommand::ProcessPendingCommands();
	}

	if ((dumpRequested || hasDump) && isMainThread) {
		Commands::SAFCommand::ProcessPendingDump();
	}

	if (mgr && isMainThread) {
		if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
			float dtSeconds = 0.0f;
			bool modelCulled = false;
			const bool hasUpdateData = ReadAnimUpdateData(&a_updateData, dtSeconds, modelCulled);
			if (!hasUpdateData || !std::isfinite(dtSeconds) || dtSeconds <= 0.0f) {
				const auto now = std::chrono::steady_clock::now();
				const std::chrono::duration<float> dt = now - lastTick;
				lastTick = now;
				dtSeconds = dt.count();
			}

			if (callNum <= 10 || callNum % 300 == 0) {
				SAF_LOG_INFO("[HOOK] BSAnimationGraph::Update tick (graphs={}, dt={}, culled={})",
					g_actorGraphs.size(), dtSeconds, modelCulled ? "true" : "false");
			}

			mgr->UpdateGraphs(dtSeconds);
			g_updateGuard.clear(std::memory_order_release);
		}
	}
}

static void WriteAbsoluteJmp(uintptr_t from, uintptr_t to);

static void AnimGraphManagerUpdateCallHook(RE::IAnimationGraphManagerHolder* a_holder, void* a_updateData, void* a_graph)
{
	static auto lastTick = std::chrono::steady_clock::now();
	static std::atomic<uint32_t> callCount{ 0 };
	const uint32_t callNum = ++callCount;

	if (g_originalAnimGraphManagerUpdate) {
		g_originalAnimGraphManagerUpdate(a_holder, a_updateData, a_graph);
	}

	if (auto remaining = g_debugHookLogs.load(std::memory_order_relaxed); remaining > 0) {
		g_debugHookLogs.fetch_sub(1, std::memory_order_relaxed);
		SAF_LOG_INFO("[HOOK] AnimGraphManagerCallHook: callNum={}, thread={}", callNum, GetCurrentThreadId());
	}

	g_hookSeen.store(true, std::memory_order_release);

	auto* mgr = GraphManager::GetSingleton();
	const DWORD curThread = GetCurrentThreadId();
	if (g_mainThreadId == 0) {
		g_mainThreadId = curThread;
		SAF_LOG_INFO("GraphManager: main thread id set to {} (AnimGraphManagerCallHook)", g_mainThreadId);
	}

	const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);
	const bool hasPending = Commands::SAFCommand::HasPendingCommands();
	const bool requested = Commands::SAFCommand::HasProcessRequest();
	const bool hasDump = Commands::SAFCommand::HasPendingDump();
	const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();

	if ((requested || hasPending) && isMainThread) {
		if (requested) {
			Commands::SAFCommand::ConsumeProcessRequest();
		}
		if (Commands::SAFCommand::ConsumeCloseConsole()) {
			Commands::SAFCommand::CloseConsoleMainThread();
		}
		Commands::SAFCommand::ProcessPendingCommands();
	}

	if ((dumpRequested || hasDump) && isMainThread) {
		Commands::SAFCommand::ProcessPendingDump();
	}

	if (mgr && isMainThread) {
		if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
			float dtSeconds = 0.0f;
			bool modelCulled = false;
			const bool hasUpdateData = ReadAnimUpdateData(a_updateData, dtSeconds, modelCulled);
			if (!hasUpdateData || !std::isfinite(dtSeconds) || dtSeconds <= 0.0f) {
				const auto now = std::chrono::steady_clock::now();
				const std::chrono::duration<float> dt = now - lastTick;
				lastTick = now;
				dtSeconds = dt.count();
			}
			if (callNum <= 10 || callNum % 300 == 0) {
				SAF_LOG_INFO("[HOOK] AnimGraphManagerCallHook tick (graphs={}, dt={}, culled={})",
					g_actorGraphs.size(), dtSeconds, modelCulled ? "true" : "false");
			}
			mgr->UpdateGraphs(dtSeconds);
			g_updateGuard.clear(std::memory_order_release);
		}
	}
}

static bool InstallAnimGraphUpdateHookByRva()
{
	if (g_animGraphUpdateHookInstalled) {
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: already installed");
		return true;
	}

	const auto overrideRva = g_animGraphUpdateRva.load(std::memory_order_acquire);
	if (overrideRva == 0) {
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: RVA not set");
		return false;
	}

	SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: START (RVA {:X})", overrideRva);

	try {
		const uintptr_t targetAddr = ResolveAnimGraphUpdateHookAddress();
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: target address = {:X}", targetAddr);

		// Trampoline: first bytes + jump back.
		constexpr size_t bytesToCopy = 14;
		uintptr_t trampolineAddr = 0;
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: allocating trampoline");
		for (int delta = -2000; delta <= 2000; delta += 64) {
			uintptr_t candidate = targetAddr + (delta * 1024 * 1024);
			candidate &= ~0xFFFF;
			void* result = VirtualAlloc(reinterpret_cast<void*>(candidate), 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (result) {
				trampolineAddr = reinterpret_cast<uintptr_t>(result);
				SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: trampoline allocated at {:X}", trampolineAddr);
				break;
			}
		}

		if (!trampolineAddr) {
			SAF_LOG_ERROR("[HOOK] InstallAnimGraphUpdateHookByRva: Failed to allocate trampoline");
			return false;
		}

		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: copying {} bytes to trampoline", bytesToCopy);
		memcpy(reinterpret_cast<void*>(trampolineAddr), reinterpret_cast<void*>(targetAddr), bytesToCopy);
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: writing return jump in trampoline");
		WriteAbsoluteJmp(trampolineAddr + bytesToCopy, targetAddr + bytesToCopy);

		g_originalAnimGraphUpdate = reinterpret_cast<BSAnimationGraphUpdateFunc>(trampolineAddr);
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: writing hook jump to target");
		WriteAbsoluteJmp(targetAddr, reinterpret_cast<uintptr_t>(BSAnimationGraphUpdateHook));
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: hook jump written");

		g_animGraphUpdateHookInstalled = true;
		SAF_LOG_INFO("[HOOK] InstallAnimGraphUpdateHookByRva: DONE - Hook installed at {:X}, trampoline {:X}",
			targetAddr, trampolineAddr);
		return true;
	}
	catch (const std::exception& e) {
		SAF_LOG_ERROR("[HOOK] InstallAnimGraphUpdateHookByRva: Exception - {}", e.what());
		return false;
	}
	catch (...) {
		SAF_LOG_ERROR("[HOOK] InstallAnimGraphUpdateHookByRva: Unknown exception");
		return false;
	}
}

static void PlayerCameraUpdateHook(RE::PlayerCamera* a_camera)
{
	static auto lastTick = std::chrono::steady_clock::now();
	static std::atomic<uint32_t> callCount{ 0 };
	const uint32_t callNum = ++callCount;

	if (g_playerCameraUpdateHookPrimary) {
		(*g_playerCameraUpdateHookPrimary)(a_camera);
	}

	const DWORD curThread = GetCurrentThreadId();
	if (g_mainThreadId == 0) {
		g_mainThreadId = curThread;
		SAF_LOG_INFO("GraphManager: main thread id set to {} (PlayerCamera::Update)", g_mainThreadId);
	}

	// If the original graph hook is ticking, don't double-run.
	if (g_hookSeen.load(std::memory_order_acquire)) {
		return;
	}

	auto* mgr = GraphManager::GetSingleton();

	const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);
	if (!isMainThread) {
		return;
	}

	const bool hasPending = Commands::SAFCommand::HasPendingCommands();
	const bool requested = Commands::SAFCommand::HasProcessRequest();
	const bool hasDump = Commands::SAFCommand::HasPendingDump();
	const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();

	if (requested || hasPending) {
		if (requested) {
			Commands::SAFCommand::ConsumeProcessRequest();
		}
		if (Commands::SAFCommand::ConsumeCloseConsole()) {
			Commands::SAFCommand::CloseConsoleMainThread();
		}
		Commands::SAFCommand::ProcessPendingCommands();
	}

	if (dumpRequested || hasDump) {
		Commands::SAFCommand::ProcessPendingDump();
	}

	if (mgr) {
		if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
			const auto now = std::chrono::steady_clock::now();
			const std::chrono::duration<float> dt = now - lastTick;
			lastTick = now;

			if (callNum <= 10 || callNum % 300 == 0) {
				SAF_LOG_INFO("[HOOK] PlayerCamera::Update tick (graphs={}, dt={})",
					g_actorGraphs.size(), dt.count());
			}

			mgr->UpdateGraphs(dt.count());
			g_updateGuard.clear(std::memory_order_release);
		}
	}
}

static RE::UI_MESSAGE_RESULT IMenuProcessMessageHook(RE::IMenu* a_menu, RE::UIMessageData& a_message)
{
	static auto lastTick = std::chrono::steady_clock::now();
	static std::atomic<uint32_t> callCount{ 0 };
	const uint32_t callNum = ++callCount;

	// Wywołaj oryginał TYLKO gdy znamy vtable tego menu – nigdy „primary” z innego menu (CTD przy help / additem).
	RE::UI_MESSAGE_RESULT result = RE::UI_MESSAGE_RESULT::kPassOn;
	if (a_menu && !g_menuVtableToHook.empty()) {
		const uintptr_t menuVtable = *reinterpret_cast<uintptr_t*>(a_menu);
		auto it = g_menuVtableToHook.find(menuVtable);
		if (it != g_menuVtableToHook.end() && it->second) {
			result = (*(it->second))(a_menu, a_message);
		}
		// Nie wywołuj g_menuProcessMessageHookPrimary gdy nie ma dopasowania – to byłby zły oryginał.
	}

	const DWORD curThread = GetCurrentThreadId();
	if (g_mainThreadId == 0) {
		g_mainThreadId = curThread;
		SAF_LOG_INFO("GraphManager: main thread id set to {} (IMenu::ProcessMessage)", g_mainThreadId);
	}

	if (g_hookSeen.load(std::memory_order_acquire)) {
		return result;
	}

	if (a_message.type != RE::UI_MESSAGE_TYPE::kUpdate) {
		return result;
	}

	// Gdy otwarta konsola lub menu kreatora – nie uruchamiamy UpdateGraphs ani kolejek (unika CTD przy help / additem).
	const char* menuName = a_menu ? a_menu->GetName() : nullptr;
	if (menuName) {
		if (std::strstr(menuName, "CharGen") || std::strstr(menuName, "Look") || std::strstr(menuName, "Face")) {
			return result;
		}
		if (std::strstr(menuName, "Console")) {
			return result;
		}
	}

	auto* mgr = GraphManager::GetSingleton();

	const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);
	if (!isMainThread) {
		return result;
	}

	const bool hasPending = Commands::SAFCommand::HasPendingCommands();
	const bool requested = Commands::SAFCommand::HasProcessRequest();
	const bool hasDump = Commands::SAFCommand::HasPendingDump();
	const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();

	if (requested || hasPending) {
		if (requested) {
			Commands::SAFCommand::ConsumeProcessRequest();
		}
		if (Commands::SAFCommand::ConsumeCloseConsole()) {
			Commands::SAFCommand::CloseConsoleMainThread();
		}
		Commands::SAFCommand::ProcessPendingCommands();
	}

	if (dumpRequested || hasDump) {
		Commands::SAFCommand::ProcessPendingDump();
	}

	if (mgr) {
		if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
			const auto now = std::chrono::steady_clock::now();
			const std::chrono::duration<float> dt = now - lastTick;
			lastTick = now;

			if (callNum <= 10 || callNum % 300 == 0) {
				const char* menuName = a_menu ? a_menu->GetName() : "<null>";
				SAF_LOG_INFO("[HOOK] IMenu::ProcessMessage tick (menu={}, graphs={}, dt={})",
					menuName, g_actorGraphs.size(), dt.count());
			}

			mgr->UpdateGraphs(dt.count());
			g_updateGuard.clear(std::memory_order_release);
		}
	}

	return result;
}

	static void TaskUpdateLoopTick();

	static void StartTaskUpdateLoop()
	{
		if (!g_taskLoopEnabled.load(std::memory_order_acquire)) {
			return;
		}
		if (g_taskLoopStarted.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		const auto* taskInterface = SFSE::GetTaskInterface();
		if (!taskInterface) {
			SAF_LOG_WARN("TaskUpdateLoop: TaskInterface not available");
			return;
		}

	taskInterface->AddPermanentTask(&TaskUpdateLoopTick);
	SAF_LOG_INFO("TaskUpdateLoop: permanent task scheduled");
	}

	static void TaskUpdateLoopTick()
	{
		if (!g_taskLoopEnabled.load(std::memory_order_acquire)) {
			return;
		}

		static auto lastTick = std::chrono::steady_clock::now();
	static std::atomic<uint32_t> callCount{ 0 };
	const uint32_t callNum = ++callCount;
		const auto now = std::chrono::steady_clock::now();
		const std::chrono::duration<float> dt = now - lastTick;
		lastTick = now;

	auto* mgr = GraphManager::GetSingleton();
	const DWORD curThread = GetCurrentThreadId();
	if (g_mainThreadId == 0 && g_taskLoopAssumeMainThread.load(std::memory_order_acquire)) {
		g_mainThreadId = curThread;
		SAF_LOG_INFO("GraphManager: main thread id set to {} (TaskUpdateLoop)", g_mainThreadId);
	}
	const bool isMainThread = (g_mainThreadId != 0 && curThread == g_mainThreadId);

		const bool requested = Commands::SAFCommand::HasProcessRequest();
		const bool hasPending = Commands::SAFCommand::HasPendingCommands();
		const bool dumpRequested = Commands::SAFCommand::ConsumeDumpRequest();
		const bool hasDump = Commands::SAFCommand::HasPendingDump();

		if (requested || hasPending) {
			if (requested) {
				Commands::SAFCommand::ConsumeProcessRequest();
			}
			if (Commands::SAFCommand::ConsumeCloseConsole()) {
				Commands::SAFCommand::CloseConsoleMainThread();
			}
			Commands::SAFCommand::ProcessPendingCommands();
		}
	if (dumpRequested || hasDump) {
		if (g_taskLoopAllowDump.load(std::memory_order_acquire)) {
			Commands::SAFCommand::ProcessPendingDump();
		} else {
			SAF_LOG_WARN("[TASK] TaskUpdateLoop dump skipped (disabled)");
			Commands::SAFCommand::ClearPendingDump();
		}
	}

		if (mgr) {
		const bool allowUpdate = g_taskLoopAllowUpdateGraphs.load(std::memory_order_acquire);
		const bool noOtherHook = !g_hookSeen.load(std::memory_order_acquire);
		// Always run UpdateGraphs from Task when on main thread so animation advances every frame.
		// (Preferring only the hook caused no updates when hook wasn't called for player -> frozen/upside-down pose.)
		const bool shouldUpdate = isMainThread && (allowUpdate || noOtherHook);
		if (shouldUpdate) {
			if (!g_updateGuard.test_and_set(std::memory_order_acq_rel)) {
				if (callNum <= 5 || callNum % 300 == 0) {
					SAF_LOG_INFO("[TASK] TaskUpdateLoop tick (dt={})", dt.count());
				}
				mgr->UpdateGraphs(dt.count());
				g_updateGuard.clear(std::memory_order_release);
			}
		} else if (callNum <= 5 || callNum % 300 == 0) {
			SAF_LOG_INFO("[TASK] TaskUpdateLoop UpdateGraphs skipped (allow={}, noOtherHook={}, isMainThread={})",
				allowUpdate ? "true" : "false",
				noOtherHook ? "true" : "false",
				isMainThread ? "true" : "false");
		}
		}
	}

	static void WriteAbsoluteJmp(uintptr_t from, uintptr_t to)
	{
		uint8_t jmp[14]{};
		jmp[0] = 0xFF;
		jmp[1] = 0x25;
		*reinterpret_cast<uint32_t*>(jmp + 2) = 0;
		*reinterpret_cast<uint64_t*>(jmp + 6) = to;

		DWORD oldProtect;
		VirtualProtect(reinterpret_cast<void*>(from), 14, PAGE_EXECUTE_READWRITE, &oldProtect);
		memcpy(reinterpret_cast<void*>(from), jmp, 14);
		VirtualProtect(reinterpret_cast<void*>(from), 14, oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(from), 14);
	}

static void WriteRelCall(uintptr_t from, uintptr_t to)
{
	uint8_t call[5]{};
	call[0] = 0xE8;
	const auto rel = static_cast<int32_t>(to - (from + 5));
	std::memcpy(call + 1, &rel, sizeof(rel));

	DWORD oldProtect;
	VirtualProtect(reinterpret_cast<void*>(from), 5, PAGE_EXECUTE_READWRITE, &oldProtect);
	memcpy(reinterpret_cast<void*>(from), call, 5);
	VirtualProtect(reinterpret_cast<void*>(from), 5, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(from), 5);
}

static bool InstallAnimGraphManagerCallHook()
{
	if (g_animGraphManagerCallHookInstalled) {
		SAF_LOG_INFO("[HOOK] InstallAnimGraphManagerCallHook: already installed");
		return true;
	}

	uintptr_t callSite = ResolveAnimGraphManagerCallAddress();
	if (!callSite) {
		SAF_LOG_WARN("[HOOK] InstallAnimGraphManagerCallHook: call site unavailable");
		return false;
	}

	uint8_t opcode = *reinterpret_cast<uint8_t*>(callSite);
	if (opcode != 0xE8) {
		SAF_LOG_WARN("[HOOK] InstallAnimGraphManagerCallHook: primary offset {:X} has {:02X}, scanning for CALL...", callSite, opcode);
		const uintptr_t funcBase = callSite - 0x61;
		constexpr int scanStart = 0x40;
		constexpr int scanEnd = 0xC0;
		bool found = false;
		for (int off = scanStart; off < scanEnd && !found; ++off) {
			const uintptr_t candidate = funcBase + off;
			if (*reinterpret_cast<uint8_t*>(candidate) == 0xE8) {
				callSite = candidate;
				opcode = 0xE8;
				found = true;
				SAF_LOG_INFO("[HOOK] InstallAnimGraphManagerCallHook: found CALL at offset +{:X} (addr {:X})", off, callSite);
			}
		}
		if (!found) {
			SAF_LOG_WARN("[HOOK] InstallAnimGraphManagerCallHook: no CALL in range +{:X}..+{:X}", scanStart, scanEnd);
			return false;
		}
	}

	const int32_t rel = *reinterpret_cast<int32_t*>(callSite + 1);
	const uintptr_t originalTarget = callSite + 5 + rel;
	g_originalAnimGraphManagerUpdate = reinterpret_cast<AnimGraphManagerUpdateFunc>(originalTarget);

	SAF_LOG_INFO("[HOOK] InstallAnimGraphManagerCallHook: call site {:X}, original {:X}", callSite, originalTarget);
	WriteRelCall(callSite, reinterpret_cast<uintptr_t>(AnimGraphManagerUpdateCallHook));

	g_animGraphManagerCallHookInstalled = true;
	SAF_LOG_INFO("[HOOK] InstallAnimGraphManagerCallHook: DONE");
	return true;
}

	static bool InstallGraphUpdateHook()
	{
		if (g_graphUpdateHookInstalled) {
			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: already installed");
			return true;
		}

		SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: START");

		try {
			const auto overrideRva = g_hookOverrideRva.load(std::memory_order_acquire);
			if (overrideRva != 0) {
				SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: using RVA override {:X}", overrideRva);
			} else {
				SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: resolving ID {}", ID::GraphUpdateHook.id());
			}
			uintptr_t targetAddr = ResolveGraphUpdateHookAddress();
			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: target address = {:X}", targetAddr);

			// Alokuj trampoline (kopia pierwszych bajtów + powrót)
			constexpr size_t bytesToCopy = 14;
			uintptr_t trampolineAddr = 0;
			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: allocating trampoline");
			for (int delta = -2000; delta <= 2000; delta += 64) {
				uintptr_t candidate = targetAddr + (delta * 1024 * 1024);
				candidate &= ~0xFFFF;
				void* result = VirtualAlloc(reinterpret_cast<void*>(candidate), 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
				if (result) {
					trampolineAddr = reinterpret_cast<uintptr_t>(result);
					SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: trampoline allocated at {:X}", trampolineAddr);
					break;
				}
			}

			if (!trampolineAddr) {
				SAF_LOG_ERROR("[HOOK] InstallGraphUpdateHook: Failed to allocate trampoline");
				return false;
			}

			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: copying {} bytes to trampoline", bytesToCopy);
			memcpy(reinterpret_cast<void*>(trampolineAddr), reinterpret_cast<void*>(targetAddr), bytesToCopy);
			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: writing return jump in trampoline");
			WriteAbsoluteJmp(trampolineAddr + bytesToCopy, targetAddr + bytesToCopy);

			g_originalGraphUpdate = reinterpret_cast<GraphUpdateFunc>(trampolineAddr);
			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: writing hook jump to target");
			WriteAbsoluteJmp(targetAddr, reinterpret_cast<uintptr_t>(GraphUpdateHookImpl));
			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: hook jump written");

			g_graphUpdateHookInstalled = true;
			SAF_LOG_INFO("[HOOK] InstallGraphUpdateHook: DONE - Hook installed at {:X}, trampoline {:X}", targetAddr, trampolineAddr);
			return true;
		}
		catch (const std::exception& e) {
			SAF_LOG_ERROR("[HOOK] InstallGraphUpdateHook: Exception - {} - verify Address Library ID", e.what());
			return false;
		}
		catch (...) {
			SAF_LOG_ERROR("[HOOK] InstallGraphUpdateHook: Unknown exception - verify Address Library ID");
			return false;
		}
	}

	bool GraphManager::IsModelDBForRestPoseEnabled()
	{
		LoadHookOverrideFromIni();
		return g_useModelDBForRestPose.load(std::memory_order_acquire);
	}

	std::uint64_t GraphManager::GetModelDBGetEntryID()
	{
		LoadHookOverrideFromIni();
		std::uint64_t id = g_modelDBGetEntryID.load(std::memory_order_acquire);
		return id != 0 ? id : 949826;
	}

	std::uint64_t GraphManager::GetModelDBDecRefID()
	{
		LoadHookOverrideFromIni();
		std::uint64_t id = g_modelDBDecRefID.load(std::memory_order_acquire);
		return id != 0 ? id : 36741;
	}

	std::uint32_t GraphManager::GetModelDBDecRefRVA()
	{
		LoadHookOverrideFromIni();
		return g_modelDBDecRefRVA.load(std::memory_order_acquire);
	}

	// Wywoływane przy starcie - instaluje hook
	void GraphManager::InstallHooks()
	{
		LoadHookOverrideFromIni();

	if (!g_allowVtableHooks.load(std::memory_order_acquire)) {
		SAF_LOG_INFO("Animation vtable hooks disabled");
	}

	if (g_allowVtableHooks.load(std::memory_order_acquire) && !g_animGraphVtableHookInstalled) {
			constexpr std::size_t kUpdateIndex = 4;
			const auto hookVtableList = [&](std::string_view a_name, const auto& a_ids) {
				for (const auto id : a_ids) {
					auto hook = std::make_unique<REL::THookVFT<void(RE::BSAnimationGraph*, RE::BSAnimationUpdateData&)>>(
						id, kUpdateIndex, &BSAnimationGraphUpdateHook);
					if (hook->Enable()) {
						if (!g_animGraphVtablePrimaryHook) {
							g_animGraphVtablePrimaryHook = hook.get();
						}
						g_animGraphVtableHooks.emplace_back(std::move(hook));
						SAF_LOG_INFO("{}::Update VFT hook enabled (VTABLE ID {})", a_name, id.id());
					} else {
						SAF_LOG_WARN("{}::Update VFT hook failed (VTABLE ID {})", a_name, id.id());
					}
				}
			};

			hookVtableList("BSAnimationGraph", RE::VTABLE::BSAnimationGraph);
			hookVtableList("AnimationManager", RE::VTABLE::AnimationManager);
			g_animGraphVtableHookInstalled = !g_animGraphVtableHooks.empty();
		}

	if (g_cameraHookEnabled.load(std::memory_order_acquire) && !g_playerCameraHookInstalled) {
		constexpr std::size_t kUpdateIndex = 3;
		for (const auto id : RE::VTABLE::PlayerCamera) {
			auto hook = std::make_unique<REL::THookVFT<void(RE::PlayerCamera*)>>(
				id, kUpdateIndex, &PlayerCameraUpdateHook);
			if (hook->Enable()) {
				if (!g_playerCameraUpdateHookPrimary) {
					g_playerCameraUpdateHookPrimary = hook.get();
				}
				g_playerCameraUpdateHooks.emplace_back(std::move(hook));
				SAF_LOG_INFO("PlayerCamera::Update VFT hook enabled (VTABLE ID {})", id.id());
			}
		}
		g_playerCameraHookInstalled = !g_playerCameraUpdateHooks.empty();
		if (!g_playerCameraHookInstalled) {
			SAF_LOG_WARN("PlayerCamera::Update VFT hook failed");
		}
	}

	if (g_menuHookEnabled.load(std::memory_order_acquire) && !g_menuProcessMessageHookInstalled) {
		constexpr std::size_t kProcessMessageIndex = 8;
		g_menuVtableToHook.clear();
		for (const auto id : RE::VTABLE::IMenu) {
			auto hook = std::make_unique<REL::THookVFT<RE::UI_MESSAGE_RESULT(RE::IMenu*, RE::UIMessageData&)>>(
				id, kProcessMessageIndex, &IMenuProcessMessageHook);
			if (hook->Enable()) {
				const uintptr_t vtableAddr = id.address();
				g_menuVtableToHook[vtableAddr] = hook.get();
				if (!g_menuProcessMessageHookPrimary) {
					g_menuProcessMessageHookPrimary = hook.get();
				}
				g_menuProcessMessageHooks.emplace_back(std::move(hook));
				SAF_LOG_INFO("IMenu::ProcessMessage VFT hook enabled (VTABLE ID {}, vtable={:X})", id.id(), vtableAddr);
			}
		}
		g_menuProcessMessageHookInstalled = !g_menuProcessMessageHooks.empty();
		if (!g_menuProcessMessageHookInstalled) {
			SAF_LOG_WARN("IMenu::ProcessMessage VFT hook failed");
		}
	}

	if (g_menuHookEnabled.load(std::memory_order_acquire) && !g_updateSceneRectSinkInstalled) {
		auto* source = ResolveEventSourceByIdOrRva<RE::UpdateSceneRectEvent>(
			RE::ID::UpdateSceneRectEvent::GetEventSource,
			ID::UpdateSceneRectEventGetEventSource,
			g_updateSceneRectEventRva,
			"UpdateSceneRectEvent",
			&RE::UpdateSceneRectEvent::GetEventSource);
		if (source) {
			source->RegisterSink(&g_updateSceneRectSink);
			g_updateSceneRectSinkInstalled = true;
			SAF_LOG_INFO("UpdateSceneRectEvent sink registered");
		} else {
			SAF_LOG_WARN("UpdateSceneRectEvent source not available");
		}
	}

	if (g_playerUpdateEventEnabled.load(std::memory_order_acquire) && !g_playerUpdateSinkInstalled) {
		auto* source = ResolveEventSourceByIdOrRva<RE::PlayerUpdateEvent>(
			RE::ID::PlayerUpdateEvent::GetEventSource,
			ID::PlayerUpdateEventGetEventSource,
			g_playerUpdateEventRva,
			"PlayerUpdateEvent",
			&RE::PlayerUpdateEvent::GetEventSource);
		if (source) {
			source->RegisterSink(&g_playerUpdateSink);
			g_playerUpdateSinkInstalled = true;
			SAF_LOG_INFO("PlayerUpdateEvent sink registered");
		} else {
			SAF_LOG_WARN("PlayerUpdateEvent source not available");
		}
	}

		if (g_inputHookEnabled.load(std::memory_order_acquire)) {
			Tasks::Input::InstallHook();
		}

		if (g_taskLoopEnabled.load(std::memory_order_acquire)) {
			StartTaskUpdateLoop();
		}

		if (!g_animGraphManagerCallHookInstalled) {
			if (InstallAnimGraphManagerCallHook()) {
				g_debugHookLogs.store(200, std::memory_order_relaxed);
				SAF_LOG_INFO("[HOOK] Debug: will log next {} hook calls", g_debugHookLogs.load());
			}
		}

		if (g_allowVtableHooks.load(std::memory_order_acquire) && !g_animGraphVtableHookInstalled) {
			// Zawsze waliduj ID (adres + pierwsze bajty) – sprawdź log po starcie gry
			ValidateGraphUpdateHookID();

			if (!g_enableGraphUpdateHook) {
				SAF_LOG_INFO("GraphUpdateHook disabled (g_enableGraphUpdateHook = false)");
				return;
			}
			if (g_animGraphUpdateRva.load(std::memory_order_acquire) != 0) {
				if (InstallAnimGraphUpdateHookByRva()) {
					SAF_LOG_INFO("BSAnimationGraph::Update RVA hook installed - keeping GraphUpdateHook as fallback");
					g_debugHookLogs.store(200, std::memory_order_relaxed);
					SAF_LOG_INFO("[HOOK] Debug: will log next {} hook calls", g_debugHookLogs.load());
				}
			}
			if (!InstallGraphUpdateHook()) {
				SAF_LOG_WARN("GraphUpdateHook not installed - animations will not display on actors");
			} else {
				g_debugHookLogs.store(200, std::memory_order_relaxed);
				SAF_LOG_INFO("[HOOK] Debug: will log next {} hook calls", g_debugHookLogs.load());
			}
		} else if (g_allowVtableHooks.load(std::memory_order_acquire)) {
			SAF_LOG_INFO("GraphUpdateHook skipped (BSAnimationGraph/AnimationManager VFT hook active)");
		} else {
			// No vtable hook; keep GraphUpdateHook path active.
			if (g_animGraphUpdateRva.load(std::memory_order_acquire) != 0) {
				if (InstallAnimGraphUpdateHookByRva()) {
					SAF_LOG_INFO("BSAnimationGraph::Update RVA hook installed - keeping GraphUpdateHook as fallback");
					g_debugHookLogs.store(200, std::memory_order_relaxed);
					SAF_LOG_INFO("[HOOK] Debug: will log next {} hook calls", g_debugHookLogs.load());
				}
			}
			ValidateGraphUpdateHookID();
			if (!g_enableGraphUpdateHook) {
				SAF_LOG_INFO("GraphUpdateHook disabled (g_enableGraphUpdateHook = false)");
				return;
			}
			if (!InstallGraphUpdateHook()) {
				SAF_LOG_WARN("GraphUpdateHook not installed - animations will not display on actors");
			} else {
				g_debugHookLogs.store(200, std::memory_order_relaxed);
				SAF_LOG_INFO("[HOOK] Debug: will log next {} hook calls", g_debugHookLogs.load());
			}
		}
	}

	// ============================================================================
	// Implementacje metod GraphManager (przeniesione z .h - można rozbudować)
	// ============================================================================

	bool GraphManager::LoadAndStartAnimation(RE::Actor* a_actor, std::string_view a_path, bool a_looping, int a_animIndex)
	{
		s_lastLoadError.clear();
		try {
			SAF_LOG_INFO("LoadAndStartAnimation: actor={}, path={}",
				static_cast<void*>(a_actor), a_path);
			if (!a_actor) {
				s_lastLoadError = "Actor is null.";
				SAF_LOG_WARN("LoadAndStartAnimation: actor is null");
				return false;
			}

			auto skeleton = Settings::GetSkeleton(a_actor);
			if (!skeleton || !skeleton->GetRawSkeleton()) {
				s_lastLoadError = "No skeleton for this actor (wrong race or skeleton not loaded).";
				SAF_LOG_WARN("LoadAndStartAnimation: missing skeleton for actor");
				return false;
			}

			std::filesystem::path path = Util::String::ResolveAnimationPath(a_path);
			bool resolved = false;
			if (path.extension().empty()) {
				auto glb = path;
				glb.replace_extension(".glb");
				if (std::filesystem::exists(glb)) {
					path = glb;
					resolved = true;
				} else {
					auto gltf = path;
					gltf.replace_extension(".gltf");
					if (std::filesystem::exists(gltf)) {
						path = gltf;
						resolved = true;
					}
				}
				if (!resolved) {
					s_lastLoadError = "File not found: " + path.string() + " (.glb/.gltf)";
					SAF_LOG_ERROR("LoadAndStartAnimation: missing file '{}' (.glb/.gltf not found)", path.string());
					return false;
				}
			} else if (!std::filesystem::exists(path)) {
				s_lastLoadError = "File not found: " + path.string();
				SAF_LOG_ERROR("LoadAndStartAnimation: file not found '{}'", path.string());
				return false;
			}
			SAF_LOG_INFO("LoadAndStartAnimation: loading file '{}'", path.string());

			auto asset = Serialization::GLTFImport::LoadGLTF(path);
			if (!asset) {
				s_lastLoadError = "Failed to load GLTF: " + path.string();
				SAF_LOG_ERROR("LoadAndStartAnimation: failed to load GLTF '{}'", path.string());
				return false;
			}
			if (asset->asset.animations.empty()) {
				s_lastLoadError = "GLTF has no animations: " + path.string();
				SAF_LOG_WARN("LoadAndStartAnimation: GLTF has no animations");
				return false;
			}
			size_t animIdx = static_cast<size_t>(std::max(0, a_animIndex));
			if (animIdx >= asset->asset.animations.size()) animIdx = asset->asset.animations.size() - 1;
			SAF_LOG_INFO("LoadAndStartAnimation: GLTF animations={}, using index {}, skeleton joints={}",
				asset->asset.animations.size(), animIdx, skeleton->jointNames.size());

			SAF_LOG_INFO("LoadAndStartAnimation: building raw animation");
			auto rawAnim = Serialization::GLTFImport::CreateRawAnimation(
				asset.get(),
				&asset->asset.animations[animIdx],
				skeleton->GetRawSkeleton(),
				&skeleton->jointNames);
			if (!rawAnim || !rawAnim->data) {
				s_lastLoadError = "Failed to build raw animation (check bone names match skeleton).";
				SAF_LOG_ERROR("LoadAndStartAnimation: failed to build raw animation");
				return false;
			}
			SAF_LOG_INFO("LoadAndStartAnimation: raw animation built");

			ozz::animation::offline::AnimationBuilder builder;
			SAF_LOG_INFO("LoadAndStartAnimation: building runtime animation");
			auto runtimeAnim = builder(*rawAnim->data);
			if (!runtimeAnim) {
				s_lastLoadError = "Failed to build runtime animation.";
				SAF_LOG_ERROR("LoadAndStartAnimation: failed to build runtime animation");
				return false;
			}
			SAF_LOG_INFO("LoadAndStartAnimation: runtime animation built");

			const auto trackCount = runtimeAnim->num_tracks();
			const auto soaCount = runtimeAnim->num_soa_tracks();
			SAF_LOG_INFO("LoadAndStartAnimation: runtime tracks={}, soaTracks={}", trackCount, soaCount);
			const auto jointCount = skeleton->jointNames.size();
			if (trackCount == 0 || soaCount == 0) {
				s_lastLoadError = "Invalid animation (no tracks).";
				SAF_LOG_ERROR("LoadAndStartAnimation: invalid runtime animation (tracks={}, soa={})", trackCount, soaCount);
				return false;
			}
			if (trackCount > 4096 || soaCount > 4096) {
				s_lastLoadError = "Animation track count out of range.";
				SAF_LOG_ERROR("LoadAndStartAnimation: unreasonable track counts (tracks={}, soa={})", trackCount, soaCount);
				return false;
			}
			if (jointCount != 0 && trackCount != jointCount) {
				SAF_LOG_WARN("LoadAndStartAnimation: track/joint mismatch (tracks={}, joints={})", trackCount, jointCount);
			}

			SAF_LOG_INFO("LoadAndStartAnimation: creating ClipGenerator");
			auto gen = std::make_unique<Animation::ClipGenerator>(skeleton, std::move(runtimeAnim));
			if (auto* cg = gen.get()) cg->SetLooping(a_looping);
			SAF_LOG_INFO("LoadAndStartAnimation: ClipGenerator created");
			AttachGenerator(a_actor, std::move(gen), 0.0f);
			// Store which joints are actually animated by this clip (so we can skip defaults on face bones etc.).
			{
				std::lock_guard<std::mutex> lock(g_graphMutex);
				auto it = g_actorGraphs.find(a_actor->GetFormID());
				if (it != g_actorGraphs.end()) {
					it->second.jointHasChannel = rawAnim->jointHasChannel;
				}
			}
			{
				std::lock_guard<std::mutex> lock(g_graphMutex);
				auto it = g_actorGraphs.find(a_actor->GetFormID());
				if (it != g_actorGraphs.end())
					it->second.currentAnimationPath = std::string(a_path);
			}
			SAF_LOG_INFO("LoadAndStartAnimation: generator attached");
			return true;
		} catch (const std::exception& e) {
			s_lastLoadError = std::string("Exception: ") + e.what();
			SAF_LOG_ERROR("LoadAndStartAnimation: exception {}", e.what());
			return false;
		} catch (...) {
			s_lastLoadError = "Unknown exception.";
			SAF_LOG_ERROR("LoadAndStartAnimation: unknown exception");
			return false;
		}
	}

	void GraphManager::DetachGenerator(RE::Actor* a_actor, float a_duration)
	{
		SAF_LOG_INFO("DetachGenerator: actor={}, duration={}",
			static_cast<void*>(a_actor), a_duration);
		if (!a_actor) return;
		const RE::TESFormID id = a_actor->GetFormID();
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(id);
		if (it == g_actorGraphs.end()) {
			SAF_LOG_WARN("DetachGenerator: no graph found for actor {:X}", id);
			return;
		}

		// Restore original game bone rotations before removing the graph
		ActorGraphState& state = it->second;
		const std::uintptr_t off = g_niTransformOffset.load(std::memory_order_acquire);
		if (off != 0 && !state.gameBaseRotations.empty()) {
			for (size_t i = 0; i < state.jointNodes.size() && i < state.gameBaseRotations.size(); ++i) {
				RE::NiAVObject* node = state.jointNodes[i];
				if (!node) continue;
				const float* b = state.gameBaseRotations[i].data();
				RestoreBoneRotationRaw(node, off, b);
			}
			SAF_LOG_INFO("DetachGenerator: bone rotations restored for actor {:X}", id);
		}

		g_actorGraphs.erase(it);
		g_actorSequenceState.erase(id);
		g_syncOwner.erase(id);
		SAF_LOG_INFO("DetachGenerator: graph removed for actor {:X}", id);
	}

	// StopAnimation – called by "saf stop" console command.
	// Removes the animation graph for the actor and restores their original bone rotations.
	void GraphManager::StopAnimation(RE::Actor* a_actor)
	{
		if (!a_actor) {
			SAF_LOG_WARN("StopAnimation: actor is null");
			return;
		}
		SAF_LOG_INFO("[STOP] StopAnimation: actor={:p}", static_cast<void*>(a_actor));
		DetachGenerator(a_actor, 0.0f);
		// UpdateWorldData so the renderer picks up the restored rotations immediately
		SafeUpdateWorldData(a_actor, nullptr, nullptr);
		SAF_LOG_INFO("[STOP] StopAnimation: done");
	}

	void GraphManager::SyncGraphs(const std::vector<RE::Actor*>& a_actors)
	{
		if (a_actors.empty()) return;
		RE::TESFormID ownerId = a_actors[0]->GetFormID();
		std::lock_guard<std::mutex> lock(g_graphMutex);
		for (RE::Actor* a : a_actors) {
			if (a) g_syncOwner[a->GetFormID()] = ownerId;
		}
		SAF_LOG_INFO("SyncGraphs: {} actors, owner={:X}", a_actors.size(), ownerId);
	}

	void GraphManager::StopSyncing(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		RE::TESFormID id = a_actor->GetFormID();
		RE::TESFormID ownerId = g_syncOwner[id];
		g_syncOwner.erase(id);
		if (ownerId == id) {
			for (auto it = g_syncOwner.begin(); it != g_syncOwner.end(); ) {
				if (it->second == ownerId) it = g_syncOwner.erase(it);
				else ++it;
			}
		}
		SAF_LOG_INFO("StopSyncing: actor={:X}", id);
	}

	void GraphManager::StartSequence(RE::Actor* a_actor, std::vector<Sequencer::PhaseData>&& a_phases, bool a_loop)
	{
		if (!a_actor || a_phases.empty()) return;
		RE::TESFormID id = a_actor->GetFormID();
		std::string firstFile;
		bool firstLoop = true;
		size_t numPhases = 0;
		{
			std::lock_guard<std::mutex> lock(g_graphMutex);
			g_actorSequenceState[id] = ActorSequenceState{ std::move(a_phases), 0, a_loop };
			numPhases = g_actorSequenceState[id].phases.size();
			firstFile = g_actorSequenceState[id].phases[0].file;
			firstLoop = (g_actorSequenceState[id].phases[0].loopCount != 0);
		}
		LoadAndStartAnimation(a_actor, firstFile, firstLoop);
		Papyrus::EventManager::GetSingleton()->DispatchPhaseBegin(a_actor, 0, firstFile);
		SAF_LOG_INFO("StartSequence: actor={:X}, phases={}", id, numPhases);
	}

	void GraphManager::AdvanceSequence(RE::Actor* a_actor, bool a_smooth)
	{
		if (!a_actor) return;
		RE::TESFormID id = a_actor->GetFormID();
		std::string nextFile;
		bool nextLoop = true;
		bool sequenceEnded = false;
		int nextPhaseIndex = -1;
		{
			std::lock_guard<std::mutex> lock(g_graphMutex);
			auto it = g_actorSequenceState.find(id);
			if (it == g_actorSequenceState.end()) return;
			ActorSequenceState& seq = it->second;
			seq.currentPhaseIndex++;
			if (seq.currentPhaseIndex >= static_cast<int>(seq.phases.size())) {
				if (seq.loop) seq.currentPhaseIndex = 0;
				else {
					g_actorSequenceState.erase(it);
					sequenceEnded = true;
				}
			}
			if (!sequenceEnded) {
				nextPhaseIndex = seq.currentPhaseIndex;
				nextFile = seq.phases[seq.currentPhaseIndex].file;
				nextLoop = (seq.phases[seq.currentPhaseIndex].loopCount != 0);
			}
		}
		if (sequenceEnded) {
			std::string animName = GetCurrentAnimation(a_actor);
			Papyrus::EventManager::GetSingleton()->DispatchSequenceEnd(a_actor, animName);
			StopAnimation(a_actor);
			return;
		}
		LoadAndStartAnimation(a_actor, nextFile, nextLoop);
		Papyrus::EventManager::GetSingleton()->DispatchPhaseBegin(a_actor, nextPhaseIndex, nextFile);
	}

	void GraphManager::SetGraphControlsPosition(RE::Actor* a_actor, bool a_lock)
	{
		if (!a_actor) return;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		if (it != g_actorGraphs.end()) it->second.positionLocked = a_lock;
	}

	std::string GraphManager::GetCurrentAnimation(RE::Actor* a_actor) const
	{
		if (!a_actor) return "";
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		return it != g_actorGraphs.end() ? it->second.currentAnimationPath : "";
	}

	void GraphManager::SetAnimationSpeed(RE::Actor* a_actor, float a_speed)
	{
		if (!a_actor) return;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		if (it == g_actorGraphs.end()) return;
		it->second.playbackSpeed = a_speed;
		if (auto* cg = dynamic_cast<Animation::ClipGenerator*>(it->second.generator.get()))
			cg->SetSpeed(a_speed);
	}

	void GraphManager::SetAnimationLooping(RE::Actor* a_actor, bool a_loop)
	{
		if (!a_actor) return;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		if (it == g_actorGraphs.end()) return;
		if (auto* cg = dynamic_cast<Animation::ClipGenerator*>(it->second.generator.get()))
			cg->SetLooping(a_loop);
	}

	bool GraphManager::GetAnimationLooping(RE::Actor* a_actor) const
	{
		if (!a_actor) return false;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		if (it == g_actorGraphs.end()) return false;
		if (auto* cg = dynamic_cast<Animation::ClipGenerator*>(it->second.generator.get()))
			return cg->IsLooping();
		return false;
	}

	float GraphManager::GetAnimationSpeed(RE::Actor* a_actor) const
	{
		if (!a_actor) return 0.0f;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		return it != g_actorGraphs.end() ? it->second.playbackSpeed : 0.0f;
	}

	void GraphManager::SetActorPosition(RE::Actor* a_actor, float a_x, float a_y, float a_z)
	{
		if (!a_actor) return;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		if (it != g_actorGraphs.end()) {
			it->second.positionX = a_x; it->second.positionY = a_y; it->second.positionZ = a_z;
		}
	}

	int GraphManager::GetSequencePhase(RE::Actor* a_actor) const
	{
		if (!a_actor) return -1;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorSequenceState.find(a_actor->GetFormID());
		return it != g_actorSequenceState.end() ? it->second.currentPhaseIndex : -1;
	}

	bool GraphManager::SetSequencePhase(RE::Actor* a_actor, int a_phase)
	{
		if (!a_actor) return false;
		std::string phaseFile;
		bool phaseLoop = true;
		{
			std::lock_guard<std::mutex> lock(g_graphMutex);
			auto it = g_actorSequenceState.find(a_actor->GetFormID());
			if (it == g_actorSequenceState.end() || a_phase < 0 || a_phase >= static_cast<int>(it->second.phases.size())) return false;
			it->second.currentPhaseIndex = a_phase;
			phaseFile = it->second.phases[a_phase].file;
			phaseLoop = (it->second.phases[a_phase].loopCount != 0);
		}
		LoadAndStartAnimation(a_actor, phaseFile, phaseLoop);
		Papyrus::EventManager::GetSingleton()->DispatchPhaseBegin(a_actor, a_phase, phaseFile);
		return true;
	}

	bool GraphManager::SetBlendGraphVariable(RE::Actor* a_actor, const std::string& a_name, float a_value)
	{
		if (!a_actor) return false;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		if (it == g_actorGraphs.end()) return false;
		it->second.blendGraphVariables[a_name] = a_value;
		return true;
	}

	float GraphManager::GetBlendGraphVariable(RE::Actor* a_actor, const std::string& a_name) const
	{
		if (!a_actor) return 0.0f;
		std::lock_guard<std::mutex> lock(g_graphMutex);
		auto it = g_actorGraphs.find(a_actor->GetFormID());
		if (it == g_actorGraphs.end()) return 0.0f;
		auto v = it->second.blendGraphVariables.find(a_name);
		return v != it->second.blendGraphVariables.end() ? v->second : 0.0f;
	}

	void GraphManager::AttachGenerator(RE::Actor* a_actor, std::unique_ptr<Generator> a_generator, float a_transitionTime)
	{
	SAF_LOG_INFO("AttachGenerator: ENTRY actor={}, gen={}, threadId={}, mainThreadId={}",
		static_cast<void*>(a_actor),
		static_cast<void*>(a_generator.get()),
		GetCurrentThreadId(),
		g_mainThreadId);
		if (!a_actor || !a_generator) {
			SAF_LOG_WARN("AttachGenerator: invalid args");
			return;
		}

		auto skeleton = Settings::GetSkeleton(a_actor);
		if (!skeleton) {
			SAF_LOG_WARN("AttachGenerator: no skeleton for actor");
			return;
		}
		SAF_LOG_INFO("AttachGenerator: skeleton ok (joints={})", skeleton->jointNames.size());

		RE::TESFormID id = a_actor->GetFormID();
		float preservedSpeed = 1.0f;
		bool preservedLock = false;
		float px = 0.0f, py = 0.0f, pz = 0.0f;
		std::unordered_map<std::string, float> preservedVars;
		{
			std::lock_guard<std::mutex> lock(g_graphMutex);
			auto it = g_actorGraphs.find(id);
			if (it != g_actorGraphs.end()) {
				preservedSpeed = it->second.playbackSpeed;
				preservedLock = it->second.positionLocked;
				px = it->second.positionX; py = it->second.positionY; pz = it->second.positionZ;
				preservedVars = it->second.blendGraphVariables;
			}
		}

		ActorGraphState state;
		state.skeleton = skeleton;
		state.generator = std::move(a_generator);
		state.jointNodes.clear();
		state.jointMapBuilt = false;
		state.loggedFirstUpdate = false;
		state.animFrameCount = 0;
		state.playbackSpeed = preservedSpeed;
		state.positionLocked = preservedLock;
		state.positionX = px; state.positionY = py; state.positionZ = pz;
		state.blendGraphVariables = std::move(preservedVars);

		if (auto* cg = dynamic_cast<Animation::ClipGenerator*>(state.generator.get()))
			cg->SetSpeed(state.playbackSpeed);

		SAF_LOG_INFO("AttachGenerator: joint map will be built in UpdateGraphs for actor={}", static_cast<void*>(a_actor));
		g_debugHookLogs.store(120, std::memory_order_relaxed);

		{
			std::lock_guard<std::mutex> lock(g_graphMutex);
			SAF_LOG_INFO("AttachGenerator: storing graph state");
			g_actorGraphs[id] = std::move(state);
		}
		SAF_LOG_INFO("AttachGenerator: graphs now={}", g_actorGraphs.size());
	g_rebindOnNextHook.store(true, std::memory_order_release);
	g_forceUpdate.store(true, std::memory_order_release);
		SAF_LOG_INFO("AttachGenerator: actor={}, transition={}s, joints={}",
			static_cast<void*>(a_actor), a_transitionTime, skeleton->jointNames.size());
	}

void GraphManager::RequestGraphUpdate()
{
	g_rebindOnNextHook.store(true, std::memory_order_release);
	g_forceUpdate.store(true, std::memory_order_release);
}

void GraphManager::EnableAnimationVtableHooks()
{
	g_allowVtableHooks.store(true, std::memory_order_release);
}

bool GraphManager::ShouldDeferHookInstall() const
{
	LoadHookOverrideFromIni();
	return g_hookOverrideRva.load(std::memory_order_acquire) != 0;
}

	void GraphManager::SetMainThreadId(std::uint32_t a_threadId)
	{
		if (g_mainThreadId == 0 && a_threadId != 0) {
			g_mainThreadId = a_threadId;
			SAF_LOG_INFO("GraphManager: main thread id set to {}", g_mainThreadId);
		}
	}

	std::uint32_t GraphManager::GetMainThreadId() const
	{
		return g_mainThreadId;
	}

	bool GraphManager::IsMainThread() const
	{
		const auto curThread = GetCurrentThreadId();
		return g_mainThreadId != 0 && curThread == g_mainThreadId;
	}

	void GraphManager::UpdateGraphs(float a_deltaSeconds)
	{
		const DWORD curThread = GetCurrentThreadId();
		if (g_mainThreadId == 0 && curThread != 0) {
			g_mainThreadId = curThread;
			SAF_LOG_INFO("GraphManager: main thread id set to {} (UpdateGraphs self-healing)", g_mainThreadId);
		}
		if (!IsMainThread()) {
			return;
		}

		// Gdy konsola lub menu edycji postaci – nie dotykaj grafów (unika CTD przy help / additem / showlooksmenu).
		if (auto* ui = RE::UI::GetSingleton()) {
			if (ui->IsMenuOpen("CharGenMenu") || ui->IsMenuOpen("LooksMenu") || ui->IsMenuOpen("FaceMenu")) {
				return;
			}
			if (ui->IsMenuOpen("Console")) {
				return;
			}
		}

		std::vector<RE::Actor*> sequenceAdvanceActors;
		{
			std::lock_guard<std::mutex> lock(g_graphMutex);
			if (g_actorGraphs.empty()) {
				static std::atomic<uint32_t> emptyGraphCount{ 0 };
				uint32_t c = ++emptyGraphCount;
				if (c <= 5 || c % 120 == 0) {
					SAF_LOG_INFO("[UPDATE] UpdateGraphs: no active graphs");
				}
				return;
			}

			static std::atomic<uint32_t> updateCount{ 0 };
			const uint32_t ucount = ++updateCount;

			for (auto& kv : g_actorGraphs) {
			const auto id = kv.first;
			auto& state = kv.second;

			if (!state.generator || !state.skeleton) {
				continue;
			}

			RE::Actor* actor = nullptr;

			if (!state.loggedFirstUpdate) {
				state.loggedFirstUpdate = true;
				SAF_LOG_INFO("[UPDATE] UpdateGraphs: first update for actor={}, joints={}", id, state.skeleton->jointNames.size());
			}

			if (!state.jointMapBuilt && !state.jointMapBuildFailed) {
				actor = RE::TESForm::LookupByID<RE::Actor>(id);
				if (!actor && id == 0x14) {
					actor = RE::PlayerCharacter::GetSingleton();
				}

				if (!actor) {
					if (ucount <= 5 || ucount % 120 == 0) {
						SAF_LOG_WARN("[UPDATE] UpdateGraphs: actor not found for id={}, will retry", id);
					}
				} else {
					SAF_LOG_INFO("[UPDATE] UpdateGraphs: building joint map for actor={}", id);
					const bool ok = SafeBuildJointMap(actor, *state.skeleton, state.jointNodes);
					if (!ok) {
						state.jointMapBuildFailed = true;  // loadedData AV, stop retrying this actor
					} else {
						size_t found = 0;
						for (auto* node : state.jointNodes) {
							if (node) {
								++found;
							}
						}

						if (found == 0) {
							if (ucount <= 5 || ucount % 120 == 0) {
								SAF_LOG_WARN("[UPDATE] UpdateGraphs: joint map not ready yet (found 0/{}), will retry", state.jointNodes.size());
							}
						} else {
							state.jointMapBuilt = true;
							SAF_LOG_INFO("[UPDATE] UpdateGraphs: joint map built (found {}/{})", found, state.jointNodes.size());

							// ── Capture rest pose: use animation bind (t=0) when available, else skeleton rest ──
							if (state.skeleton && state.skeleton->data) {
								if (auto* clipGen = dynamic_cast<Animation::ClipGenerator*>(state.generator.get())) {
									static thread_local std::vector<ozz::math::SoaTransform> s_restSoaBuf;
									clipGen->SampleAtRatio(0.f, s_restSoaBuf);
									UnpackSoaTransforms(std::span<const ozz::math::SoaTransform>(s_restSoaBuf.data(), s_restSoaBuf.size()),
										state.restTransforms, state.skeleton->jointNames.size());
								} else {
									auto restSoa = state.skeleton->data->joint_rest_poses();
									UnpackSoaTransforms(restSoa, state.restTransforms, state.skeleton->jointNames.size());
								}
							}

							// ── Snapshot game's original bone rotations ────────────────────────
							// Ensure NiTransform offset is known before capture (probe runs on first non-null node).
							if (!g_niTransformSearchDone.load(std::memory_order_acquire)) {
								for (size_t si = 0; si < state.jointNodes.size(); ++si) {
									if (state.jointNodes[si]) {
										g_niTransformOffset.store(ProbeNiTransformOffset(state.jointNodes[si]), std::memory_order_release);
										g_niTransformSearchDone.store(true, std::memory_order_release);
										break;
									}
								}
							}

							const std::uintptr_t off = g_niTransformOffset.load(std::memory_order_acquire);
							state.gameBaseRotations.resize(state.jointNodes.size());
							for (size_t si = 0; si < state.jointNodes.size(); ++si) {
								auto& arr = state.gameBaseRotations[si];
								if (!state.jointNodes[si] || !ReadBoneRotation(state.jointNodes[si], off, arr.data())) {
									arr = {1,0,0, 0,1,0, 0,0,1};
								}
							}
							SAF_LOG_INFO("[UPDATE] UpdateGraphs: game base rotations captured ({} joints)", state.gameBaseRotations.size());

							// Optional: capture actor rest pose (rotation + translation) from each bone for UseActorRestPose.
							if (g_useActorRestPose.load(std::memory_order_acquire)) {
								state.actorRestTransforms.resize(state.jointNodes.size());
								size_t captured = 0;
								for (size_t si = 0; si < state.jointNodes.size(); ++si) {
									Animation::Transform tr;
									tr.rotation = {0,0,0,1};
									tr.translation = {0,0,0};
									tr.scale = 1.0f;
									if (state.jointNodes[si] && ReadBoneFullTransform(state.jointNodes[si], off, tr)) {
										state.actorRestTransforms[si] = tr;
										++captured;
									} else {
										state.actorRestTransforms[si] = tr;
									}
								}
								SAF_LOG_INFO("[UPDATE] UpdateGraphs: actor rest pose captured ({} joints)", captured);
								// Używaj tylko ROTACJI z aktora jako rest, translację zostaw z ozz (unikamy lewitacji).
								if (captured > 0 && state.actorRestTransforms.size() == state.restTransforms.size()
									&& state.skeleton && state.skeleton->jointNames.size() == state.restTransforms.size()) {
									for (size_t si = 0; si < state.restTransforms.size(); ++si) {
										const char* jname = state.skeleton->jointNames[si].c_str();
										// Na początek koryguj tylko kręgosłup i nogi – ręce już mają własne fixy.
										if (!IsSpineOrLegJoint(jname))
											continue;
										state.restTransforms[si].rotation = state.actorRestTransforms[si].rotation;
									}
									SAF_LOG_INFO("[UPDATE] UpdateGraphs: restTransforms rotation overridden from actor rest for spine/legs");
								}
							}

							// ── EXTENDED BONEMAT DUMP (arm + spine + leg bones) ──────────────────
							static const char* kDumpBones[] = {
								"COM","C_Spine","C_Spine2","C_Chest",
								"L_Clavicle","L_Biceps","L_Forearm","L_Wrist","L_Arm",
								"R_Clavicle","R_Biceps","R_Forearm","R_Wrist","R_Arm",
								"L_Thigh","L_Calf","L_Foot","L_Ankle",
								"R_Thigh","R_Calf","R_Foot","R_Ankle",
								"C_Pelvis","Pelvis","Hips",
								nullptr
							};
							for (size_t si = 0; si < state.jointNodes.size() && si < state.gameBaseRotations.size(); ++si) {
								if (si >= state.skeleton->jointNames.size()) break;
								const std::string& jname = state.skeleton->jointNames[si];
								bool dump = false;
								for (const char** d = kDumpBones; *d; ++d) {
									if (jname == *d) { dump = true; break; }
								}
								if (!dump) continue;
								const float* b = state.gameBaseRotations[si].data();
								SAF_LOG_INFO("[BONEMAT] joint='{}' idx={} game_base_row0=[{:.4f},{:.4f},{:.4f}] row1=[{:.4f},{:.4f},{:.4f}] row2=[{:.4f},{:.4f},{:.4f}]",
									jname, si, b[0],b[1],b[2], b[3],b[4],b[5], b[6],b[7],b[8]);
								if (si < state.restTransforms.size()) {
									const auto& r = state.restTransforms[si];
									SAF_LOG_INFO("[BONEMAT] joint='{}' ozz_rest_q=[{:.4f},{:.4f},{:.4f},{:.4f}]",
										jname, r.rotation.x, r.rotation.y, r.rotation.z, r.rotation.w);
								}
							}
						}
					}
				}
			}

		if (!state.jointMapBuilt) {
			continue;
		}

			Animation::ClipGenerator* clipGen = dynamic_cast<Animation::ClipGenerator*>(state.generator.get());
			if (clipGen) {
				clipGen->SetDeltaTime(a_deltaSeconds);
				clipGen->SetSpeed(state.playbackSpeed);
			}

			auto soa = state.generator->Generate(nullptr);
			UnpackSoaTransforms(soa, state.localTransforms, state.skeleton->jointNames.size());
			++state.animFrameCount;

			if (clipGen && clipGen->IsFinished()) {
				auto itSeq = g_actorSequenceState.find(id);
				if (itSeq != g_actorSequenceState.end()) {
					if (!actor) {
						actor = RE::TESForm::LookupByID<RE::Actor>(id);
						if (!actor && id == 0x14) actor = RE::PlayerCharacter::GetSingleton();
					}
					if (actor) sequenceAdvanceActors.push_back(actor);
				}
			}

			if (ucount <= 5 || ucount % 120 == 0) {
				SAF_LOG_INFO("[UPDATE] UpdateGraphs: actor={}, joints={}, dt={}",
					id, state.skeleton->jointNames.size(), a_deltaSeconds);
			}

			if (!actor) {
				actor = RE::TESForm::LookupByID<RE::Actor>(id);
				if (!actor && id == 0x14) {
					actor = RE::PlayerCharacter::GetSingleton();
				}
			}
			RE::NiAVObject* actorRoot = actor ? GetActor3DRootRaw(actor) : nullptr;

			static const float kIdentBase[9] = {1,0,0, 0,1,0, 0,0,1};
			static Animation::Transform kIdentRest;  // default ctor: rot=(0,0,0,1)

			size_t appliedCount = 0, skippedRoot = 0, skippedControlled = 0;
			std::vector<RE::NiAVObject*> modifiedBones;
			modifiedBones.reserve(state.jointNodes.size());

			// Joint 0 is always the actor 3D root (or mapped to it). Never write to it or to any node that is the same as joint 0.
			RE::NiAVObject* const skeletonRootNode = state.jointNodes.empty() ? nullptr : state.jointNodes[0];
			ClearTorsoRefForFrame();
			for (size_t i = 0; i < state.jointNodes.size(); ++i) {
				if (!state.jointNodes[i]) continue;
				// Never apply to joint 0 (actor 3D root) – writing to it makes the character disappear.
				if (i == 0) {
					++skippedRoot;
					continue;
				}
				// Skip any joint that points to the same node as joint 0 (e.g. "Root_" alias can map to same node as "HumanRace").
				if (skeletonRootNode && state.jointNodes[i] == skeletonRootNode) {
					++skippedRoot;
					continue;
				}
				// Skip if this node is the actor root (in case GetActor3DRootRaw returns a different pointer).
				if (actorRoot && state.jointNodes[i] == actorRoot) {
					++skippedRoot;
					continue;
				}
				if (i < state.skeleton->controlledByGame.size() && state.skeleton->controlledByGame[i]) {
					++skippedControlled;
					continue;
				}

				const char* jointName = (i < state.skeleton->jointNames.size()) ? state.skeleton->jointNames[i].c_str() : nullptr;
				// Kości twarzy – pomijamy tylko gdy AnimateFaceJoints=0 (gra nimi steruje). Gdy =1, animujemy je z blendem żeby nie znikały.
				if (jointName && IsFaceJoint(jointName)) {
					if (!g_animateFaceJoints.load(std::memory_order_acquire)) {
						++skippedControlled;
						continue;
					}
				}
				// W trybie NAF nie piszemy do kości dłoni (palce, cup, Wrist) – gra nimi steruje (IK).
				if (g_useNAFApplyMode.load(std::memory_order_acquire) && jointName) {
					if (IsHandJoint(jointName)) {
						++skippedControlled;
						continue;
					}
				}
				// Dłonie: jeśli klip nie animuje tej kości (brak kanału), nie nadpisuj – zostaw grze (naturalna poza/IK). Jak przy twarzy.
				if (jointName && IsHandJoint(jointName)) {
					if (i < state.jointHasChannel.size() && state.jointHasChannel[i] == 0) {
						++skippedControlled;
						continue;
					}
				}
				// HumanRace.json ma zduplikowane nazwy (L_Wrist/R_Wrist dwa razy) – oba mapują na ten sam węzeł. Zapis tylko raz (pierwszy indeks wygrywa).
				{
					bool alreadyApplied = false;
					for (size_t j = 0; j < i; ++j) {
						if (state.jointNodes[j] == state.jointNodes[i]) {
							alreadyApplied = true;
							break;
						}
					}
					if (alreadyApplied) {
						++skippedControlled;
						continue;
					}
				}

				const auto& rest = (i < state.restTransforms.size())     ? state.restTransforms[i]         : kIdentRest;
				Animation::Transform anim = state.localTransforms[i];
				// Skip joints with no animation (anim == rest) only when not NAF mode – w NAF piszemy do wszystkich kości co klatkę, żeby po kilku sekundach nie wyginało (gra nie nadpisuje części kości).
				if (g_applyOnlyToAnimatedJoints.load(std::memory_order_acquire) && !g_useNAFApplyMode.load(std::memory_order_acquire) && !g_applyAnimRotationOnly.load(std::memory_order_acquire)) {
					const float dot = anim.rotation.x * rest.rotation.x + anim.rotation.y * rest.rotation.y +
					                 anim.rotation.z * rest.rotation.z + anim.rotation.w * rest.rotation.w;
					if (dot * dot > 0.9999f)  // same rotation (quat dot ±1)
						continue;
				}

				const float* base = (i < state.gameBaseRotations.size()) ? state.gameBaseRotations[i].data() : kIdentBase;

				// Kości twarzy (AnimateFaceJoints=1):
				// - jeśli klip ich nie animuje (brak kanałów), nie pisz nic (zostaw grze) – inaczej domyślne tracki potrafią "wyciąć" dolną twarz.
				// - jeśli animuje: miksuj z pozą gry, żeby były animowane i nie znikały.
				// - jeśli nie da się odczytać pozy gry: nie pisz (zostaw grze).
				if (jointName && IsFaceJoint(jointName) && g_animateFaceJoints.load(std::memory_order_acquire)) {
					if (i < state.jointHasChannel.size() && state.jointHasChannel[i] == 0) {
						++skippedControlled;
						continue;
					}
					Animation::Transform gamePose;
					if (!ReadBoneFullTransform(state.jointNodes[i], g_niTransformOffset.load(std::memory_order_acquire), gamePose)) {
						// Nie nadpisuj kości twarzy bez blendu – inaczej znika dolna część (usta, broda). Gra zostaje przy swojej pozie.
						++skippedControlled;
						continue;
					}
					float w = g_faceAnimStrength.load(std::memory_order_acquire);
					if (w <= 0.0f || w > 1.0f) w = 0.5f;
					if (w > 0.85f) w = 0.85f;  // cap: zawsze min. ~15% gry, żeby twarz nie znikała przy FaceAnimationStrength=1
					const float iw = 1.0f - w;
					float qx = iw * gamePose.rotation.x + w * anim.rotation.x;
					float qy = iw * gamePose.rotation.y + w * anim.rotation.y;
					float qz = iw * gamePose.rotation.z + w * anim.rotation.z;
					float qw = iw * gamePose.rotation.w + w * anim.rotation.w;
					const float qlen2 = qx*qx + qy*qy + qz*qz + qw*qw;
					if (qlen2 > 1e-8f) {
						const float invLen = 1.0f / std::sqrt(qlen2);
						qx *= invLen; qy *= invLen; qz *= invLen; qw *= invLen;
						anim.rotation.x = qx; anim.rotation.y = qy; anim.rotation.z = qz; anim.rotation.w = qw;
					}
					const float tw = 0.2f;  // 20% anim translation, 80% game – dolna część twarzy się nie rozjeżdża
					anim.translation.x = (1.0f - tw) * gamePose.translation.x + tw * anim.translation.x;
					anim.translation.y = (1.0f - tw) * gamePose.translation.y + tw * anim.translation.y;
					anim.translation.z = (1.0f - tw) * gamePose.translation.z + tw * anim.translation.z;
				}

				// ── FRAMEDUMP: first 6 frames + frames 30,60,90 for key bones ──────────────
				if (jointName && (state.animFrameCount <= 6 || state.animFrameCount == 30 || state.animFrameCount == 60 || state.animFrameCount == 90)) {
					static const char* kFrameDumpBones[] = {
						"R_Biceps","R_Thigh","R_Calf","C_Spine","C_Pelvis","Pelvis","Hips",
						"L_Thigh","L_Calf",nullptr
					};
					bool wantDump = false;
					for (const char** d = kFrameDumpBones; *d; ++d)
						if (strcmp(jointName, *d) == 0) { wantDump = true; break; }
					if (wantDump) {
						const float dot = anim.rotation.x * rest.rotation.x + anim.rotation.y * rest.rotation.y +
						                  anim.rotation.z * rest.rotation.z + anim.rotation.w * rest.rotation.w;
						SAF_LOG_INFO("[FRAMEDUMP] f={} '{}' animated={} anim_q=[{:.4f},{:.4f},{:.4f},{:.4f}] rest_q=[{:.4f},{:.4f},{:.4f},{:.4f}]",
							state.animFrameCount, jointName, (dot*dot<=0.9999f)?"YES":"no",
							anim.rotation.x, anim.rotation.y, anim.rotation.z, anim.rotation.w,
							rest.rotation.x, rest.rotation.y, rest.rotation.z, rest.rotation.w);
					}
				}

				ApplyTransform(state.jointNodes[i], anim, rest, base, jointName);
				modifiedBones.push_back(state.jointNodes[i]);
				++appliedCount;
			}

			if (ucount <= 5 || ucount % 120 == 0) {
				SAF_LOG_INFO("[UPDATE] UpdateGraphs: apply counts applied={} skippedRoot={} skippedControlled={}",
					appliedCount, skippedRoot, skippedControlled);
			}

			if (actor) {
				if (state.positionLocked) {
					RE::NiPoint3 pos(state.positionX, state.positionY, state.positionZ);
					actor->SetPosition(pos, false);
				} else {
					auto itSync = g_syncOwner.find(id);
					if (itSync != g_syncOwner.end() && itSync->second != id) {
						RE::Actor* owner = RE::TESForm::LookupByID<RE::Actor>(itSync->second);
						if (owner) actor->SetPosition(owner->GetPosition(), false);
					}
				}
				SafeUpdateWorldData(actor, actorRoot, &modifiedBones);
			}
			}
		}
		for (RE::Actor* a : sequenceAdvanceActors) AdvanceSequence(a, false);
	}

	bool GraphManager::HasActiveGraphs() const
	{
		std::lock_guard<std::mutex> lock(g_graphMutex);
		return !g_actorGraphs.empty();
	}

	void GraphManager::Reset()
	{
		SAF_LOG_INFO("GraphManager::Reset - clearing animation data");
	}
}