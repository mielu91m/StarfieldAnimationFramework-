#pragma once
#include "Commands/RegisterCommands.h"
#include "Animation/GraphManager.h"
#include "Animation/NIFBoneManager.h"
#include "Settings/SkeletonImpl.h"
#include "Papyrus/SAFScript.h"


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
            
            // Inicjalizacja NIFBoneManager - rejestruj domyślne kości penisa
            Animation::NIFBoneManager::InitializeDefaultPenisBones();
            
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
            // W praktyce event PreLoadGame bywa pomijany, więc ustaw flagę ochronną
            // już tutaj - zostanie zdjęta dopiero gdy player 3D będzie gotowy.
            Animation::GraphManager::GetSingleton()->SetSaveLoadInProgress(true);
            spdlog::info("OnPostDataLoad - save-load flag set (waiting for player 3D)");
            // Nie wywołuj LoadBaseSkeletons() tutaj – ładowanie przy pierwszym GetSkeleton w grze, żeby korpus SFF/body replacery zdążyły się wyświetlić.
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

            // Wyczyść flagę save-load natychmiast.
            // g_actorGraphs jest puste po Reset(false) z kPreLoadGame (lub przy pierwszym uruchomieniu).
            // UpdateGraphs z pustym g_actorGraphs natychmiast wraca – bezpieczne nawet gdy świat 3D
            // jeszcze nie jest gotowy. NIE wołamy vtable[0xAC] (Get3D) podczas streamowania tekstur –
            // to powodowało CTD w BSStreaming::ResourceUploader (Starfield.exe+2A462CF).
            Animation::GraphManager::GetSingleton()->SetSaveLoadInProgress(false);
            spdlog::info("OnPostLoadGame - save-load flag cleared");

            if (!hooksInstalled_) {
                spdlog::info("Installing animation hooks (PostLoadGame)...");
                Animation::GraphManager::GetSingleton()->InstallHooks();
                hooksInstalled_ = true;
                spdlog::info("Animation hooks installed (PostLoadGame)");
            }

            // Automatyczny rebind Papyrus po wczytaniu zapisu
            spdlog::info("OnPostLoadGame - performing automatic Papyrus rebind");
            Papyrus::SAFScript::RebindAfterLoad();
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