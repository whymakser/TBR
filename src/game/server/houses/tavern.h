// made by fokkonaut

#ifndef GAME_HOUSES_TAVERN_H
#define GAME_HOUSES_TAVERN_H

#include "house.h"
#include <engine/shared/protocol.h>

class CTavern : public CHouse
{
public:
	CTavern(CGameContext *pGameServer);
	virtual ~CTavern() {};

	virtual void OnPageChange(int ClientID);
	virtual void OnSuccess(int ClientID);
	virtual const char *GetWelcomeMessage(int ClientID);
	virtual const char *GetConfirmMessage(int ClientID);
	virtual const char *GetEndMessage(int ClientID);
};

#endif // GAME_HOUSES_TAVERN_H
