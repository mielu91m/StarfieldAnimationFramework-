#pragma once

// Legacy: SAF ładuje NIF przez RE::ModelDB::GetEntry (ModelDB.h, NAF-style, ID 183072+36741).
// Ten plik zostawiony na wypadek innego użycia Demand(); główna ścieżka to ModelDB.h.

#include "RE/B/BSResourceEnums.h"
#include "RE/N/NiNode.h"
#include "RE/N/NiSmartPointer.h"
#include "REL/Relocation.h"

namespace RE
{
	struct BSModelDB
	{
		struct ArgsType
		{
			std::int32_t  lodFadeMult = 0;           // 00
			std::uint32_t loadLevel = 0;             // 04
			std::uint8_t  prepareAfterLoad : 1 = 1; // 08:00
			std::uint8_t  faceGenModel : 1 = 0;     // 08:01
			std::uint8_t  useErrorMarker : 1 = 0;    // 08:02
			std::uint8_t  performProcess : 1 = 1;   // 08:03
			std::uint8_t  createFadeNode : 1 = 0;   // 08:04
			std::uint8_t  loadTextures : 1 = 0;     // 08:05
		};
		static_assert(sizeof(ArgsType) <= 0x0C, "ArgsType must match game layout (e.g. 0x0C)");

		/// Ładuje model NIF po ścieżce i zwraca root (NiNode) w a_result.
		/// Zwraca BSResource::ErrorCode (kNone = sukces).
		static BSResource::ErrorCode Demand(
			const char*               a_modelPath,
			NiPointer<NiNode>*        a_result,
			const ArgsType&           a_args)
		{
			using func_t = BSResource::ErrorCode (*)(const char*, NiPointer<NiNode>*, const ArgsType&);
			// ID z NAF/starego CommonLib – może wymagać aktualizacji pod Twoją wersję Starfield
			static REL::Relocation<func_t> func{ REL::ID(36741) };
			return func(a_modelPath, a_result, a_args);
		}
	};
}
