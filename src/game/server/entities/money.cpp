// made by fokkonaut

#include <engine/server.h>
#include <game/server/gamecontext.h>
#include <game/server/teams.h>
#include "money.h"

CMoney::CMoney(CGameWorld *pGameWorld, vec2 Pos, int64 Amount, int Owner, float Direction, bool GlobalPickupDelay)
: CAdvancedEntity(pGameWorld, CGameWorld::ENTTYPE_MONEY, Pos, vec2(GetRadius(Amount)*2, GetRadius(Amount)*2), Owner, false)
{
	m_Pos = Pos;
	m_Amount = Amount;
	m_GlobalPickupDelay = GlobalPickupDelay;
	m_Vel = vec2(5*Direction, Direction == 0 ? 0 : -5);
	m_StartTick = Server()->Tick();
	
	m_Snap.m_Pos = m_Pos;
	m_Snap.m_Time = 0.f;
	m_Snap.m_LastTime = Server()->Tick();

	for (int i = 0; i < NUM_DOTS_BIG; i++)
		m_aID[i] = Server()->SnapNewID();

	GameWorld()->InsertEntity(this);
}

CMoney::~CMoney()
{
	for (int i = 0; i < NUM_DOTS_BIG; i++)
		Server()->SnapFreeID(m_aID[i]);
}

bool CMoney::SecondsPassed(float Seconds)
{
	return m_StartTick < (Server()->Tick() - Server()->TickSpeed() * Seconds);
}

void CMoney::Tick()
{
	if (IsMarkedForDestroy())
		return;

	CAdvancedEntity::Tick();

	// Remove small money drops after 10 minutes
	if (m_Amount < SMALL_MONEY_AMOUNT && SecondsPassed(60 * 10))
	{
		Reset();
		return;
	}

	m_Gravity = true;

	CCharacter *pClosest = 0;
	bool TwoSecondsPassed = SecondsPassed(2);
	if (!m_GlobalPickupDelay || TwoSecondsPassed)
	{
		pClosest = GameWorld()->ClosestCharacter(m_Pos, RADIUS_FIND_PLAYERS, TwoSecondsPassed ? 0 : GetOwner(), -1, false, true, false, m_DDTeam);
		if (pClosest)
		{
			if (distance(m_Pos, pClosest->GetPos()) < GetRadius() + pClosest->GetProximityRadius())
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "Collected %lld money", m_Amount);
				GameServer()->SendChatTarget(pClosest->GetPlayer()->GetCID(), aBuf);
				pClosest->GetPlayer()->WalletTransaction(m_Amount, "collected");

				str_format(aBuf, sizeof(aBuf), "+%lld", m_Amount);
				GameServer()->CreateLaserText(m_Pos, pClosest->GetPlayer()->GetCID(), aBuf, GameServer()->MoneyLaserTextTime(m_Amount));
				GameServer()->CreateSound(m_Pos, SOUND_HOOK_LOOP, pClosest->TeamMask());

				Reset();
				return;
			}
			else
				MoveTo(pClosest->GetPos(), RADIUS_FIND_PLAYERS);
		}
	}

	CMoney *pMoney = (CMoney *)GameWorld()->ClosestEntity(m_Pos, RADIUS_FIND_MONEY, CGameWorld::ENTTYPE_MONEY, this, true, m_DDTeam);
	if (pMoney && !pMoney->IsMarkedForDestroy())
	{
		if (distance(m_Pos, pMoney->GetPos()) < GetRadius() + pMoney->GetRadius())
		{
			m_Amount += pMoney->m_Amount;
			pMoney->Reset();
			GameServer()->CreateDeath(m_Pos, m_Owner, m_TeamMask);
		}
		else if (!pClosest)
			MoveTo(pMoney->GetPos(), RADIUS_FIND_MONEY);
	}

	HandleDropped();

	m_PrevPos = m_Pos;
}

void CMoney::MoveTo(vec2 Pos, int Radius)
{
	float MaxFlySpeed = GameServer()->TuningFromChrOrZone(m_Owner, m_TuneZone)->m_MoneyMaxFlySpeed;
	if (MaxFlySpeed <= 0.f)
		return;

	m_Gravity = false;

	vec2 Diff = vec2(Pos.x - m_Pos.x, Pos.y - m_Pos.y);
	float AddVelX = (Diff.x/Radius*5);
	m_Vel.x = clamp(m_Vel.x+AddVelX, min(-MaxFlySpeed, m_Vel.x-AddVelX), max(MaxFlySpeed, m_Vel.x-AddVelX));

	float AddVelY = (Diff.y/Radius*5);
	m_Vel.y = clamp(m_Vel.y+AddVelY, min(-MaxFlySpeed, m_Vel.y-AddVelY), max(MaxFlySpeed, m_Vel.y-AddVelY));
}

void CMoney::Snap(int SnappingClient)
{
	if (NetworkClipped(SnappingClient))
		return;

	if (!CmaskIsSet(m_TeamMask, SnappingClient))
		return;

	CNetObj_Projectile *pBullet = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(!pBullet)
		return;

	pBullet->m_X = (int)m_Pos.x;
	pBullet->m_Y = (int)m_Pos.y;
	pBullet->m_VelX = 0;
	pBullet->m_VelY = 0;
	pBullet->m_StartTick = 0;
	pBullet->m_Type = WEAPON_SHOTGUN;

	float AngleStep = 2.0f * pi / GetNumDots();
	m_Snap.m_Time += (Server()->Tick() - m_Snap.m_LastTime) / Server()->TickSpeed();
	m_Snap.m_LastTime = Server()->Tick();

	for(int i = 0; i < GetNumDots(); i++)
	{
		vec2 Pos = m_Pos;
		Pos.x += GetRadius() * cosf(m_Snap.m_Time * 10.f + AngleStep * i);
		Pos.y += GetRadius() * sinf(m_Snap.m_Time * 10.f + AngleStep * i);
		
		CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_aID[i], sizeof(CNetObj_Projectile)));
		if(!pObj)
			return;

		pObj->m_X = (int)Pos.x;
		pObj->m_Y = (int)Pos.y;
		pObj->m_VelX = 0;
		pObj->m_VelY = 0;
		pObj->m_StartTick = 0;
		pObj->m_Type = WEAPON_HAMMER;
	}
}
