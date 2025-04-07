// made by fokkonaut

#include "durak.h"
#include <game/server/gamecontext.h>
#include <game/server/teams.h>
#include <engine/shared/config.h>
#include <game/server/gamemodes/DDRace.h>

const vec2 CCard::ms_CardSizeRadius = vec2(14.f, 16.f);
const vec2 CCard::ms_TableSizeRadius = vec2(4.9f * 32.f, 3.8f * 32.f); // 11*9 blocks around table center tile (all 4 corner tiles can be cut-out)
const vec2 CCard::ms_AttackAreaRadius = vec2(4.f * 32.f, 1.5f * 32.f); // 8x3
const vec2 CCard::ms_AttackAreaCenterOffset = vec2(-32.f, 32.f);

CDurak::CDurak(CGameContext *pGameServer, int Type) : CMinigame(pGameServer, Type)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aLastSeatOccupiedMsg[i] = 0;
		m_aUpdatedPassive[i] = false;
		m_aInDurakGame[i] = false;
		m_aKeyboardControl[i] = false;
		m_aDurakNumReserved[i] = 0;
	}
	m_aStaticCards[DURAK_TEXT_CARDS_STACK].m_TableOffset = vec2(3.9f * 32.f, 32.f);
	m_aStaticCards[DURAK_TEXT_TRUMP_CARD].m_TableOffset = vec2(4.9f * 32.f, 32.f);
	m_aStaticCards[DURAK_TEXT_KEYBOARD_CONTROL].m_TableOffset = vec2(3.f * 32.f, -2.f * 32.f);
	for (auto &Game : m_vpGames)
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

CDurakGame *CDurak::GetOrAddGame(int Number)
{
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
	return pGame;
}

void CDurak::AddMapTableTile(int Number, vec2 Pos)
{
	if (Number <= 0)
		return;
	CDurakGame *pGame = GetOrAddGame(Number);
	if (pGame)
	{
		pGame->m_TablePos = Pos;
		GameServer()->m_aMinigameDisabled[MINIGAME_DURAK] = false;
	}
}

void CDurak::AddMapSeatTile(int Number, int MapIndex, int SeatIndex)
{
	if (Number <= 0 || SeatIndex < 0 || SeatIndex >= MAX_DURAK_PLAYERS)
		return;
	CDurakGame *pGame = GetOrAddGame(Number);
	if (pGame)
	{
		pGame->m_aSeats[SeatIndex].m_MapIndex = MapIndex;
	}
}

void CDurak::UpdatePassive(int ClientID, int Seconds)
{
	CCharacter* pChr = GameServer()->GetPlayerChar(ClientID);
	if (pChr && pChr->UpdatePassiveEndTick(Server()->Tick() + Server()->TickSpeed() * Seconds))
		m_aUpdatedPassive[ClientID] = true;
}

void CDurak::OnCharacterSeat(int ClientID, int Number, int SeatIndex)
{
	if (InDurakGame(ClientID) || GameServer()->m_aMinigameDisabled[MINIGAME_DURAK])
		return;

	int Game = GetGameByNumber(Number, false);
	if (Game < 0)
		return;

	char aBuf[128];
	CDurakGame *pGame = m_vpGames[Game];
	CDurakGame::SSeat::SPlayer *pPlayer = &pGame->m_aSeats[SeatIndex].m_Player;
	if (pPlayer->m_ClientID != -1)
	{
		if (pPlayer->m_ClientID != ClientID && (!m_aLastSeatOccupiedMsg[ClientID] || m_aLastSeatOccupiedMsg[ClientID] + Server()->TickSpeed() * 5 < Server()->Tick()))
		{
			str_format(aBuf, sizeof(aBuf), "Welcome to the Durák table, %s! This seat is taken already, please try another one.", Server()->ClientName(ClientID));
			GameServer()->SendChatTarget(ClientID, aBuf);
			m_aLastSeatOccupiedMsg[ClientID] = Server()->Tick();
		}
		return;
	}

	// occupy seat
	pPlayer->m_ClientID = ClientID;
	UpdatePassive(ClientID, 5);

	if (pGame->m_Stake == -1)
	{
		str_format(aBuf, sizeof(aBuf), "Welcome to Durák, %s! You are first, please enter a stake in the chat.", Server()->ClientName(ClientID));
	}
	else if (pGame->NumParticipants() == 1)
	{
		str_format(aBuf, sizeof(aBuf), "Welcome to Durák, %s! You are second, please enter the current stake of '%d' or propose a new one in the chat.", Server()->ClientName(ClientID), pGame->m_Stake);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Welcome to Durák, %s! In order to participate, please enter the current stake of '%d' in the chat.", Server()->ClientName(ClientID), pGame->m_Stake);
	}
	GameServer()->SendChatTarget(ClientID, aBuf);
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
	if (Stake <= 0 && pMessage[0] != '0')
		return false;

	#define VALIDATE_WALLET() do { \
			if (GameServer()->m_apPlayers[ClientID]->GetUsableMoney() < Stake) \
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

		pGame->m_Stake = Stake;
		pPlayer->m_Stake = Stake;
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

			int NumPlayers = pGame->NumParticipants();
			if (NumPlayers == MAX_DURAK_PLAYERS)
			{
				StartGame(Game);
			}
			else if (NumPlayers >= MIN_DURAK_PLAYERS)
			{
				pGame->m_GameStartTick = Server()->Tick() + Server()->TickSpeed() * 15;
			}
			return true;
		}
		else if (pGame->NumParticipants() <= 1)
		{
			VALIDATE_WALLET();

			str_format(aBuf, sizeof(aBuf), "'%s' proposed a new stake, please enter the current stake of '%d' or propose a new one in the chat.", Server()->ClientName(ClientID), Stake);
			SendChatToDeployedStakePlayers(Game, aBuf, ClientID);

			pGame->m_Stake = Stake;
			pPlayer->m_Stake = Stake;
			UpdatePassive(ClientID, 30);

			str_format(aBuf, sizeof(aBuf), "Successfully updated the stake for the current round to '%d'.", Stake);
			GameServer()->SendChatTarget(ClientID, aBuf);
			return true;
		}
	}

	#undef VALIDATE_WALLET
	return false;
}

