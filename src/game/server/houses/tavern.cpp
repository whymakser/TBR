// made by fokkonaut

#include "tavern.h"
#include <game/server/gamecontext.h>

CTavern::CTavern(CGameContext *pGameServer) : CHouse(pGameServer, HOUSE_TAVERN)
{
}

const char *CTavern::GetWelcomeMessage(int ClientID)
{
	static char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Welcome to the tavern, %s! Press F4 to buy a grog.", Server()->ClientName(ClientID));
	return aBuf;
}

const char *CTavern::GetConfirmMessage(int ClientID)
{
	return "Are you sure you want to buy a grog?";
}

const char *CTavern::GetEndMessage(int ClientID)
{
	return "You canceled the purchase.";
}

void CTavern::OnSuccess(int ClientID)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	int GrogPrice = GameServer()->Config()->m_SvGrogPrice;
	if (pPlayer->GetWalletOrBank() < GrogPrice)
	{
		GameServer()->SendChatTarget(ClientID, "You don't have enough money in your wallet to buy a grog.");
		return;
	}

	if (pPlayer->GetCharacter() && pPlayer->GetCharacter()->AddGrog())
	{
		pPlayer->WalletTransaction(-GrogPrice, "bought grog");
		GameServer()->SendChatTarget(ClientID, "You bought one grog");
	}
}

void CTavern::OnPageChange(int ClientID)
{
	const char *pFooter = "Press F3 to confirm your assignment.";
	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "Welcome to the tavern!\n\nIf you would like to buy a grog for %d money, please press F3.", GameServer()->Config()->m_SvGrogPrice);
	SendWindow(ClientID, aMsg, pFooter);
	m_aClients[ClientID].m_LastMotd = Server()->Tick();
}
