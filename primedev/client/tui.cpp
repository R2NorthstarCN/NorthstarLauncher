#include "client/tui.h"

#include "core/math/bitbuf.h"
#include "shared/tui_protocol.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>

#define LOG_VERBOSE_INFO(...)                                                                                                              \
	do                                                                                                                                     \
	{                                                                                                                                      \
		if (::CVar_ns_tui_verbose->GetBool()) [[unlikely]]                                                                                 \
			spdlog::info(__VA_ARGS__);                                                                                                     \
	} while (0)

#define LOG_VERBOSE_WARN(...)                                                                                                              \
	do                                                                                                                                     \
	{                                                                                                                                      \
		if (::CVar_ns_tui_verbose->GetBool()) [[unlikely]]                                                                                 \
			spdlog::warn(__VA_ARGS__);                                                                                                     \
	} while (0)

namespace
{
	// clang-format off
	using HookMessageFn 					= void 		(__fastcall*)(void*, const char*, void(__fastcall*)(BFRead*));
	using ScreenWidthFn 					= int 		(__fastcall*)();
	using ScreenHeightFn 					= int 		(__fastcall*)();
	using CUserMessages__RegisterFn 		= void 		(__fastcall*)(void*, const char*, int);
	using CHudMessage__ShouldDrawFn 		= bool 		(__fastcall*)(void*);
	using CHudMessage__PaintFn 				= void 		(__fastcall*)(void*);
	using CClientState__SetSignonStateFn 	= uint64_t 	(__fastcall*)(void*, unsigned int, int, void*);
	ScreenWidthFn 					ScreenWidth 					= nullptr;
	ScreenHeightFn 					ScreenHeight 					= nullptr;
	CHudMessage__ShouldDrawFn 		CHudMessage__ShouldDraw 		= nullptr;
	CHudMessage__PaintFn 			CHudMessage__Paint 				= nullptr;
	CClientState__SetSignonStateFn 	CClientState__SetSignonState 	= nullptr;
	constexpr int VFUNC_DRAW_SET_COLOR 				= 14;
	constexpr int VFUNC_DRAW_FILLED_RECT 			= 16;
	constexpr int VFUNC_DRAW_SET_TEXT_FONT 			= 22;
	constexpr int VFUNC_DRAW_SET_TEXT_COLOR 		= 25;
	constexpr int VFUNC_DRAW_SET_TEXT_POS 			= 26;
	constexpr int VFUNC_DRAW_UNICODE_CHAR 			= 30;
	constexpr int VFUNC_CREATE_FONT 				= 78;
	constexpr int VFUNC_SET_FONT_GLYPH_SET 			= 79;
	constexpr int VFUNC_GET_FONT_TALL 				= 81;
	void** PVGUISurface = nullptr;
	void*   VGUISurface = nullptr;
	// clang-format on

	constexpr float GLYPH_ASPECT_RATIO = 0.5f;
	constexpr uint32_t UNI_REPLACEMENT = 0xFFFD;

	struct RGBA
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;

