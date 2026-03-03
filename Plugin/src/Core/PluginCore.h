#pragma once
#include "Commands/RegisterCommands.h"
#include "Animation/GraphManager.h"
#include "Settings/SkeletonImpl.h"


namespace SAF::Core
{
    class PluginCore
    {
    public:
        // Pobiera instancję singletona
        static PluginCore* GetSingleton()
        {
            static PluginCore singleton;
            return &singleton;
        }

        // Inicjalizacja głównych systemów
        bool Initialize()
        {
            spdlog::info("Initializing SAF Plugin Core...");
            spdlog::info("SAF Plugin Core initialized successfully");
            return true;
        }

        // Obsługa zdarzeń z MessageHandler w main.cpp
        void OnPostPostLoad() 
        { 
            spdlog::info("OnPostPostLoad - all plugins loaded");
            
            // TUTAJ rejestrujemy komendy MCF po załadowaniu wszystkich pluginów
            // W tym momencie ModernCommandFramework.dll powinno już być załadowane
            RegisterCommands();

            if (!hooksInstalled_) {
                auto* graphMgr = Animation::GraphManager::GetSingleton();
                if (graphMgr && graphMgr->ShouldDeferHookInstall()) {
                    spdlog::info("Deferring animation hooks until PostLoadGame (RVA override set)");
                } else {
                    spdlog::info("Installing animation hooks...");
                    Animation::GraphManager::GetSingleton()->InstallHooks();
                    hooksInstalled_ = true;
                    spdlog::info("Animation hooks installed");
                }
            }
        }
        
        void OnPostDataLoad() 
        { 
            spdlog::info("OnPostDataLoad - game data loaded");
            Settings::LoadBaseSkeletons();
            // SFSE nie ma kPostLoadGame – gdy hooki były odroczone, instaluj je teraz (dane gry załadowane)
            if (!hooksInstalled_) {
                spdlog::info("Installing animation hooks (PostDataLoad, deferred)...");
                Animation::GraphManager::GetSingleton()->InstallHooks();
                hooksInstalled_ = true;
                spdlog::info("Animation hooks installed (PostDataLoad)");
            }
        }
        
        void OnNewGame()      
        { 
            spdlog::info("OnNewGame - new game started");
        }
        
        void OnPreLoadGame()  
        { 
            spdlog::info("OnPreLoadGame - about to load save");
        }
        
        void OnPostLoadGame() 
        { 
            spdlog::info("OnPostLoadGame - save loaded");
            if (!hooksInstalled_) {
                spdlog::info("Installing animation hooks (PostLoadGame)...");
                Animation::GraphManager::GetSingleton()->InstallHooks();
                hooksInstalled_ = true;
                spdlog::info("Animation hooks installed (PostLoadGame)");
            }
        }
        
        void OnSaveGame()     
        { 
            spdlog::info("OnSaveGame - saving game");
        }

    private:
        PluginCore() = default;
        ~PluginCore() = default;
        PluginCore(const PluginCore&) = delete;
        PluginCore& operator=(const PluginCore&) = delete;
        bool hooksInstalled_ = false;

        // Rejestracja komend konsolowych
        void RegisterCommands()
        {
            spdlog::info("Attempting to register console commands...");
            
            // Wywołaj funkcję rejestrującą komendy SAF w MCF
            Commands::RegisterSAFCommands();
        }
    };
}