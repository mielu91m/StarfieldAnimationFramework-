#include "PCH.h"

#include "Commands/SAFCommand.h"
#include "API/MCF_API.h"

namespace Commands
{
    void RegisterSAFCommands()
    {
        spdlog::info("Registering SAF console commands...");
        
        if (!MCF::IsAvailable()) {
            spdlog::warn("ModernCommandFramework unavailable.");
            return;
        }

        bool success = MCF::RegisterCommand("saf", Commands::SAFCommand::Run);
        
        if (success) {
            spdlog::info("SAF command registered.");
        } else {
            spdlog::error("Failed to register SAF command.");
        }
    }
}