		bool operator==(const RGBA&) const = default;
	};

	// clang-format off
	constexpr RGBA PALETTE16_DARK[8] = {
		{0,   0,   0,   255},
		{196, 0,   0,   255},
		{0,   196, 0,   255},
		{196, 126, 0,   255},
		{0,   0,   196, 255},
		{196, 0,   196, 255},
		{0,   196, 196, 255},
		{196, 196, 196, 255},
	};
	constexpr RGBA PALETTE16_LIGHT[8] = {
		{78,  78,  78,  255},
		{220, 78,  78,  255},
		{78,  220, 78,  255},
		{243, 243, 78,  255},
		{78,  78,  220, 255},
		{243, 78,  243, 255},
		{78,  243, 243, 255},
		{255, 255, 255, 255},
	};
	// clang-format on
	constexpr RGBA DEFAULT_FOREGROUND = PALETTE16_DARK[7];

	struct TextStyle
	{
		RGBA foreground = DEFAULT_FOREGROUND;
		RGBA background {};
		bool hasBackground = false;
		bool bold = false;
	};
	struct Cell : TextStyle
	{
		uint8_t span = 1;
		bool mono = false;
		std::array<uint32_t, 7> glyph;
		uint8_t pos = 0;
	};

	struct TUIInstance : NSTUI::Config
	{
		uint8_t index;
		std::vector<Cell> cells;
		std::bitset<NSTUI::MAX_ROWS> rowsWithGlyphs {};
		std::bitset<NSTUI::MAX_ROWS> rowsWithBackground {};
	};

	struct PendingUpdate
	{
		std::vector<uint8_t> payload;
		uint32_t received = 0;
		uint32_t payloadSize = 0;
		uint32_t textSize = 0;
		bool compressed = false;
	};

	struct FontMetrics
	{
		uint32_t cjk = 0;
		uint32_t mono = 0;
		int height = 0;
		int width = 0;
	};

	std::array<std::optional<TUIInstance>, NSTUI::MAX_TUI_INSTANCES> TUIInstances;
	std::array<std::optional<PendingUpdate>, NSTUI::MAX_TUI_INSTANCES> PendingUpdates;
	std::unordered_map<int, FontMetrics> FontCache;
	ConVar* CVar_ns_tui_verbose = nullptr;

	template <size_t N> consteval size_t LUTSize(const std::pair<uint32_t, uint32_t> (&src)[N], int32_t offset = 0)
	{
		uint64_t max = (std::end(src) - 1)->second;
		if (offset)
		{
			if ((max += offset) > UINT32_MAX)
				throw "bad offset";
		}

		return (max + 7) >> 3;
	}
	template <size_t Size, size_t N>
	consteval std::array<uint8_t, Size> LookupTable(const std::pair<uint32_t, uint32_t> (&src)[N], const int32_t offset = 0)
	{
		std::array<uint8_t, Size> lut {};
		for (auto [lo, hi] : src)
		{
			if (offset)
			{
				const uint64_t lo64 = (uint64_t)lo + offset;
				const uint64_t hi64 = (uint64_t)hi + offset;
				if (lo64 > UINT32_MAX || hi64 > UINT32_MAX)
					throw "bad offset";
				if (hi64 >= uint64_t(Size) * 8)
					throw "bad size";
				lo = (uint32_t)lo64;
				hi = (uint32_t)hi64;
			}

			for (uint32_t v = lo; v <= hi; ++v)
				lut[v >> 3] |= uint8_t(0x80u >> (v & 7));
		}
		return lut;
	}
	template <size_t N> consteval bool IsAscending(const std::pair<uint32_t, uint32_t> (&src)[N])
	{
		for (size_t i = 1; i < N; ++i)
		{
			if (src[i].first <= src[i - 1].second)
				return false;
			if (src[i].first > src[i].second)
				return false;
		}
		return true;
	}

	bool TestBit(uint32_t v, const uint8_t* src)
	{
		return src[v >> 3] & (0x80u >> (v & 7));
	}
	bool TestBit(uint32_t v, const uint8_t* src, int32_t offset)
	{
		return TestBit(v + offset, src);
	}

	//// Renderer ////

	uint32_t CreateGlyphFont(const char* fontName, int pixelSize)
	{
		uint32_t font = CallVFunc<uint32_t>(VFUNC_CREATE_FONT, VGUISurface);
		if (!CallVFunc<bool>(VFUNC_SET_FONT_GLYPH_SET, VGUISurface, font, fontName, pixelSize, 400, 0, 0, 0x10, 0, 0))
			return 0;
		return font;
	}

	FontMetrics GetFontMetrics(int textSize, int screenHeight)
	{
		static constexpr char DEFAULT_FONT[] = "Default";
		static constexpr char NOTO_MONO_FONT[] = "noto sans mono";

		int pixelSize = std::max(1, static_cast<int>(std::lround(textSize * screenHeight / 1080.0f)));
		if (auto it = FontCache.find(pixelSize); it != FontCache.end())
		{
			if (!it->second.cjk || !it->second.mono)
			{
				spdlog::error(
					"Unable to create NSTUI font \"{}\"={}, \"{}\"={}, pixel_size={}",
					DEFAULT_FONT,
					it->second.cjk,
					NOTO_MONO_FONT,
					it->second.mono,
					pixelSize);
				return {};
			}

			return it->second;
		}

		FontMetrics metrics;
		metrics.cjk = CreateGlyphFont(DEFAULT_FONT, pixelSize);
		metrics.mono = CreateGlyphFont(NOTO_MONO_FONT, pixelSize);
		if (!metrics.cjk || !metrics.mono)
		{
			// we cache this to avoid memory leaks, since source engine doesnt give us a way to free the fonts
			// and error logs will printed in the next frame, so i dont need to write duplicate code
			FontCache[pixelSize] = metrics;
			return {};
		}

		int fontHeight = CallVFunc<int>(VFUNC_GET_FONT_TALL, VGUISurface, metrics.cjk);
		int monoFontHeight = CallVFunc<int>(VFUNC_GET_FONT_TALL, VGUISurface, metrics.mono);
		metrics.height = std::max(std::max(fontHeight, monoFontHeight), 1);
		metrics.width = std::max(static_cast<int>(std::lround(metrics.height * GLYPH_ASPECT_RATIO)), 1);

		FontCache[pixelSize] = metrics;
		return metrics;
	}

	void DrawBackgroundRun(RGBA color, int x, int y, int w, int h)
	{
		CallVFunc<void>(VFUNC_DRAW_SET_COLOR, VGUISurface, (int)color.r, (int)color.g, (int)color.b, (int)color.a);
		CallVFunc<void>(VFUNC_DRAW_FILLED_RECT, VGUISurface, x, y, x + w, y + h);
	}
	void DrawBackgrounds(const TUIInstance& instance, FontMetrics metrics, int left, int top)
	{
		for (int row = 0; row < instance.rows; ++row)
		{
			if (!instance.rowsWithBackground.test(row))
				continue;

			const size_t line = instance.cols * row;
			for (int col = 0; col < instance.cols;)
			{
				const size_t index = line + col;
				const Cell& start = instance.cells[index];
				if (!start.hasBackground)
				{
					++col;
					continue;
				}

				int end = col + 1;
				while (end < instance.cols && instance.cells[line + end].hasBackground &&
					   instance.cells[line + end].background == start.background)
				{
					++end;
				}

				int x = left + col * metrics.width;
				int y = top + row * metrics.height;
				int w = (end - col) * metrics.width;
				int h = metrics.height;
				DrawBackgroundRun(start.background, x, y, w, h);
				col = end;
			}
		}
	}

	uint32_t SelectFont(uint32_t cp, FontMetrics font)
	{
		// clang-format off
		static constexpr std::pair<uint32_t, uint32_t> NOTO_MONO_RANGES[] = {
			{0x0020, 0x007E}, {0x00A0, 0x00AC}, {0x00AE, 0x0377}, {0x037A, 0x037F}, {0x0384, 0x038A}, {0x038C, 0x038C},
			{0x038E, 0x03A1}, {0x03A3, 0x03E1}, {0x03F0, 0x052F}, {0x10FB, 0x10FB}, {0x1AB0, 0x1AC0}, {0x1AC5, 0x1AC5},
			{0x1AC7, 0x1ACE}, {0x1C80, 0x1C88}, {0x1D00, 0x1DF9}, {0x1DFB, 0x1F15}, {0x1F18, 0x1F1D}, {0x1F20, 0x1F45},
			{0x1F48, 0x1F4D}, {0x1F50, 0x1F57}, {0x1F59, 0x1F59}, {0x1F5B, 0x1F5B}, {0x1F5D, 0x1F5D}, {0x1F5F, 0x1F7D},
			{0x1F80, 0x1FB4}, {0x1FB6, 0x1FC4}, {0x1FC6, 0x1FD3}, {0x1FD6, 0x1FDB}, {0x1FDD, 0x1FEF}, {0x1FF2, 0x1FF4},
			{0x1FF6, 0x1FFE}, {0x2000, 0x2064}, {0x2066, 0x2071}, {0x2074, 0x208E}, {0x2090, 0x209C}, {0x20A0, 0x20C0},
			{0x20F0, 0x20F0}, {0x2100, 0x215F}, {0x2183, 0x2184}, {0x2189, 0x2189}, {0x2190, 0x2195}, {0x219C, 0x219E},
			{0x21A0, 0x21A0}, {0x21A2, 0x21A4}, {0x21A6, 0x21A6}, {0x21D0, 0x21D4}, {0x21DA, 0x21DB}, {0x21E6, 0x21E6},
			{0x21E8, 0x21E8}, {0x2200, 0x220E}, {0x2210, 0x2210}, {0x2212, 0x2212}, {0x2218, 0x221A}, {0x221E, 0x221E},
			{0x2220, 0x2220}, {0x2223, 0x2223}, {0x2227, 0x222A}, {0x2234, 0x2238}, {0x223C, 0x223D}, {0x2241, 0x2241},
			{0x2243, 0x2243}, {0x2245, 0x2245}, {0x2247, 0x224C}, {0x2254, 0x2255}, {0x2257, 0x2257}, {0x225F, 0x2262},
			{0x2264, 0x2265}, {0x226C, 0x226C}, {0x226E, 0x2275}, {0x227A, 0x227B}, {0x2282, 0x2289}, {0x228E, 0x228E},
			{0x2291, 0x229C}, {0x22A2, 0x22A5}, {0x22B4, 0x22B5}, {0x22B8, 0x22B8}, {0x22C2, 0x22C4}, {0x22C6, 0x22C6},
			{0x22C8, 0x22CA}, {0x22CD, 0x22CE}, {0x22D0, 0x22D1}, {0x22E2, 0x22E3}, {0x2308, 0x230B}, {0x2310, 0x2310},
			{0x2319, 0x2319}, {0x2320, 0x2321}, {0x2336, 0x237A}, {0x2395, 0x2395}, {0x239B, 0x23AE}, {0x23B0, 0x23BD},
			{0x23DC, 0x23E1}, {0x2474, 0x2475}, {0x2500, 0x25FF}, {0x266D, 0x266F}, {0x2736, 0x2736}, {0x2758, 0x275A},
			{0x27D5, 0x27D7}, {0x27DC, 0x27DC}, {0x27E6, 0x27EB}, {0x27F5, 0x27F6}, {0x2987, 0x2988}, {0x29A3, 0x29A3},
			{0x29B8, 0x29B8}, {0x2A00, 0x2A00}, {0x2A05, 0x2A06}, {0x2C60, 0x2C7F}, {0x2DE0, 0x2E5D}, {0xA640, 0xA69F},
			{0xA700, 0xA7CA}, {0xA7D0, 0xA7D1}, {0xA7D3, 0xA7D3}, {0xA7D5, 0xA7D9}, {0xA7F2, 0xA7FF}, {0xA92E, 0xA92E},
			{0xAB30, 0xAB6B}, {0xFE00, 0xFE00}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0xFF5B, 0xFF5B}, {0xFF5D, 0xFF5D},
			{0xFFFC, 0xFFFD}, {0x10780, 0x10785}, {0x10787, 0x107B0}, {0x107B2, 0x107BA}, {0x1DF00, 0x1DF1E}, {0x1F67C, 0x1F67F},
		};
		// clang-format on
		static_assert(IsAscending(NOTO_MONO_RANGES));

		static constexpr auto RANGES_LUT = LookupTable<LUTSize(NOTO_MONO_RANGES)>(NOTO_MONO_RANGES);
		static constexpr uint32_t MAXIUM_VALUE = (std::end(NOTO_MONO_RANGES) - 1)->second;

		if (cp > MAXIUM_VALUE)
			return font.cjk;
		if (TestBit(cp, RANGES_LUT.data()))
			return font.mono;
		return font.cjk;
	}
	uint8_t EncodeUTF16(uint32_t cp, wchar_t out[2])
	{
		if (cp <= 0xD7FFu || (cp >= 0xE000u && cp <= 0xFFFFu))
		{
			out[0] = static_cast<wchar_t>(cp);
			return 1;
		}

		if (cp >= 0x10000u && cp <= 0x10FFFFu)
		{
			const uint32_t v = cp - 0x10000u;
			out[0] = static_cast<wchar_t>(0xD800u + (v >> 10));
			out[1] = static_cast<wchar_t>(0xDC00u + (v & 0x3FFu));
			return 2;
		}

		out[0] = static_cast<wchar_t>(UNI_REPLACEMENT);
		return 1;
	}
	void DrawCellTextRun(uint32_t cp, RGBA foreground, int x, int y, bool bold, uint32_t font, uint32_t& currFont, RGBA& currColor)
	{
		if (!(foreground == currColor))
		{
			CallVFunc<void>(
				VFUNC_DRAW_SET_TEXT_COLOR, VGUISurface, (int)foreground.r, (int)foreground.g, (int)foreground.b, (int)foreground.a);
			currColor = foreground;
		}

		if (font != currFont)
		{
			CallVFunc<void>(VFUNC_DRAW_SET_TEXT_FONT, VGUISurface, font);
			currFont = font;
		}

		wchar_t units[2];
		const uint8_t count = EncodeUTF16(cp, units);
		CallVFunc<void>(VFUNC_DRAW_SET_TEXT_POS, VGUISurface, x, y);
		for (uint8_t i = 0; i < count; ++i)
			CallVFunc<void>(VFUNC_DRAW_UNICODE_CHAR, VGUISurface, (int)units[i], 0);

		if (!bold)
			return;

		static constexpr std::pair<int, int> BOLD_OFFSETS[] = {{1, 0}, {0, 1}, {1, 1}};
		for (const auto [dx, dy] : BOLD_OFFSETS)
		{
			CallVFunc<void>(VFUNC_DRAW_SET_TEXT_POS, VGUISurface, x + dx, y + dy);
			for (uint8_t i = 0; i < count; ++i)
				CallVFunc<void>(VFUNC_DRAW_UNICODE_CHAR, VGUISurface, (int)units[i], 0);
		}
	}

	bool IsSpace(uint32_t cp)
	{
		// clang-format off
		return cp == 0x0020u					// space
			|| cp == 0x00A0u					// no-break space
			|| cp == 0x1680u					// ogham space mark
			|| (cp >= 0x2000u && cp <= 0x200Au)	// en quad..hair space
			|| cp == 0x202Fu					// narrow no-break space
			|| cp == 0x205Fu					// medium mathematical space
			|| cp == 0x3000u;					// ideographic space
		// clang-format on
	}
	void DrawCellText(const TUIInstance& instance, FontMetrics font, int left, int top)
	{
		for (int row = 0; row < instance.rows; ++row)
		{
			if (!instance.rowsWithGlyphs.test(row))
				continue;

			uint32_t currFont = 0;
			RGBA currColor {};
			const size_t line = instance.cols * row;
			const int y = top + row * font.height;
			for (int col = 0; col < instance.cols; ++col)
			{
				const Cell& cell = instance.cells[line + col];
				if (cell.pos == 0 || (cell.pos == 1 && IsSpace(cell.glyph[0])))
					continue;

				const int x = left + col * font.width;
				const uint32_t drawFont = cell.mono ? font.mono : font.cjk;
				const uint8_t count = cell.pos;
				for (uint8_t i = 0; i < count; ++i)
				{
					// only bold the main glyph, for performance... probably
					DrawCellTextRun(cell.glyph[i], cell.foreground, x, y, (i == 0 ? cell.bold : false), drawFont, currFont, currColor);
				}
			}
		}
	}

	void DrawTUI(const TUIInstance& instance, int screenWidth, int screenHeight)
	{
		FontMetrics metrics = GetFontMetrics(instance.fontSize, screenHeight);
		if (!metrics.cjk)
			return;

		int panelWidth = instance.cols * metrics.width;
		int panelHeight = instance.rows * metrics.height;
		static constexpr float EPSILON = 1e-2f;
		int left = static_cast<int>(instance.x * screenWidth - panelWidth * 0.5f + EPSILON);
		int top = static_cast<int>(instance.y * screenHeight - panelHeight * 0.5f + EPSILON);
		left = std::clamp(left, 0, std::max(0, screenWidth - panelWidth));
		top = std::clamp(top, 0, std::max(0, screenHeight - panelHeight));

		DrawBackgrounds(instance, metrics, left, top);
		DrawCellText(instance, metrics, left, top);
	}

	void RenderClientTUIs()
	{
		if (!(VGUISurface = *PVGUISurface))
			return;

		std::vector<const TUIInstance*> sortedInstances;
		for (const auto& instance : TUIInstances)
			if (instance)
				sortedInstances.push_back(&*instance);

		static const auto compare = [](const TUIInstance* left, const TUIInstance* right) -> bool
		{
			if (left->priority != right->priority)
				return left->priority < right->priority;
			return left->index < right->index;
		};
		std::sort(sortedInstances.begin(), sortedInstances.end(), compare);

		for (const TUIInstance* instance : sortedInstances)
			DrawTUI(*instance, ScreenWidth(), ScreenHeight());
	}

	void __fastcall h_CHudMessage__Paint(void* self)
	{
		CHudMessage__Paint(self);
		RenderClientTUIs();
	}

	bool __fastcall h_CHudMessage__ShouldDraw(void* self)
	{
		bool hasTUI = std::ranges::any_of(TUIInstances, [](const auto& instance) { return instance.has_value(); });
		return CHudMessage__ShouldDraw(self) || hasTUI;
	}

	uint64_t __fastcall h_CClientState__SetSignonState(void* self, unsigned int state, int serverCount, void* connectInfo)
	{
		if (state <= 2 || state == 9)
			ClearClientTUIs();
		return CClientState__SetSignonState(self, state, serverCount, connectInfo);
	}

	//// Unicode ////

	// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
	// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
	namespace utf8_dfa
	{
		constexpr uint32_t ACCEPT = 0;
		constexpr uint32_t REJECT = 1;

		// clang-format off
		const uint8_t utf8d[] = {
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
			1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
			7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
			8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
			0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
			0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
			0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
			1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
			1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
			1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
			1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
		};
		// clang-format on

		uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte)
		{
			uint32_t type = utf8d[byte];

			if (*state != ACCEPT)
				*codep = (byte & 0x3fu) | (*codep << 6);
			else
				*codep = (0xff >> type) & byte;

			*state = utf8d[256 + *state * 16 + type];
			return *state;
		}
	} // namespace utf8_dfa
	uint32_t UTF8Decode(std::string_view text, size_t& offset)
	{
		const size_t i = offset;
		const uint8_t* p = (const uint8_t*)text.data();

		// ascii
		if (p[i] < 0x80u)
		{
			++offset;
			return p[i];
		}

		const size_t end = std::min(i + 4, text.size());
		uint32_t state = utf8_dfa::ACCEPT;
		uint32_t codep = 0;
		for (size_t k = i; k < end; ++k)
		{
			utf8_dfa::decode(&state, &codep, p[k]);
			if (state == utf8_dfa::ACCEPT)
			{
				offset = k + 1;
				return codep;
			}
			if (state == utf8_dfa::REJECT)
				break;
		}

		// failed
		offset = i + 1;
		return UNI_REPLACEMENT;
	}

	bool SupportMonoFont(uint32_t cp)
	{
		// clang-format off
		static constexpr std::pair<uint32_t, uint32_t> NOTO_MONO_RANGES[] = {
			{0x0020, 0x007E}, {0x00A0, 0x00AC}, {0x00AE, 0x0377}, {0x037A, 0x037F}, {0x0384, 0x038A}, {0x038C, 0x038C},
			{0x038E, 0x03A1}, {0x03A3, 0x03E1}, {0x03F0, 0x052F}, {0x10FB, 0x10FB}, {0x1AB0, 0x1AC0}, {0x1AC5, 0x1AC5},
			{0x1AC7, 0x1ACE}, {0x1C80, 0x1C88}, {0x1D00, 0x1DF9}, {0x1DFB, 0x1F15}, {0x1F18, 0x1F1D}, {0x1F20, 0x1F45},
			{0x1F48, 0x1F4D}, {0x1F50, 0x1F57}, {0x1F59, 0x1F59}, {0x1F5B, 0x1F5B}, {0x1F5D, 0x1F5D}, {0x1F5F, 0x1F7D},
			{0x1F80, 0x1FB4}, {0x1FB6, 0x1FC4}, {0x1FC6, 0x1FD3}, {0x1FD6, 0x1FDB}, {0x1FDD, 0x1FEF}, {0x1FF2, 0x1FF4},
			{0x1FF6, 0x1FFE}, {0x2000, 0x2064}, {0x2066, 0x2071}, {0x2074, 0x208E}, {0x2090, 0x209C}, {0x20A0, 0x20C0},
			{0x20F0, 0x20F0}, {0x2100, 0x215F}, {0x2183, 0x2184}, {0x2189, 0x2189}, {0x2190, 0x2195}, {0x219C, 0x219E},
			{0x21A0, 0x21A0}, {0x21A2, 0x21A4}, {0x21A6, 0x21A6}, {0x21D0, 0x21D4}, {0x21DA, 0x21DB}, {0x21E6, 0x21E6},
			{0x21E8, 0x21E8}, {0x2200, 0x220E}, {0x2210, 0x2210}, {0x2212, 0x2212}, {0x2218, 0x221A}, {0x221E, 0x221E},
			{0x2220, 0x2220}, {0x2223, 0x2223}, {0x2227, 0x222A}, {0x2234, 0x2238}, {0x223C, 0x223D}, {0x2241, 0x2241},
			{0x2243, 0x2243}, {0x2245, 0x2245}, {0x2247, 0x224C}, {0x2254, 0x2255}, {0x2257, 0x2257}, {0x225F, 0x2262},
			{0x2264, 0x2265}, {0x226C, 0x226C}, {0x226E, 0x2275}, {0x227A, 0x227B}, {0x2282, 0x2289}, {0x228E, 0x228E},
			{0x2291, 0x229C}, {0x22A2, 0x22A5}, {0x22B4, 0x22B5}, {0x22B8, 0x22B8}, {0x22C2, 0x22C4}, {0x22C6, 0x22C6},
			{0x22C8, 0x22CA}, {0x22CD, 0x22CE}, {0x22D0, 0x22D1}, {0x22E2, 0x22E3}, {0x2308, 0x230B}, {0x2310, 0x2310},
			{0x2319, 0x2319}, {0x2320, 0x2321}, {0x2336, 0x237A}, {0x2395, 0x2395}, {0x239B, 0x23AE}, {0x23B0, 0x23BD},
			{0x23DC, 0x23E1}, {0x2474, 0x2475}, {0x2500, 0x25FF}, {0x266D, 0x266F}, {0x2736, 0x2736}, {0x2758, 0x275A},
			{0x27D5, 0x27D7}, {0x27DC, 0x27DC}, {0x27E6, 0x27EB}, {0x27F5, 0x27F6}, {0x2987, 0x2988}, {0x29A3, 0x29A3},
			{0x29B8, 0x29B8}, {0x2A00, 0x2A00}, {0x2A05, 0x2A06}, {0x2C60, 0x2C7F}, {0x2DE0, 0x2E5D}, {0xA640, 0xA69F},
			{0xA700, 0xA7CA}, {0xA7D0, 0xA7D1}, {0xA7D3, 0xA7D3}, {0xA7D5, 0xA7D9}, {0xA7F2, 0xA7FF}, {0xA92E, 0xA92E},
			{0xAB30, 0xAB6B}, {0xFE00, 0xFE00}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0xFF5B, 0xFF5B}, {0xFF5D, 0xFF5D},
			{0xFFFC, 0xFFFD}, {0x10780, 0x10785}, {0x10787, 0x107B0}, {0x107B2, 0x107BA}, {0x1DF00, 0x1DF1E}, {0x1F67C, 0x1F67F},
		};
		// clang-format on
		static_assert(IsAscending(NOTO_MONO_RANGES));

		static constexpr uint32_t MIN_VALUE = std::begin(NOTO_MONO_RANGES)->first;
		static constexpr uint32_t MAX_VALUE = (std::end(NOTO_MONO_RANGES) - 1)->second;
		static constexpr auto RANGES_LUT = LookupTable<LUTSize(NOTO_MONO_RANGES)>(NOTO_MONO_RANGES);

		if (cp > MAX_VALUE || cp < MIN_VALUE)
			return false;
		return TestBit(cp, RANGES_LUT.data());
	}
	bool IsCombining(uint32_t cp)
	{
		// clang-format off
		return (cp >= 0x0300  && cp <= 0x036F)		// combining diacritical marks
			|| (cp >= 0x0483  && cp <= 0x0489)		// cyrillic combining marks
			|| (cp >= 0x302A  && cp <= 0x302D)		// ideographic tone marks (302E..302F are Mc)
			|| (cp >= 0x3099  && cp <= 0x309A)		// kana voicing marks
			|| (cp >= 0xFE00  && cp <= 0xFE0F)		// variation selectors VS1..VS16
			|| (cp >= 0xE0100 && cp <= 0xE01EF);	// variation selectors VS17..VS256
		// clang-format on
	}
	bool IsWide(uint32_t cp)
	{
		// clang-format off
		static constexpr std::pair<uint32_t, uint32_t> WIDE_RANGES[] = {
			{0x1100, 0x115F},	// hangul jamo (leading consonants)
			{0x2E80, 0x2E99},	// CJK radicals
			{0x2E9B, 0x2EF3},	// CJK radicals (cont.)
			{0x2F00, 0x2FD5},	// kangxi radicals
			{0x2FF0, 0x2FFF},	// ideographic description characters
			{0x3000, 0x303E},	// CJK punctuation and symbols
			{0x3041, 0x3096},	// hiragana
			{0x3099, 0x30FF},	// kana voicing marks, katakana
			{0x3105, 0x312F},	// bopomofo
			{0x3131, 0x318E},	// hangul compatibility letters
			{0x3190, 0x319F},	// kanbun annotation marks
			{0x31A0, 0x31BF},	// bopomofo extended
			{0x31C0, 0x31E5},	// CJK strokes
			{0x31EF, 0x31EF},	// ideographic description character (subtraction)
			{0x31F0, 0x31FF},	// katakana phonetic extensions
			{0x3200, 0x321E},	// parenthesized hangul
			{0x3220, 0x3247},	// parenthesized/circled ideographs
			{0x3250, 0x325F},	// circled numbers
			{0x3260, 0x327F},	// circled hangul
			{0x3280, 0x33FF},	// circled ideographs, squared/telegraph/era symbols
			{0x3400, 0x4DBF},	// CJK unified ideographs extension A
			{0x4E00, 0x9FFF},	// CJK unified ideographs
			{0xAC00, 0xD7A3},	// hangul syllables
			{0xF900, 0xFAFF},	// CJK compatibility ideographs
			{0xFE10, 0xFE19},	// vertical presentation forms (punctuation)
			{0xFE30, 0xFE52},	// vertical forms, small forms
			{0xFE54, 0xFE66},	// small forms
			{0xFE68, 0xFE6B},	// small forms
			{0xFF01, 0xFF60},	// fullwidth ascii forms (F)
			{0xFFE0, 0xFFE6},	// fullwidth symbols (F)
		};
		// clang-format on
		static_assert(IsAscending(WIDE_RANGES));

		static constexpr uint32_t MIN_VALUE = std::begin(WIDE_RANGES)->first;
		static constexpr uint32_t MAX_VALUE = (std::end(WIDE_RANGES) - 1)->second;
		static constexpr int32_t OFFSET = static_cast<int32_t>(MIN_VALUE) * -1;
		static constexpr auto RANGES_LUT = LookupTable<LUTSize(WIDE_RANGES, OFFSET)>(WIDE_RANGES, OFFSET);

		if (cp > MAX_VALUE || cp < MIN_VALUE)
			return false;
		return TestBit(cp, RANGES_LUT.data(), OFFSET);
	}

	//// Text parsing ////

	void ApplyColor(bool foreground, RGBA color, TextStyle& style)
	{
		if (foreground)
		{
			style.foreground = color;
			return;
		}
		style.background = color;
		style.hasBackground = true;
	}

	void ApplyBasicSGR(uint32_t v, TextStyle& style)
	{
		if (v == 0)
		{
			style = {};
			return;
		}
		if (v == 1)
		{
			style.bold = true;
			return;
		}
		if (v == 22)
		{
			style.bold = false;
			return;
		}
		if (v == 39)
		{
			style.foreground = DEFAULT_FOREGROUND;
			return;
		}
		if (v == 49)
		{
			style.background = {};
			style.hasBackground = false;
			return;
		}
		if (v >= 30 && v <= 37)
		{
			ApplyColor(true, PALETTE16_DARK[v - 30], style);
			return;
		}
		if (v >= 40 && v <= 47)
		{
			ApplyColor(false, PALETTE16_DARK[v - 40], style);
			return;
		}
		if (v >= 90 && v <= 97)
		{
			ApplyColor(true, PALETTE16_LIGHT[v - 90], style);
			return;
		}
		if (v >= 100 && v <= 107)
		{
			ApplyColor(false, PALETTE16_LIGHT[v - 100], style);
			return;
		}
	}

	RGBA Palette256(uint32_t index)
	{
		if (index < 8)
			return PALETTE16_DARK[index];
		if (index < 16)
			return PALETTE16_LIGHT[index - 8];

		if (index < 232)
		{
			index -= 16;
			static constexpr uint8_t LEVELS[6] = {0, 95, 135, 175, 215, 255};
			return {LEVELS[index / 36], LEVELS[(index / 6) % 6], LEVELS[index % 6], 255};
		}
		const uint8_t gray = static_cast<uint8_t>(8 + 10 * (index - 232));
		return {gray, gray, gray, 255};
	}
	void ApplyColonColor(const uint32_t* sub, size_t count, TextStyle& style)
	{
		if (count < 3)
			return;

		const bool foreground = sub[0] == 38;
		const uint32_t mode = sub[1];
		if (mode == 5)
		{
			if (sub[2] < 256)
				return ApplyColor(foreground, Palette256(sub[2]), style);
		}
		if (mode == 2)
		{
			if (count >= 6)
			{
				return ApplyColor(
					foreground, {static_cast<uint8_t>(sub[3]), static_cast<uint8_t>(sub[4]), static_cast<uint8_t>(sub[5]), 255}, style);
			}
			if (count == 5)
			{
				return ApplyColor(
					foreground, {static_cast<uint8_t>(sub[2]), static_cast<uint8_t>(sub[3]), static_cast<uint8_t>(sub[4]), 255}, style);
			}
		}
		if (mode == 6 && !foreground)
		{
			if (count >= 7)
			{
				return ApplyColor(
					false,
					{static_cast<uint8_t>(sub[3]),
					 static_cast<uint8_t>(sub[4]),
					 static_cast<uint8_t>(sub[5]),
					 static_cast<uint8_t>(sub[6])},
					style);
			}
			if (count == 6)
			{
				return ApplyColor(
					false,
					{static_cast<uint8_t>(sub[2]),
					 static_cast<uint8_t>(sub[3]),
					 static_cast<uint8_t>(sub[4]),
					 static_cast<uint8_t>(sub[5])},
					style);
			}
		}
	}

	template <size_t N> size_t ParseSGRParams(std::string_view text, std::array<uint32_t, N>& params, std::array<bool, N>& isSubParam)
	{
		size_t count = 0;
		uint32_t value = 0;
		bool inSubGroup = false;

		const auto pushParams = [&]() -> void
		{
			if (count >= N)
				return;
			params[count] = value;
			isSubParam[count] = inSubGroup;
			++count;
			value = 0;
		};

		for (char c : text)
		{
			if (c >= '0' && c <= '9')
			{
				value = value * 10u + (c - '0');
				continue;
			}
			if (c == ':')
			{
				pushParams();
				inSubGroup = true;
				continue;
			}
			if (c == ';')
			{
				pushParams();
				inSubGroup = false;
				continue;
			}
		}
		pushParams();
		return count;
	}
	void DecodeSGR(std::string_view text, TextStyle& style)
	{
		static constexpr size_t MAX_SGR_PARAMETERS = 16;
		std::array<uint32_t, MAX_SGR_PARAMETERS> params;
		std::array<bool, params.size()> isSubParam;
		const size_t count = ParseSGRParams(text, params, isSubParam);

		size_t i = 0;
		while (i < count)
		{
			const uint32_t v = params[i];
			if (v != 38 && v != 48)
			{
				ApplyBasicSGR(v, style);
				++i;
				continue;
			}

			if (i + 1 < count && isSubParam[i + 1])
			{
				size_t start = i;
				size_t end = start + 1;
				while (end < count && isSubParam[end])
					++end;
				// ESC[xx;xx:xx:xx;xxmtext
				//        ^~~~~~~~
				ApplyColonColor(params.data() + start, end - start, style);
				i = end;
				continue;
			}

			if (i + 1 >= count)
				break;

			const bool foreground = v == 38;
			const uint32_t mode = params[i + 1];
			if (mode == 5)
			{
				if (i + 2 < count)
					ApplyColor(foreground, Palette256(params[i + 2]), style);
				i += 3;
				continue;
			}
			if (mode == 2)
			{
				if (i + 4 < count)
				{
					ApplyColor(
						foreground,
						{static_cast<uint8_t>(params[i + 2]),
						 static_cast<uint8_t>(params[i + 3]),
						 static_cast<uint8_t>(params[i + 4]),
						 255},
						style);
				}
				i += 5;
				continue;
			}

			i += 2;
		}
	}

	bool TryParseSGR(std::string_view text, size_t& offset, TextStyle& style)
	{
		static constexpr char ASCII_ESCAPE = 0x1B;
		if (text[offset] != ASCII_ESCAPE)
			return false;
		if (offset + 1 >= text.size() || text[offset + 1] != '[')
			return false;

		size_t start = offset + 2;
		size_t end = start;
		static constexpr size_t MAX_SGR_SEQUENCE_LENGTH = 64;
		while (end < text.size() && end - offset <= MAX_SGR_SEQUENCE_LENGTH && text[end] != 'm')
			++end;
		if (end >= text.size() || text[end] != 'm')
			return false;

		// ESC[xx;xx;xx;xx;xxmtext
		//     ^~~~~~~~~~~~~~
		DecodeSGR(text.substr(start, end - start), style);
		offset = end + 1;
		return true;
	}

	void ParseText(TUIInstance& instance, std::string_view text)
	{
		const size_t totalSize = instance.cols * instance.rows;
		instance.cells.assign(totalSize, {});
		instance.rowsWithGlyphs.reset();
		instance.rowsWithBackground.reset();

		int row = 0;
		int col = 0;
		size_t anchor = SIZE_MAX;
		TextStyle style;
		for (size_t offset = 0; offset < text.size();)
		{
			if (TryParseSGR(text, offset, style))
				continue;

			uint32_t codePoint = UTF8Decode(text, offset);
			if (codePoint == '\n')
			{
				if (++row >= instance.rows)
					break;
				col = 0;
				anchor = SIZE_MAX;
				continue;
			}
			if (codePoint == '\r')
			{
				col = 0;
				anchor = SIZE_MAX;
				continue;
			}
			if (codePoint == '\t')
			{
				col = (col + 4) & ~3;
				continue;
			}
			// non-printable characters
			static constexpr char ASCII_DEL = 0x7F;
			if (codePoint < ' ' || codePoint == ASCII_DEL)
				continue;

			if (IsCombining(codePoint))
			{
				if (anchor >= totalSize)
					continue;

				Cell& cell = instance.cells[anchor];
				if (cell.pos < cell.glyph.size())
					cell.glyph[cell.pos++] = codePoint;
				continue;
			}

			uint8_t width = IsWide(codePoint) ? 2 : 1;
			if (col + width > instance.cols)
			{
				col = instance.cols;
				continue;
			}

			size_t lineIndex = row * instance.cols;
			size_t cellIndex = lineIndex + col;
			Cell& cell = instance.cells[cellIndex];
			static_cast<TextStyle&>(cell) = style;

			if (cell.span > 1)
			{
				for (int i = 1; i < cell.span; i++)
					instance.cells[cellIndex + i] = {};
			}
			if (cell.span < 1 && cellIndex > lineIndex)
			{
				Cell& prev = instance.cells[cellIndex - 1];
				if (prev.span == 2)
					prev = {};
			}
			cell.span = width;
			cell.mono = SupportMonoFont(codePoint);
			cell.glyph[0] = codePoint;
			cell.pos = 1;

			for (int i = 1; i < width; i++)
			{
				Cell& continuation = instance.cells[cellIndex + i];
				continuation.background = style.background;
				continuation.hasBackground = style.hasBackground;
				continuation.span = 0;
				continuation.mono = cell.mono;
				continuation.pos = 0;
			}
			anchor = cellIndex;

			instance.rowsWithGlyphs.set(row);
			if (style.hasBackground)
				instance.rowsWithBackground.set(row);

			col += width;
		}
	}

	//// Networking ////

	bool ValidIndexRange(int index)
	{
		return index >= 0 && index < NSTUI::MAX_TUI_INSTANCES;
	}

	bool ValidTUIConfig(const NSTUI::Config& config)
	{
		if (config.cols < 1 || config.cols > NSTUI::MAX_COLS)
			return false;
		if (config.rows < 1 || config.rows > NSTUI::MAX_ROWS)
			return false;
		if (!std::isfinite(config.x) || !std::isfinite(config.y) || config.x < 0.0f || config.x > 1.0f || config.y < 0.0f ||
			config.y > 1.0f)
			return false;
		if (config.priority < NSTUI::MIN_PRIORITY || config.priority > NSTUI::MAX_PRIORITY)
			return false;
		if (config.fontSize < NSTUI::MIN_FONT_SIZE || config.fontSize > NSTUI::MAX_FONT_SIZE)
			return false;
		return true;
	}
	int TotalUsedCells()
	{
		size_t cells = 0;
		for (std::optional<TUIInstance>& instance : TUIInstances)
		{
			if (!instance)
				continue;

			cells += instance->cols * instance->rows;
		}
		return cells;
	}
	void HandleCreate(BFRead* message)
	{
		uint8_t index = (uint8_t)message->ReadByte();
		NSTUI::Config config;
		config.cols = (uint8_t)message->ReadByte();
		config.rows = (uint8_t)message->ReadByte();
		config.fontSize = (uint8_t)message->ReadByte();
		config.priority = (uint8_t)message->ReadByte();
		config.x = message->ReadFloat();
		config.y = message->ReadFloat();

		if (message->IsOverflowed() || !ValidIndexRange(index) || !ValidTUIConfig(config))
		{
			LOG_VERBOSE_WARN("Ignored invalid NSTUI create message");
			return;
		}

		std::optional<TUIInstance>& instance = TUIInstances[index];
		if (instance)
		{
			TUIInstance& existing = *instance;
			if (static_cast<const NSTUI::Config&>(existing) == config)
				return;

			instance.reset();
		}

		size_t cells = config.cols * config.rows;
		if (TotalUsedCells() + cells > NSTUI::MAX_TEXT_CELLS)
		{
			LOG_VERBOSE_WARN("Ignored NSTUI create message exceeding the total cell limit");
			return;
		}

		TUIInstance newInstance(config);
		newInstance.index = index;
		instance = std::move(newInstance);
		LOG_VERBOSE_INFO(
			"Received NSTUI create: index {}, {}x{}, pos {}-{}, priority {}, font size {}",
			index,
			config.cols,
			config.rows,
			config.x,
			config.y,
			config.priority,
			config.fontSize);
	}

	void HandleUpdateBegin(BFRead* message)
	{
		uint8_t index = (uint8_t)message->ReadByte();
		uint8_t compressed = (uint8_t)message->ReadByte();
		uint32_t textSize = (uint32_t)message->ReadLong();
		uint32_t payloadSize = (uint32_t)message->ReadLong();
		if (message->IsOverflowed() || !ValidIndexRange(index))
		{
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk begin");
			return;
		}

		PendingUpdates[index].reset();

		if ((compressed & ~1) != 0 || textSize <= 0 || textSize > NSTUI::MAX_TEXT_BYTES || payloadSize <= 0 ||
			payloadSize > NSTUI::MAX_TEXT_BYTES)
		{
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk begin");
			return;
		}
		if (!compressed && textSize != payloadSize)
		{
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk begin");
			return;
		}

		PendingUpdate update {};
		update.received = 0;
		update.payloadSize = payloadSize;
		update.textSize = textSize;
		update.compressed = (bool)compressed;
		PendingUpdates[index] = std::move(update);
		LOG_VERBOSE_INFO("Received NSTUI chunk begin: index {}, {} bytes ({} byte payload)", index, PendingUpdates[index]->textSize, PendingUpdates[index]->payloadSize);
	}

	void HandleUpdateChunk(BFRead* message)
	{
		uint8_t index = (uint8_t)message->ReadByte();
		uint8_t chunkSize = (uint8_t)message->ReadByte();
		if (message->IsOverflowed() || !ValidIndexRange(index))
		{
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk");
			return;
		}

		std::optional<PendingUpdate>& update = PendingUpdates[index];
		if (!update || chunkSize <= 0 || chunkSize > NSTUI::TEXT_CHUNK_BYTES)
		{
			update.reset();
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk");
			return;
		}

		if (update->received + chunkSize > update->payloadSize)
		{
			update.reset();
			LOG_VERBOSE_WARN("Ignored overflowed NSTUI chunk");
			return;
		}

		uint8_t chunk[NSTUI::TEXT_CHUNK_BYTES];
		message->ReadBytes((uintptr_t)chunk, (uint32_t)chunkSize);
		if (message->IsOverflowed())
		{
			update.reset();
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk");
			return;
		}
		update->payload.reserve(update->payloadSize);
		update->payload.insert(update->payload.end(), chunk, chunk + chunkSize);
		update->received += chunkSize;
	}

	void* DecompressData(const void* src, size_t len, size_t* out)
	{
		static z_stream strm {};
		static bool strmReady = false;
		static uint8_t buffer[NSTUI::MAX_TEXT_BYTES];
		if (!strmReady)
		{
			if (inflateInit(&strm) != Z_OK)
				return nullptr;
			strmReady = true;
		}

		if (inflateReset(&strm) != Z_OK)
			return nullptr;

		strm.next_in = (Bytef*)src;
		strm.avail_in = len;
		strm.next_out = buffer;
		strm.avail_out = sizeof(buffer);

		if (inflate(&strm, Z_FINISH) != Z_STREAM_END)
			return nullptr;

		*out = strm.total_out;
		return buffer;
	}

	void HandleUpdateEnd(BFRead* message)
	{
		int index = message->ReadByte();
		if (message->IsOverflowed() || !ValidIndexRange(index))
		{
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk end");
			return;
		}
		std::optional<PendingUpdate>& update = PendingUpdates[index];
		if (!update || update->received != update->payloadSize)
		{
			update.reset();
			LOG_VERBOSE_WARN("Ignored invalid NSTUI chunk end");
			return;
		}

		std::string_view text((const char*)update->payload.data(), update->payload.size());
		if (update->compressed)
		{
			size_t outSize;
			void* out = DecompressData(update->payload.data(), update->payload.size(), &outSize);
			if (!out || outSize != update->textSize)
			{
				update.reset();
				LOG_VERBOSE_WARN("Ignored bad NSTUI chunk after decompressed");
				return;
			}
			text = std::string_view((const char*)out, outSize);
		}

		if (!TUIInstances[index])
		{
			update.reset();
			LOG_VERBOSE_WARN("Ignored intact NSTUI chunk end: server removed TUI instances mid-transfer");
			return;
		}
		ParseText(*TUIInstances[index], text);
		LOG_VERBOSE_INFO(
			"Received complete NSTUI update: index {}, {} bytes ({} byte payload)", index, update->textSize, update->payloadSize);
	}

	void HandleDestroy(BFRead* message)
	{
		int index = message->ReadByte();
		if (message->IsOverflowed() || !ValidIndexRange(index))
		{
			LOG_VERBOSE_WARN("Ignored invalid NSTUI destroy message");
			return;
		}

		TUIInstances[index].reset();
		LOG_VERBOSE_INFO("Received NSTUI destroy: index {}", index);
	}

	void __fastcall HandleTUIMessage(BFRead* message)
	{
		NSTUI::Operation operation = static_cast<NSTUI::Operation>(message->ReadByte());
		switch (operation)
		{
		case NSTUI::Operation::Create:
			HandleCreate(message);
			break;
		case NSTUI::Operation::UpdateBegin:
			HandleUpdateBegin(message);
			break;
		case NSTUI::Operation::UpdateChunk:
			HandleUpdateChunk(message);
			break;
		case NSTUI::Operation::UpdateEnd:
			HandleUpdateEnd(message);
			break;
		case NSTUI::Operation::Destroy:
			HandleDestroy(message);
			break;
		default:
			LOG_VERBOSE_WARN("Ignored unknown NSTUI operation {}", static_cast<int>(operation));
			break;
		}
	}
} // namespace