void CDurak::OnPlayerLeave(int ClientID, bool Disconnect)
{
	CGameTeams *pTeams = &((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams;
	for (unsigned int g = 0; g < m_vpGames.size(); g++)
	{
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			if (m_vpGames[g]->m_aSeats[i].m_Player.m_ClientID == ClientID)
			{
				if (m_vpGames[g]->m_Running)
				{
					// Add stake for the winner, or simply the next winner. if everybody leaves, last person gets the win
					m_vpGames[g]->m_Stake += m_vpGames[g]->m_aSeats[i].m_Player.m_Stake;
				}
				m_vpGames[g]->m_aSeats[i].m_Player.Reset();

				if (!GameServer()->Collision()->TileUsed(TILE_DURAK_LOBBY))
				{
					GameServer()->SetMinigame(ClientID, MINIGAME_NONE, false, false);
				}
				GameServer()->m_apPlayers[ClientID]->m_ForceSpawnPos = vec2(-1, -1);
				pTeams->SetForceCharacterTeam(ClientID, 0);
				m_aInDurakGame[ClientID] = false;
				GameServer()->SendTuningParams(ClientID);
				break;
			}
		}
	}

	if (Disconnect)
	{
		m_aDurakNumReserved[ClientID] = 0;
	}
}

bool CDurak::OnDropMoney(int ClientID, int Amount)
{
	int Game = GetGameByClient(ClientID);
	// If the game is running already, the money has been subtracted already
	if (Game < 0 || m_vpGames[Game]->m_Running)
		return false;
	bool CanDrop = GameServer()->m_apPlayers[ClientID]->GetUsableMoney() - Amount >= m_vpGames[Game]->GetSeatByClient(ClientID)->m_Player.m_Stake;
	if (!CanDrop)
	{
		GameServer()->SendChatTarget(ClientID, "You can't currently drop that much money due to your stake");
		return true;
	}
	return false;
}

void CDurak::OnInput(CCharacter *pCharacter, CNetObj_PlayerInput *pNewInput)
{
	int ClientID = pCharacter->GetPlayer()->GetCID();
	int Game = GetGameByClient(ClientID);
	if (Game < 0)
		return;

	CDurakGame *pGame = m_vpGames[Game];
	CDurakGame::SSeat *pSeat = pGame->GetSeatByClient(ClientID);
	CCard *pCard = 0;
	if (pSeat->m_Player.m_HoveredCard != -1)
	{
		pCard = &pSeat->m_Player.m_vHandCards[pSeat->m_Player.m_HoveredCard];
	}

	if (m_aLastInput[ClientID].m_Direction == 0 && pNewInput->m_Direction != 0)
	{
		m_aKeyboardControl[ClientID] = true;
		if (pSeat->m_Player.m_HoveredCard == -1)
		{
			pSeat->m_Player.m_HoveredCard = 0;
		}
		else if (pCard)
		{
			pCard->SetHovered(false);
			pSeat->m_Player.m_HoveredCard = mod(pSeat->m_Player.m_HoveredCard + pNewInput->m_Direction, pSeat->m_Player.m_vHandCards.size());
		}
		// Update pCard
		pCard = &pSeat->m_Player.m_vHandCards[pSeat->m_Player.m_HoveredCard];
		if (pCard->m_HoverState == CCard::HOVERSTATE_NONE)
		{
			pCard->SetHovered(true);
		}
	}
	else if (m_aLastInput[ClientID].m_Jump == 0 && pNewInput->m_Jump != 0)
	{
		m_aKeyboardControl[ClientID] = true;
	}
	else if (abs(pNewInput->m_TargetX - m_aLastInput[ClientID].m_TargetX) > 1.f || abs(pNewInput->m_TargetY - m_aLastInput[ClientID].m_TargetY) > 1.f)
	{
		m_aKeyboardControl[ClientID] = false;
	}

	m_aLastInput[ClientID].m_Direction = pNewInput->m_Direction;
	m_aLastInput[ClientID].m_Jump = pNewInput->m_Jump;
	m_aLastInput[ClientID].m_TargetX = pNewInput->m_TargetX;
	m_aLastInput[ClientID].m_TargetY = pNewInput->m_TargetY;
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
		if (ClientID == -1)
			continue;

		// Stake can change as soon as players leave, so no money is removed. That means when the game is running, a player is automatically participant
		if (pGame->m_Running || (pGame->m_Stake != -1 && pGame->m_aSeats[i].m_Player.m_Stake == pGame->m_Stake))
		{
			GameServer()->SendChatTarget(ClientID, pMessage);
		}
	}
}

