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
	static const vec2 ms_AttackAreaCenterOffset;

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
	void Invalidate() { m_Suit = m_Rank = -1; }

	static bool InAttackArea(vec2 Target) // Target - m_TablePos
	{
		Target.y += DURAK_CARD_NAME_OFFSET;
		return (ms_AttackAreaCenterOffset.x - ms_AttackAreaRadius.x < Target.x && ms_AttackAreaCenterOffset.x + ms_AttackAreaRadius.x > Target.x
			&& ms_AttackAreaCenterOffset.y - ms_AttackAreaRadius.y < Target.y && ms_AttackAreaCenterOffset.y + ms_AttackAreaRadius.y > Target.y);
	}

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

	void SetKeyboardControl(bool Activated)
	{
		m_Suit = Activated ? -2 : -3;
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

	CCard *GetTrumpCard() { return m_vDeck.size() ? &m_vDeck[0] : 0; }
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
		float CardSize = CCard::ms_CardSizeRadius.x + 4.f; // Gap
		float PosX = CCard::ms_AttackAreaCenterOffset.x - (MAX_DURAK_ATTACKS / 2) * CardSize;
		for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
		{
			m_Attacks[i].m_Offense.m_TableOffset.y = CCard::ms_AttackAreaCenterOffset.y;
			m_Attacks[i].m_Offense.m_TableOffset.x = PosX;
			m_Attacks[i].m_Defense.m_TableOffset.y = CCard::ms_AttackAreaCenterOffset.y + CCard::ms_CardSizeRadius.y;
			m_Attacks[i].m_Defense.m_TableOffset.x = PosX;
			PosX += CardSize;
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

	void DrawCardsAfterRound()
	{
		int Start = m_InitialAttackerIndex;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			int Index = (Start + i) % MAX_DURAK_PLAYERS;
			auto &Player = m_aSeats[Index].m_Player;
			if (Player.m_ClientID != -1)
			{
				while ((int)Player.m_vHandCards.size() < NUM_DURAK_INITIAL_HAND_CARDS && !m_Deck.IsEmpty())
				{
					CCard Card = m_Deck.DrawCard();
					Card.m_TableOffset.y = CCard::ms_TableSizeRadius.y;
					Player.m_vHandCards.push_back(Card);
				}
				SortHand(Player.m_vHandCards, m_Deck.GetTrumpSuit());
			}
		}
	}

	bool IsGameOver()
	{
		int PlayersLeft = 0;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			if (m_aSeats[i].m_Player.m_ClientID != -1 && !m_aSeats[i].m_Player.m_vHandCards.empty())
				PlayersLeft++;
		}
		return PlayersLeft <= 1 && m_Deck.IsEmpty();
	}

	bool TryAttack(int Seat, CCard *pCard)
	{
		if (Seat != m_InitialAttackerIndex || !pCard->Valid())
			return false;

		int Used = 0;
		for (auto &pair : m_Attacks)
			if (pair.m_Offense.Valid())
				Used++;

		if (Used >= MAX_DURAK_ATTACKS)
			return false;

		// If not first attack, card has to match existing ranks on table
		if (Used > 0)
		{
			bool Match = false;
			for (auto &pair : m_Attacks)
			{
				if ((pair.m_Offense.Valid() && pair.m_Offense.m_Rank == pCard->m_Rank) ||
					(pair.m_Defense.Valid() && pair.m_Defense.m_Rank == pCard->m_Rank))
				{
					Match = true;
					break;
				}
			}
			if (!Match)
				return false;
		}

		// Assign card to attack slot
		m_Attacks[Used].m_Offense.m_Suit = pCard->m_Suit;
		m_Attacks[Used].m_Offense.m_Rank = pCard->m_Rank;

		// Remove card from hand
		auto &vHand = m_aSeats[Seat].m_Player.m_vHandCards;
		vHand.erase(std::remove_if(vHand.begin(), vHand.end(),
			[&](const CCard &c) { return c.m_Suit == pCard->m_Suit && c.m_Rank == pCard->m_Rank; }),
			vHand.end());

		return true;
	}

	bool TryDefend(int Seat, int Attack, CCard *pCard)
	{
		if (Seat != m_DefenderIndex || !pCard->Valid())
			return false;

		if (Attack < 0 || Attack >= MAX_DURAK_ATTACKS)
			return false;

		CCard &AttackCard = m_Attacks[Attack].m_Offense;
		if (!AttackCard.Valid() || m_Attacks[Attack].m_Defense.Valid())
			return false;

		if (!pCard->IsStrongerThan(AttackCard, m_Deck.GetTrumpSuit()))
			return false;

		m_Attacks[Attack].m_Defense.m_Suit = pCard->m_Suit;
		m_Attacks[Attack].m_Defense.m_Rank = pCard->m_Rank;

		auto &vHand = m_aSeats[Seat].m_Player.m_vHandCards;
		vHand.erase(std::remove_if(vHand.begin(), vHand.end(),
			[&](const CCard &c) { return c.m_Suit == pCard->m_Suit && c.m_Rank == pCard->m_Rank; }),
			vHand.end());

		return true;
	}

	bool TryPush(int Seat, CCard *pCard)
	{
		if (!pCard->Valid())
			return false;

		int TargetRank = -1;
		for (auto &pair : m_Attacks)
		{
			if (pair.m_Offense.Valid())
			{
				if (pair.m_Defense.Valid()) // No defense has happened already
					return false;

				if (TargetRank == -1)
					TargetRank = pair.m_Offense.m_Rank;
				else if (pair.m_Offense.m_Rank != TargetRank)
					return false; // Can only push on same ranks
			}
		}

		if (TargetRank == -1 || pCard->m_Rank != TargetRank)
			return false;

		// Find first free attack slot
		for (auto &pair : m_Attacks)
		{
			if (!pair.m_Offense.Valid())
			{
				pair.m_Offense.m_Suit = pCard->m_Suit;
				pair.m_Offense.m_Rank = pCard->m_Rank;

				auto &vHand = m_aSeats[Seat].m_Player.m_vHandCards;
				vHand.erase(std::remove_if(vHand.begin(), vHand.end(),
					[&](const CCard &c) { return c.m_Suit == pCard->m_Suit && c.m_Rank == pCard->m_Rank; }),
					vHand.end());

				return true;
			}
		}

		return false;
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

	struct
	{
		CCard m_Offense;
		CCard m_Defense;
	} m_Attacks[MAX_DURAK_ATTACKS];
};

class CDurak : public CMinigame
{
	enum
	{
		DURAK_TEXT_CARDS_STACK = 0,
		DURAK_TEXT_TRUMP_CARD,
		DURAK_TEXT_KEYBOARD_CONTROL,
		DURAK_TEXT_CURSOR_TOOLTIP,
		NUM_DURAK_STATIC_CARDS
	};

	CCard m_aStaticCards[NUM_DURAK_STATIC_CARDS];
	void PrepareStaticCards(int SnappingClient, int Game, int ClientID);
	std::map<int, std::map<CCard*, int>> m_aLastSnapID; // [ClientID][Card] = SnapID
	void PrepareDurakSnap(int SnappingClient, CDurakGame *pGame, CDurakGame::SSeat *pSeat);
	void UpdateCardSnapMapping(int SnappingClient, const std::map<CCard *, int> &NewMap);
	void SnapDurakCard(int SnappingClient, vec2 TablePos, CCard *pCard);

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
	void OnPlayerLeave(int ClientID, bool Disconnect = false);
};
#endif // GAME_SERVER_MINIGAMES_DURAK_H
