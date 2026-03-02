#pragma once

#include "API/MCF_API.h"
#include "RE/Starfield.h"

namespace Commands::SAFCommand
{
    void Run(const MCF::simple_array<MCF::simple_string_view>& a_args, const char* a_fullString, MCF::ConsoleInterface* a_intfc);
    void RegisterKeybinds();

    // Wywoływane z głównego wątku (np. Input hook) – przetwarza kolejkę odroczonych zleceń
    void ProcessPendingCommands();
    bool HasPendingCommands();
    void RequestProcessPending();
    bool HasProcessRequest();
    bool ConsumeProcessRequest();
    void ProcessPendingDump();
    bool HasPendingDump();
    bool ConsumeDumpRequest();
    void ClearPendingDump();
    void RequestCloseConsole();
    bool ConsumeCloseConsole();
    void CloseConsoleMainThread();
}