bool CDurak::StartGame(int Game)
{
	if (Game < 0 || Game >= m_vpGames.size())
		return false;
	CDurakGame *pGame = m_vpGames[Game];
	if (pGame->m_Running)
		return false;

	CGameTeams *pTeams = &((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams;
	int FirstFreeTeam = -1;
	for (int i = 1; i < VANILLA_MAX_CLIENTS; i++)
	{
		if (pTeams->Count(i) == 0)
		{
			FirstFreeTeam = i;
			break;
		}
	}

	// New game for other players at this table, since we move to a new team or this gets deleted if no team was free
	m_vpGames.push_back(new CDurakGame(pGame));
	if (FirstFreeTeam == -1)
	{
		SendChatToParticipants(Game, "Couldn't start game, no empty team found");
		delete m_vpGames[Game];
		m_vpGames.erase(m_vpGames.begin() + Game);
		return false;
	}
	
	pGame->m_GameStartTick = Server()->Tick();
	pGame->m_Running = true;
	pGame->m_Deck.Shuffle();

	for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
	{
		CDurakGame::SSeat *pSeat = &pGame->m_aSeats[i];
		int ClientID = pSeat->m_Player.m_ClientID;
		if (ClientID == -1)
			continue;

		if (pSeat->m_Player.m_Stake != pGame->m_Stake)
		{
			GameServer()->SendChatTarget(ClientID, "You didn't deploy your stake in time, game started without you");
			pSeat->m_Player.Reset();
			continue;
		}

		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
		pPlayer->WalletTransaction(-pGame->m_Stake, "Durák stake");

		GameServer()->SetMinigame(ClientID, MINIGAME_DURAK, true, false);
		pPlayer->m_ForceSpawnPos = GameServer()->Collision()->GetPos(pSeat->m_MapIndex);
		m_aInDurakGame[ClientID] = true;
		pTeams->SetForceCharacterTeam(ClientID, FirstFreeTeam);

		// Todo: send tuning params only when actively playing and reset inbetween so non-actively-playing players can move around maybe
		GameServer()->SendTuningParams(ClientID);
	}

	pGame->DealHandCards();
	int LowestBeginTrump = pGame->NextRound();

	char aBuf[128];
	int BeginnerID = pGame->m_aSeats[pGame->m_InitialAttackerIndex].m_Player.m_ClientID;
	if (LowestBeginTrump != -1)
		str_format(aBuf, sizeof(aBuf), "Beginner determined: '%s' (Lowest trump card: %s)", Server()->ClientName(BeginnerID), GetCardSymbol(pGame->m_Deck.GetTrumpSuit(), LowestBeginTrump));
	else
		str_format(aBuf, sizeof(aBuf), "Beginner chosen: '%s' (No trump cards dealt)", Server()->ClientName(BeginnerID));
	SendChatToParticipants(Game, aBuf);
	return true;
}

bool CDurak::EndGame(int Game)
{
	if (Game < 0 || Game >= m_vpGames.size())
		return false;
	CDurakGame* pGame = m_vpGames[Game];
	if (!pGame->m_Running)
		return false;

	CGameTeams *pTeams = &((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams;
	for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
	{
		int ClientID = pGame->m_aSeats[i].m_Player.m_ClientID;
		if (ClientID == -1)
			continue;
		OnPlayerLeave(ClientID);
	}

	delete m_vpGames[Game];
	m_vpGames.erase(m_vpGames.begin() + Game);
	return true;
}

const char *CDurak::GetCardSymbol(int Suit, int Rank)
{
	if (Suit == -1 && Rank == -1) {
		static const char *pBackCard = "🂠";
		return pBackCard;
	}
	if (Suit == -2)
		return "⌨☒";
	if (Suit == -3)
		return "⌨☐";
	if (Suit < 0 || Suit > 3 || Rank < 6 || Rank > 14) {
		return "??";
	}
	static const char *aapCards[4][9] = {
		{"🂦", "🂧", "🂨", "🂩", "🂪", "🂫", "🂭", "🂮", "🂡"}, // Spades
		{"🂶", "🂷", "🂸", "🂹", "🂺", "🂻", "🂽", "🂾", "🂱"}, // Hearts
		{"🃆", "🃇", "🃈", "🃉", "🃊", "🃋", "🃍", "🃎", "🃁"}, // Diamonds
		{"🃖", "🃗", "🃘", "🃙", "🃚", "🃛", "🃝", "🃞", "🃑"}  // Clubs
	};
	return aapCards[Suit][Rank - 6];
}

int CDurak::GetPlayerState(int ClientID)
{
	if (!InDurakGame(ClientID))
		return DURAK_PLAYERSTATE_NONE;

	int Game = GetGameByClient(ClientID);
	if (Game < 0)
		return DURAK_PLAYERSTATE_NONE;

	CDurakGame *pGame = m_vpGames[Game];
	if (!pGame->m_Running)
		return DURAK_PLAYERSTATE_NONE;

	int SeatIndex = -1;
	for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
	{
		if (pGame->m_aSeats[i].m_Player.m_ClientID == ClientID)
		{
			return pGame->GetStateBySeat(i);
		}
	}
	return DURAK_PLAYERSTATE_NONE;
}

void CDurak::Tick()
{
	for (int g = (int)m_vpGames.size() - 1; g >= 0; g--)
	{
		if (UpdateGame(g))
		{
			// no need to decrement g even though m_vpGames[g] got erased, since we're iterating backwards
			continue;
		}
	}
}

bool CDurak::UpdateGame(int Game)
{
	auto &&HandleCardHover = [this](int Game, int Seat, int Card, vec2 CursorPos, bool Dragging) {
		CDurakGame::SSeat *pSeat = &m_vpGames[Game]->m_aSeats[Seat];
		if (m_aKeyboardControl[pSeat->m_Player.m_ClientID])
		{
			return false;
		}
		CCard *pCard = &pSeat->m_Player.m_vHandCards[Card];
		vec2 RelativeCursorPos = CursorPos - m_vpGames[Game]->m_TablePos;
		if (Dragging)
		{
			if (pCard->m_HoverState > CCard::HOVERSTATE_NONE)
			{
				pCard->m_HoverState = CCard::HOVERSTATE_DRAGGING;
				float CornerY = 0.f;
				if ((RelativeCursorPos.x < -CCard::ms_TableSizeRadius.x + 32.f) ||
					RelativeCursorPos.x > CCard::ms_TableSizeRadius.x - 32.f)
				{
					CornerY = 32.f;
				}
				pCard->m_TableOffset.x = clamp(RelativeCursorPos.x, -CCard::ms_TableSizeRadius.x, CCard::ms_TableSizeRadius.x);
				pCard->m_TableOffset.y = clamp(RelativeCursorPos.y + DURAK_CARD_NAME_OFFSET,
					-CCard::ms_TableSizeRadius.y + DURAK_CARD_NAME_OFFSET + 4.f + CornerY,
					CCard::ms_TableSizeRadius.y - CornerY);
			}
		}
		else if (pCard->m_HoverState == CCard::HOVERSTATE_DRAGGING)
		{
			pCard->SetHovered(false);
			pSeat->m_Player.m_HoveredCard = -1;

			if (CCard::InAttackArea(RelativeCursorPos))
			{
				int PlayerState = GetPlayerState(pSeat->m_Player.m_ClientID);
				if (PlayerState == DURAK_PLAYERSTATE_ATTACK)
				{
					if (m_vpGames[Game]->TryAttack(Seat, pCard))
					{
						//pSeat->m_Player.m_Tooltip = CDurakGame::TOOLTIP_NONE;
						return true;
					}
					//pSeat->m_Player.m_Tooltip = CDurakGame::TOOLTIP_ATTACK;
				}
				else if (PlayerState == DURAK_PLAYERSTATE_DEFEND)
				{
					bool TryingDefend = false;
					for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
					{
						CCard *pOffense = &m_vpGames[Game]->m_Attacks[i].m_Offense;
						if (pOffense->Valid() && pOffense->MouseOver(RelativeCursorPos))
						{
							TryingDefend = true;
							if (m_vpGames[Game]->TryDefend(Seat, i, pCard))
							{
								//pSeat->m_Player.m_Tooltip = CDurakGame::TOOLTIP_NONE;
								return true;
							}

							//pSeat->m_Player.m_Tooltip = CDurakGame::TOOLTIP_DEFEND;
							break;
						}
					}

					if (!TryingDefend)
					{
						if (m_vpGames[Game]->TryPush(Seat, pCard))
						{
							//pSeat->m_Player.m_Tooltip = CDurakGame::TOOLTIP_NONE;
							return true;
						}

						//pSeat->m_Player.m_Tooltip = CDurakGame::TOOLTIP_PUSH;
					}
				}
			}
		}
		else
		{
			bool MouseOver = (pSeat->m_Player.m_HoveredCard == -1 || pSeat->m_Player.m_HoveredCard == Card) && pCard->MouseOver(RelativeCursorPos);
			if (MouseOver && pCard->m_HoverState == CCard::HOVERSTATE_NONE)
			{
				pCard->SetHovered(true);
				pSeat->m_Player.m_HoveredCard = Card;
			}
			else if (!MouseOver && pCard->m_HoverState == CCard::HOVERSTATE_MOUSEOVER)
			{
				pCard->SetHovered(false);
				pSeat->m_Player.m_HoveredCard = -1;
			}
		}
		return false;
	};

	CDurakGame *pGame = m_vpGames[Game];
	bool CancelledTimer = false;
	int NumParticipants = pGame->NumParticipants();
	if (NumParticipants < MIN_DURAK_PLAYERS)
	{
		if (!pGame->m_Running && pGame->m_GameStartTick)
		{
			pGame->m_GameStartTick = 0;
			CancelledTimer = true;
		}

		// Reset stake when all players left the table and no round started
		if (pGame->NumDeployedStakes() == 0)
		{
			pGame->m_Stake = -1;
		}

		if (EndGame(Game))
		{
			// Will only trigger if game is running and everyone left
			return true;
		}
	}

	bool TimerUpGameStart = !pGame->m_Running && pGame->m_GameStartTick && pGame->m_GameStartTick <= Server()->Tick();
	if (TimerUpGameStart && !StartGame(Game))
	{
		// will only trigger when the timer is up and startgame fails, if no team is free
		return true;
	}

	bool DefenseOngoing = false;
	for (auto &pair : pGame->m_Attacks)
		if (pair.m_Defense.Valid())
		{
			DefenseOngoing = true;
			break;
		}

	for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
	{
		CDurakGame::SSeat *pSeat = &pGame->m_aSeats[i];
		int ClientID = pSeat->m_Player.m_ClientID;
		if (ClientID == -1)
			continue;

		CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
		CCharacter *pChr = pPlayer->GetCharacter();

		if (!pGame->m_Running)
		{
			if (!pChr || GameServer()->Collision()->GetMapIndex(pChr->GetPos()) != pSeat->m_MapIndex)
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
			else if (pGame->m_GameStartTick > Server()->Tick() && (pGame->m_GameStartTick - Server()->Tick() + 1) % Server()->TickSpeed() == 0)
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Players [%d/%d]\nStake [%d]\nGame start [%d]", NumParticipants, (int)MAX_DURAK_PLAYERS,
					pGame->m_Stake, (pGame->m_GameStartTick - Server()->Tick()) / Server()->TickSpeed() + 1);
				GameServer()->SendBroadcast(aBuf, ClientID);
			}
			continue;
		}

		if (TimerUpGameStart)
		{
			GameServer()->SendBroadcast("Game started!", ClientID);
		}

		// game logic
		if (!pChr)
			continue;
		
		if (DefenseOngoing && pGame->GetStateBySeat(i) != DURAK_PLAYERSTATE_NONE)
		{
			pChr->ForceSetPos(GameServer()->Collision()->GetPos(pSeat->m_MapIndex));
		}

		// Card sorting
		unsigned int NumCards = pSeat->m_Player.m_vHandCards.size();
		if (NumCards <= 0)
			continue;

		// Dynamically sort hand cards
		float Gap = 4.f;
		const float RequiredSpace = min(NumCards * (CCard::ms_CardSizeRadius.x*2 + Gap) - Gap, (CCard::ms_TableSizeRadius.x - 32.f) * 2);
		float PosX = -RequiredSpace / 2.f;
		if (NumCards > 1)
		{
			Gap = RequiredSpace / (NumCards - 1);
		}
		if (NumCards % 2 != 0)
		{
			PosX += CCard::ms_CardSizeRadius.x;
		}
		for (unsigned int c = 0; c < NumCards; c++)
		{
			CCard *pCard = &pSeat->m_Player.m_vHandCards[c];
			pCard->m_TableOffset.x = PosX;
			PosX += Gap;
		}

		// scrolling order is fucked up otherwise when moving mouse from left to right with too many cards
		vec2 CursorPos = pChr->GetCursorPos();
		bool Dragging = pChr->Input()->m_Fire & 1;
		if (pSeat->m_Player.m_LastCursorX < CursorPos.x)
		{
			for (unsigned int c = 0; c < NumCards; c++)
				if (HandleCardHover(Game, i, c, CursorPos, Dragging))
					break;
		}
		else
		{
			for (int c = (int)NumCards - 1; c >= 0; c--)
				if (HandleCardHover(Game, i, c, CursorPos, Dragging))
					break;
		}
		pSeat->m_Player.m_LastCursorX = CursorPos.x;
	}

	// Handle game logic
	if (!pGame->m_Running)
		return false;

	if (pGame->IsGameOver())
	{
		int DurakClientID = -1;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			if (pGame->m_aSeats[i].m_Player.m_ClientID != -1 && !pGame->m_aSeats[i].m_Player.m_vHandCards.empty())
			{
				DurakClientID = pGame->m_aSeats[i].m_Player.m_ClientID;
				break;
			}
		}

		if (DurakClientID != -1)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "'%s' is the Durák!", Server()->ClientName(DurakClientID));
			SendChatToParticipants(Game, aBuf);
		}
		else
		{
			SendChatToParticipants(Game, "Game over, no Durák found");
		}

		// Dont just end here immediately
		return EndGame(Game);
	}

	if (!pGame->m_NextMove)
	{
		pGame->m_NextMove = Server()->Tick() + Server()->TickSpeed() * 30;
	}

	if (pGame->m_NextMove <= Server()->Tick())
	{
		bool AllAttacksDefended = true;
		bool HasUndefendedAttacks = false;
		bool HasActiveAttacks = false;

		for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
		{
			if (pGame->m_Attacks[i].m_Offense.Valid())
			{
				HasActiveAttacks = true;
				if (!pGame->m_Attacks[i].m_Defense.Valid())
				{
					AllAttacksDefended = false;
					HasUndefendedAttacks = true;
				}
			}
		}

		if (HasActiveAttacks && AllAttacksDefended)
		{
			char aBuf[128];
			int DefenderID = pGame->m_aSeats[pGame->m_DefenderIndex].m_Player.m_ClientID;
			str_format(aBuf, sizeof(aBuf), "'%s' successfully defended", Server()->ClientName(DefenderID));
			SendChatToParticipants(Game, aBuf);

			pGame->NextRound(true);
		}
		else if (HasUndefendedAttacks)
		{
			// Defender takes all cards from table
			for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
			{
				if (pGame->m_Attacks[i].m_Offense.Valid())
				{
					CCard OffenseCard = pGame->m_Attacks[i].m_Offense;
					OffenseCard.m_TableOffset.y = CCard::ms_TableSizeRadius.y;
					pGame->m_aSeats[pGame->m_DefenderIndex].m_Player.m_vHandCards.push_back(OffenseCard);

					if (pGame->m_Attacks[i].m_Defense.Valid())
					{
						CCard DefenseCard = pGame->m_Attacks[i].m_Defense;
						DefenseCard.m_TableOffset.y = CCard::ms_TableSizeRadius.y;
						pGame->m_aSeats[pGame->m_DefenderIndex].m_Player.m_vHandCards.push_back(DefenseCard);
					}

					// Remove cards from table, they got picked up
					pGame->m_Attacks[i].m_Offense.Invalidate();
					pGame->m_Attacks[i].m_Defense.Invalidate();
				}
			}

			char aBuf[128];
			int DefenderID = pGame->m_aSeats[pGame->m_DefenderIndex].m_Player.m_ClientID;
			str_format(aBuf, sizeof(aBuf), "'%s' couldn't defend all attacks and has to take all cards", Server()->ClientName(DefenderID));
			SendChatToParticipants(Game, aBuf);

			// Sort hand cards after taking
			pGame->SortHand(pGame->m_aSeats[pGame->m_DefenderIndex].m_Player.m_vHandCards, pGame->m_Deck.GetTrumpSuit());

			pGame->NextRound();
		}
		else
		{
			char aBuf[128];
			int AttackerID = pGame->m_aSeats[pGame->m_InitialAttackerIndex].m_Player.m_ClientID;
			str_format(aBuf, sizeof(aBuf), "'%s' took too long and was skipped", Server()->ClientName(AttackerID));
			SendChatToParticipants(Game, aBuf);
			// Just skip this guy // Emulate successful defense, so that the next guy's turn is now and he is not skipped.
			pGame->NextRound(true);
		}
	}

	// Safely return and let the tick function know we are still alive
	return false;
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
	PrepareStaticCards(SnappingClient, Game, ClientID);
	if (Game < 0)
	{
		// render default fallback tabledesign
		return;
	}

	CDurakGame *pGame = m_vpGames[Game];
	CDurakGame::SSeat *pSeat = pGame->GetSeatByClient(ClientID); // Maybe put SnappingClient in here to avoid checking other cards in pause

	// Prepare snap ids..
	PrepareDurakSnap(SnappingClient, pGame, pSeat);

	// Hand cards
	if (pSeat)
	{
		for (unsigned int i = 0; i < pSeat->m_Player.m_vHandCards.size(); i++)
			SnapDurakCard(SnappingClient, pGame->m_TablePos, &pSeat->m_Player.m_vHandCards[i]);
	}

	// Attack cards
	for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
	{
		if (pGame->m_Attacks[i].m_Defense.Valid())
			SnapDurakCard(SnappingClient, pGame->m_TablePos, &pGame->m_Attacks[i].m_Defense);
		if (pGame->m_Attacks[i].m_Offense.Valid())
			SnapDurakCard(SnappingClient, pGame->m_TablePos, &pGame->m_Attacks[i].m_Offense);
	}

	// Static cards
	for (int i = 0; i < NUM_DURAK_STATIC_CARDS; i++)
	{
		CCard *pCard = &m_aStaticCards[i];
		if (pCard->m_Active)
			SnapDurakCard(SnappingClient, pGame->m_TablePos, pCard);
	}
}

