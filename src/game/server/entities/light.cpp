/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include <engine/config.h>
#include <engine/server.h>
#include <generated/protocol.h>
#include <game/server/gamecontext.h>
#include "light.h"
#include <game/mapitems.h>
#include "character.h"
#include <game/server/player.h>

CLight::CLight(CGameWorld *pGameWorld, vec2 Pos, float Rotation, int Length,
		int Layer, int Number) :
		CEntity(pGameWorld, CGameWorld::ENTTYPE_LIGHT, Pos)
{
	m_Layer = Layer;
	m_Number = Number;
	m_Tick = (Server()->TickSpeed() * 0.15f);
	m_Pos = Pos;
	m_Rotation = Rotation;
	m_Length = Length;
	m_EvalTick = Server()->Tick();
	GameWorld()->InsertEntity(this);
	Step();
}

bool CLight::HitCharacter()
{
	std::list<CCharacter *> HitCharacters =
			GameWorld()->IntersectedCharacters(m_Pos, m_To, 0.0f, 0);
	if (HitCharacters.empty())
		return false;
	for (std::list<CCharacter *>::iterator i = HitCharacters.begin();
			i != HitCharacters.end(); i++)
	{
		CCharacter * Char = *i;
		if (m_Layer == LAYER_SWITCH
				&& !GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[Char->Team()])
			continue;
		Char->Freeze();

		// F-DDrace
		Char->SetLastTouchedSwitcher(m_Number);
	}
	return true;
}

void CLight::Move()
{
	if (m_Speed != 0)
	{
		if ((m_CurveLength >= m_Length && m_Speed > 0)
				|| (m_CurveLength <= 0 && m_Speed < 0))
			m_Speed = -m_Speed;
		m_CurveLength += m_Speed * m_Tick + m_LengthL;
		m_LengthL = 0;
		if (m_CurveLength > m_Length)
		{
			m_LengthL = m_CurveLength - m_Length;
			m_CurveLength = m_Length;
		}
		else if (m_CurveLength < 0)
		{
			m_LengthL = 0 + m_CurveLength;
			m_CurveLength = 0;
		}
	}

	m_Rotation += m_AngularSpeed * m_Tick;
	const float pidouble = pi * 2;
	if (m_Rotation > pidouble)
		m_Rotation -= pidouble;
	else if (m_Rotation < 0)
		m_Rotation += pidouble;
}

void CLight::Step()
{
	Move();
	vec2 dir(sin(m_Rotation), cos(m_Rotation));
	vec2 to2 = m_Pos + normalize(dir) * m_CurveLength;
	GameServer()->Collision()->IntersectNoLaser(m_Pos, to2, &m_To, 0);
}

void CLight::Reset()
{

}

void CLight::Tick()
{

	if (Server()->Tick() % int(Server()->TickSpeed() * 0.15f) == 0)
	{
		int Flags;
		m_EvalTick = Server()->Tick();
		int index = GameServer()->Collision()->IsMover(m_Pos.x, m_Pos.y,
				&Flags);
		if (index)
		{
			m_Core = GameServer()->Collision()->CpSpeed(index, Flags);
		}
		m_Pos += m_Core;
		Step();
	}

	HitCharacter();
	return;

}

void CLight::Snap(int SnappingClient)
{
	if (NetworkClipped(SnappingClient, m_Pos)
			&& NetworkClipped(SnappingClient, m_To))
		return;

	CCharacter *Char = GameServer()->GetPlayerChar(SnappingClient);

	if(SnappingClient > -1 && (GameServer()->m_apPlayers[SnappingClient]->GetTeam() == -1
				|| GameServer()->m_apPlayers[SnappingClient]->IsPaused())
			&& GameServer()->m_apPlayers[SnappingClient]->GetSpectatorID() != -1)
		Char = GameServer()->GetPlayerChar(GameServer()->m_apPlayers[SnappingClient]->GetSpectatorID());

	CNetObj_EntityEx *pEntData = 0;
	CCharacter *pChr = GameServer()->GetPlayerChar(SnappingClient);
	if (pChr && pChr->SendExtendedEntity(this))
		pEntData = static_cast<CNetObj_EntityEx *>(Server()->SnapNewItem(NETOBJTYPE_ENTITYEX, GetID(), sizeof(CNetObj_EntityEx)));

	if (pEntData)
	{
		pEntData->m_SwitchNumber = m_Number;
		pEntData->m_Layer = m_Layer;
		pEntData->m_EntityClass = ENTITYCLASS_LIGHT;
	}
	else
	{
		int Tick = (Server()->Tick() % Server()->TickSpeed()) % 6;
		if (Char && Char->IsAlive() && m_Layer == LAYER_SWITCH && !GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[Char->Team()] && (Tick))
			return;
	}

	// Build the object
	vec2 From = m_Pos;
	int StartTick = 0;

	if (Char && Char->Team() == TEAM_SUPER)
		From = m_Pos;
	else if (Char && m_Layer == LAYER_SWITCH && GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[Char->Team()])
		From = m_To;
	else if (m_Layer != LAYER_SWITCH)
		From = m_To;

	if (!pEntData)
	{
		StartTick = m_EvalTick;
		if (StartTick < Server()->Tick() - 4)
			StartTick = Server()->Tick() - 4;
		else if (StartTick > Server()->Tick())
			StartTick = Server()->Tick();
	}

	if(GameServer()->GetClientDDNetVersion(SnappingClient) >= VERSION_DDNET_MULTI_LASER)
	{
		CNetObj_DDNetLaser *pObj = static_cast<CNetObj_DDNetLaser *>(Server()->SnapNewItem(NETOBJTYPE_DDNETLASER, GetID(), sizeof(CNetObj_DDNetLaser)));
		if(!pObj)
			return;

		pObj->m_ToX = round_to_int(m_Pos.x);
		pObj->m_ToY = round_to_int(m_Pos.y);
		pObj->m_FromX = round_to_int(From.x);
		pObj->m_FromY = round_to_int(From.y);
		pObj->m_StartTick = StartTick;
		pObj->m_Owner = -1;
		pObj->m_Type = LASERTYPE_FREEZE;
	}
	else
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = round_to_int(m_Pos.x);
		pObj->m_Y = round_to_int(m_Pos.y);
		pObj->m_FromX = round_to_int(From.x);
		pObj->m_FromY = round_to_int(From.y);
		pObj->m_StartTick = StartTick;
	}
}
