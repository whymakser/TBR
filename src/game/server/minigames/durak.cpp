// made by fokkonaut

#include "durak.h"
#include <game/server/gamecontext.h>
#include <game/server/teams.h>
#include <engine/shared/config.h>
#include <game/server/gamemodes/DDRace.h>

CDurak::CDurak(CGameContext *pGameServer, int Type) : CMinigame(pGameServer, Type)
{
	vec2 aOffsets[NUM_DURAK_FAKE_TEES] = {
		vec2(4, 0),
		vec2(-3, 0.66f),
		vec2(0.5f, 0.66f),
		vec2(-3, 0),
		vec2(0.5f, 0),
		vec2(-1.75f, 3.8f),
		vec2(1.75f, 3.8f),
		vec2(0, -3.5f),
	};
	const char *apNames[NUM_DURAK_FAKE_TEES] = {
		"🂠🃞",
		"🂹 🂹 🂹", "🂽 🂽 🂽",
		"🂡 🂡 🂡", "🂡 🃂 🃂",
		"🂪 🂪 🂪", "🃚 🃚 🃚",
		"Cursor"
	};
	for (int i = 0; i < NUM_DURAK_FAKE_TEES; i++)
	{
		m_aStaticCards[i].m_TableOffset = vec2(aOffsets[i].x * 32.f, aOffsets[i].y * 32.f);
		str_copy(m_aStaticCards[i].m_aName, apNames[i], sizeof(m_aStaticCards[i].m_aName));
	}
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		m_LastSeatOccupiedMsg[i] = 0;
		m_aUpdatedPassive[i] = false;
	}
	for (auto& Game : m_vpGames)
		delete Game;
	m_vpGames.clear();
}

CDurak::~CDurak()
{
	for (auto &Game : m_vpGames)
		delete Game;
	m_vpGames.clear();
}

int CDurak::GetGameByNumber(int Number, bool AllowRunning)
{
	if (Number <= 0)
		return -1;
	for (unsigned int g = 0; g < m_vpGames.size(); g++)
		if (m_vpGames[g]->m_Number == Number && (AllowRunning || !m_vpGames[g]->m_Running))
			return g;
	return -1;
}

int CDurak::GetGameByClient(int ClientID)
{
	if (ClientID == -1)
		return -1;
	for (unsigned int g = 0; g < m_vpGames.size(); g++)
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
			if (m_vpGames[g]->m_aSeats[i].m_Player.m_ClientID == ClientID)
				return g;
	return -1;
}

void CDurak::AddMapTableTile(int Number, vec2 Pos)
{
	if (Number <= 0)
		return;

	CDurakGame *pGame = 0;
	int Game = GetGameByNumber(Number);
	if (Game != -1)
	{
		pGame = m_vpGames[Game];
	}
	else
	{
		pGame = new CDurakGame(Number);
		m_vpGames.push_back(pGame);
	}
	pGame->m_TablePos = Pos;
	GameServer()->m_aMinigameDisabled[MINIGAME_DURAK] = false;
}

void CDurak::AddMapSeatTile(int Number, int MapIndex, int SeatIndex)
{
	if (Number <= 0 || SeatIndex < 0 || SeatIndex >= MAX_DURAK_PLAYERS)
		return;

	CDurakGame *pGame = 0;
	int Game = GetGameByNumber(Number);
	if (Game != -1)
	{
		pGame = m_vpGames[Game];
	}
	else
	{
		pGame = new CDurakGame(Number);
		m_vpGames.push_back(pGame);
	}
	pGame->m_aSeats[SeatIndex].m_SeatMapIndex = MapIndex;
}

void CDurak::OnCharacterSeat(int ClientID, int Number, int SeatIndex)
{
	if (GameServer()->m_aMinigameDisabled[MINIGAME_DURAK])
		return;

	int Game = GetGameByNumber(Number, false);
	if (Game < 0)
		return;

	char aBuf[128];
	CDurakGame *pGame = m_vpGames[Game];
	CDurakGame::SSeat::SPlayer *pPlayer = &pGame->m_aSeats[SeatIndex].m_Player;
	if (pPlayer->m_ClientID != -1)
	{
		if (pPlayer->m_ClientID != ClientID && m_LastSeatOccupiedMsg[ClientID] && m_LastSeatOccupiedMsg[ClientID] + Server()->TickSpeed() * 2 < Server()->Tick())
		{
			str_format(aBuf, sizeof(aBuf), "Welcome to the Durák table, %s! This seat is taken already, please try another one.", Server()->ClientName(ClientID));
			GameServer()->SendChatTarget(ClientID, aBuf);
			m_LastSeatOccupiedMsg[ClientID] = Server()->Tick();
		}
		return;
	}
	else
	{
		// occupy seat
		pPlayer->m_ClientID = ClientID;
		UpdatePassive(ClientID, 5);

		if (pGame->m_Stake == -1)
		{
			str_format(aBuf, sizeof(aBuf), "Welcome to Durák, %s! You are first, please enter a stake in the chat.", Server()->ClientName(ClientID));
			GameServer()->SendChatTarget(ClientID, aBuf);
		}
		else if (pGame->NumDeployedStakes() == 1)
		{
			str_format(aBuf, sizeof(aBuf), "Welcome to Durák, %s! You are second, please enter the current stake of '%d' or propose a new one in the chat.", Server()->ClientName(ClientID), pGame->m_Stake);
			GameServer()->SendChatTarget(ClientID, aBuf);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Welcome to Durák, %s! In order to participate, please enter the current stake of '%d' in the chat.",
				Server()->ClientName(ClientID), pGame->m_Stake);
			GameServer()->SendChatTarget(ClientID, aBuf);
		}
	}
}