void CDurak::PrepareStaticCards(int SnappingClient, int Game, int ClientID)
{
	bool InGame = Game >= 0 && m_vpGames[Game]->m_Running;
	CDurakGame *pGame = InGame ? m_vpGames[Game] : 0;
	for (int i = 0; i < NUM_DURAK_STATIC_CARDS; i++)
	{
		CCard *pCard = &m_aStaticCards[i];
		pCard->m_Active = false;
		switch (i)
		{
			case DURAK_TEXT_CARDS_STACK:
			{
				if (InGame && pGame->m_Deck.Size() > 1)
				{
					pCard->m_Active = true;
					pCard->Invalidate();
				}
			} break;
			case DURAK_TEXT_TRUMP_CARD:
			{
				if (InGame && !pGame->m_Deck.IsEmpty())
				{
					CCard *pTrumpCard = pGame->m_Deck.GetTrumpCard();
					pCard->m_Suit = pTrumpCard->m_Suit;
					pCard->m_Rank = pTrumpCard->m_Rank;
					pCard->m_Active = true;
				}
			} break;
			case DURAK_TEXT_KEYBOARD_CONTROL:
			{
				if (InGame)
				{
					pCard->m_Active = true;
					pCard->SetKeyboardControl(m_aKeyboardControl[ClientID]);
				}
			} break;
			case DURAK_TEXT_CURSOR_TOOLTIP:
			{
				if (InGame)
				{
					CDurakGame::SSeat *pSeat = pGame->GetSeatByClient(ClientID);
					int Tooltip = pSeat->m_Player.m_Tooltip;
					pCard->m_Active = Tooltip != CDurakGame::TOOLTIP_NONE;
				}
			} break;
		}
	}
}

