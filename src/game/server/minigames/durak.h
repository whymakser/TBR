// made by fokkonaut

#ifndef GAME_SERVER_MINIGAMES_DURAK_H
#define GAME_SERVER_MINIGAMES_DURAK_H

#include "minigame.h"
#include <vector>
#include <map>
#include <algorithm>
#include <random>

enum
{
	MIN_DURAK_PLAYERS = 2,
	MAX_DURAK_PLAYERS = 6,
	NUM_DURAK_INITIAL_HAND_CARDS = 6,
	MAX_DURAK_ATTACKS = 6,
	MAX_DURAK_ATTACKS_BEFORE_FIRST_TEE = 5,
	MAX_DURAK_GAMES = VANILLA_MAX_CLIENTS-1,
	DURAK_CARD_NAME_OFFSET = 48,
};

#define DURAK_CARD_HOVER_OFFSET (CCard::ms_CardSizeRadius.y / 2)

class CCard
{
public:
	static const vec2 ms_CardSizeRadius;
	static const vec2 ms_TableSizeRadius;
	static const vec2 ms_AttackAreaRadius;

	CCard() : CCard(-1, -1) {}
	CCard(int Suit, int Rank) : m_Suit(Suit), m_Rank(Rank)
	{
		m_TableOffset = vec2(0, 0);
		m_HoverState = HOVERSTATE_NONE;
		m_SnapID = -1;
		m_Active = false;
	}

	int m_Suit;
	int m_Rank;
	vec2 m_TableOffset;
	enum
	{
		HOVERSTATE_NONE,
		HOVERSTATE_MOUSEOVER,
		HOVERSTATE_DRAGGING,
	};
	int m_HoverState;
	int m_SnapID;
	bool m_Active;

	bool Valid() { return m_Suit != -1 && m_Rank != -1; }

	bool MouseOver(vec2 Target) // Target - m_TablePos
	{
		vec2 CardPos = vec2(m_TableOffset.x, m_TableOffset.y-DURAK_CARD_NAME_OFFSET);
		float HighY = ms_CardSizeRadius.y;
		if (m_HoverState == HOVERSTATE_MOUSEOVER) // Avoid flickering
			HighY += DURAK_CARD_HOVER_OFFSET;
		return (CardPos.x - ms_CardSizeRadius.x < Target.x && CardPos.x + ms_CardSizeRadius.x > Target.x && CardPos.y - ms_CardSizeRadius.y < Target.y && CardPos.y + HighY > Target.y);
	}

	void SetHovered(bool Set)
	{
		if (Set)
		{
			m_TableOffset.y = CCard::ms_TableSizeRadius.y - DURAK_CARD_HOVER_OFFSET;
			m_HoverState = HOVERSTATE_MOUSEOVER;
		}
		else
		{
			m_TableOffset.y = CCard::ms_TableSizeRadius.y;
			m_HoverState = HOVERSTATE_NONE;
		}
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

    void Shuffle()
	{
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(m_vDeck.begin(), m_vDeck.end(), g);
    }

	CCard DrawCard()
    {
        if (IsEmpty()) // Invalid card if deck is empty
			return CCard(-1, -1);
        CCard TopCard = m_vDeck.back();
        m_vDeck.pop_back();
        return TopCard;
    }

	void PushFrontTrumpCard(CCard TrumpCard)
	{
		m_vDeck.insert(m_vDeck.begin(), TrumpCard);
	}

    bool IsEmpty() const { return m_vDeck.empty(); }
    int Size() const { return m_vDeck.size(); }

	void SetTrumpSuit(int Suit) { m_TrumpSuit = Suit; }
    int GetTrumpSuit() const { return m_TrumpSuit; }
};

class CDurakGame
{
public:
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

	void DealHandCards()
	{
		for (int i = 0; i < NUM_DURAK_INITIAL_HAND_CARDS; i++)
		{
			for (int s = 0; s < MAX_DURAK_PLAYERS; s++)
			{
				if (m_aSeats[s].m_Player.m_ClientID != -1)
				{
					CCard Card = m_Deck.DrawCard();
					Card.m_TableOffset.y = CCard::ms_TableSizeRadius.y;
					m_aSeats[s].m_Player.m_vHandCards.push_back(Card);
					if (m_Deck.IsEmpty())
					{
						m_Deck.SetTrumpSuit(Card.m_Suit);
						break; // Should be last card anyways, but better be sure xd
					}
				}
			}
		}

		if (!m_Deck.IsEmpty())
		{
			CCard Card = m_Deck.DrawCard();
			m_Deck.SetTrumpSuit(Card.m_Suit);
			m_Deck.PushFrontTrumpCard(Card);
		}

		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			SortHand(m_aSeats[i].m_Player.m_vHandCards, m_Deck.GetTrumpSuit());
		}
	}