void CDurak::UpdatePassive(int ClientID, int Seconds)
{
	CCharacter* pChr = GameServer()->GetPlayerChar(ClientID);
	if (!pChr)
		return;
	if (pChr->UpdatePassiveEndTick(Server()->Tick() + Server()->TickSpeed() * Seconds))
		m_aUpdatedPassive[ClientID] = true;
}

bool CDurak::TryEnterBetStake(int ClientID, const char *pMessage)
{
	int Game = GetGameByClient(ClientID);
	if (Game < 0)
		return false;

	CDurakGame *pGame = m_vpGames[Game];
	if (pGame->m_Running)
		return false;

	int64 Stake = atoll(pMessage);
	if (Stake < 0)
	{
		return false;
	}

	#define VALIDATE_WALLET() do { \
			if (Stake > GameServer()->m_apPlayers[ClientID]->GetUsableMoney()) \
			{ \
				GameServer()->SendChatTarget(ClientID, "You don't have enough money in your wallet"); \
				return true; \
			} \
		} while(0)

	char aBuf[128];
	CDurakGame::SSeat::SPlayer *pPlayer = &pGame->GetSeatByClient(ClientID)->m_Player;

	if (pGame->m_Stake == -1 && pPlayer->m_Stake == -1)
	{
		VALIDATE_WALLET();

		pGame->m_Stake = pPlayer->m_Stake = Stake;
		UpdatePassive(ClientID, 30);

		str_format(aBuf, sizeof(aBuf), "Successfully set the stake for the current round to '%d'.", Stake);
		GameServer()->SendChatTarget(ClientID, aBuf);
		return true;
	}
	else if (pGame->m_Stake != pPlayer->m_Stake)
	{
		if (pGame->m_Stake == Stake)
		{
			VALIDATE_WALLET();

			pPlayer->m_Stake = Stake;
			UpdatePassive(ClientID, 30);

			GameServer()->SendChatTarget(ClientID, "Successfully deployed your stake.");

			int NumDeployedStakes = pGame->NumDeployedStakes();
			if (NumDeployedStakes == MAX_DURAK_PLAYERS)
			{
				StartGame(Game);
			}
			else if (NumDeployedStakes >= MIN_DURAK_PLAYERS)
			{
				pGame->m_GameStartTick = Server()->Tick() + Server()->TickSpeed() * 15;
			}
			return true;
		}
		else if (pGame->NumDeployedStakes() == 1)
		{
			VALIDATE_WALLET();

			str_format(aBuf, sizeof(aBuf), "'%s' proposed a new stake, please enter the current stake of '%d' or propose a new one in the chat.", Server()->ClientName(ClientID), Stake);
			SendChatToDeployedStakePlayers(Game, aBuf, ClientID);

			pGame->m_Stake = pPlayer->m_Stake = Stake;
			UpdatePassive(ClientID, 30);

			str_format(aBuf, sizeof(aBuf), "Successfully updated the stake for the current round to '%d'.", Stake);
			GameServer()->SendChatTarget(ClientID, aBuf);
			return true;
		}
	}

	#undef VALIDATE_WALLET
	return false;
}

void CDurak::SendChatToDeployedStakePlayers(int Game, const char *pMessage, int NotThisID)
{
	if (Game < 0 || Game >= m_vpGames.size())
		return;
	CDurakGame *pGame = m_vpGames[Game];
	for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
	{
		int ClientID = pGame->m_aSeats[i].m_Player.m_ClientID;
		if (ClientID != -1 && pGame->m_aSeats[i].m_Player.m_Stake != -1 && (NotThisID == -1 || ClientID != NotThisID))
			GameServer()->SendChatTarget(ClientID, pMessage);
	}
}

