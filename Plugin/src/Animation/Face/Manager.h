#pragma once
#include "PCH.h"

namespace Animation::Face
{
    class Manager
    {
    public:
        static Manager* GetSingleton()
        {
            static Manager singleton;
            return &singleton;
        }

        // DODANE: Metoda Reset wymagana przez SaveLoadListener
        void Reset()
        {
            SAF_LOG_INFO("Face::Manager::Reset - clearing facial animation data");
            // Tutaj można dodać czyszczenie danych animacji twarzy
        }

        // USUNIĘTE: BGSLoadGameBuffer i BGSSaveGameBuffer nie istnieją w Starfield
        // Jeśli potrzebujesz serializacji, użyj SFSE::SerializationInterface
        // void Load(RE::BGSLoadGameBuffer* a_buf);
        // void Save(RE::BGSSaveGameBuffer* a_buf);
    };
}