#pragma once

// API ModelDB – GetEntry z dwoma REL::ID z Address Library.
// ID przekazywane z zewnątrz (GraphManager), domyślnie 949826 (GetModelDBEntry) i 36741 (DecRef).

#include "RE/N/NiNode.h"
#include "REL/Relocation.h"

namespace RE
{
	namespace ModelDB
	{
		struct Entry
		{
			void*    unk00;
			void*    unk08;
			void*    unk10;
			void*    unk18;
			NiNode*  node;
		};

		/// UnkOut – struktura używana przez GetEntry i DecRef (tak jak w grze).
		struct UnkOut
		{
			std::uint32_t unk00 = 0;
			std::uint32_t unk04 = 0;
			std::uint64_t unk08 = 0;
			std::uint64_t unk10 = 0;
		};

		/// Ładuje wpis modelu NIF. a_getEntryID i a_decRefID z Address Library (INI).
		inline Entry* GetEntry(const char* filename, std::uint64_t a_getEntryID, std::uint64_t a_decRefID)
		{
			constexpr std::uint64_t flag = 0x3;
			UnkOut out;
			out.unk00 = static_cast<std::uint32_t>(flag & 0xFFFFFFu);
			const bool isBound = true;
			out.unk00 |= ((16 * (isBound ? 1 : 0)) | 0x2D) << 24;
			out.unk04 = 0xFEu;

			Entry* entry = nullptr;
			REL::Relocation<int(const char*, Entry**, UnkOut&)> fnGetEntry{ REL::ID(a_getEntryID) };
			fnGetEntry(filename, &entry, out);

			REL::Relocation<std::uint64_t(UnkOut&)> fnDecRef{ REL::ID(a_decRefID) };
			fnDecRef(out);

			return entry;
		}

		/// Jak GetEntry, ale DecRef z adresu (moduleBase + a_decRefRVA). Gdy masz tylko jedno ID, podaj RVA DecRef z IDA.
		inline Entry* GetEntryWithDecRefRVA(const char* filename, std::uint64_t a_getEntryID, std::uint32_t a_decRefRVA, std::uintptr_t a_moduleBase)
		{
			constexpr std::uint64_t flag = 0x3;
			UnkOut out;
			out.unk00 = static_cast<std::uint32_t>(flag & 0xFFFFFFu);
			const bool isBound = true;
			out.unk00 |= ((16 * (isBound ? 1 : 0)) | 0x2D) << 24;
			out.unk04 = 0xFEu;

			Entry* entry = nullptr;
			REL::Relocation<int(const char*, Entry**, UnkOut&)> fnGetEntry{ REL::ID(a_getEntryID) };
			fnGetEntry(filename, &entry, out);

			auto* fnDecRef = reinterpret_cast<std::uint64_t(*)(UnkOut&)>(a_moduleBase + a_decRefRVA);
			fnDecRef(out);

			return entry;
		}
	}
}
