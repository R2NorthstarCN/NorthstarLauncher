#include "server/tui.h"

#include "core/hooks.h"
#include "server/r2server.h"
#include "spdlog/spdlog.h"
#include "squirrel/squirrel.h"
#include "shared/tui_protocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iterator>
#include <numeric>
#include <string>

#include <zlib.h>

namespace
{
	class CRecipientFilter
	{
		uint8_t unknown[0x38];
	};
	static_assert(sizeof(CRecipientFilter) == 0x38);

	// clang-format off
	using CUserMessages__RegisterFn 					= void 	(__fastcall*)(void*, const char*, int);
	using CRecipientFilter__ConstructFn 				= void* (__fastcall*)(CRecipientFilter*);
	using CRecipientFilter__MakeReliableFn 				= void 	(__fastcall*)(CRecipientFilter*);
	using CRecipientFilter__AddRecipientFn 				= void 	(__fastcall*)(CRecipientFilter*, const CBasePlayer*);
	using CRecipientFilter__RemoveRecipientByIndexFn 	= void 	(__fastcall*)(CRecipientFilter*, int);
	using UserMessageBeginFn 							= void* (__fastcall*)(CRecipientFilter*, const char*, int);
	using MessageEndFn 									= void 	(__fastcall*)();
	using MessageWriteByteFn 							= void 	(__fastcall*)(int);
	using MessageWriteFloatFn 							= void 	(__fastcall*)(float);
	using MessageWriteLongFn 							= void 	(__fastcall*)(int);
	using SV_SendClientMessagesFn 						= void 	(__fastcall*)(void*, char);
	CUserMessages__RegisterFn 					CUserMessages__Register 					= nullptr;
	CRecipientFilter__ConstructFn 				CRecipientFilter__Construct 				= nullptr;
	CRecipientFilter__MakeReliableFn 			CRecipientFilter__MakeReliable 				= nullptr;
	CRecipientFilter__AddRecipientFn 			CRecipientFilter__AddRecipient 				= nullptr;
	CRecipientFilter__RemoveRecipientByIndexFn 	CRecipientFilter__RemoveRecipientByIndex 	= nullptr;
	UserMessageBeginFn 							UserMessageBegin 							= nullptr;
	MessageEndFn 								MessageEnd 									= nullptr;
	MessageWriteByteFn 							MessageWriteByte 							= nullptr;
	MessageWriteFloatFn 						MessageWriteFloat 							= nullptr;
	MessageWriteLongFn 							MessageWriteLong 							= nullptr;
	SV_SendClientMessagesFn 					SV_SendClientMessages 						= nullptr;
	// clang-format on

	struct PackInfo
	{
		uint32_t begin = 0;

		uint32_t textSize = 0;
		uint8_t tuiIndex = 0;
		bool compressed = false;
		bool aborted = false;
	};
	struct Payload
	{
		std::array<uint8_t, NSTUI::MAX_TEXT_BYTES> data {};
		std::array<PackInfo, NSTUI::MAX_TUI_INSTANCES> meta {};
		uint32_t read = 0;
		uint32_t end = 0;
		uint32_t metaEnd = 0;
	};

	struct TUIState
	{
		std::array<bool, NSTUI::MAX_TUI_INSTANCES> allocated {};
		std::array<uint32_t, NSTUI::MAX_TUI_INSTANCES> cells {};
		std::array<NSTUI::Config, NSTUI::MAX_TUI_INSTANCES> configs {};
	};

	std::array<TUIState, NSTUI::MAX_PLAYERS> PlayerTUIStates;
	std::array<Payload, NSTUI::MAX_PLAYERS> PendingSend;
	int CompressionLevel = NSTUI::DEFAULT_COMPRESSION_LEVEL;

	constexpr uint32_t USERMSG_RELIABLE_SLOT_BYTES = 0x8000;
	constexpr uint32_t DEFAULT_USERMSG_BUDGET = 0x4000;
	uint32_t UserMessageBudget = DEFAULT_USERMSG_BUDGET;

	//// Utility ////

	template <ScriptContext context> SQRESULT RaiseError(HSQUIRRELVM sqvm, const std::string& message)
	{
		g_pSquirrel<context>->raiseerror(sqvm, message.c_str());
		return SQRESULT_ERROR;
	}

