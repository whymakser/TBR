// made by fokkonaut

#include <engine/server.h>
#include <game/server/gamecontext.h>
#include "grog.h"
#include <game/server/teams.h>
#include "character.h"
#include <generated/server_data.h>

CGrog::CGrog(CGameWorld *pGameWorld, vec2 Pos, int Owner)
: CAdvancedEntity(pGameWorld, CGameWorld::ENTTYPE_GROG, Pos, vec2(8, 20), Owner)
{
	m_Owner = Owner;
	m_LastAimDir = -1;
	m_NumSips = 0;
	m_Lifetime = -1;

	vec2 aOffsets[NUM_GROG_LINES][2] = {
		{ vec2(0, 0), vec2(0, -40) },
		{ vec2(8, -20), vec2(16, -20) },
		{ vec2(8, 0), vec2(-8, 0) },
		{ vec2(-8, -40), vec2(-8, 0), },
		{ vec2(8, -40), vec2(8, 0) }
	};

	// Sort ids, so that liquid will always be behind glass lines
	int aID[NUM_GROG_LINES];
	for (int i = 0; i < NUM_GROG_LINES; i++)
		aID[i] = Server()->SnapNewID();
	std::sort(std::begin(aID), std::end(aID));

	for (int i = 0; i < NUM_GROG_LINES; i++)
	{
		m_aLines[i].m_From = aOffsets[i][0];
		m_aLines[i].m_To = aOffsets[i][1];
		m_aLines[i].m_ID = aID[i];
	}

	GameWorld()->InsertEntity(this);
}

CGrog::~CGrog()
{
	for (int i = 0; i < NUM_GROG_LINES; i++)
		Server()->SnapFreeID(m_aLines[i].m_ID);
}

void CGrog::Reset(bool CreateDeath)
{
	if (CreateDeath)
		GameServer()->CreateDeath(m_Pos, m_Owner, m_TeamMask);
	CAdvancedEntity::Reset();
}

void CGrog::DecreaseNumGrogsHolding()
{
	if (!GetOwner())
		return;

	GetOwner()->m_NumGrogsHolding--;
	GetOwner()->m_pGrog = GetOwner()->m_NumGrogsHolding ? new CGrog(GameWorld(), GetOwner()->GetPos(), m_Owner) : 0;
	GetOwner()->UpdateWeaponIndicator();
}

void CGrog::OnSip()
{
	m_NumSips++;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN, m_TeamMask);
	GetOwner()->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed() * 2);

	if (m_NumSips >= NUM_GROG_SIPS)
	{
		GetOwner()->IncreasePermille(3);
		DecreaseNumGrogsHolding();
		Reset(false);
		return;
	}
}

bool CGrog::Drop(int Dir, bool OnDeath)
{
	// Can't drop a grog which you started drinking. don't spread viruses. Drink it up first. If you drank before sharing with your friends, you're a donkey!
	if (m_NumSips)
	{
		// Still remove the grog from being held and remove it
		if (OnDeath)
		{
			DecreaseNumGrogsHolding();
			Reset();
		}
		return false;
	}

	// Remove after 5 min of being dropped
	m_Lifetime = Server()->TickSpeed() * 300;
	m_PickupDelay = Server()->TickSpeed() * 2;
	Dir = Dir == -3 ? 2*GetOwner()->GetAimDir() : Dir;
	m_Vel = vec2(Dir, -5);
	DecreaseNumGrogsHolding();
	return true;
}

void CGrog::Tick()
{
	CAdvancedEntity::Tick();

	if (m_Lifetime == -1)
	{
		if (!GetOwner())
		{
			Reset();
			return;
		}

		int Dir = GetOwner()->GetAimDir();
		m_Pos = GetOwner()->GetPos();
		m_Pos.x += 32.f * Dir;
		if (Dir != m_LastAimDir)
		{
			for (int i = 0; i < NUM_GROG_LINES; i++)
			{
				m_aLines[i].m_From.x *= -1;
				m_aLines[i].m_To.x *= -1;
			}
		}
		m_LastAimDir = Dir;
	}
	else
	{
		m_Lifetime--;
		if (m_Lifetime <= 0)
		{
			Reset();
			return;
		}

		HandleDropped();

		if (m_PickupDelay > 0)
			m_PickupDelay--;
		Pickup();
	}

	m_TeamMask = GetOwner() ? GetOwner()->TeamMask() : Mask128();
}

void CGrog::Pickup()
{
	CCharacter *apEnts[MAX_CLIENTS];
	int Num = GameWorld()->FindEntities(m_Pos, 20.0f, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

	for (int i = 0; i < Num; i++)
	{
		CCharacter* pChr = apEnts[i];
		if ((m_PickupDelay > 0 && pChr == GetOwner()) || (m_Owner >= 0 && !pChr->CanCollide(m_Owner, false)))
			continue;

		if (pChr->AddGrog())
		{
			Reset(false);
			break;
		}
	}
}

void CGrog::Snap(int SnappingClient)
{
	if (NetworkClipped(SnappingClient, m_Pos))
		return;

	if (!CmaskIsSet(m_TeamMask, SnappingClient))
		return;

	if (GetOwner() && GetOwner()->IsPaused())
		return;

	for (int i = 0; i < NUM_GROG_LINES; i++)
	{
		bool IsLiquid = i == GROG_LINE_LIQUID;
		int StartTick = IsLiquid ? Server()->Tick() : Server()->Tick() - 2;
		vec2 PosTo = m_Pos + m_aLines[i].m_To;
		vec2 PosFrom = m_Pos + m_aLines[i].m_From;
		if (IsLiquid)
			PosTo.y += m_NumSips * 8.f;

		if(GameServer()->GetClientDDNetVersion(SnappingClient) >= VERSION_DDNET_MULTI_LASER)
		{
			CNetObj_DDNetLaser *pObj = static_cast<CNetObj_DDNetLaser *>(Server()->SnapNewItem(NETOBJTYPE_DDNETLASER, m_aLines[i].m_ID, sizeof(CNetObj_DDNetLaser)));
			if(!pObj)
				return;

			int Owner = m_Owner;
			if (!Server()->Translate(Owner, SnappingClient))
				Owner = -1;

			pObj->m_ToX = round_to_int(PosTo.x);
			pObj->m_ToY = round_to_int(PosTo.y);
			pObj->m_FromX = round_to_int(PosFrom.x);
			pObj->m_FromY = round_to_int(PosFrom.y);
			pObj->m_StartTick = StartTick;
			pObj->m_Owner = Owner;
			pObj->m_Type = IsLiquid ? LASERTYPE_SHOTGUN : LASERTYPE_FREEZE;
		}
		else
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_aLines[i].m_ID, sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = round_to_int(PosTo.x);
			pObj->m_Y = round_to_int(PosTo.y);
			pObj->m_FromX = round_to_int(PosFrom.x);
			pObj->m_FromY = round_to_int(PosFrom.y);
			pObj->m_StartTick = StartTick;
		}
	}
}