void CDurak::PrepareDurakSnap(int SnappingClient, CDurakGame *pGame, CDurakGame::SSeat *pSeat)
{
	int NumStatic = 0;
	for (int i = 0; i < NUM_DURAK_STATIC_CARDS; i++)
		if (m_aStaticCards[i].m_Active)
			NumStatic++;

	int NumHand = 0;
	if (pSeat)
		NumHand = pSeat->m_Player.m_vHandCards.size();

	int NumNeeded = NumStatic + NumHand;
	int Diff = NumNeeded - m_aDurakNumReserved[SnappingClient];
	if (Diff != 0)
	{
		GameServer()->m_World.AddToNumReserved(SnappingClient, Diff);
		m_aDurakNumReserved[SnappingClient] += Diff;
	}

	std::map<CCard *, int> NewSnapMap;
	int SnapID = GameServer()->m_World.GetFirstDurakID(SnappingClient);

	// Hand cards
	if (pSeat)
	{
		for (unsigned int i = 0; i < pSeat->m_Player.m_vHandCards.size(); i++)
		{
			CCard *pCard = &pSeat->m_Player.m_vHandCards[i];
			pCard->m_SnapID = NewSnapMap[pCard] = SnapID--;
		}
	}

	// Attack cards
	for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
	{
		if (pGame->m_Attacks[i].m_Defense.Valid())
			pGame->m_Attacks[i].m_Defense.m_SnapID = NewSnapMap[&pGame->m_Attacks[i].m_Defense] = SnapID--;
		if (pGame->m_Attacks[i].m_Offense.Valid())
			pGame->m_Attacks[i].m_Offense.m_SnapID = NewSnapMap[&pGame->m_Attacks[i].m_Offense] = SnapID--;
	}

	// Static cards
	for (int i = 0; i < NUM_DURAK_STATIC_CARDS; i++)
	{
		CCard *pCard = &m_aStaticCards[i];
		if (pCard->m_Active)
		{
			pCard->m_SnapID = NewSnapMap[pCard] = SnapID--;
		}
	}

	// 0.7
	UpdateCardSnapMapping(SnappingClient, NewSnapMap);
}

