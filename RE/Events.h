#pragma once

namespace RE
{
	struct MenuModeChangeEvent
	{
		BSFixedString menuName;
		bool enteringMenuMode;
	};
	static_assert(sizeof(MenuModeChangeEvent) == 0x10);

	struct InitLoadEvent
	{
		enum Stage
		{
			Unk1 = 1,
			Unk2,
			Unk3,
			Unk4,
			Unk5
		};
		uint32_t stage;
		uint32_t stageMax;
		uint64_t unk;

		static void RegisterSink(BSTEventSink<InitLoadEvent>* a_sink) {
			using func_t = decltype(&RegisterSink);
			// Nowy RVA z IDA dla wersji 1.16.236: 0x2B6B490
			// Stary ID 37725 nie istnieje w nowej wersji Address Library
			REL::Relocation<func_t> func(REL::Offset(0x2B6B490));
			return func(a_sink);
		}
	};
}