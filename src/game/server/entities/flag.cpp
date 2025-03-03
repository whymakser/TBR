/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include <game/server/teams.h>
#include "flag.h"
#include <game/server/gamemodes/DDRace.h>
#include "character.h"

CFlag::CFlag(CGameWorld *pGameWorld, int Team, vec2 Pos)
: CAdvancedEntity(pGameWorld, CGameWorld::ENTTYPE_FLAG, Pos, vec2(ms_PhysSize, ms_PhysSize))
{
	m_Pos = m_PrevPos = Pos;
	m_StandPos = Pos;
	m_Team = Team;
	Reset(true);
	m_AllowVipPlus = false;

	GameWorld()->InsertEntity(this);
}

void CFlag::Reset(bool Init)
{
	ReleaseHooked();

	if (!Init)
	{
		if (Config()->m_SvFlagSounds)
			GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "flag_return");
		GameServer()->CreateDeath(m_Pos, GetCarrier() ? m_Carrier : GetLastCarrier() ? m_LastCarrier : -1);
	}
	m_Carrier = -1;
	m_LastCarrier = -1;
	m_AtStand = true;
	m_Pos = m_PrevPos = m_StandPos;
	m_Vel = vec2(0,0);
	m_DropTick = 0;
	m_GrabTick = 0;
	m_TeleCheckpoint = 0;
	m_SoundTick = 0;
	m_CanPlaySound = true;
	m_TeamMask = Mask128();
}

void CFlag::SetAtStand(bool AtStand)
{
	if (m_AtStand && !AtStand)
		m_DropTick = Server()->Tick();
	m_AtStand = AtStand;
}

CCharacter *CFlag::GetCarrier()
{
	return GameServer()->GetPlayerChar(m_Carrier);
}

CCharacter *CFlag::GetLastCarrier()
{
	return GameServer()->GetPlayerChar(m_LastCarrier);
}

void CFlag::PlaySound(int Sound)
{
	if (!Config()->m_SvFlagSounds)
		return;

	if (!m_SoundTick)
		m_CanPlaySound = true;

	if (m_SoundTick < 10 && m_CanPlaySound)
	{
		m_SoundTick++;

		if (Config()->m_SvFlagSounds == 1)
		{
			GameServer()->CreateSoundGlobal(Sound);
		}
		else if (Config()->m_SvFlagSounds == 2)
		{
			Mask128 TeamMask = Mask128();
			CCharacter *pChr = GetCarrier() ? GetCarrier() : GetLastCarrier() ? GetLastCarrier() : 0;
			if (pChr)
				TeamMask = pChr->TeamMask();
			GameServer()->CreateSound(m_Pos, Sound, TeamMask);
		}
	}
	else
		m_CanPlaySound = false;
}

void CFlag::UpdateSpectators(int SpectatorID)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if (pPlayer && ((m_Team == TEAM_RED && pPlayer->GetSpecMode() == SPEC_FLAGRED) || (m_Team == TEAM_BLUE && pPlayer->GetSpecMode() == SPEC_FLAGBLUE)))
			pPlayer->ForceSetSpectatorID(SpectatorID);
	}
}

void CFlag::ReleaseHooked()
{
	int Flag = m_Team == TEAM_RED ? HOOK_FLAG_RED : HOOK_FLAG_BLUE;
	GameWorld()->ReleaseHooked(Flag);
}

void CFlag::TickPaused()
{
	if(m_DropTick)
		m_DropTick++;
	if(m_GrabTick)
		m_GrabTick++;
}

void CFlag::TickDeferred()
{
	if (GetCarrier())
		m_Pos = GetCarrier()->GetPos();
}

void CFlag::Drop(int Dir)
{
	if (m_Carrier == -1)
		return;

	PlaySound(SOUND_CTF_DROP);
	m_DropTick = Server()->Tick();
	GameServer()->SendBroadcast("", m_Carrier, false);
	m_LastCarrier = m_Carrier;
	m_Carrier = -1;
	m_Vel = vec2(5 * Dir, Dir == 0 ? 0 : -5);
	UpdateSpectators(-1);
}

void CFlag::Grab(int NewCarrier)
{
	PlaySound(m_Team == TEAM_RED ? SOUND_CTF_GRAB_EN : SOUND_CTF_GRAB_PL);
	if (m_AtStand)
		m_GrabTick = Server()->Tick();
	m_AtStand = false;
	m_Carrier = NewCarrier;
	GetCarrier()->m_FirstFreezeTick = 0;
	GameServer()->UnsetTelekinesis(this);
	UpdateSpectators(m_Carrier);
}

void CFlag::TeleToPlot(int PlotID)
{
	GameServer()->CreateDeath(m_Pos, GetCarrier() ? m_Carrier : GetLastCarrier() ? m_LastCarrier : -1);
	Drop();
	m_Vel = vec2(0, 0);
	m_Pos = m_PrevPos = GameServer()->m_aPlots[PlotID].m_ToTele;
}