	TUIState* GetPlayerTUIState(const CBasePlayer* player)
	{
		return &PlayerTUIStates[player->m_nPlayerIndex - 1];
	}

	bool AcquireBuffer(const CBasePlayer* player, Payload** buf)
	{
		*buf = PendingSend.data() + (player->m_nPlayerIndex - 1);
		return (*buf)->read == 0;
	}
	void ResetPendingSend(uint32_t index)
	{
		Payload& payload = PendingSend[index];
		payload.read = 0;
		payload.end = 0;
		payload.metaEnd = 0;
	}
	void ResetPendingSend(const CBasePlayer* player)
	{
		ResetPendingSend(player->m_nPlayerIndex - 1);
	}

	void StartUserMessage(const CBasePlayer* player)
	{
		static CRecipientFilter recipientFilter {};
		static uint32_t recipientFilterPlayerIndex = 0;
		static bool ready = false;
		if (!ready)
		{
			CRecipientFilter__Construct(&recipientFilter);
			CRecipientFilter__MakeReliable(&recipientFilter);
			ready = true;
		}

		uint32_t index = player->m_nPlayerIndex;
		if (recipientFilterPlayerIndex != index)
		{
			CRecipientFilter__RemoveRecipientByIndex(&recipientFilter, recipientFilterPlayerIndex);
			CRecipientFilter__AddRecipient(&recipientFilter, player);
			recipientFilterPlayerIndex = index;
		}

		UserMessageBegin(&recipientFilter, NSTUI::MESSAGE_NAME, 0);
	}

	//// Networking ////

	void SendCreate(const CBasePlayer* player, uint8_t tuiIndex, const NSTUI::Config& config)
	{
		StartUserMessage(player);
		{
			MessageWriteByte((int)NSTUI::Operation::Create);
			MessageWriteByte((int)tuiIndex);
			MessageWriteByte((int)config.cols);
			MessageWriteByte((int)config.rows);
			MessageWriteByte((int)config.fontSize);
			MessageWriteByte((int)config.priority);
			MessageWriteFloat(config.x);
			MessageWriteFloat(config.y);
		}
		MessageEnd();
	}

	bool CompressData(const void* src, uint32_t srcLen, void* dst, uint32_t* dstLen, int level)
	{
		static z_stream strm {};
		static int currLevel;
		static bool strmReady = false;
		if (!strmReady)
		{
			if (deflateInit(&strm, level) != Z_OK)
				return false;
			strmReady = true;
			currLevel = level;
		}
		if (level != currLevel)
		{
			if (deflateParams(&strm, level, Z_DEFAULT_STRATEGY) != Z_OK)
				return false;
			currLevel = level;
		}

		if (deflateReset(&strm) != Z_OK)
			return false;

		strm.next_in = (Bytef*)src;
		strm.avail_in = srcLen;
		strm.next_out = (Bytef*)dst;
		strm.avail_out = *dstLen;

		if (deflate(&strm, Z_FINISH) != Z_STREAM_END)
			return false;

		*dstLen = strm.total_out;
		return true;
	}
	void SendUpdate(const CBasePlayer* player, size_t tuiIndex, const char* text, uint32_t textSize)
	{
		Payload* buffer = nullptr;
		if (!AcquireBuffer(player, &buffer))
		{
			spdlog::warn(
				"Discarded unsent packets: previous payload still in transit ({}/{} bytes); "
				"reduce the text size, reduce the send frequency, or raise the compression level",
				buffer->read,
				buffer->end);
			ResetPendingSend(player);
		}

		if (buffer->metaEnd >= buffer->meta.size())
		{
			spdlog::warn(
				"Dropped incoming packet: not enough packet slot left in the payload buffer ({}/{} slots); "
				"sending multiple TUI updates to the same instance within same frame is pointless, or reduce the send frequency",
				buffer->metaEnd,
				buffer->meta.size());
			return;
		}

		uint32_t packIndex = buffer->metaEnd;
		PackInfo* meta = buffer->meta.data() + packIndex;
		uint32_t start = buffer->end;
		uint32_t remaining = buffer->data.size() - start;

		meta->begin = start;
		meta->textSize = textSize;
		meta->tuiIndex = tuiIndex;
		meta->compressed = false;
		meta->aborted = false;

		if (CompressionLevel > 0 && textSize >= 64)
		{
			uint32_t size = remaining;
			if (!CompressData((const void*)text, textSize, buffer->data.data() + start, &size, CompressionLevel))
			{
				spdlog::warn("Failed to compress TUI data, falling back to sending raw");
			}
			else if (size < textSize)
			{
				buffer->end += size;
				buffer->metaEnd++;
				meta->compressed = true;
				return;
			}
		}

		if (textSize > remaining)
		{
			spdlog::warn(
				"Dropped incoming packet: not enough space left in payload buffer ({}/{} bytes); "
				"reduce the text size, reduce the send frequency, or raise the compression level",
				buffer->end,
				buffer->data.size());
			return;
		}
		memcpy(buffer->data.data() + start, text, textSize);
		buffer->end += textSize;
		buffer->metaEnd++;
		return;
	}