void CDurak::SendChatToParticipants(int Game, const char *pMessage)
{
	if (Game < 0 || Game >= m_vpGames.size())
		return;
	CDurakGame *pGame = m_vpGames[Game];
	for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
	{
		int ClientID = pGame->m_aSeats[i].m_Player.m_ClientID;
		if (ClientID != -1 && pGame->m_Stake != -1 && pGame->m_aSeats[i].m_Player.m_Stake == pGame->m_Stake)
			GameServer()->SendChatTarget(ClientID, pMessage);
	}
}

bool CDurak::OnInput(int ClientID, CNetObj_PlayerInput *pNewInput)
{
	return false;
}

void CDurak::StartGame(int Game)
{
	if (Game < 0 || Game >= m_vpGames.size())
		return;
	CDurakGame *pGame = m_vpGames[Game];
	if (pGame->m_Running)
		return;

	CGameTeams *pTeams = &((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams;

	int FirstFreeTeam = -1;
	for (int i = 1; i < VANILLA_MAX_CLIENTS / 2; i++)
	{
		if (pTeams->Count(i) == 0)
		{
			FirstFreeTeam = i;
			break;
		}
	}

	// New game for other players, since we move to a new team
	m_vpGames.push_back(new CDurakGame(pGame->m_Number));

	if (FirstFreeTeam == -1)
	{
		SendChatToParticipants(Game, "Couldn't start game, no empty team found");
		m_vpGames.erase(m_vpGames.begin() + Game);
		return;
	}

	pGame->m_GameStartTick = Server()->Tick();
	pGame->m_Running = true;

	for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
	{
		int ClientID = pGame->m_aSeats[i].m_Player.m_ClientID;
		if (pGame->m_aSeats[i].m_Player.m_Stake != pGame->m_Stake)
		{
			GameServer()->SendChatTarget(ClientID, "You didn't deploy your stake in time, game started without you");
			pGame->m_aSeats[i].m_Player.Reset();
			continue;
		}

		GameServer()->SetMinigame(ClientID, MINIGAME_DURAK, true);
		pTeams->SetForceCharacterTeam(ClientID, FirstFreeTeam);
	}

	SendChatToParticipants(Game, "Game started lul");
}

void CDurak::EndGame(int Game)
{
	if (Game < 0 || Game >= m_vpGames.size())
		return;
	CDurakGame* pGame = m_vpGames[Game];
	if (!pGame->m_Running)
		return;

	m_vpGames.erase(m_vpGames.begin() + Game);
}

void CDurak::Tick()
{
	for (unsigned int g = 0; g < m_vpGames.size(); g++)
	{
		CDurakGame *pGame = m_vpGames[g];
		if (pGame->m_Number <= 0)
		{
			m_vpGames.erase(m_vpGames.begin() + g--);
			continue;
		}

		bool CancelledTimer = false;
		int NumDeployedStakes = pGame->NumDeployedStakes(true);
		// Reset stake when all players left the table and no round started
		if (NumDeployedStakes == 0)
		{
			pGame->m_Stake = -1;
		}
		else if (NumDeployedStakes < MIN_DURAK_PLAYERS)
		{
			if (!pGame->m_Running && pGame->m_GameStartTick)
			{
				pGame->m_GameStartTick = 0;
				CancelledTimer = true;
			}

			// will only trigger if game is running
			EndGame(g);
		}

		bool TimerUpGameStart = !pGame->m_Running && pGame->m_GameStartTick && pGame->m_GameStartTick <= Server()->Tick();
		if (TimerUpGameStart)
		{
			StartGame(g);
		}

		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			CDurakGame::SSeat *pSeat = &pGame->m_aSeats[i];
			int ClientID = pSeat->m_Player.m_ClientID;
			if (ClientID == -1)
				continue;

			CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
			if (!pPlayer || pPlayer->GetTeam() == TEAM_SPECTATORS)
			{
				pSeat->m_Player.Reset();
				continue;
			}

			if (!pGame->m_Running)
			{
				CCharacter *pChr = pPlayer->GetCharacter();
				if (!pChr || GameServer()->Collision()->GetMapIndex(pChr->GetPos()) != pSeat->m_SeatMapIndex)
				{
					if (pChr && m_aUpdatedPassive[ClientID])
					{
						pChr->m_PassiveEndTick = 0;
						pChr->Passive(false, -1, true);
					}
					m_aUpdatedPassive[ClientID] = false;
					pSeat->m_Player.Reset();
					continue;
				}

				if (CancelledTimer)
				{
					GameServer()->SendBroadcast("Game aborted, too few players...", ClientID);
				}
				else if (pGame->m_GameStartTick > Server()->Tick() && Server()->Tick() % Server()->TickSpeed() == 0)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Players [%d/%d]\nGame start [%d]", pGame->NumDeployedStakes(), (int)MAX_DURAK_PLAYERS, (pGame->m_GameStartTick - Server()->Tick()) / Server()->TickSpeed() + 1);
					GameServer()->SendBroadcast(aBuf, ClientID);
				}
				continue;
			}

			if (TimerUpGameStart)
			{
				GameServer()->SendBroadcast("Game started!", ClientID);
			}

			if (pPlayer->m_Minigame != MINIGAME_DURAK)
			{
				pSeat->m_Player.Reset();
				continue;
			}

			// game logic
		}
	}
}

