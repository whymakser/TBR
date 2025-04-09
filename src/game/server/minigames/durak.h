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

enum
{
	DURAK_PLAYERSTATE_NONE,
	DURAK_PLAYERSTATE_ATTACK,
	DURAK_PLAYERSTATE_DEFEND
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

	enum EIndicatorSuit
	{
		IND_KEYBOARD_ON = -2,
		IND_KEYBOARD_OFF = -3,
		IND_END_MOVE_BUTTON = -4,
		IND_TOOLTIP_ATTACK = -5,
		IND_TOOLTIP_DEFEND = -6,
		IND_TOOLTIP_PUSH = -7,
		IND_TOOLTIP_END_MOVE = -8,
		IND_TOOLTIP_TAKE_CARDS = -9,
	};
	enum
	{
		TOOLTIP_NONE = -1,
		TOOLTIP_ATTACK,
		TOOLTIP_DEFEND,
		TOOLTIP_PUSH,
		TOOLTIP_END_MOVE,
		TOOLTIP_TAKE_CARDS,
		NUM_TOOLTIPS
	};
	void SetInd(EIndicatorSuit IndSuit)
	{
		m_Suit = (int)IndSuit;
	}
	void SetTooltip(int Tooltip)
	{
		// Don't pass TOOLTIP_NONE(0), will be handled at a different place xd
		m_Suit = IND_END_MOVE_BUTTON - 1 - Tooltip;
		m_TableOffset = vec2(-40.f, -40.f);
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
			return CCard();
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
		m_AttackerIndex = -1;
		m_DefenderIndex = -1;
		m_RoundCount = 0;
		m_NextMove = 0;
		m_GameOverTick = 0;
		m_LeftPlayersStake = 0;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			m_aSeats[i].m_ID = i;
			m_aSeats[i].m_MapIndex = -1;
			m_aSeats[i].m_Player.Reset();
		}
		float CardSize = CCard::ms_CardSizeRadius.x * 2 + 4.f; // Gap
		float PosX = CCard::ms_AttackAreaCenterOffset.x - (MAX_DURAK_ATTACKS / 2) * CardSize;
		for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
		{
			m_Attacks[i].m_Offense.m_TableOffset = vec2(PosX, CCard::ms_AttackAreaCenterOffset.y);
			m_Attacks[i].m_Defense.m_TableOffset = vec2(PosX - CCard::ms_CardSizeRadius.x / 3.f, CCard::ms_AttackAreaCenterOffset.y + CCard::ms_CardSizeRadius.y * 1.25f);
			PosX += CardSize;
		}
	}

	struct SSeat
	{
		int m_ID;
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
				m_Tooltip = CCard::TOOLTIP_NONE;
				m_LastTooltip = CCard::TOOLTIP_NONE;
				m_KeyboardControl = false;
				m_LastKeyboardControl = false;
				m_LastCursorMove = 0;
			}
			struct
			{
				int m_Direction = 0;
				int m_Jump = 0;
				int m_TargetX = 0;
				int m_TargetY = 0;
			} m_LastInput;
			int m_ClientID;
			int64 m_Stake;
			std::vector<CCard> m_vHandCards;
			int m_HoveredCard;
			float m_LastCursorX;
			int m_Tooltip;
			int m_LastTooltip;
			bool m_KeyboardControl;
			bool m_LastKeyboardControl;
			int64 m_LastCursorMove;
		} m_Player;
	} m_aSeats[MAX_DURAK_PLAYERS];

	struct SAttack
	{
		CCard m_Offense;
		CCard m_Defense;
	} m_Attacks[MAX_DURAK_ATTACKS];

	int m_Number;
	vec2 m_TablePos;
	int64 m_Stake;
	int64 m_GameStartTick;
	bool m_Running;
	int m_InitialAttackerIndex;
	int m_AttackerIndex;
	int m_DefenderIndex;
	CDeck m_Deck;
	int m_RoundCount;
	int64 m_NextMove;
	std::vector<int> m_vWinners;
	int64 m_GameOverTick;
	int64 m_LeftPlayersStake;

	SSeat *GetSeatByClient(int ClientID)
	{
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
			if (m_aSeats[i].m_Player.m_ClientID == ClientID)
				return &m_aSeats[i];
		return 0;
	}

	int GetStateBySeat(int SeatIndex)
	{
		if (SeatIndex == m_AttackerIndex || SeatIndex == GetNextPlayer(m_DefenderIndex))
			return DURAK_PLAYERSTATE_ATTACK;
		if (SeatIndex == m_DefenderIndex)
			return DURAK_PLAYERSTATE_DEFEND;
		return DURAK_PLAYERSTATE_NONE;
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
			if (m_aSeats[i].m_Player.m_ClientID != -1)
				if (m_Stake != -1 && m_aSeats[i].m_Player.m_Stake == m_Stake)
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

	int GetNextPlayer(int CurrentIndex)
	{
		for (int i = 1; i < MAX_DURAK_PLAYERS; i++)
		{
			int NextIndex = (CurrentIndex + i) % MAX_DURAK_PLAYERS;
			if (m_aSeats[NextIndex].m_Player.m_ClientID != -1)
				return NextIndex;
		}
		return -1;
	}

	bool ProcessNextMove(int CurrentTick)
	{
		if (!m_NextMove)
			m_NextMove = CurrentTick + SERVER_TICK_SPEED * 60;
		return m_NextMove <= CurrentTick;
	}

	int NextRound(bool SuccessfulDefense = false)
	{
		for (int i = 0; i < MAX_DURAK_ATTACKS; i++)
		{
			m_Attacks[i].m_Offense.Invalidate();
			m_Attacks[i].m_Defense.Invalidate();
		}

		int Ret = -1;
		if (m_RoundCount == 0)
		{
			int LowestTrumpPlayerIndex = -1;
			int LowestTrumpRank = 15;

			for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
			{
				if (m_aSeats[i].m_Player.m_ClientID == -1)
					continue;

				for (auto &Card : m_aSeats[i].m_Player.m_vHandCards)
				{
					if (Card.m_Suit == m_Deck.GetTrumpSuit() && Card.m_Rank < LowestTrumpRank)
					{
						LowestTrumpRank = Card.m_Rank;
						LowestTrumpPlayerIndex = i;
					}
				}
			}

			// In case no trump was dealt, just begin somewhere
			if (LowestTrumpPlayerIndex == -1)
			{
				for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
				{
					if (m_aSeats[i].m_Player.m_ClientID != -1)
					{
						LowestTrumpPlayerIndex = i;
						break;
					}
				}
			}
			else
			{
				Ret = LowestTrumpRank;
			}
			m_AttackerIndex = m_InitialAttackerIndex = LowestTrumpPlayerIndex;
		}
		else
		{
			// Do before determining next attacker / defender to keep the order
			DrawCardsAfterRound();

			if (SuccessfulDefense)
			{
				// If successfully defended, defender turns to attacker
				m_AttackerIndex = m_InitialAttackerIndex = m_DefenderIndex;
			}
			else
			{
				// Otherwise skip us, next player starts attacking
				m_AttackerIndex = m_InitialAttackerIndex = GetNextPlayer(m_DefenderIndex);
			}
		}
		m_DefenderIndex = GetNextPlayer(m_InitialAttackerIndex);
		m_RoundCount++;
		m_NextMove = 0;
		return Ret;
	}

	bool IsDefenseOngoing()
	{
		for (auto &pair : m_Attacks)
			if (pair.m_Defense.Valid())
				return true;
		return false;
	}

	void DrawCardsAfterRound()
	{
		std::vector<int> vDrawOrder;
		vDrawOrder.push_back(m_InitialAttackerIndex);
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
			if (i != m_InitialAttackerIndex && i != m_DefenderIndex)
				vDrawOrder.push_back(i);
		vDrawOrder.push_back(m_DefenderIndex);

		for (unsigned int i = 0; i < vDrawOrder.size(); i++)
		{
			int Index = vDrawOrder[i];
			if (m_aSeats[Index].m_Player.m_ClientID != -1)
			{
				while ((int)m_aSeats[Index].m_Player.m_vHandCards.size() < NUM_DURAK_INITIAL_HAND_CARDS)
				{
					if (!m_Deck.IsEmpty())
					{
						CCard Card = m_Deck.DrawCard();
						Card.m_TableOffset.y = CCard::ms_TableSizeRadius.y;
						m_aSeats[Index].m_Player.m_vHandCards.push_back(Card);
					}
					else
					{
						if (m_aSeats[Index].m_Player.m_vHandCards.empty())
						{
							m_vWinners.push_back(Index);
						}
						break;
					}
				}
				SortHand(m_aSeats[Index].m_Player.m_vHandCards, m_Deck.GetTrumpSuit());
			}
		}
	}

	bool IsGameOver(int *pDurakClientID = 0)
	{
		int PlayersLeft = 0;
		for (int i = 0; i < MAX_DURAK_PLAYERS; i++)
		{
			if (m_aSeats[i].m_Player.m_ClientID != -1 && !m_aSeats[i].m_Player.m_vHandCards.empty())
			{
				PlayersLeft++;
				if (pDurakClientID)
					*pDurakClientID = m_aSeats[i].m_Player.m_ClientID;
			}
		}
		if (PlayersLeft >= MIN_DURAK_PLAYERS || !m_Deck.IsEmpty())
		{
			if (pDurakClientID)
				*pDurakClientID = -1;
			return false;
		}
		return true;
	}

	bool CanProcessWin(int Seat)
	{
		return m_Running && m_aSeats[Seat].m_Player.m_Stake >= 0;
	}

	bool TryAttack(int Seat, CCard *pCard)
	{
		if (!pCard->Valid())
			return false;

		int Used = 0;
		for (auto &pair : m_Attacks)
			if (pair.m_Offense.Valid())
				Used++;

		// allow attack from left guy too, as soon as at least 1 slot has been used
		if (Seat != m_AttackerIndex && (!Used || GetStateBySeat(Seat) != DURAK_PLAYERSTATE_ATTACK))
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
		RemoveCard(Seat, pCard);

		m_NextMove = 0;
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
		RemoveCard(Seat, pCard);

		m_NextMove = 0;
		return true;
	}

	bool TryPush(int Seat, CCard *pCard)
	{
		if (Seat != m_DefenderIndex || !pCard->Valid())
			return false;

		int NumAttacks = 0;
		int TargetRank = -1;
		for (auto &pair : m_Attacks)
		{
			if (pair.m_Offense.Valid())
			{
				if (pair.m_Defense.Valid()) // No defense has happened already
					return false;

				NumAttacks++;
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
				RemoveCard(Seat, pCard);

				int NewDefender = GetNextPlayer(m_DefenderIndex);
				if ((int)m_aSeats[NewDefender].m_Player.m_vHandCards.size() < NumAttacks + 1)
					return false;

				// Successfully moved
				m_AttackerIndex = m_DefenderIndex;
				m_DefenderIndex = NewDefender;
				m_NextMove = 0;
				return true;
			}
		}
		return false;
	}

	void RemoveCard(int Seat, CCard *pCard)
	{
		auto &vHand = m_aSeats[Seat].m_Player.m_vHandCards;
		vHand.erase(std::remove_if(vHand.begin(), vHand.end(),
			[&](const CCard &c) { return c.m_Suit == pCard->m_Suit && c.m_Rank == pCard->m_Rank; }),
			vHand.end());

		if (m_Deck.IsEmpty() && m_aSeats[Seat].m_Player.m_vHandCards.empty())
		{
			m_vWinners.push_back(Seat);
		}
	}
};