	void SortHand(std::vector<CCard> &vHandCards, int TrumpSuit) {
		// Card count per suit
		int aSuitCount[4] = { 0 };
		for (const CCard &Card : vHandCards)
			aSuitCount[Card.m_Suit]++;

		// Highest rank within each suit
		int aSuitMaxRank[4] = { 0 };
		for (const CCard &Card : vHandCards)
			aSuitMaxRank[Card.m_Suit] = max(aSuitMaxRank[Card.m_Suit], Card.m_Rank);

		std::sort(vHandCards.begin(), vHandCards.end(), [&](const CCard &a, const CCard &b) {
			// Trump cards first
			if (a.m_Suit == TrumpSuit && b.m_Suit != TrumpSuit)
				return true;
			if (a.m_Suit != TrumpSuit && b.m_Suit == TrumpSuit)
				return false;

			// Both cards same suit, sort by rank (higher = left)
			if (a.m_Suit == b.m_Suit)
				return a.m_Rank > b.m_Rank;

			// Sort by count of cards per suit (more = left)
			if (aSuitCount[a.m_Suit] != aSuitCount[b.m_Suit])
				return aSuitCount[a.m_Suit] > aSuitCount[b.m_Suit];

			// If everything is same, sort by highest rank of suit
			return aSuitMaxRank[a.m_Suit] > aSuitMaxRank[b.m_Suit];
		});
	}

	struct SSeat
	{
		int m_MapIndex;
		struct SPlayer
		{
			void Reset()
			{
				m_ClientID = -1;
				m_Stake = -1;
				m_vHandCards.clear();
				m_HoveredCard = -1;
				m_LastCursorX = -1.f;
			}
			int m_ClientID;
			int64 m_Stake;
			std::vector<CCard> m_vHandCards;
			int m_HoveredCard;
			float m_LastCursorX;
		} m_Player;
	} m_aSeats[MAX_DURAK_PLAYERS];
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

class CDurak : public CMinigame
{
	enum
	{
		DURAK_TEXT_CURSOR_TOOLTIP = 0,
		DURAK_TEXT_KEYBOARD_CONTROL,
		DURAK_TEXT_STACK_AND_TRUMP,
		DURAK_TEXT_OFFSET_DEFENSE,
		DURAK_TEXT_DEFENSE_1 = DURAK_TEXT_OFFSET_DEFENSE,
		DURAK_TEXT_DEFENSE_2,
		DURAK_TEXT_DEFENSE_3,
		DURAK_TEXT_DEFENSE_4,
		DURAK_TEXT_DEFENSE_5,
		DURAK_TEXT_DEFENSE_6,
		DURAK_TEXT_OFFSET_OFFENSE,
		DURAK_TEXT_OFFENSE_1 = DURAK_TEXT_OFFSET_OFFENSE,
		DURAK_TEXT_OFFENSE_2,
		DURAK_TEXT_OFFENSE_3,
		DURAK_TEXT_OFFENSE_4,
		DURAK_TEXT_OFFENSE_5,
		DURAK_TEXT_OFFENSE_6,
		NUM_DURAK_STATIC_CARDS
	};
	CCard m_aStaticCards[NUM_DURAK_STATIC_CARDS];
	void SnapDurakCard(int SnappingClient, int Game, CCard *pCard);
	std::map<int, std::map<CCard*, int>> m_aLastSnapID; // [ClientID][Card] = SnapID
	void PrepareDurakSnap(int SnappingClient, CDurakGame::SSeat *pSeat);
	void UpdateCardSnapMapping(int SnappingClient, const std::map<CCard *, int> &NewMap);

	int m_aDurakNumReserved[MAX_CLIENTS];
	int64 m_aLastSeatOccupiedMsg[MAX_CLIENTS];
	bool m_aUpdatedPassive[MAX_CLIENTS];
	bool m_aInDurakGame[MAX_CLIENTS];
	bool m_aKeyboardControl[MAX_CLIENTS];
	struct
	{
		int m_Direction = 0;
		int m_Jump = 0;
		int m_TargetX = 0;
		int m_TargetY = 0;
	} m_aLastInput[MAX_CLIENTS];

	std::vector<CDurakGame *> m_vpGames;
	bool UpdateGame(int Game);
	bool StartGame(int Game);
	bool EndGame(int Game);

	void SendChatToDeployedStakePlayers(int Game, const char *pMessage, int NotThisID);
	void SendChatToParticipants(int Game, const char *pMessage);

	const char *GetCardSymbol(int Suit, int Rank);
	void UpdatePassive(int ClientID, int Seconds);

	CDurakGame *GetOrAddGame(int Number);

public:
	CDurak(CGameContext *pGameServer, int Type);
	virtual ~CDurak();

	virtual void Tick();
	virtual void Snap(int SnappingClient);

	void AddMapTableTile(int Number, vec2 Pos);
	void AddMapSeatTile(int Number, int MapIndex, int SeatIndex);

	int GetGameByNumber(int Number, bool AllowRunning = false);
	int GetGameByClient(int ClientID);

	bool InDurakGame(int ClientID) { return ClientID >= 0 && m_aInDurakGame[ClientID]; }
	bool ActivelyPlaying(int ClientID);
	bool OnDropMoney(int ClientID, int Amount);

	void OnCharacterSeat(int ClientID, int Number, int SeatIndex);
	bool TryEnterBetStake(int ClientID, const char *pMessage);
	void OnInput(class CCharacter *pCharacter, CNetObj_PlayerInput *pNewInput);
	void OnPlayerLeave(int ClientID);
};
#endif // GAME_SERVER_MINIGAMES_DURAK_H
