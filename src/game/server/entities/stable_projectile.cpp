#include <game/server/gamecontext.h>
#include "stable_projectile.h"
#include <game/server/teams.h>
#include <game/server/player.h>
#include "character.h"

CStableProjectile::CStableProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, bool HideOnSpec, bool OnlyShowOwner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_STABLE_PROJECTILE, Pos)
{
	m_Type = GameServer()->GetWeaponType(Type);
	m_Pos = Pos;
	m_LastResetPos = Pos;
	m_Owner = Owner;
	m_HideOnSpec = HideOnSpec;
	m_LastResetTick = Server()->Tick();
	m_CalculatedVel = false;
	m_TeamMask = Mask128();
	m_OnlyShowOwner = OnlyShowOwner;

	GameWorld()->InsertEntity(this);
}

void CStableProjectile::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CStableProjectile::TickDeferred()
{
	if(Server()->Tick()%4 == 1)
	{
		m_LastResetPos = m_Pos;
		m_LastResetTick = Server()->Tick();
	}
	m_CalculatedVel = false;
	m_TeamMask = GameServer()->GetPlayerChar(m_Owner) ? GameServer()->GetPlayerChar(m_Owner)->TeamMask() : Mask128();
}

void CStableProjectile::CalculateVel()
{
	float Time = (Server()->Tick()-m_LastResetTick)/(float)Server()->TickSpeed();
	float Curvature = 0;
	float Speed = 0;

	switch(m_Type)
	{
		case WEAPON_GRENADE:
			Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
			Speed = GameServer()->Tuning()->m_GrenadeSpeed;
			break;

		case WEAPON_SHOTGUN:
			Curvature = GameServer()->Tuning()->m_ShotgunCurvature;
			Speed = GameServer()->Tuning()->m_ShotgunSpeed;
			break;

		case WEAPON_GUN:
			Curvature = GameServer()->Tuning()->m_GunCurvature;
			Speed = GameServer()->Tuning()->m_GunSpeed;
			break;
	}

	m_VelX = ((m_Pos.x - m_LastResetPos.x)/Time/Speed) * 100;
	m_VelY = ((m_Pos.y - m_LastResetPos.y)/Time/Speed - Time*Speed*Curvature/10000) * 100;

	m_CalculatedVel = true;
}

void CStableProjectile::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if (m_Owner != -1 && !GameServer()->m_apPlayers[m_Owner])
	{
		Reset();
		return;
	}

	if (m_OnlyShowOwner && SnappingClient != m_Owner)
		return;

	if (!CmaskIsSet(m_TeamMask, SnappingClient))
		return;

	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if (m_HideOnSpec && pOwner && pOwner->IsPaused())
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;

	if(!m_CalculatedVel)
		CalculateVel();

	pProj->m_X = round_to_int(m_LastResetPos.x);
	pProj->m_Y = round_to_int(m_LastResetPos.y);
	pProj->m_VelX = m_VelX;
	pProj->m_VelY = m_VelY;
	if (Server()->IsSevendown(SnappingClient) && m_VelY >= 0 && (m_VelY & 512) != 0) // dont send PROJECTILEFLAG_IS_DDNET
		pProj->m_VelY = 0;
	pProj->m_StartTick = m_LastResetTick;
	pProj->m_Type = m_Type;
}
