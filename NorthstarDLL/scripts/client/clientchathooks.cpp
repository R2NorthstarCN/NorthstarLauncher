#include "pch.h"
#include "squirrel/squirrel.h"

#include "server/serverchathooks.h"
#include "client/localchatwriter.h"

#include <rapidjson/document.h>

AUTOHOOK_INIT()

bool skip_valid_ansi_csi_sgr(char*& str)
{
	if (*str++ != '\x1B')
		return false;
	if (*str++ != '[') // CSI
		return false;
	for (char* c = str; *c; c++)
	{
		if (*c >= '0' && *c <= '9')
			continue;
		if (*c == ';' || *c == ':')
			continue;
		if (*c == 'm') // SGR
			break;
		return false;
	}
	return true;
}

void RemoveAsciiControlSequences(char* str, bool allow_color_codes)
{
	for (char *pc = str, c = *pc; c = *pc; pc++)
	{
		// skip UTF-8 characters
		int bytesToSkip = 0;
		if ((c & 0xE0) == 0xC0)
			bytesToSkip = 1; // skip 2-byte UTF-8 sequence
		if ((c & 0xF0) == 0xE0)
			bytesToSkip = 2; // skip 3-byte UTF-8 sequence
		if ((c & 0xF8) == 0xF0)
			bytesToSkip = 3; // skip 4-byte UTF-8 sequence
		if ((c & 0xFC) == 0xF8)
			bytesToSkip = 4; // skip 5-byte UTF-8 sequence
		if ((c & 0xFE) == 0xFC)
			bytesToSkip = 5; // skip 6-byte UTF-8 sequence

		bool invalid = false;
		char* orgpc = pc;
		for (int i = 0; i < bytesToSkip; i++)
		{
			char next = pc[1];

			// valid UTF-8 part
			if ((next & 0xC0) == 0x80)
			{
				pc++;
				continue;
			}

			// invalid UTF-8 part or encountered \0
			invalid = true;
			break;
		}
		if (invalid)
		{
			// erase the whole "UTF-8" sequence
			for (char* x = orgpc; x <= pc; x++)
				if (*x != '\0')
					*x = ' ';
				else
					break;
		}
		if (bytesToSkip > 0)
			continue; // this byte was already handled as UTF-8

		// an invalid control character or an UTF-8 part outside of UTF-8 sequence
		if ((iscntrl(c) && c != '\n' && c != '\r' && c != '\x1B') || (c & 0x80) != 0)
		{
			*pc = ' ';
			continue;
		}

		if (c == '\x1B') // separate handling for this escape sequence...
			if (allow_color_codes && skip_valid_ansi_csi_sgr(pc)) // ...which we allow for color codes...
				pc--;
			else // ...but remove it otherwise
				*pc = ' ';
	}
}

// clang-format off
AUTOHOOK(CHudChat__AddGameLine, client.dll + 0x22E580,
void, __fastcall, (void* self, const char* message, int inboxId, bool isTeam, bool isDead))
// clang-format on
{
	// This hook is called for each HUD, but we only want our logic to run once.
	if (self != *CHudChat::allHuds)
		return;

	int senderId = inboxId & CUSTOM_MESSAGE_INDEX_MASK;
	bool isAnonymous = senderId == 0;
	bool isCustom = isAnonymous || (inboxId & CUSTOM_MESSAGE_INDEX_BIT);

	// Type is set to 0 for non-custom messages, custom messages have a type encoded as the first byte
	int type = 0;
	const char* payload = message;
	if (isCustom)
	{
		type = message[0];
		payload = message + 1;
	}

    RemoveAsciiControlSequences(const_cast<char*>(message), true);

	SQRESULT result = g_pSquirrel<ScriptContext::CLIENT>->Call(
		"CHudChat_ProcessMessageStartThread", static_cast<int>(senderId) - 1, payload, isTeam, isDead, type);
	if (result == SQRESULT_ERROR)
		for (CHudChat* hud = *CHudChat::allHuds; hud != NULL; hud = hud->next)
			CHudChat__AddGameLine(hud, message, inboxId, isTeam, isDead);
}

ADD_SQFUNC("void", NSChatWrite, "int context, string text", "", ScriptContext::CLIENT)
{
	int chatContext = g_pSquirrel<ScriptContext::CLIENT>->getinteger(sqvm, 1);
	const char* str = g_pSquirrel<ScriptContext::CLIENT>->getstring(sqvm, 2);

	LocalChatWriter((LocalChatWriter::Context)chatContext).Write(str);
	return SQRESULT_NULL;
}

ADD_SQFUNC("void", NSChatWriteRaw, "int context, string text", "", ScriptContext::CLIENT)
{
	int chatContext = g_pSquirrel<ScriptContext::CLIENT>->getinteger(sqvm, 1);
	const char* str = g_pSquirrel<ScriptContext::CLIENT>->getstring(sqvm, 2);

	LocalChatWriter((LocalChatWriter::Context)chatContext).InsertText(str);
	return SQRESULT_NULL;
}

ADD_SQFUNC("void", NSChatWriteLine, "int context, string text", "", ScriptContext::CLIENT)
{
	int chatContext = g_pSquirrel<ScriptContext::CLIENT>->getinteger(sqvm, 1);
	const char* str = g_pSquirrel<ScriptContext::CLIENT>->getstring(sqvm, 2);

	LocalChatWriter((LocalChatWriter::Context)chatContext).WriteLine(str);
	return SQRESULT_NULL;
}

ON_DLL_LOAD_CLIENT("client.dll", ClientChatHooks, (CModule module))
{
	AUTOHOOK_DISPATCH()
}
