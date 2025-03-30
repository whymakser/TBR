// made by fokkonaut

#ifndef GAME_SERVER_MINIGAMES_DURAK_H
#define GAME_SERVER_MINIGAMES_DURAK_H

#include "minigame.h"
#include <vector>

enum
{
	MIN_DURAK_PLAYERS = 2,
	MAX_DURAK_PLAYERS = 6,
	MAX_DURAK_GAMES = VANILLA_MAX_CLIENTS-1,
};

class CDurakGame
{
public:
	~CDurakGame() {} // move players back out of the game, or call endgame everywhere, or idk yet
	CDurakGame(CDurakGame *pDurakBase) : CDurakGame(pDurakBase->m_Number)
	{
		m_TablePos = pDurakBase->m_TablePos;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			m_aSeats[i].m_MapIndex = pDurakBase->m_aSeats[i].m_MapIndex;
		}
	}
	CDurakGame(int Number)
	{
		m_Number = Number;
		m_TablePos = vec2(-1, -1);
		m_Stake = -1;
		m_GameStartTick = 0;
		m_Running = false;
		m_InitialAttackerIndex = -1;
		m_DefenderIndex = -1;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			m_aSeats[i].m_MapIndex = -1;
			m_aSeats[i].m_Player.Reset();
		}
	}

	int NumDeployedStakes()
	{
		int Num = 0;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
			if (m_aSeats[i].m_Player.m_ClientID != -1 && m_aSeats[i].m_Player.m_Stake >= 0)
				Num++;
		return Num;
	}

	int NumParticipants()
	{
		int Num = 0;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
			if (m_aSeats[i].m_Player.m_ClientID != -1 && m_Stake != -1 && m_aSeats[i].m_Player.m_Stake == m_Stake)
				Num++;
		return Num;
	}

	//void OnPlayerLeave(int Seat) {}

	struct SSeat
	{
		int m_MapIndex;
		struct SPlayer
		{
			void Reset()
			{
				m_ClientID = -1;
				m_Stake = -1;
			}
			int m_ClientID;
			int64 m_Stake;
		} m_Player;
	};
	SSeat m_aSeats[MAX_DURAK_PLAYERS];
	SSeat *GetSeatByClient(int ClientID)
	{
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
			if (m_aSeats[i].m_Player.m_ClientID == ClientID)
				return &m_aSeats[i];
		return 0;
	}

	int m_Number;
	vec2 m_TablePos;
	int64 m_Stake;
	int64 m_GameStartTick;
	bool m_Running;
	int m_InitialAttackerIndex;
	int m_DefenderIndex;
};

class CCard
{
public:
	char m_aName[MAX_NAME_LENGTH];
	vec2 m_TableOffset;
};

class CDurak : public CMinigame
{
	enum
	{
		// max 3 cards per name
		DURAK_TEXT_STACK_AND_TRUMP = 0,
		DURAK_TEXT_DEFENSE_1,
		DURAK_TEXT_DEFENSE_2,
		DURAK_TEXT_OFFENSE_1,
		DURAK_TEXT_OFFENSE_2,
		DURAK_TEXT_HAND_CARDS_1,
		DURAK_TEXT_HAND_CARDS_2,
		DURAK_TEXT_CURSOR_TOOLTIP,
		NUM_DURAK_FAKE_TEES
	};

	CCard m_aStaticCards[NUM_DURAK_FAKE_TEES];
	void SnapFakeTee(int SnappingClient, int ID, vec2 Pos, const char *pName);

	std::vector<CDurakGame *> m_vpGames;

	int64 m_aLastSeatOccupiedMsg[MAX_CLIENTS];
	bool m_aUpdatedPassive[MAX_CLIENTS];
	bool m_aInDurakGame[MAX_CLIENTS];

	bool UpdateGame(int Game);
	bool StartGame(int Game);
	bool EndGame(int Game);

public:
	CDurak(CGameContext *pGameServer, int Type);
	virtual ~CDurak();

	virtual void Tick();
	virtual void Snap(int SnappingClient);

	bool InDurakGame(int ClientID) { return m_aInDurakGame[ClientID]; }
	bool OnDropMoney(int ClientID, int Amount);

	int GetGameByNumber(int Number, bool AllowRunning = false);
	int GetGameByClient(int ClientID);

	void AddMapTableTile(int Number, vec2 Pos);
	void AddMapSeatTile(int Number, int MapIndex, int SeatIndex);

	void OnCharacterSeat(int ClientID, int Number, int SeatIndex);
	bool TryEnterBetStake(int ClientID, const char *pMessage);

	void OnPlayerLeave(int ClientID);

	bool OnInput(int ClientID, CNetObj_PlayerInput *pNewInput);

	void SendChatToDeployedStakePlayers(int Game, const char *pMessage, int NotThisID);
	void SendChatToParticipants(int Game, const char *pMessage);
	void UpdatePassive(int ClientID, int Seconds);
};
#endif // GAME_SERVER_MINIGAMES_DURAK_H
