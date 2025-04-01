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
	NUM_DURAK_INITIAL_HAND_CARDS = 6,
	MAX_DURAK_ATTACKS = 6,
	MAX_DURAK_ATTACKS_BEFORE_FIRST_TEE = 5,
	MAX_DURAK_GAMES = VANILLA_MAX_CLIENTS-1,
	DURAK_CARD_NAME_OFFSET = 48,
};

#define DURAK_CARD_HOVER_OFFSET (CCard::s_CardSize.y / 4)

class CCard
{
public:
	static const vec2 s_CardSize;
	static const vec2 s_TableSize;

	CCard(int Suit, int Rank) : m_Suit(Suit), m_Rank(Rank)
	{
		m_TableOffset = vec2(0, 0);
		m_HoverState = HOVERSTATE_NONE;
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

	bool Valid() { return m_Suit != -1 && m_Rank != -1; }

	bool MouseOver(vec2 Target) // Target - m_TablePos
	{
		vec2 CardPos = vec2(m_TableOffset.x, m_TableOffset.y-DURAK_CARD_NAME_OFFSET);
		float HighY = s_CardSize.y / 2;
		if (m_HoverState == HOVERSTATE_MOUSEOVER) // Avoid flickering
			HighY += DURAK_CARD_HOVER_OFFSET;
		return (CardPos.x - s_CardSize.x/2 < Target.x && CardPos.x + s_CardSize.x/2 > Target.x && CardPos.y - s_CardSize.y/2 < Target.y && CardPos.y + HighY > Target.y);
	}

	void SetHovered(bool Set)
	{
		if (Set)
		{
			m_TableOffset.y = CCard::s_TableSize.y - DURAK_CARD_HOVER_OFFSET;
			m_HoverState = HOVERSTATE_MOUSEOVER;
		}
		else
		{
			m_TableOffset.y = CCard::s_TableSize.y;
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

    void Shuffle() {
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

	void DealHandCards()
	{
		for (int i = 0; i < NUM_DURAK_INITIAL_HAND_CARDS; i++)
		{
			for (int s = 0; s < MAX_DURAK_PLAYERS; s++)
			{
				if (m_aSeats[s].m_Player.m_ClientID != -1)
				{
					CCard Card = m_Deck.DrawCard();
					Card.m_TableOffset.y = CCard::s_TableSize.y;
					m_aSeats[s].m_Player.m_vpHandCards.push_back(Card);
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
				m_vpHandCards.clear();
				m_HoveredCard = -1;
				m_LastCursorX = -1.f;
			}
			int m_ClientID;
			int64 m_Stake;
			std::vector<CCard> m_vpHandCards;
			int m_HoveredCard;
			float m_LastCursorX;
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
	void SnapFakeTee(int SnappingClient, int ID, vec2 Pos, const char *pName, int Game);

	int64 m_aLastSeatOccupiedMsg[MAX_CLIENTS];
	bool m_aUpdatedPassive[MAX_CLIENTS];
	bool m_aInDurakGame[MAX_CLIENTS];
	bool m_aKeyboardControl[MAX_CLIENTS];

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
			{"🂦", "🂧", "🂨", "🂩", "🂪", "🂫", "🂭", "🂮", "🂡"}, // Spades
			{"🂶", "🂷", "🂸", "🂹", "🂺", "🂻", "🂽", "🂾", "🂱"}, // Hearts
			{"🃆", "🃇", "🃈", "🃉", "🃊", "🃋", "🃍", "🃎", "🃁"}, // Diamonds
			{"🃖", "🃗", "🃘", "🃙", "🃚", "🃛", "🃝", "🃞", "🃑"}  // Clubs
		};
		return aapCards[Suit][Rank - 6];
	}

public:
	CDurak(CGameContext *pGameServer, int Type);
	virtual ~CDurak();

	virtual void Tick();
	virtual void Snap(int SnappingClient);

	bool InDurakGame(int ClientID) { return ClientID >= 0 && m_aInDurakGame[ClientID]; }
	bool OnDropMoney(int ClientID, int Amount);

	int GetGameByNumber(int Number, bool AllowRunning = false);
	int GetGameByClient(int ClientID);

	void AddMapTableTile(int Number, vec2 Pos);
	void AddMapSeatTile(int Number, int MapIndex, int SeatIndex);

	void OnCharacterSeat(int ClientID, int Number, int SeatIndex);
	bool TryEnterBetStake(int ClientID, const char *pMessage);

	void OnPlayerLeave(int ClientID);

	bool OnInput(class CCharacter *pCharacter, CNetObj_PlayerInput *pNewInput);

	void SendChatToDeployedStakePlayers(int Game, const char *pMessage, int NotThisID);
	void SendChatToParticipants(int Game, const char *pMessage);
	void UpdatePassive(int ClientID, int Seconds);
};
#endif // GAME_SERVER_MINIGAMES_DURAK_H

/*
* #include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

struct Point {
    float x;
    bool hovered = false;
};

class PointSystem {
public:
    PointSystem(int count, float spacing)
        : count(count), spacing(spacing) {
        for (int i = 0; i < count; ++i) {
            points.push_back({i * spacing});
        }
    }

    void update(float mouseX) {
        int closestIndex = -1;
        float minDist = spacing;

        // Finde den nächsten Punkt zur Maus
        for (int i = 0; i < count; ++i) {
            float dist = std::abs(points[i].x - mouseX);
            if (dist < minDist) {
                minDist = dist;
                closestIndex = i;
            }
        }

        // Setze die Punkte mit Verschiebung
        for (int i = 0; i < count; ++i) {
            if (i == closestIndex) {
                points[i].hovered = true;
                points[i].x = i * spacing + 10; // Etwas größerer Abstand
            } else {
                points[i].hovered = false;
                points[i].x = i * spacing;
            }
        }
    }

    void tick(float mouseX) {
        update(mouseX);
    }

    void print() {
        for (const auto& p : points) {
            std::cout << (p.hovered ? "[X]" : "[.]") << " ";
        }
        std::cout << std::endl;
    }

private:
    int count;
    float spacing;
    std::vector<Point> points;
};

int main() {
    PointSystem system(10, 20.0f);
    float mouseX = 0.0f; 

    const int TPS = 50; // 50 Ticks pro Sekunde
    const int tickDelay = 1000 / TPS; // Millisekunden pro Tick

    for (int tick = 0; tick < 500; ++tick) { // Simuliere 500 Ticks (~10 Sekunden)
        // Simulierte Mausbewegung: Maus bewegt sich langsam nach rechts
        mouseX = std::fmod(mouseX + 2, 200); 

        system.tick(mouseX);
        system.print();

        std::this_thread::sleep_for(std::chrono::milliseconds(tickDelay));
    }

    return 0;
}
*/