	void SendDestroy(const CBasePlayer* player, uint8_t tuiIndex)
	{
		Payload* payload = nullptr;
		AcquireBuffer(player, &payload);
		for (uint32_t i = 0; i < payload->metaEnd; i++)
			if (payload->meta[i].tuiIndex == tuiIndex)
				payload->meta[i].aborted = true;

		StartUserMessage(player);
		{
			MessageWriteByte((int)NSTUI::Operation::Destroy);
			MessageWriteByte((int)tuiIndex);
		}
		MessageEnd();
	}

	void FlushPendingTUIPayloads()
	{
		for (uint32_t payloadIndex = 0; payloadIndex < PendingSend.size(); payloadIndex++)
		{
			Payload& payload = PendingSend[payloadIndex];
			if (payload.end == 0)
				continue;

			const CBasePlayer* player = UTIL_PlayerByIndex(payloadIndex + 1);
			if (!player)
			{
				ResetPendingSend(payloadIndex);
				continue;
			}

			uint32_t budget = UserMessageBudget;
			for (uint32_t pakIndex = 0; pakIndex < payload.metaEnd; pakIndex++)
			{
				if (budget == 0)
					goto nextPayload;

				const PackInfo& meta = payload.meta[pakIndex];
				if (meta.aborted)
					continue;

				uint32_t start = std::max(payload.read, meta.begin);
				uint32_t end = payload.end;
				if (pakIndex + 1 < payload.metaEnd)
					end = payload.meta[pakIndex + 1].begin;
				if (start >= end)
					continue;

				if (start == meta.begin)
				{
					StartUserMessage(player);
					{
						MessageWriteByte((int)NSTUI::Operation::UpdateBegin);
						MessageWriteByte((int)meta.tuiIndex);
						MessageWriteByte((int)meta.compressed);
						MessageWriteLong(meta.textSize);
						MessageWriteLong(end - meta.begin);
					}
					MessageEnd();
				}

				uint32_t pakSize = end - start;
				uint32_t sendSize = std::min(budget, pakSize);
				budget -= sendSize;
				for (uint32_t offset = 0; offset < sendSize; offset += NSTUI::TEXT_CHUNK_BYTES)
				{
					uint8_t chunkSize = std::min<uint32_t>(NSTUI::TEXT_CHUNK_BYTES, sendSize - offset);
					StartUserMessage(player);
					{
						MessageWriteByte((int)NSTUI::Operation::UpdateChunk);
						MessageWriteByte((int)meta.tuiIndex);
						MessageWriteByte((int)chunkSize);
						for (uint32_t i = 0; i < chunkSize; ++i)
							MessageWriteByte((int)*((uint8_t*)payload.data.data() + start + offset + i));
					}
					MessageEnd();
				}
				payload.read += sendSize;

				if (sendSize != pakSize)
					goto nextPayload;

				StartUserMessage(player);
				{
					MessageWriteByte((int)NSTUI::Operation::UpdateEnd);
					MessageWriteByte((int)meta.tuiIndex);
				}
				MessageEnd();
			}
			ResetPendingSend(player);

		nextPayload:;
		}
	}
	void __fastcall h_SV_SendClientMessages(void* server, char finalTick)
	{
		FlushPendingTUIPayloads();
		SV_SendClientMessages(server, finalTick);
	}
} // namespace