void CFlag::ResetPrevPos()
{
	GameServer()->CreateDeath(m_Pos, GetCarrier() ? m_Carrier : GetLastCarrier() ? m_LastCarrier : -1);
	Drop();
	m_Vel = vec2(0, 0);
	m_Pos = m_PrevPos;
}

void CFlag::Tick()
{
	// for the CAdvancedEntity part
	m_Owner = GetCarrier() ? m_Carrier : GetLastCarrier() ? m_LastCarrier : -1;
	CAdvancedEntity::Tick();

	if (GetCarrier())
	{
		if (GetCarrier()->m_IsFrozen && GetCarrier()->m_FirstFreezeTick != 0)
			if (Server()->Tick() > GetCarrier()->m_FirstFreezeTick + Server()->TickSpeed() * 8)
				Drop(GetCarrier()->GetAimDir());
	}
	else
	{
		CCharacter *apCloseCCharacters[MAX_CLIENTS];
		int Num = GameWorld()->FindEntities(m_Pos, GetProximityRadius(), (CEntity**)apCloseCCharacters, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for (int i = 0; i < Num; i++)
		{
			CCharacter *pChr = apCloseCCharacters[i];
			if (!pChr || pChr->GetPlayer()->GetTeam() == TEAM_SPECTATORS || GameServer()->Collision()->IntersectLineFlagPickup(m_Pos, pChr->GetPos(), NULL, NULL))
				continue;

			if (GetLastCarrier() == pChr && (m_DropTick + Server()->TickSpeed() * 2) > Server()->Tick())
				continue;

			if (pChr->HasFlag() != -1 || GameServer()->Arenas()->FightStarted(pChr->GetPlayer()->GetCID()))
				continue;

			// Disallow taking flag with passive or solo, also drop it when one of this gets activated while holding a flag
			if (pChr->m_Passive || pChr->IsSolo())
				continue;

			// take the flag
			Grab(pChr->GetPlayer()->GetCID());
			break;
		}
	}

	if (!GetCarrier() && !m_AtStand)
	{
		if (m_DropTick && Config()->m_SvFlagRespawnDropped && Server()->Tick() > m_DropTick + Server()->TickSpeed() * Config()->m_SvFlagRespawnDropped)
		{
			Reset();
			return;
		}
		else
			HandleDropped();
	}

	// check tiles inbetween pos and prevpos in case a flag is being carried and the guy has vip+ or a plot door is opened and he tries to skip it with ninja or speed.
	// in such a case the tee wouldnt be stopped by MoveBox() means he would simply skip the tile without the flag noticing
	const int End = distance(m_Pos, m_PrevPos)+1;
	const float InverseEnd = 1.0f/End;
	for (int i = 0; i <= End; i++)
	{
		vec2 Pos = mix(m_Pos, m_PrevPos, i*InverseEnd);

		// plots
		int PlotID = GameServer()->GetTilePlotID(Pos, true);
		if (PlotID >= PLOT_START)
			TeleToPlot(PlotID);

		// vip plus
		int MapIndex = GameServer()->Collision()->GetMapIndex(Pos);
		int TileIndex = GameServer()->Collision()->GetTileIndex(MapIndex);
		int TileFIndex = GameServer()->Collision()->GetFTileIndex(MapIndex);
		if (TileIndex == TILE_VIP_PLUS_ONLY || TileFIndex == TILE_VIP_PLUS_ONLY || TileIndex == TILE_FLAG_STOP || TileFIndex == TILE_FLAG_STOP)
		{
			ResetPrevPos();
		}
	}

	m_TeamMask = GetCarrier() ? GetCarrier()->TeamMask() : Mask128();
	m_PrevPos = m_Pos;

	if (m_SoundTick && Server()->Tick() % Server()->TickSpeed() == 0)
		m_SoundTick--;
}

void CFlag::Snap(int SnappingClient)
{
	if (NetworkClipped(SnappingClient))
		return;

	CPlayer* pPlayer = SnappingClient >= 0 ? GameServer()->m_apPlayers[SnappingClient] : 0;
	if (pPlayer && pPlayer->GetTeam() != TEAM_SPECTATORS && !pPlayer->IsPaused() && GameServer()->Arenas()->FightStarted(SnappingClient))
		return;

	if (!CmaskIsSet(m_TeamMask, SnappingClient) || (GetCarrier() && GetCarrier()->IsPaused()))
		return;

	CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, m_Team, sizeof(CNetObj_Flag));
	if (!pFlag)
		return;

	pFlag->m_X = round_to_int(m_Pos.x);
	pFlag->m_Y = round_to_int(m_Pos.y);
	pFlag->m_Team = m_Team;
}