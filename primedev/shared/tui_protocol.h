#pragma once

#include <cstddef>
#include <cstdint>

namespace NSTUI
{
	inline constexpr char MESSAGE_NAME[] = "NSTUI";

	inline constexpr size_t MAX_PLAYERS = 64;
	inline constexpr size_t MAX_TUI_INSTANCES = 0x1f;
	inline constexpr size_t MAX_TEXT_CELLS = 1 * 1024 * 1024;
	inline constexpr size_t MAX_TEXT_BYTES = 1 * 1024 * 1024;

	inline constexpr uint8_t MAX_COLS = 0xff;
	inline constexpr uint8_t MAX_ROWS = 0xff;
	inline constexpr uint8_t MIN_FONT_SIZE = 1;
	inline constexpr uint8_t MAX_FONT_SIZE = 0xff;
	inline constexpr uint8_t MIN_PRIORITY = 0;
	inline constexpr uint8_t MAX_PRIORITY = 255;

	inline constexpr uint8_t MAX_USER_MSG_DATA = 0xff;
	inline constexpr uint8_t TEXT_CHUNK_BYTES = MAX_USER_MSG_DATA - 3; // operation, index, chunk size
	inline constexpr int DEFAULT_COMPRESSION_LEVEL = 3;

	enum class Operation : uint8_t
	{
		Create,
		UpdateBegin,
		UpdateChunk,
		UpdateEnd,
		Destroy
	};

	struct Config
	{
		uint8_t cols {};
		uint8_t rows {};
		uint8_t fontSize {};
		uint8_t priority {};
		float x {};
		float y {};

		bool operator==(const Config&) const = default;
	};
} // namespace NSTUI
