#include "PCH.h"
#include "Util/General.h"
#include "SaveLoadListener.h"
#include "Animation/GraphManager.h"
#include "Animation/Face/Manager.h"
#include "Papyrus/EventManager.h"

namespace Tasks::SaveLoadListener
{
	static Util::VFuncHook<void*(RE::BSScript::Internal::VirtualMachine*)> RevertHook(
		481117, 
		0x7, 
		"BSScript::Internal::VirtualMachine::DropAllRunningData",
		[](RE::BSScript::Internal::VirtualMachine* a_this) -> void* {
			SAF_LOG_INFO("SaveLoadListener: Game loading/reverting - resetting managers");
			
			Animation::GraphManager::GetSingleton()->Reset();
			Animation::Face::Manager::GetSingleton()->Reset();
			Papyrus::EventManager::GetSingleton()->Reset();
			
			return RevertHook(a_this);
		}
	);
}