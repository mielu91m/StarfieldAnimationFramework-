#include "PCH.h"
#include "Papyrus/SAFScript.h"
#include "Papyrus/EventManager.h"
#include "Animation/GraphManager.h"

namespace Papyrus::SAFScript
{
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

			SAF_LOG_INFO("Successfully bound SAFScript Papyrus functions");
			return true;
			
		} catch (const std::exception& e) {
			SAF_LOG_ERROR("Failed to bind SAFScript: {}", e.what());
			return false;
		}
	}
}