class CDurak : public CMinigame
{
	enum
	{
		DURAK_TEXT_CARDS_STACK = 0,
		DURAK_TEXT_TRUMP_CARD,
		DURAK_TEXT_KEYBOARD_CONTROL,
		DURAK_TEXT_TOOLTIP,
		NUM_DURAK_STATIC_CARDS
	};

	CCard m_aStaticCards[NUM_DURAK_STATIC_CARDS];
	std::map<int, std::map<CCard*, int>> m_aLastSnapID; // [ClientID][Card] = SnapID
	std::map<int, std::map<CCard*, bool>> m_aCardUpdate; // [ClientID][Card] = true
	void PrepareStaticCards(int SnappingClient, CDurakGame *pGame, CDurakGame::SSeat *pSeat);
	void PrepareDurakSnap(int SnappingClient, CDurakGame *pGame, CDurakGame::SSeat *pSeat);
	void UpdateCardSnapMapping(int SnappingClient, const std::map<CCard *, int> &NewMap);
	void SnapDurakCard(int SnappingClient, vec2 TablePos, CCard *pCard);

	int m_aDurakNumReserved[MAX_CLIENTS];
	int64 m_aLastSeatOccupiedMsg[MAX_CLIENTS];
	bool m_aUpdatedPassive[MAX_CLIENTS];
	bool m_aInDurakGame[MAX_CLIENTS];

