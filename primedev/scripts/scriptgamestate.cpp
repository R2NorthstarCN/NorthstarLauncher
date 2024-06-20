#include "scriptgamestate.h"
#include "squirrel/squirrel.h"

SQGameState* g_pSQGameState = new SQGameState;


std::string GameStateToString(int gamestate)
{
	switch (gamestate)
	{
	case 0:
		return "WaitingForCustomStart";
	case 1:
		return "WaitingForPlayers";
	case 2:
		return "PickLoadout";
	case 3:
		return "Prematch";
	case 4:
		return "Playing";
	case 5:
		return "SuddenDeath";
	case 6:
		return "SwitchingSides";
	case 7:
		return "WinnerDetermined";
	case 8:
		return "Epilogue";
	case 9:
		return "Postmatch";
	}
	return "none";
}

ADD_SQFUNC("void", NSUpdateSQGameState, "int gamestate", "", ScriptContext::SERVER)
{
	int state = g_pSquirrel<context>->getinteger(sqvm, 1);
	g_pSQGameState->eGameState = state;
	spdlog::info("GAMESTATE: {} STRING: {}", state, GameStateToString(state));
	return SQRESULT_NOTNULL;
}