void ClearClientTUIs()
{
	TUIInstances = {};
	PendingUpdates = {};
}

ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ClientTUI, ClientSquirrel, (CModule module))
{
	CVar_ns_tui_verbose = new ConVar("ns_tui_verbose", "0", FCVAR_CLIENTDLL, "Toggles verbose TUI logging");

	CUserMessages__RegisterFn RegisterUserMessage = module.Offset(0x342890).RCast<CUserMessages__RegisterFn>();
	HookMessageFn HookMessage = module.Offset(0x3415F0).RCast<HookMessageFn>();

	void* userMessages = *module.Offset(0xB28E98).RCast<void**>();
	RegisterUserMessage(userMessages, NSTUI::MESSAGE_NAME, -1);
	HookMessage(userMessages, NSTUI::MESSAGE_NAME, HandleTUIMessage);

	ScreenHeight = module.Offset(0x199C10).RCast<ScreenHeightFn>();
	ScreenWidth = module.Offset(0x199C30).RCast<ScreenWidthFn>();
	CHudMessage__Paint = module.Offset(0x266710).RCast<CHudMessage__PaintFn>();
	CHudMessage__ShouldDraw = module.Offset(0x266FA0).RCast<CHudMessage__ShouldDrawFn>();
	PVGUISurface = module.Offset(0x2E44020).RCast<void**>();

	HookAttach((void**)&CHudMessage__Paint, (void*)h_CHudMessage__Paint);
	HookAttach((void**)&CHudMessage__ShouldDraw, (void*)h_CHudMessage__ShouldDraw);
}

ON_DLL_LOAD_CLIENT("engine.dll", ClientTUILifecycle, (CModule module))
{
	CClientState__SetSignonState = module.Offset(0x91D20).RCast<CClientState__SetSignonStateFn>();
	HookAttach((void**)&CClientState__SetSignonState, (void*)h_CClientState__SetSignonState);
}

#undef LOG_VERBOSE_INFO