	std::vector<CDurakGame *> m_vpGames;
	bool UpdateGame(int Game);
	bool StartGame(int Game);
	void EndGame(int Game);
	void ProcessPlayerWin(int Game, CDurakGame::SSeat *pSeat, int WinPos, bool ForceEnd = false);
	void HandleMoneyTransaction(int ClientID, int Amount, const char *pMsg);

	void SendChatToDeployedStakePlayers(int Game, const char *pMessage, int NotThisID);
	void SendChatToParticipants(int Game, const char *pMessage);

	int GetPlayerState(int ClientID);
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

	int GetTeam(int ClientID, int MapID);
	bool InDurakGame(int ClientID) { return ClientID >= 0 && m_aInDurakGame[ClientID]; }
	bool ActivelyPlaying(int ClientID) { return GetPlayerState(ClientID) != DURAK_PLAYERSTATE_NONE; }
	bool OnDropMoney(int ClientID, int Amount);

	void OnCharacterSeat(int ClientID, int Number, int SeatIndex);
	bool TryEnterBetStake(int ClientID, const char *pMessage);
	void OnInput(class CCharacter *pCharacter, CNetObj_PlayerInput *pNewInput);
	void OnPlayerLeave(int ClientID, bool Disconnect = false, bool Shutdown = false);
};
#endif // GAME_SERVER_MINIGAMES_DURAK_H