void CDurak::UpdateCardSnapMapping(int SnappingClient, const std::map<CCard *, int> &NewMap)
{
	auto &OldMap = m_aLastSnapID[SnappingClient];
	for (auto &[pOldCard, OldID] : OldMap)
		if (NewMap.find(pOldCard) == NewMap.end())
			GameServer()->m_apPlayers[SnappingClient]->SendDisconnect(OldID);
	for (auto &[pCard, NewID] : NewMap)
	{
		auto it = OldMap.find(pCard);
		if (it == OldMap.end() || it->second != NewID/* || pCard->m_Updated*/)
		{
			if (it != OldMap.end())
			{
				GameServer()->m_apPlayers[SnappingClient]->SendDisconnect(it->second);
			}
			CNetMsg_Sv_ClientInfo NewClientInfoMsg;
			NewClientInfoMsg.m_ClientID = NewID;
			NewClientInfoMsg.m_Local = 0;
			NewClientInfoMsg.m_Team = TEAM_BLUE;
			NewClientInfoMsg.m_pName = GetCardSymbol(pCard->m_Suit, pCard->m_Rank);
			NewClientInfoMsg.m_pClan = "";
			NewClientInfoMsg.m_Country = -1;
			NewClientInfoMsg.m_Silent = 1;

			for (int p = 0; p < NUM_SKINPARTS; p++)
			{
				NewClientInfoMsg.m_apSkinPartNames[p] = "default";
				NewClientInfoMsg.m_aUseCustomColors[p] = 1;
				NewClientInfoMsg.m_aSkinPartColors[p] = 255;
			}

			Server()->SendPackMsg(&NewClientInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD|MSGFLAG_NOTRANSLATE, SnappingClient);
		}
	}
	OldMap = NewMap;
}

void CDurak::SnapDurakCard(int SnappingClient, vec2 TablePos, CCard *pCard)
{
	int ID = pCard->m_SnapID;
	if (!Server()->IsSevendown(SnappingClient))
	{
		// 0.7 uses UpdateCardSnapMapping()
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
		StrToInts(&pClientInfo[0], 4, GetCardSymbol(pCard->m_Suit, pCard->m_Rank));
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

	vec2 Pos = TablePos + pCard->m_TableOffset;
	pCharacter->m_X = Pos.x;
	pCharacter->m_Y = Pos.y;
	pCharacter->m_Weapon = WEAPON_GUN;
	if (Pos.x > TablePos.x)
	{
		pCharacter->m_Angle = 804;
	}

	if(Server()->IsSevendown(SnappingClient))
	{
		int PlayerFlags = 0;
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