void ResetPlayerTUIState(uint32_t index)
{
	PlayerTUIStates[index] = {};
	ResetPendingSend(index);
}

void ResetServerTUIStates()
{
	PlayerTUIStates = {};
	for (int i = 0; i < PendingSend.size(); i++)
		ResetPendingSend(i);
}

ADD_SQFUNC("int", NSCreateTUI, "entity player, int cols, int rows, int fontSize, float x, float y, int priority", "", ScriptContext::SERVER)
{
	const CBasePlayer* player = g_pSquirrel<context>->template getentity<CBasePlayer>(sqvm, 1);
	int cols = g_pSquirrel<context>->getinteger(sqvm, 2);
	int rows = g_pSquirrel<context>->getinteger(sqvm, 3);
	int fontSize = g_pSquirrel<context>->getinteger(sqvm, 4);
	float x = g_pSquirrel<context>->getfloat(sqvm, 5);
	float y = g_pSquirrel<context>->getfloat(sqvm, 6);
	int priority = g_pSquirrel<context>->getinteger(sqvm, 7);

	if (!player)
		return RaiseError<context>(sqvm, "player is null");
	if (cols < 1 || cols > NSTUI::MAX_COLS)
		return RaiseError<context>(sqvm, fmt::format("columns must be between {} and {}", 1, NSTUI::MAX_COLS));
	if (rows < 1 || rows > NSTUI::MAX_ROWS)
		return RaiseError<context>(sqvm, fmt::format("rows must be between {} and {}", 1, NSTUI::MAX_ROWS));
	if (fontSize < NSTUI::MIN_FONT_SIZE || fontSize > NSTUI::MAX_FONT_SIZE)
		return RaiseError<context>(sqvm, fmt::format("font size must be between {} and {}", NSTUI::MIN_FONT_SIZE, NSTUI::MAX_FONT_SIZE));
	if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
		return RaiseError<context>(sqvm, fmt::format("coordinates must be finite values between {} and {}", 0.0f, 1.0f));
	if (priority < NSTUI::MIN_PRIORITY || priority > NSTUI::MAX_PRIORITY)
		return RaiseError<context>(sqvm, fmt::format("priority must be between {} and {}", NSTUI::MIN_PRIORITY, NSTUI::MAX_PRIORITY));

	TUIState* state = GetPlayerTUIState(player);
	if (std::accumulate(state->cells.begin(), state->cells.end(), 0) + cols * rows > NSTUI::MAX_TEXT_CELLS)
		return RaiseError<context>(sqvm, fmt::format("player TUI instances exceed the {} cell limit", NSTUI::MAX_TEXT_CELLS));

	auto it = std::find(state->allocated.begin(), state->allocated.end(), false);
	if (it == state->allocated.end())
		return RaiseError<context>(sqvm, fmt::format("player already has maximum {} TUI instances", NSTUI::MAX_TUI_INSTANCES));

	size_t index = std::distance(state->allocated.begin(), it);
	NSTUI::Config config {(uint8_t)cols, (uint8_t)rows, (uint8_t)fontSize, (uint8_t)priority, x, y};
	state->allocated[index] = true;
	state->cells[index] = cols * rows;
	state->configs[index] = config;
	SendCreate(player, index, config);

	g_pSquirrel<context>->pushinteger(sqvm, index);
	return SQRESULT_NOTNULL;
}

ADD_SQFUNC("void", NSSendTUI, "entity player, int index, string text", "", ScriptContext::SERVER)
{
	const CBasePlayer* player = g_pSquirrel<context>->template getentity<CBasePlayer>(sqvm, 1);
	int index = g_pSquirrel<context>->getinteger(sqvm, 2);
	const char* text = g_pSquirrel<context>->getstring(sqvm, 3);

	if (!player)
		return RaiseError<context>(sqvm, "player is null");
	TUIState* state = GetPlayerTUIState(player);
	if (index < 0 || index >= NSTUI::MAX_TUI_INSTANCES)
		return RaiseError<context>(sqvm, "invalid TUI index");
	if (!state->allocated[index])
		return RaiseError<context>(sqvm, "invalid TUI index");

	size_t len = strlen(text) + 1;
	if (len > NSTUI::MAX_TEXT_BYTES)
		return RaiseError<context>(sqvm, fmt::format("TUI text exceeds {} bytes", NSTUI::MAX_TEXT_BYTES));

	SendCreate(player, index, state->configs[index]);
	SendUpdate(player, index, text, len);
	return SQRESULT_NULL;
}

