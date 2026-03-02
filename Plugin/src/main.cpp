#include "PCH.h" // TO MUSI BYĆ PIERWSZA LINIA

// Lokalne nagłówki (one korzystają już z oczyszczonego środowiska PCH)
#include "API/API_External.h"
#include "Core/PluginCore.h"
#include "Animation/GraphManager.h"
#include <Windows.h>

// SFSE używa GetProcAddress("SFSEPlugin_Version") – symbol MUSI mieć linkage C (bez manglowania)
extern "C" DLLEXPORT constinit const SFSE::PluginVersionData SFSEPlugin_Version = []() noexcept {
	SFSE::PluginVersionData v{};
	v.PluginVersion({ 1, 0, 0, 0 });
	v.PluginName("StarfieldAnimationFramework");
	v.AuthorName("mielu91m");
	v.UsesSigScanning(false);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse(true);
	v.IsLayoutDependent(false);
	v.CompatibleVersions({ SFSE::RUNTIME_LATEST });
	return v;
}();

// Nie potrzebujemy tu ponownie definiować MessageHandler ani InitializeLogging
// jeśli są one długie, ale dla kompletności wklejam uproszczoną wersję, 
// która korzysta z już załadowanych bibliotek z PCH.

namespace
{
    bool g_coreInitialized = false;

    void LogStartupError(const char* a_message) noexcept
    {
        OutputDebugStringA(a_message);
        OutputDebugStringA("\n");
    }

    void InitializeLogging()
    {
        try {
            auto path = SFSE::log::log_directory();
            if (!path) {
                LogStartupError("SAF: log_directory returned null");
                return;
            }

            *path /= "StarfieldAnimationFramework.log";

            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
            auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

#ifdef NDEBUG
            log->set_level(spdlog::level::info);
            log->flush_on(spdlog::level::info);
#else
            log->set_level(spdlog::level::trace);
            log->flush_on(spdlog::level::trace);
#endif

            spdlog::set_default_logger(std::move(log));
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");

            SAF_LOG_INFO("SAF v1.0.0 init...");
        } catch (const std::exception& e) {
            LogStartupError(e.what());
        } catch (...) {
            LogStartupError("SAF: unknown exception in InitializeLogging");
        }
    }

    void MessageHandler(SFSE::MessagingInterface::Message* a_msg)
    {
        try {
            switch (a_msg->type) {
            case SFSE::MessagingInterface::kPostLoad:
                SAF_LOG_INFO("PostLoad");
                break;
            case SFSE::MessagingInterface::kPostPostLoad:
                SAF_LOG_INFO("PostPostLoad");
                if (!g_coreInitialized) {
                    auto* core = SAF::Core::PluginCore::GetSingleton();
                    if (!core || !core->Initialize()) {
                        SAF_LOG_ERROR("Failed to initialize core!");
                        break;
                    }
                    g_coreInitialized = true;
                }
                SAF::Core::PluginCore::GetSingleton()->OnPostPostLoad();
                break;
            case SFSE::MessagingInterface::kPostDataLoad:
                SAF_LOG_INFO("PostDataLoad");
                if (g_coreInitialized) {
                    SAF::Core::PluginCore::GetSingleton()->OnPostDataLoad();
                }
                break;
            default:
                break;
            }
        } catch (const std::exception& e) {
            LogStartupError(e.what());
        } catch (...) {
            LogStartupError("SAF: unknown exception in MessageHandler");
        }
    }
}

extern "C" DLLEXPORT bool SFSEAPI SFSEPlugin_Query(const SFSE::QueryInterface* a_sfse, SFSE::PluginInfo* a_info)
{
    a_info->infoVersion = SFSE::PluginInfo::kVersion;
    a_info->name = SFSEPlugin_Version.pluginName;
    a_info->version = SFSEPlugin_Version.pluginVersion;

    if (a_sfse->RuntimeVersion() < SFSE::RUNTIME_LATEST) {
        return false;
    }
    return true;
}

extern "C" DLLEXPORT bool SFSEAPI SFSEPlugin_Load(const SFSE::LoadInterface* a_sfse)
{
    try {
        InitializeLogging();
        SAF_LOG_INFO("SAF Plugin loading...");
        SFSE::Init(a_sfse);
        if (auto* messaging = SFSE::GetMessagingInterface()) {
            messaging->RegisterListener(MessageHandler);
        }
        SAF_LOG_INFO("SAF Loaded.");
        return true;
    } catch (const std::exception& e) {
        LogStartupError(e.what());
        return false;
    } catch (...) {
        LogStartupError("SAF: unknown exception in SFSEPlugin_Load");
        return false;
    }
}