void CDurak::Snap(int SnappingClient)
{
	if (SnappingClient == -1)
		return;

	int ClientID = SnappingClient;
	CPlayer *pSnap = GameServer()->m_apPlayers[SnappingClient];
	if ((pSnap->GetTeam() == TEAM_SPECTATORS || pSnap->IsPaused()) && pSnap->GetSpectatorID() >= 0)
		ClientID = pSnap->GetSpectatorID();

	int Game = GetGameByClient(ClientID);
	if (Game < 0)
	{
		// render default fallback tabledesign
		return;
	}

	// render current round

	/*int FakeID = GameServer()->m_World.GetSeeOthersID(SnappingClient) - 1;
	for (int i = 0; i < NUM_DURAK_FAKE_TEES; i++)
	{
		SnapFakeTee(SnappingClient, FakeID, m_TablePos + m_aStaticCards[i].m_TableOffset, m_aStaticCards[i].m_aName);
		FakeID--;
	}*/
}

void CDurak::SnapFakeTee(int SnappingClient, int ID, vec2 Pos, const char *pName)
{
	if (!Server()->IsSevendown(SnappingClient))
	{
		// todo 0.7: add clientinfo update, like CGameWorld::PlayerMap::UpdateSeeOthers()
		CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, ID, sizeof(CNetObj_PlayerInfo)));
		if(!pPlayerInfo)
			return;
		pPlayerInfo->m_PlayerFlags = 0;
		pPlayerInfo->m_Score = -1;
		pPlayerInfo->m_Latency = 0;
	}
	else
	{
		int *pClientInfo = (int*)Server()->SnapNewItem(11 + NUM_NETOBJTYPES, ID, 17*4); // NETOBJTYPE_CLIENTINFO
		if(!pClientInfo)
			return;
		StrToInts(&pClientInfo[0], 4, pName);
		StrToInts(&pClientInfo[4], 3, "");
		StrToInts(&pClientInfo[8], 6, "default");
		pClientInfo[14] = 1;
		pClientInfo[15] = pClientInfo[16] = 255;

		CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, ID, 5*4));
		if(!pPlayerInfo)
			return;
		((int*)pPlayerInfo)[0] = 0; // local
		((int*)pPlayerInfo)[1] = ID;
		((int*)pPlayerInfo)[2] = TEAM_BLUE;
		((int*)pPlayerInfo)[3] = -1; // score
		((int*)pPlayerInfo)[4] = 0;
	}

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, ID, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;
	pCharacter->m_X = Pos.x;
	pCharacter->m_Y = Pos.y;
	pCharacter->m_Weapon = WEAPON_GUN;
	//if (Pos.x > m_TablePos.x)
	//	pCharacter->m_Angle = 804;

	if(Server()->IsSevendown(SnappingClient))
	{
		int PlayerFlags = 0;
		/*if (m_pPlayer->m_PlayerFlags & PLAYERFLAG_CHATTING) PlayerFlags |= 4;
		if (m_pPlayer->m_PlayerFlags&PLAYERFLAG_SCOREBOARD) PlayerFlags |= 8;
		if (m_pPlayer->m_PlayerFlags&PLAYERFLAG_AIM) PlayerFlags |= 16;
		if (m_pPlayer->m_PlayerFlags&PLAYERFLAG_SPEC_CAM) PlayerFlags |= 32;*/

		int Health = pCharacter->m_Health;
		int Armor = pCharacter->m_Armor;
		int AmmoCount = pCharacter->m_AmmoCount;
		int Weapon = pCharacter->m_Weapon;
		int Emote = pCharacter->m_Emote;
		int AttackTick = pCharacter->m_AttackTick;

		int Offset = sizeof(CNetObj_CharacterCore) / 4;
		((int*)pCharacter)[Offset+0] = PlayerFlags;
		((int*)pCharacter)[Offset+1] = Health;
		((int*)pCharacter)[Offset+2] = Armor;
		((int*)pCharacter)[Offset+3] = AmmoCount;
		((int*)pCharacter)[Offset+4] = Weapon;
		((int*)pCharacter)[Offset+5] = Emote;
		((int*)pCharacter)[Offset+6] = AttackTick;
	}
	else
	{
		if(pCharacter->m_Angle > (int)(pi * 256.0f))
		{
			pCharacter->m_Angle -= (int)(2.0f * pi * 256.0f);
		}
	}
}
