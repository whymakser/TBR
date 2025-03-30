// made by fokkonaut

#ifndef GAME_SERVER_MINIGAMES_DURAK_H
#define GAME_SERVER_MINIGAMES_DURAK_H

#include "minigame.h"
#include <vector>
#include <algorithm>
#include <random>

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
	CDeck m_Deck;
};

class CCard
{
public:
	CCard(int Suit, int Rank) : m_Suit(Suit), m_Rank(Rank)
	{
		m_TableOffset = vec2(-1, -1);
	}

	int m_Suit;
	int m_Rank;
	vec2 m_TableOffset;

	bool MouseOver(vec2 Target) // Target - m_TablePos
	{
		vec2 CardPos = vec2(m_TableOffset.x, m_TableOffset.y - 48.f);
		return (CardPos.x - 16.f < Target.x && CardPos.x + 16.f > Target.x && CardPos.y - 16.f < Target.y && CardPos.y + 16.f > Target.y);
	}

	bool IsStrongerThan(const CCard &Other, int TrumpSuit) const
    {
        if (m_Suit == TrumpSuit && Other.m_Suit != TrumpSuit)
            return true;
        if (m_Suit != TrumpSuit && Other.m_Suit == TrumpSuit)
            return false;
        return m_Suit == Other.m_Suit && m_Rank > Other.m_Rank;
    }
};

class CDeck
{
	std::vector<CCard> m_vDeck;
	int m_TrumpSuit;

public:
    CDeck()
	{
        for (int suit = 0; suit < 4; suit++)
            for (int rank = 6; rank <= 14; rank++)
                m_vDeck.emplace_back(suit, rank);
    }

    void Shuffle() {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_vDeck.begin(), m_vDeck.end(), g);
    }

	CCard DrawCard()
    {
        if (IsEmpty()) // Invalid card if deck is empty
			return CCard(-1, -1);
        CCard topCard = m_vDeck.back();
        m_vDeck.pop_back();
        return topCard;
    }

    bool IsEmpty() const { return m_vDeck.empty(); }
    int Size() const { return m_vDeck.size(); }

	void SetTrumpSuit(int Suit) { m_TrumpSuit = Suit; }
    int GetTrumpSuit() const { return m_TrumpSuit; }
};

class CDurak : public CMinigame
{
	/*enum
	{
		// max 3 cards per name
		DURAK_TEXT_STACK_AND_TRUMP = 0,
		DURAK_TEXT_DEFENSE_1,
		DURAK_TEXT_DEFENSE_2,
		DURAK_TEXT_OFFENSE_1,
		DURAK_TEXT_OFFENSE_2,
		DURAK_TEXT_CURSOR_TOOLTIP,
		NUM_DURAK_FAKE_TEES
	};

	CCard m_aStaticCards[NUM_DURAK_FAKE_TEES];*/
	void SnapFakeTee(int SnappingClient, int ID, vec2 Pos, const char *pName);

	int64 m_aLastSeatOccupiedMsg[MAX_CLIENTS];
	bool m_aUpdatedPassive[MAX_CLIENTS];
	bool m_aInDurakGame[MAX_CLIENTS];

	std::vector<CDurakGame *> m_vpGames;
	bool UpdateGame(int Game);
	bool StartGame(int Game);
	bool EndGame(int Game);

	const char *GetCardSymbol(int Suit, int Rank)
	{
		if (Suit == -1 && Rank == -1) {
			static const char *pBackCard = "🂠";
			return pBackCard;
		}
		if (Suit < 0 || Suit > 3 || Rank < 6 || Rank > 14) {
			return "??";
		}
		static const char *aapCards[4][9] = {
			{"🂦", "🂧", "🂨", "🂩", "🂪", "🂫", "🂭", "🂬", "🂡"}, // Spades
			{"🂶", "🂷", "🂸", "🂹", "🂺", "🂻", "🂽", "🂼", "🂱"}, // Hearts
			{"🃆", "🃇", "🃈", "🃉", "🃊", "🃋", "🃍", "🃌", "🃁"}, // Diamonds
			{"🃖", "🃗", "🃘", "🃙", "🃚", "🃛", "🃝", "🃜", "🃑"}  // Clubs
		};
		return aapCards[Suit][Rank - 6];
	}

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