ADD_SQFUNC("void", NSDestroyTUI, "entity player, int index", "", ScriptContext::SERVER)
{
	const CBasePlayer* player = g_pSquirrel<context>->template getentity<CBasePlayer>(sqvm, 1);
	int index = g_pSquirrel<context>->getinteger(sqvm, 2);

	if (!player)
		return RaiseError<context>(sqvm, "player is null");
	TUIState* state = GetPlayerTUIState(player);
	if (index < 0 || index >= NSTUI::MAX_TUI_INSTANCES)
		return RaiseError<context>(sqvm, "invalid TUI index");
	if (!state->allocated[index])
		return RaiseError<context>(sqvm, "invalid TUI index");

	SendDestroy(player, index);
	state->allocated[index] = false;
	state->cells[index] = 0;
	return SQRESULT_NULL;
}

ADD_SQFUNC("void", NSTUISetCompressionLevel, "int level", "", ScriptContext::SERVER)
{
	int level = g_pSquirrel<context>->getinteger(sqvm, 1);
	if (level < 0 || level > 9)
		return RaiseError<context>(sqvm, "compression level must be between 0 and 9");

	CompressionLevel = level;
	return SQRESULT_NULL;
}

ADD_SQFUNC("void", NSTUISetUserMessageBudget, "int bytes", "", ScriptContext::SERVER)
{
	int bytes = g_pSquirrel<context>->getinteger(sqvm, 1);
	if (bytes < NSTUI::TEXT_CHUNK_BYTES || bytes > (int)USERMSG_RELIABLE_SLOT_BYTES)
	{
		return RaiseError<context>(
			sqvm, fmt::format("usermessage budget must be between {} and {} bytes", NSTUI::TEXT_CHUNK_BYTES, USERMSG_RELIABLE_SLOT_BYTES));
	}

	UserMessageBudget = (uint32_t)bytes;
	return SQRESULT_NULL;
}

ON_DLL_LOAD_RELIESON("server.dll", ServerTUI_Server, ServerSquirrel, (CModule module))
{
	CUserMessages__Register = module.Offset(0x6C65D0).RCast<CUserMessages__RegisterFn>();
	CRecipientFilter__Construct = module.Offset(0x1E9440).RCast<CRecipientFilter__ConstructFn>();
	CRecipientFilter__MakeReliable = module.Offset(0x1EA4E0).RCast<CRecipientFilter__MakeReliableFn>();
	CRecipientFilter__AddRecipient = module.Offset(0x1E9B30).RCast<CRecipientFilter__AddRecipientFn>();
	CRecipientFilter__RemoveRecipientByIndex = module.Offset(0x1EA820).RCast<CRecipientFilter__RemoveRecipientByIndexFn>();
	UserMessageBegin = module.Offset(0x15C520).RCast<UserMessageBeginFn>();
	MessageEnd = module.Offset(0x158880).RCast<MessageEndFn>();
	MessageWriteByte = module.Offset(0x158A90).RCast<MessageWriteByteFn>();
	MessageWriteFloat = module.Offset(0x158BF0).RCast<MessageWriteFloatFn>();
	MessageWriteLong = module.Offset(0x158C30).RCast<MessageWriteLongFn>();

	void* userMessages = *module.Offset(0xBB5230).RCast<void**>();
	CUserMessages__Register(userMessages, NSTUI::MESSAGE_NAME, -1);
	spdlog::info("Registered server usermessage {}", NSTUI::MESSAGE_NAME);
}

ON_DLL_LOAD("engine.dll", ServerTUI_Engine, (CModule module))
{
	SV_SendClientMessages = module.Offset(0x118860).RCast<SV_SendClientMessagesFn>();
	HookAttach(&(PVOID&)SV_SendClientMessages, (PVOID)h_SV_SendClientMessages);
}
