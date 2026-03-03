#include "PCH.h"
#include "Papyrus/SAFScript.h"
#include "Papyrus/EventManager.h"
#include "Animation/GraphManager.h"
#include "RE/P/PlayerCharacter.h"
#include "Util/String.h"
#include <filesystem>

namespace Papyrus::SAFScript
{
	namespace
	{
		// Lokalny resolver ścieżek animacji – duplikat logiki z Commands::SAFCommand,
		// żeby Papyrus nie musiał przechodzić przez konsolowy parser.
		std::filesystem::path ResolveAnimationPathWithFallback(std::string_view a_path)
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

			return resolved;
		}

		bool PlayOnActorImpl(RE::Actor* a_actor, std::string_view a_animId)
		{
			if (!a_actor) {
				SAF_LOG_WARN("[Papyrus] PlayOnActorImpl: actor is null");
				return false;
			}

			auto* mgr = Animation::GraphManager::GetSingleton();
			if (!mgr) {
				SAF_LOG_ERROR("[Papyrus] PlayOnActorImpl: GraphManager singleton is null");
				return false;
			}

			auto resolvedKey = Util::String::ToLower(a_animId);
			auto resolvedPath = ResolveAnimationPathWithFallback(resolvedKey);
			std::string pathStr = resolvedPath.string();

			try {
				SAF_LOG_INFO("[Papyrus] PlayOnActorImpl: playing '{}' on actor {:08X}", pathStr, a_actor->GetFormID());
				mgr->LoadAndStartAnimation(a_actor, pathStr);
				mgr->RequestGraphUpdate();
				return true;
			} catch (const std::exception& e) {
				SAF_LOG_ERROR("[Papyrus] PlayOnActorImpl: exception '{}'", e.what());
				return false;
			} catch (...) {
				SAF_LOG_ERROR("[Papyrus] PlayOnActorImpl: unknown exception");
				return false;
			}
		}
	}

	// Pomocnicza funkcja do pobierania handle
	// TODO: Po otrzymaniu Object.h sprawdzić faktyczne API
	Papyrus::VMHandle GetHandle(const RE::BSScript::IVirtualMachine* a_vm, const RE::BSScript::Object* a_obj)
	{
		if (!a_obj) return 0;
		
		// Tymczasowe rozwiązanie - używamy adresu obiektu jako handle
		// WYMAGA SPRAWDZENIA: jak faktycznie pobrać handle w Starfield?
		// Możliwe metody: a_obj->GetHandle(), a_obj->GetVMHandle(), etc.
		return reinterpret_cast<Papyrus::VMHandle>(a_obj);
	}

	// CommonLibSF BindNativeMethod: "self" musi być lvalue reference (Object&), nie Object*.
	// valid_parameter nie dopuszcza surowego Object*; dozwolone są reference do Object lub TESForm.
	void RegisterForSAFEvent(
		RE::BSScript::IVirtualMachine& a_vm,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script,
		std::uint32_t a_eventType,
		RE::BSFixedString a_funcName)
	{
		auto type = static_cast<Papyrus::EventType>(a_eventType);
		auto handle = GetHandle(&a_vm, &a_script);

		Papyrus::EventManager::GetSingleton()->RegisterScript(type, handle, a_funcName.c_str());
		SAF_LOG_INFO("Registered for SAF event type {}", a_eventType);
	}

	void UnregisterFromSAFEvent(
		RE::BSScript::IVirtualMachine& a_vm,
		std::uint32_t /* a_stackID */,
		RE::BSScript::Object& a_script,
		std::uint32_t a_eventType)
	{
		auto type = static_cast<Papyrus::EventType>(a_eventType);
		auto handle = GetHandle(&a_vm, &a_script);

		Papyrus::EventManager::GetSingleton()->UnregisterScript(type, handle);
		SAF_LOG_INFO("Unregistered from SAF event type {}", a_eventType);
	}

	// Papyrus: SAFScript.PlayOnActor(Actor akActor, string animId) -> bool
	bool PlayOnActor(
		RE::BSScript::IVirtualMachine& /*a_vm*/,
		std::uint32_t /*a_stackID*/,
		RE::BSScript::Object& /*a_script*/,
		RE::Actor* a_actor,
		RE::BSFixedString a_animId)
	{
		if (!a_actor) {
			SAF_LOG_WARN("[Papyrus] PlayOnActor: actor is none");
			return false;
		}
		if (!a_animId.data()) {
			SAF_LOG_WARN("[Papyrus] PlayOnActor: animId is empty");
			return false;
		}
		return PlayOnActorImpl(a_actor, a_animId.c_str());
	}

	// Papyrus: SAFScript.PlayOnPlayer(string animId) -> bool
	bool PlayOnPlayer(
		RE::BSScript::IVirtualMachine& /*a_vm*/,
		std::uint32_t /*a_stackID*/,
		RE::BSScript::Object& /*a_script*/,
		RE::BSFixedString a_animId)
	{
		if (!a_animId.data()) {
			SAF_LOG_WARN("[Papyrus] PlayOnPlayer: animId is empty");
			return false;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			SAF_LOG_ERROR("[Papyrus] PlayOnPlayer: PlayerCharacter singleton is null");
			return false;
		}

		return PlayOnActorImpl(player, a_animId.c_str());
	}

	bool Bind(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			SAF_LOG_ERROR("Cannot bind SAFScript: VM is null");
			return false;
		}

		const char* className = "SAFScript";

		try {
			// STARFIELD API: BindNativeMethod wymaga 5 argumentów
			// Sygnatura: BindNativeMethod(className, methodName, function, stateful, latent)
			
			a_vm->BindNativeMethod(
				className,                    // std::string_view - nazwa klasy
				"RegisterForSAFEvent",        // std::string_view - nazwa metody
				RegisterForSAFEvent,          // F - wskaźnik do funkcji
				std::nullopt,                 // std::optional<bool> - czy stateful?
				false                         // bool - czy latent (asynchroniczna)?
			);
			
			a_vm->BindNativeMethod(
				className,
				"UnregisterFromSAFEvent",
				UnregisterFromSAFEvent,
				std::nullopt,
				false
			);

			// Funkcje odtwarzania animacji
			a_vm->BindNativeMethod(
				className,
				"PlayOnActor",
				PlayOnActor,
				std::nullopt,
				false
			);

			a_vm->BindNativeMethod(
				className,
				"PlayOnPlayer",
				PlayOnPlayer,
				std::nullopt,
				false
			);

			SAF_LOG_INFO("Successfully bound SAFScript Papyrus functions");
			return true;
			
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("Failed to bind SAFScript: {}", e.what());
			return false;
		}
	}
}