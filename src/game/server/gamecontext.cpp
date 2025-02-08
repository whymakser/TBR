﻿/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>

#include <antibot/antibot_data.h>
#include <base/hash_ctxt.h>

#include <engine/shared/config.h>
#include <engine/shared/memheap.h>
#include <engine/shared/datafile.h>
#include <engine/shared/json.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>
#include <engine/map.h>

#include <generated/server_data.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <game/version.h>

#include "entities/character.h"
#include "entities/money.h"
#include "entities/helicopter.h"
#include "entities/speedup.h"
#include "entities/button.h"
#include "entities/teleporter.h"
#include "entities/playercounter.h"
#include "gamemodes/DDRace.h"
#include "teeinfo.h"
#include "gamecontext.h"
#include "player.h"
#include "houses/shop.h"
#include "houses/bank.h"
#include "houses/tavern.h"
#include "minigames/arenas.h"

#include "entities/flag.h"
#include "entities/lasertext.h"
#include <fstream>
#include <limits>
#include <string>
#include <stdio.h>
#include <stdlib.h>

#include "score.h"
#include "score/file_score.h"

#if defined (CONF_SQL)
	#include "score/sql_score.h"
#endif

#include <engine/server/server.h>
#include <string.h>

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LastMapVote = 0;
	m_LockTeams = 0;

	if(Resetting==NO_RESET)
	{
		m_pVoteOptionHeap = new CHeap();
		m_pScore = 0;
		m_NumMutes = 0;
		m_NumVoteMutes = 0;
		for (int i = 0; i < NUM_HOUSES; i++)
			m_pHouses[i] = 0;
		for (int i = 0; i < NUM_MINIGAMES; i++)
			m_pMinigames[i] = 0;
		m_NumAccountSystemBans = 0;
	}

	m_aDeleteTempfile[0] = 0;
	m_ChatResponseTargetID = -1;
	m_TeeHistorianActive = false;

	m_pRandomMapResult = nullptr;
	m_pMapVoteResult = nullptr;
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;

	if (m_pScore)
		delete m_pScore;

	for (int i = 0; i < NUM_HOUSES; i++)
		if (m_pHouses[i])
			delete m_pHouses[i];

	for (int i = 0; i < NUM_MINIGAMES; i++)
		if (m_pMinigames[i])
			delete m_pMinigames[i];
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;
	CVotingMenu VotingMenu = m_VotingMenu;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
	m_VotingMenu = VotingMenu;
}


void CGameContext::TeeHistorianWrite(const void *pData, int DataSize, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	io_write(pSelf->m_TeeHistorianFile, pData, DataSize);
}

void CGameContext::CommandCallback(int ClientID, int FlagMask, const char *pCmd, IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	if(pSelf->m_TeeHistorianActive)
	{
		pSelf->m_TeeHistorian.RecordConsoleCommand(ClientID, FlagMask, pCmd, pResult);
	}
}

class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

CTuningParams *CGameContext::TuningFromChrOrZone(int ClientID, int Zone)
{
	if(GetPlayerChar(ClientID))
		return GetPlayerChar(ClientID)->Tuning();
	if(Zone > 0)
		return &TuningList()[Zone];
	return &m_Tuning;
}

bool CGameContext::SetLockedTune(LOCKED_TUNES *pLockedTunings, CLockedTune &Tune)
{
	const char *pParam = Tune.m_aParam;
	float NewValue = Tune.m_Value;

	float GlobalValue;
	if(!m_Tuning.Get(pParam, &GlobalValue))
		return false;

	for(unsigned int i = 0; i < pLockedTunings->size(); i++)
	{
		if(str_comp_nocase(pLockedTunings->at(i).m_aParam, pParam) == 0)
		{
			if(NewValue == GlobalValue)
				pLockedTunings->erase(pLockedTunings->begin() + i);
			else
				pLockedTunings->at(i).m_Value = NewValue;
			return true;
		}
	}

	CLockedTune LockedTune(pParam, NewValue);
	pLockedTunings->push_back(LockedTune);
	return true;
}

void CGameContext::ApplyTuneLock(LOCKED_TUNES *pLockedTunings, int TuneLock)
{
	if(TuneLock < 0 || TuneLock >= NUM_TUNEZONES)
	{
		pLockedTunings->clear();
		return;
	}

	for(unsigned int i = 0; i < LockedTuning()[TuneLock].size(); i++)
		SetLockedTune(pLockedTunings, LockedTuning()[TuneLock][i]);
}

CTuningParams *CGameContext::ApplyLockedTunings(CTuningParams *pTuning, LOCKED_TUNES &LockedTunings)
{
	static CTuningParams Tuning;
	Tuning = *pTuning;
	for(unsigned int i = 0; i < LockedTunings.size(); i++)
		Tuning.Set(LockedTunings[i].m_aParam, LockedTunings[i].m_Value);
	return &Tuning;
}

void CGameContext::SetBotDetected(int ClientID)
{
	if (m_apPlayers[ClientID])
		m_apPlayers[ClientID]->m_BotDetected = true;
}

void CGameContext::FillAntibot(CAntibotRoundData *pData)
{
	if(!pData->m_Map.m_pTiles)
	{
		Collision()->FillAntibot(&pData->m_Map);
	}
	pData->m_Tick = Server()->Tick();
	mem_zero(pData->m_aCharacters, sizeof(pData->m_aCharacters));
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CAntibotCharacterData *pChar = &pData->m_aCharacters[i];
		for(auto &LatestInput : pChar->m_aLatestInputs)
		{
			LatestInput.m_TargetX = -1;
			LatestInput.m_TargetY = -1;
		}
		pChar->m_Alive = false;
		pChar->m_Pause = false;
		pChar->m_Team = -1;

		pChar->m_Pos = vec2(-1, -1);
		pChar->m_Vel = vec2(0, 0);
		pChar->m_Angle = -1;
		pChar->m_HookedPlayer = -1;
		pChar->m_SpawnTick = -1;
		pChar->m_WeaponChangeTick = -1;

		if(m_apPlayers[i] && !m_apPlayers[i]->m_IsDummy)
		{
			str_copy(pChar->m_aName, Server()->ClientName(i), sizeof(pChar->m_aName));
			CCharacter *pGameChar = m_apPlayers[i]->GetCharacter();
			pChar->m_Alive = (bool)pGameChar;
			pChar->m_Pause = m_apPlayers[i]->IsPaused();
			pChar->m_Team = m_apPlayers[i]->GetTeam();
			if(pGameChar)
			{
				pGameChar->FillAntibot(pChar);
			}
		}
	}
}

void CGameContext::CreateDamage(vec2 Pos, int Id, vec2 Source, int HealthAmount, int ArmorAmount, bool Self, Mask128 Mask, int SevendownAmount)
{
	float a = 3 * 3.14159f / 2 + -atan2(Source.x, Source.y);
	float s = a-pi/3;
	float e = a+pi/3;

	if (SevendownAmount == 0)
		SevendownAmount = HealthAmount+ArmorAmount;
	for(int i = 0; i < SevendownAmount; i++)
	{
		float f = mix(s, e, float(i+1)/float(SevendownAmount+2));
		int *pEvent = (int*)m_Events.Create(20 + NUM_NETOBJTYPES, 3*4, Mask);
		if(pEvent)
		{
			((int*)pEvent)[0] = (int)Pos.x;
			((int*)pEvent)[1] = (int)Pos.y;
			((int*)pEvent)[2] = (int)(f*256.0f);
		}
	}

	float f = angle(Source);
	CNetEvent_Damage *pEvent = (CNetEvent_Damage *)m_Events.Create(NETEVENTTYPE_DAMAGE, sizeof(CNetEvent_Damage), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = Id;
		pEvent->m_Angle = (int)(f*256.0f);
		pEvent->m_HealthAmount = clamp(HealthAmount, 0, 9);
		pEvent->m_ArmorAmount = clamp(ArmorAmount, 0, 9);
		pEvent->m_Self = Self;
	}
}

void CGameContext::CreateHammerHit(vec2 Pos, Mask128 Mask)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}


void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, Mask128 Mask)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	// deal damage
	CEntity *apEnts[MAX_CLIENTS];
	float Radius = g_pData->m_Explosion.m_Radius;
	float InnerRadius = 48.0f;

	int Types = (1<<CGameWorld::ENTTYPE_CHARACTER);
	if (Config()->m_SvInteractiveDrops)
		Types |= (1<<CGameWorld::ENTTYPE_FLAG) | (1<<CGameWorld::ENTTYPE_PICKUP_DROP) | (1<<CGameWorld::ENTTYPE_MONEY) | (1<<CGameWorld::ENTTYPE_GROG);
	int Num = m_World.FindEntitiesTypes(Pos, Radius, (CEntity * *)apEnts, MAX_CLIENTS, Types);
	Mask128 TeamMask = Mask128();
	for (int i = 0; i < Num; i++)
	{
		CCharacter *pChr = 0;
		CAdvancedEntity *pEnt = 0;
		bool IsCharacter = apEnts[i]->GetObjType() == CGameWorld::ENTTYPE_CHARACTER;
		if (IsCharacter)
		{
			pChr = (CCharacter *)apEnts[i];
		}
		else
		{
			pEnt = (CAdvancedEntity *)apEnts[i];
			if (pEnt->GetObjType() == CGameWorld::ENTTYPE_FLAG)
			{
				if (((CFlag *)pEnt)->GetCarrier())
					continue;
			}
			else
				pChr = pEnt->GetOwner();
		}

		vec2 Diff = apEnts[i]->GetPos() - Pos;
		vec2 ForceDir(0, 1);
		float l = length(Diff);
		if (l)
			ForceDir = normalize(Diff);
		l = 1 - clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);

		int TuneZone = (Owner == -1 || !m_apPlayers[Owner]) ? 0 : m_apPlayers[Owner]->m_TuneZone;
		float Strength = TuningFromChrOrZone(Owner, TuneZone)->m_ExplosionStrength;

		float Dmg = Strength * l;
		if (!(int)Dmg) continue;

		if ((GetPlayerChar(Owner) ? !(GetPlayerChar(Owner)->m_Hit & CCharacter::DISABLE_HIT_GRENADE) : Config()->m_SvHit || NoDamage) || (pChr && Owner == pChr->GetPlayer()->GetCID()))
		{
			if (Owner != -1 && pChr && pChr->IsAlive() && !pChr->CanCollide(Owner)) continue;
			if (Owner == -1 && ActivatedTeam != -1 && pChr && pChr->IsAlive() && pChr->Team() != ActivatedTeam) continue;

			// Explode at most once per team
			int PlayerTeam = pChr ? ((CGameControllerDDRace*)m_pController)->m_Teams.m_Core.Team(pChr->GetPlayer()->GetCID()) : 0;
			if (GetPlayerChar(Owner) ? GetPlayerChar(Owner)->m_Hit & CCharacter::DISABLE_HIT_GRENADE : !Config()->m_SvHit || NoDamage)
			{
				if (!CmaskIsSet(TeamMask, PlayerTeam)) continue;
				TeamMask = CmaskUnset(TeamMask, PlayerTeam);
			}

			vec2 Force = ForceDir * Dmg * 2;
			if (IsCharacter)
			{
				pChr->TakeDamage(Force, ForceDir*-1, (int)Dmg, Owner, Weapon);
			}
			else
			{
				if (pEnt->GetObjType() == CGameWorld::ENTTYPE_FLAG)
					((CFlag *)pEnt)->SetAtStand(false);

				vec2 Temp = pEnt->GetVel() + Force;
				pEnt->SetVel(ClampVel(pEnt->GetMoveRestrictions(), Temp));
			}
		}
	}
}

void CGameContext::CreatePlayerSpawn(vec2 Pos, Mask128 Mask)
{
	// create the event
	CNetEvent_Spawn *pEvent = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID, Mask128 Mask)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateFinishConfetti(vec2 Pos, Mask128 Mask)
{
	// create the event
	CNetEvent_Finish *pEvent = (CNetEvent_Finish *)m_Events.Create(NETEVENTTYPE_FINISH, sizeof(CNetEvent_Finish), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, Mask128 Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::SendChatMsg(CNetMsg_Sv_Chat *pMsg, int Flags, int To)
{
	if (Server()->IsSevendown(To) || str_length(pMsg->m_pMessage) < 128)
	{
		Server()->SendPackMsg(pMsg, Flags, To);
		return;
	}

	const char *pText = pMsg->m_pMessage;

	for (int i = 0; i < 2; i++)
	{
		char aTemp[128];
		for (int pos = 0; pos < 128-1; pos++)
		{
			char c = pMsg->m_pMessage[pos+(i*128)-i];
			aTemp[pos] = c;
			if (c == 0)
				break;
		}
		aTemp[128-1] = 0;
		pMsg->m_pMessage = aTemp;
		Server()->SendPackMsg(pMsg, Flags, To);
		pMsg->m_pMessage = pText;
	}
}

void CGameContext::SendChatTarget(int To, const char *pText, int Flags)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Mode = CHAT_ALL;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	Msg.m_TargetID = -1;

	if (To == -1)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if ((Server()->IsSevendown(i) && !(Flags&CHAT_SEVENDOWN))
				|| (!Server()->IsSevendown(i) && !(Flags&CHAT_SEVEN)))
				continue;

			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
	else
	{
		if ((Server()->IsSevendown(To) && !(Flags&CHAT_SEVENDOWN))
			|| (!Server()->IsSevendown(To) && !(Flags&CHAT_SEVEN)))
			return;

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendChatTeam(int Team, const char *pText)
{
	for(int i = 0; i<MAX_CLIENTS; i++)
		if(((CGameControllerDDRace*)m_pController)->m_Teams.m_Core.Team(i) == Team)
			SendChatTarget(i, pText);
}

void CGameContext::SendModLogMessage(int ClientID, const char *pMsg)
{
	if (ClientID < 0)
		return;

	char aName[128];
	str_format(aName, sizeof(aName), "%s (%s)", Server()->ClientName(ClientID), Server()->GetAuthIdent(ClientID));
	Server()->SendWebhookMessage(Config()->m_SvWebhookModLogURL, pMsg, aName, FormatURL(GetAvatarURL(ClientID)));
}

const char *CGameContext::GetAvatarURL(int ClientID)
{
	static char aAvatarURL[256];
	char aParameters[256];

	if (Config()->m_SvWebhookChatSkinRenderer == 0) // skins.tw
	{
		const char *pSkinName = m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_SkinName[0] ? m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_SkinName : "default";
		str_format(aAvatarURL, sizeof(aAvatarURL), "https://teedata.net/api/skin/render/name/%s", pSkinName);

		if (m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_UseCustomColor)
		{
			str_format(aParameters, sizeof(aParameters), "?bodyColor=%d&footColor=%d&colorFormat=code", m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_ColorBody, m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_ColorFeet);
			str_append(aAvatarURL, aParameters, sizeof(aParameters));
		}
	}
	else if (Config()->m_SvWebhookChatSkinRenderer == 1) // KoG
	{
		str_format(aAvatarURL, sizeof(aAvatarURL), "https://kog.tw/render_tee.php?skin=%s", m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_SkinName);

		if (m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_UseCustomColor)
		{
			str_format(aParameters, sizeof(aParameters), "&body_color=%d&feet_color=%d", m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_ColorBody, m_apPlayers[ClientID]->m_TeeInfos.m_Sevendown.m_ColorFeet);
			str_append(aAvatarURL, aParameters, sizeof(aParameters));
		}
	}

	return aAvatarURL;
}

void CGameContext::SendChat(int ChatterClientID, int Mode, int To, const char *pText, int SpamProtectionClientID, int Flags)
{
	if (SpamProtectionClientID >= 0 && SpamProtectionClientID < MAX_CLIENTS)
		if (ProcessSpamProtection(SpamProtectionClientID))
			return;

	// client id used to check against muted
	int MuteChecked = ChatterClientID;

	char aBuf[512], aText[256];
	if (Mode == CHAT_POLICE_CHANNEL)
	{
		str_format(aBuf, sizeof(aBuf), "[POLICE-CHANNEL] %s", pText);
		str_copy(aText, aBuf, sizeof(aText));
	}
	else
	{
		str_copy(aText, pText, sizeof(aText));
	}

	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
	{
		// Can happen when translating a player message and that player left the server before translator finished...
		if (!m_apPlayers[ChatterClientID])
			return;

		// dont trigger updating of teams twice. this means people who translate chat will probably receive the color update before their chat msg appeared
		// CHAT_SINGLE and CHAT_SINGLE_TEAM are used for translating aswell, so they would cause that
		if (Mode == CHAT_ALL || Mode == CHAT_ATEVERYONE)
			m_RainbowName.OnChatMessage(ChatterClientID);

		// join local or public chat
		bool Local = Mode == CHAT_TEAM;
		if ((Mode == CHAT_ALL || Mode == CHAT_TEAM) && m_apPlayers[ChatterClientID]->JoinChat(Local))
			Mode = Local ? CHAT_LOCAL : CHAT_ALL;

		if (Mode == CHAT_WHISPER)
			str_format(aBuf, sizeof(aBuf), "%d:%d:%s -> %d:%s: %s", ChatterClientID, Mode, Server()->ClientName(ChatterClientID), To, Server()->ClientName(To), aText);
		else
			str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Mode, Server()->ClientName(ChatterClientID), aText);
	}
	else if (ChatterClientID == -2)
	{
		str_format(aBuf, sizeof(aBuf), "### %s", aText);
		str_copy(aText, aBuf, sizeof(aText));
		ChatterClientID = -1;
		// if '/me' is used, still dont send the message when sender is muted
		MuteChecked = SpamProtectionClientID;
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "*** %s", aText);
	}

	const char *pModeStr;
	if (Mode == CHAT_WHISPER)
		pModeStr = Config()->m_SvWhisperLog ? "whisper" : 0;
	else if(Mode == CHAT_SINGLE)
		pModeStr = 0;
	else if(Mode == CHAT_TEAM)
		pModeStr = "teamchat";
	else if(Mode == CHAT_POLICE_CHANNEL)
		pModeStr = "police";
	else
		pModeStr = "chat";

	if(pModeStr)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, pModeStr, aBuf);
	}

	if (!(Flags&CHAT_NO_WEBHOOK) && (Mode == CHAT_ALL || Mode == CHAT_ATEVERYONE || Mode == CHAT_POLICE_CHANNEL ||
		(Mode == CHAT_TEAM && ChatterClientID >= 0 && GetDDRaceTeam(ChatterClientID) == 0)))
	{
		char aWebhookName[32];
		char aAvatarURL[256];
		str_copy(aWebhookName, "[Server]", sizeof(aWebhookName));
		str_copy(aAvatarURL, Config()->m_SvWebhookChatAvatarURL, sizeof(aAvatarURL));

		if (ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		{
			str_format(aWebhookName, sizeof(aWebhookName), "%s [%d]", Server()->ClientName(ChatterClientID), m_Accounts[m_apPlayers[ChatterClientID]->GetAccID()].m_Level);
			if (Config()->m_SvWebhookChatSkinAvatars)
				str_copy(aAvatarURL, GetAvatarURL(ChatterClientID), sizeof(aAvatarURL));
		}

		Server()->SendWebhookMessage(Config()->m_SvWebhookChatURL, aText, aWebhookName, FormatURL(aAvatarURL));
	}

	if (Mode == CHAT_ALL || Mode == CHAT_TEAM || Mode == CHAT_LOCAL)
		Server()->TranslateChat(ChatterClientID, aText, Mode);

	CNetMsg_Sv_Chat Msg;
	Msg.m_Mode = Mode;
	Msg.m_ClientID = ChatterClientID;
	Msg.m_pMessage = aText;
	Msg.m_TargetID = -1;

	int MsgFlags = 0;
	if (ChatterClientID >= 0 && !m_apPlayers[ChatterClientID]->m_ShowName)
		MsgFlags |= MSGFLAG_NONAME;

	if(Mode == CHAT_ALL)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
			if (!IsMuted(MuteChecked, i) && CanReceiveMessage(ChatterClientID, i) && !str_comp(Server()->GetLanguage(i), "none"))
			{
				bool Send = (Server()->IsSevendown(i) && (Flags&CHAT_SEVENDOWN)) || (!Server()->IsSevendown(i) && (Flags&CHAT_SEVEN));
				if (Send)
					SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL, i);
			}
	}
	else if(Mode == CHAT_TEAM)
	{
		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MsgFlags|MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		CTeamsCore* Teams = &((CGameControllerDDRace*)m_pController)->m_Teams.m_Core;

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] != 0)
			{
				if (IsMuted(MuteChecked, i) || !CanReceiveMessage(ChatterClientID, i) || str_comp(Server()->GetLanguage(i), "none"))
					continue;

				if(m_apPlayers[ChatterClientID]->GetTeam() == TEAM_SPECTATORS)
				{
					if(m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)
					{
						SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
					}
				}
				else
				{
					if(Teams->Team(i) == GetDDRaceTeam(ChatterClientID) && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
					{
						SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
					}
				}
			}
		}
	}
	else if (Mode == CHAT_SINGLE || Mode == CHAT_SINGLE_TEAM)
	{
		// send to the clients
		Msg.m_Mode = Mode == CHAT_SINGLE_TEAM ? CHAT_TEAM : CHAT_ALL;
		if (!IsMuted(MuteChecked, To))
			SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL, To);
	}
	else if (Mode == CHAT_ATEVERYONE)
	{
		// send to the clients
		Msg.m_Mode = CHAT_ALL;

		char aMsg[256];
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (m_apPlayers[i])
			{
				str_format(aMsg, sizeof(aMsg), "%s: %s", Server()->ClientName(i), aText);
				Msg.m_pMessage = aMsg;
				SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL, i);
			}
		}
	}
	else if (Mode == CHAT_LOCAL)
	{
		// send to the clients
		Msg.m_Mode = CHAT_TEAM;

		for (int i = 0; i < MAX_CLIENTS; i++)
			if (!IsMuted(MuteChecked, i) && IsLocal(ChatterClientID, i) && !str_comp(Server()->GetLanguage(i), "none"))
				SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL, i);
	}
	else if (Mode == CHAT_POLICE_CHANNEL)
	{
		Msg.m_Mode = CHAT_ALL;
		for (int i = 0; i < MAX_CLIENTS; i++)
			if (m_apPlayers[i] && m_Accounts[m_apPlayers[i]->GetAccID()].m_PoliceLevel)
				SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL, i);
	}
	else // Mode == CHAT_WHISPER
	{
		if (To < 0 || To >= MAX_CLIENTS || !Server()->ClientIngame(To))
		{
			char aMsg[32];
			str_format(aMsg, sizeof(aMsg), "Invalid whisper");
			SendChatTarget(ChatterClientID, aMsg);
			return;
		}

		m_apPlayers[ChatterClientID]->m_LastWhisperTo = To;

		// send to target
		if (!IsMuted(MuteChecked, To))
		{
			Msg.m_Mode = CHAT_WHISPER_RECV;
			Msg.m_TargetID = To;
			SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL, To);
		}

		// send to sender
		{
			MsgFlags = 0;
			if (!m_apPlayers[To]->m_ShowName)
				MsgFlags |= MSGFLAG_NONAME;

			// reset ids bcs they got translated
			Msg.m_Mode = CHAT_WHISPER_SEND;
			Msg.m_TargetID = To;
			Msg.m_ClientID = ChatterClientID;
			SendChatMsg(&Msg, MsgFlags|MSGFLAG_VITAL, ChatterClientID);
		}
	}
}

void CGameContext::SendBroadcast(const char* pText, int ClientID, bool IsImportant)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;

	if (ClientID == -1)
	{
		dbg_assert(IsImportant, "broadcast messages to all players must be important");
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (m_apPlayers[i])
			{
				m_apPlayers[i]->m_LastBroadcastImportance = true;
				m_apPlayers[i]->m_LastBroadcast = Server()->Tick();
			}
		}
		return;
	}

	if (!m_apPlayers[ClientID])
		return;

	if (!IsImportant && m_apPlayers[ClientID]->m_LastBroadcastImportance && m_apPlayers[ClientID]->m_LastBroadcast > Server()->Tick() - Server()->TickSpeed() * 10)
		return;

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	m_apPlayers[ClientID]->m_LastBroadcast = Server()->Tick();
	m_apPlayers[ClientID]->m_LastBroadcastImportance = IsImportant;
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	if (m_apPlayers[ClientID])
	{
		if (m_apPlayers[ClientID]->m_SpookyGhost)
			Msg.m_Emoticon = EMOTICON_GHOST;
		else if (GetPlayerChar(ClientID) && GetPlayerChar(ClientID)->GetActiveWeapon() == WEAPON_HEART_GUN)
			Msg.m_Emoticon = EMOTICON_HEARTS;
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	// dont send so the client doesnt auto switch the weapon and stops editing
	CCharacter *pChr = GetPlayerChar(ClientID);
	if (!pChr || pChr->m_DrawEditor.Active() || (pChr->GetActiveWeapon() == WEAPON_NINJA && !pChr->m_ScrollNinja))
		return;

	// include ninja, client doesnt auto switch to ninja on pickup, or when we have no weapon at all
	if (Weapon >= NUM_VANILLA_WEAPONS-1 || pChr->GetActiveWeaponUnclamped() == -1)
	{
		pChr->SetQueuedWeapon(Weapon);
	}
	else
	{
		CNetMsg_Sv_WeaponPickup Msg;
		Msg.m_Weapon = Weapon;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
}

void CGameContext::SendSettings(int ClientID)
{
	CNetMsg_Sv_ServerSettings Msg;
	Msg.m_KickVote = Config()->m_SvVoteKick;
	Msg.m_KickMin = Config()->m_SvVoteKickMin;
	Msg.m_SpecVote = Config()->m_SvVoteSpectate;
	Msg.m_TeamLock = m_LockTeams != 0;
	Msg.m_TeamBalance = 0;
	Msg.m_PlayerSlots = clamp(Config()->m_SvPlayerSlots, 0, (int)VANILLA_MAX_CLIENTS);
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendSkinChange(CTeeInfo TeeInfos, int ClientID, int TargetID)
{
	CNetMsg_Sv_SkinChange Msg;
	Msg.m_ClientID = ClientID;
	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		Msg.m_apSkinPartNames[p] = TeeInfos.m_aaSkinPartNames[p];
		Msg.m_aUseCustomColors[p] = TeeInfos.m_aUseCustomColors[p];
		Msg.m_aSkinPartColors[p] = TeeInfos.m_aSkinPartColors[p];
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, TargetID);

	// F-DDrace
	m_apPlayers[ClientID]->m_CurrentInfo.m_TeeInfos = TeeInfos;
}

void CGameContext::SendGameMsg(int GameMsgID, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Msg.AddInt(ParaI2);
	Msg.AddInt(ParaI3);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendChatCommand(const CCommandManager::CCommand *pCommand, int ClientID)
{
	if (ClientID == -1)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
			SendChatCommand(pCommand, i);
		return;
	}

	if (Server()->IsSevendown(ClientID))
	{
		CNetMsg_Sv_CommandInfoEx Msg;
		Msg.m_pName = pCommand->m_aName;
		Msg.m_pArgsFormat = pCommand->m_aArgsFormat;
		Msg.m_pHelpText = pCommand->m_aHelpText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
	}
	else
	{
		CNetMsg_Sv_CommandInfo Msg;
		Msg.m_Name = pCommand->m_aName;
		Msg.m_HelpText = pCommand->m_aHelpText;
		Msg.m_ArgsFormat = pCommand->m_aArgsFormat;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
}

void CGameContext::SendChatCommands(int ClientID)
{
	// F-DDrace
	// Remove the clientside commands for 0.7 (expect w and whisper)
	if (!Server()->IsSevendown(ClientID))
	{
		SendRemoveChatCommand("all", ClientID);
		SendRemoveChatCommand("friend", ClientID);
		SendRemoveChatCommand("m", ClientID);
		SendRemoveChatCommand("mute", ClientID);
		SendRemoveChatCommand("r", ClientID);
		SendRemoveChatCommand("team", ClientID);
	}

	for(int i = 0; i < CommandManager()->CommandCount(); i++)
	{
		SendChatCommand(CommandManager()->GetCommand(i), ClientID);
	}
}

void CGameContext::SendRemoveChatCommand(const CCommandManager::CCommand *pCommand, int ClientID)
{
	CNetMsg_Sv_CommandInfoRemove Msg;
	Msg.m_Name = pCommand->m_aName;

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendRemoveChatCommand(const char *pName, int ClientID)
{
	CNetMsg_Sv_CommandInfoRemove Msg;
	Msg.m_Name = pName;

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason, const char *pSevendownDesc)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*VOTE_TIME;
	m_VoteCancelTime = time_get() + time_freq()*VOTE_CANCEL_TIME;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aSevendownVoteDescription, pSevendownDesc, sizeof(m_aSevendownVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(m_VoteType, -1);
	m_VoteUpdate = true;
}

void CGameContext::EndVote(int Type, bool Force)
{
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	if(Force)
		m_VoteCreator = -1;
	SendVoteSet(Type, -1);
}

void CGameContext::ForceVote(int Type, const char *pDescription, const char *pReason)
{
	CNetMsg_Sv_VoteSet Msg;
	Msg.m_Type = Type;
	Msg.m_Timeout = 0;
	Msg.m_ClientID = -1;
	Msg.m_pDescription = pDescription;
	Msg.m_pReason = pReason;

	CMsgPacker MsgSevendown(NETMSGTYPE_SV_VOTESET);
	MsgSevendown.AddInt(0);
	MsgSevendown.AddString(pDescription, -1);
	MsgSevendown.AddString(pReason, -1);

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if(Server()->IsSevendown(i))
			Server()->SendMsg(&MsgSevendown, MSGFLAG_VITAL, i);
		else
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::SendVoteSet(int Type, int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_ClientID = m_VoteCreator;
		Msg.m_Type = Type;
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Type = Type;
		Msg.m_Timeout = 0;
		Msg.m_ClientID = m_VoteCreator;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}

	CMsgPacker MsgSevendown(NETMSGTYPE_SV_VOTESET);
	if(m_VoteCloseTime)
	{
		MsgSevendown.AddInt((m_VoteCloseTime-time_get())/time_freq());
		MsgSevendown.AddString(m_aSevendownVoteDescription, -1);
		MsgSevendown.AddString(m_aVoteReason, -1);
	}
	else
	{
		MsgSevendown.AddInt(0);
		MsgSevendown.AddString("", -1);
		MsgSevendown.AddString("", -1);
	}

	if (ClientID == -1)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (!m_apPlayers[i])
				continue;

			if (!Server()->IsSevendown(i))
			{
				m_World.ForceInsertPlayer(m_VoteCreator, i); // 0.7 clients need the client id in order to show the vote and the name of the caller, so its important that we get him in
				int id = m_VoteCreator;
				Server()->Translate(id, i);
				Msg.m_ClientID = id;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
				Msg.m_ClientID = m_VoteCreator;
			}
			else
			{
				Server()->SendMsg(&MsgSevendown, MSGFLAG_VITAL, i);
			}
		}
	}
	else
	{
		if (!Server()->IsSevendown(ClientID))
		{
			m_World.ForceInsertPlayer(m_VoteCreator, ClientID);
			int id = m_VoteCreator;
			Server()->Translate(id, ClientID);
			Msg.m_ClientID = id;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
			Msg.m_ClientID = m_VoteCreator;
		}
		else
		{
			Server()->SendMsg(&MsgSevendown, MSGFLAG_VITAL, ClientID);
		}
	}
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && (str_startswith(m_aVoteCommand, "kick ") ||
		str_startswith(m_aVoteCommand, "set_team ") || (str_startswith(m_aVoteCommand, "ban ") && Server()->IsBanned(ClientID))))
		m_VoteCloseTime = -1;
}

void CGameContext::AbortVoteOnTeamChange(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && str_startswith(m_aVoteCommand, "set_team "))
		m_VoteCloseTime = -1;
}

void CGameContext::SendTuningParams(int ClientID, int Zone)
{
	if (ClientID == -1)
	{
		for (int i = 0; i < MAX_CLIENTS; ++i)
		{
			if (m_apPlayers[i])
			{
				if (m_apPlayers[i]->GetCharacter())
				{
					if (m_apPlayers[i]->GetCharacter()->m_TuneZone == Zone)
						SendTuningParams(i, Zone);
				}
				else if (m_apPlayers[i]->m_TuneZone == Zone)
				{
					SendTuningParams(i, Zone);
				}
			}
		}
		return;
	}

	// F-DDrace
	static CTuningParams Tunings;
	CTuningParams *pTunings = Zone > 0 ? &TuningList()[Zone] : Tuning();
	CCharacter *pChr = GetPlayerChar(ClientID);
	if (pChr)
		Tunings = *ApplyLockedTunings(pTunings, pChr->m_LockedTunings);
	else
		Tunings = *pTunings;

	// set projectile tunings to normal ones, if they are different in zones for example its handled in CProjectile
	Tunings.m_GrenadeCurvature = m_Tuning.m_GrenadeCurvature;
	Tunings.m_GrenadeSpeed = m_Tuning.m_GrenadeSpeed;
	Tunings.m_ShotgunCurvature = m_Tuning.m_ShotgunCurvature;
	Tunings.m_ShotgunSpeed = m_Tuning.m_ShotgunSpeed;
	Tunings.m_GunCurvature = m_Tuning.m_GunCurvature;
	Tunings.m_GunSpeed = m_Tuning.m_GunSpeed;

	if (pChr)
	{
		if (pChr->m_FakeTuneCollision || pChr->m_InSnake)
			Tunings.m_PlayerCollision = 0.f;
		if ((pChr->m_Passive && !pChr->m_Super) || pChr->m_Snake.Active())
			Tunings.m_PlayerHooking = 0.f;

		if (pChr->m_DrawEditor.Active() || pChr->m_pHelicopter || pChr->m_Snake.Active())
			Tunings.m_HookFireSpeed = 0.f;
		if (pChr->m_pHelicopter || pChr->m_Snake.Active())
			Tunings.m_HookDragAccel = 0.f;
		if (pChr->m_pHelicopter || pChr->m_InSnake)
			Tunings.m_HookDragSpeed = 0.f;

		if (pChr->m_DrawEditor.Active() || pChr->m_pHelicopter|| pChr->m_InSnake
			|| (!Server()->IsSevendown(ClientID) && ((pChr->m_FreezeTime && Config()->m_SvFreezePrediction) || pChr->GetPlayer()->m_TeeControllerID != -1)))
		{
			Tunings.m_GroundControlSpeed = 0.f;
			Tunings.m_GroundJumpImpulse = 0.f;
			Tunings.m_GroundControlAccel = 0.f;
			Tunings.m_AirControlSpeed = 0.f;
			Tunings.m_AirJumpImpulse = 0.f;
			Tunings.m_AirControlAccel = 0.f;
		}

		if (pChr->m_MoveRestrictions&CANTMOVE_DOWN_LASERDOOR || pChr->m_pHelicopter || pChr->m_InSnake)
			Tunings.m_Gravity = 0.f;
	}

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int* pParams = (int*)&Tunings;

	unsigned int last = sizeof(m_Tuning) / sizeof(int);
	for (unsigned i = 0; i < last; i++)
	{
		if (i >= NUM_DDNET_TUNES)
			break;

		if(i == 30 && Server()->IsSevendown(ClientID)) // laser damage
			Msg.AddInt(500); // 5 is default value
		Msg.AddInt(pParams[i]);
	}
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnTick()
{
	if(m_TeeHistorianActive)
	{
		if(!m_TeeHistorian.Starting())
		{
			m_TeeHistorian.EndInputs();
			m_TeeHistorian.EndTick();
		}
		io_flush(m_TeeHistorianFile);
		m_TeeHistorian.BeginTick(Server()->Tick());
		m_TeeHistorian.BeginPlayers();
	}

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for (int i = 0; i < NUM_MINIGAMES; i++)
		m_pMinigames[i]->Tick();

	m_RainbowName.Tick();
	// has to happen before playerticks, as the wanted players get added there and are resetted after CVotingMenu::Tick
	m_VotingMenu.Tick();

	if(m_TeeHistorianActive)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetCharacter())
			{
				CNetObj_CharacterCore Char;
				m_apPlayers[i]->GetCharacter()->GetCore().Write(&Char);
				m_TeeHistorian.RecordPlayer(i, &Char);
			}
			else
			{
				m_TeeHistorian.RecordDeadPlayer(i);
			}
		}
		m_TeeHistorian.EndPlayers();
		io_flush(m_TeeHistorianFile);
		m_TeeHistorian.BeginInputs();
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(pPlayer)
		{
			// Do it safely here so we dont get any crashes
			if (pPlayer->m_BotDetected)
			{
				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "Bot detected (%s)", Server()->ClientName(i));
				Server()->Ban(i, 60*Config()->m_SvAntibotBanMinutes, aBuf);
				continue;
			}

			// send vote options
			ProgressVoteOptions(i);

			pPlayer->Tick();
			pPlayer->PostTick();

			// F-DDrace
			for (int j = 0; j < NUM_HOUSES; j++)
				m_pHouses[j]->Tick(i);
		}
	}

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (m_apPlayers[i])
			m_apPlayers[i]->PostPostTick();
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CHAT_ALL, -1, "Vote aborted", -1, CHAT_SEVENDOWN);
			EndVote(VOTE_END_ABORT, false);
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES || (m_VoteUpdate && Yes >= Total/2+1))
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				if(m_VoteCreator != -1 && m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;

				SendChat(-1, CHAT_ALL, -1, "Vote passed", -1, CHAT_SEVENDOWN);
				EndVote(VOTE_END_PASS, m_VoteEnforce==VOTE_ENFORCE_YES);
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || (m_VoteUpdate && No >= (Total+1)/2) || time_get() > m_VoteCloseTime)
			{
				SendChat(-1, CHAT_ALL, -1, "Vote failed", -1, CHAT_SEVENDOWN);
				EndVote(VOTE_END_FAIL, m_VoteEnforce==VOTE_ENFORCE_NO);
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

	for (int i = 0; i < m_NumMutes; i++)
	{
		if (m_aMutes[i].m_Expire <= Server()->Tick())
		{
			m_NumMutes--;
			m_aMutes[i] = m_aMutes[m_NumMutes];
		}
	}
	for (int i = 0; i < m_NumVoteMutes; i++)
	{
		if (m_aVoteMutes[i].m_Expire <= Server()->Tick())
		{
			m_NumVoteMutes--;
			m_aVoteMutes[i] = m_aVoteMutes[m_NumVoteMutes];
		}
	}
	for (int i = 0; i < m_NumAccountSystemBans; i++)
	{
		// either reset if expired or if ip did not get banned reset it after ACC_SYS_BAN_DELAY seconds
		if ((m_aAccountSystemBans[i].m_Expire > 0 && m_aAccountSystemBans[i].m_Expire <= Server()->Tick())
			|| (Server()->Tick() > m_aAccountSystemBans[i].m_LastAttempt + ACC_SYS_BAN_DELAY * Server()->TickSpeed()))
		{
			m_NumAccountSystemBans--;
			m_aAccountSystemBans[i] = m_aAccountSystemBans[m_NumAccountSystemBans];
		}
	}

	if (Server()->Tick() % (Config()->m_SvAnnouncementInterval * Server()->TickSpeed() * 60) == 0)
	{
		const char* Line = Server()->GetAnnouncementLine(Config()->m_SvAnnouncementFileName);
		if (Line)
			SendChat(-1, CHAT_ALL, -1, Line, -1, CHAT_SEVEN|CHAT_SEVENDOWN|CHAT_NO_WEBHOOK);
	}

	if (Collision()->GetNumAllSwitchers() > 0)
		for (int i = 0; i < Collision()->GetNumAllSwitchers() + 1; ++i)
		{
			for (int j = 0; j < VANILLA_MAX_CLIENTS; ++j)
			{
				// F-DDrace
				// set current switcher client id to -1 if the player doesnt exist OR it is a non-timed switch and the player is not on the switch anymore
				if ((Collision()->m_pSwitchers[i].m_ClientID[j] != -1 && !m_apPlayers[Collision()->m_pSwitchers[i].m_ClientID[j]])
					|| (Collision()->m_pSwitchers[i].m_StartTick[j] < Server()->Tick() && Collision()->m_pSwitchers[i].m_EndTick[j] == 0))
					Collision()->m_pSwitchers[i].m_ClientID[j] = -1;

				// if it is a timed switch, the client id will be reset after the time is over here
				if (Collision()->m_pSwitchers[i].m_EndTick[j] <= Server()->Tick() && Collision()->m_pSwitchers[i].m_Type[j] == TILE_SWITCHTIMEDOPEN)
				{
					Collision()->m_pSwitchers[i].m_Status[j] = false;
					Collision()->m_pSwitchers[i].m_EndTick[j] = 0;
					Collision()->m_pSwitchers[i].m_Type[j] = TILE_SWITCHCLOSE;
					Collision()->m_pSwitchers[i].m_ClientID[j] = -1;
				}
				else if (Collision()->m_pSwitchers[i].m_EndTick[j] <= Server()->Tick() && Collision()->m_pSwitchers[i].m_Type[j] == TILE_SWITCHTIMEDCLOSE)
				{
					Collision()->m_pSwitchers[i].m_Status[j] = true;
					Collision()->m_pSwitchers[i].m_EndTick[j] = 0;
					Collision()->m_pSwitchers[i].m_Type[j] = TILE_SWITCHOPEN;
					Collision()->m_pSwitchers[i].m_ClientID[j] = -1;
				}
			}
		}

	if (m_pRandomMapResult && m_pRandomMapResult->m_Done)
	{
		str_copy(Config()->m_SvMap, m_pRandomMapResult->m_aMap, sizeof(Config()->m_SvMap));
		m_pRandomMapResult = nullptr;
	}

	if (m_pMapVoteResult && m_pMapVoteResult->m_Done)
	{
		m_VoteKick = false;
		m_VoteSpec = false;
		m_LastMapVote = time_get();

		char aCmd[256];
		str_format(aCmd, sizeof(aCmd), "sv_reset_file types/%s/flexreset.cfg; change_map \"%s\"", m_pMapVoteResult->m_aServer, m_pMapVoteResult->m_aMap);

		char aChatmsg[512];
		str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(m_pMapVoteResult->m_ClientID), m_pMapVoteResult->m_aMap, "/map");

		CallVote(m_pMapVoteResult->m_ClientID, m_pMapVoteResult->m_aMap, aCmd, "/map", aChatmsg, 0);

		m_pMapVoteResult = nullptr;
	}

	// F-DDrace
	if (Server()->Tick() > m_LastDataSaveTick + Server()->TickSpeed() * Config()->m_SvDataSaveInterval * 60)
	{
		// save all accounts
		dbg_msg("acc", "automatic account saving...");
		for (unsigned int i = ACC_START; i < m_Accounts.size(); i++)
			WriteAccountStats(i);
		for (int i = 0; i < Collision()->m_NumPlots + 1; i++)
			WritePlotStats(i);
		WriteMoneyListFile();
		m_LastDataSaveTick = Server()->Tick();
	}

	// minigames
	if (!m_aMinigameDisabled[MINIGAME_SURVIVAL])
		SurvivalTick();

	for (int i = 0; i < 2; i++)
		if (!m_aMinigameDisabled[i == 0 ? MINIGAME_INSTAGIB_BOOMFNG : MINIGAME_INSTAGIB_FNG])
			InstagibTick(i);

	if (IsFullHour())
	{
		ExpirePlots();
		ExpireSavedIdentities();
	}

	// Check if plot destroy is over, player is not wanted anymore as it seems
	for (int i = PLOT_START; i < Collision()->m_NumPlots + 1; i++)
	{
		if (m_aPlots[i].m_DestroyEndTick && !PlotCanBeRaided(i))
		{
			// Reset door health
			m_aPlots[i].m_DestroyEndTick = 0;
			m_aPlots[i].m_DoorHealth = Config()->m_SvPlotDoorHealth;
			int AccID = GetAccIDByUsername(m_aPlots[i].m_aOwner);
			if (AccID >= ACC_START)
			{
				int ClientID = m_Accounts[AccID].m_ClientID;
				if (ClientID >= 0 && m_apPlayers[ClientID])
				{
					SendChatTarget(ClientID, "Your plot is no longer subject to a search warrant and can no longer be destroyed");
				}
			}
		}
	}

	if (m_LastPlayerCountUpdate + Server()->TickSpeed() * 60 < Server()->Tick())
	{
		SendPlayerCountUpdate();
	}


#ifdef CONF_DEBUG
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->IsDummy())
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[i]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	int NumFailures = m_NetObjHandler.NumObjFailures();
	if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
	{
		if(Config()->m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT failed on '%s'", m_NetObjHandler.FailedObjOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
	}
	else
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);

	if(m_TeeHistorianActive)
	{
		m_TeeHistorian.RecordPlayerInput(ClientID, (CNetObj_PlayerInput *)pInput);
	}
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
	{
		int NumFailures = m_NetObjHandler.NumObjFailures();
		if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
		{
			if(Config()->m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT corrected on '%s'", m_NetObjHandler.FailedObjOn());
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
		else
			m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
	}
}

void CGameContext::OnClientPredictedEarlyInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedEarlyInput((CNetObj_PlayerInput *)pInput);
}

struct CVoteOptionServer *CGameContext::GetVoteOption(int Index)
{
	CVoteOptionServer *pCurrent;
	for (pCurrent = m_pVoteOptionFirst;
			Index > 0 && pCurrent;
			Index--, pCurrent = pCurrent->m_pNext);

	if (Index > 0)
		return 0;
	return pCurrent;
}

void CGameContext::StartResendingVotes(int ClientID, bool ResendVotesPage)
{
	m_VotingMenu.SendPageVotes(ClientID, ResendVotesPage);
}

void CGameContext::ProgressVoteOptions(int ClientID)
{
	CPlayer *pPl = m_apPlayers[ClientID];

	if (pPl->m_SendVoteIndex == -1)
		return;

	if(pPl->m_SendVoteIndex > m_NumVoteOptions)
		return; // shouldn't happen / fail silently

	int VotesLeft = m_NumVoteOptions - pPl->m_SendVoteIndex;
	int NumVotesToSend = min(Config()->m_SvVotesPerTick, VotesLeft);

	if (!VotesLeft)
	{
		// player has up to date vote option list
		return;
	}

	// build vote option list msg
	int CurIndex = 0;

	// get current vote option by index
	CVoteOptionServer *pCurrent = GetVoteOption(pPl->m_SendVoteIndex);

	// pack and send vote list packet
	CMsgPacker Msg(NETMSGTYPE_SV_VOTEOPTIONLISTADD);
	Msg.AddInt(NumVotesToSend);

	m_VotingMenu.OnProgressVoteOptions(ClientID, &Msg, &CurIndex, &pCurrent);

	while(pCurrent && CurIndex < NumVotesToSend)
	{
		Msg.AddString(pCurrent->m_aDescription, VOTE_DESC_LENGTH);
		pCurrent = pCurrent->m_pNext;
		CurIndex++;
	}

	if (Server()->IsSevendown(ClientID))
	{
		while (CurIndex < CVotingMenu::MAX_VOTES_PER_PACKET)
		{
			Msg.AddString("", VOTE_DESC_LENGTH);
			CurIndex++;
		}
	}

	if(pPl->m_SendVoteIndex == 0)
	{
		CNetMsg_Sv_VoteOptionGroupStart StartMsg;
		Server()->SendPackMsg(&StartMsg, MSGFLAG_VITAL, ClientID);
	}

	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);

	pPl->m_SendVoteIndex += NumVotesToSend;
	if(pPl->m_SendVoteIndex == m_NumVoteOptions)
	{
		CNetMsg_Sv_VoteOptionGroupEnd EndMsg;
		Server()->SendPackMsg(&EndMsg, MSGFLAG_VITAL, ClientID);
	}
}

void CGameContext::OnClientEnter(int ClientID)
{
	// F-DDrace
	str_copy(m_apPlayers[ClientID]->m_CurrentInfo.m_aName, Server()->ClientName(ClientID), sizeof(m_apPlayers[ClientID]->m_CurrentInfo.m_aName));
	str_copy(m_apPlayers[ClientID]->m_CurrentInfo.m_aClan, Server()->ClientClan(ClientID), sizeof(m_apPlayers[ClientID]->m_CurrentInfo.m_aClan));
	m_apPlayers[ClientID]->m_CurrentInfo.m_TeeInfos = m_apPlayers[ClientID]->m_TeeInfos;
	m_apPlayers[ClientID]->Respawn();

	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	// load score
	{
		Score()->PlayerData(ClientID)->Reset();
		Score()->LoadScore(ClientID);
		Score()->PlayerData(ClientID)->m_CurrentTime = Score()->PlayerData(ClientID)->m_BestTime;
		m_apPlayers[ClientID]->m_Score = !Score()->PlayerData(ClientID)->m_BestTime ? -1 : Score()->PlayerData(ClientID)->m_BestTime;
	}

	m_VoteUpdate = true;

	if(Server()->DemoRecorder_IsRecording())
	{
		CNetMsg_De_ClientEnter Msg;
		Msg.m_pName = Server()->ClientName(ClientID);
		Msg.m_ClientID = ClientID;
		Msg.m_Team = m_apPlayers[ClientID]->GetTeam();
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	}

	// F-DDrace
	Server()->ExpireServerInfo();

	UpdateHidePlayers();

	if (!Config()->m_SvSilentSpectatorMode || m_apPlayers[ClientID]->GetTeam() != TEAM_SPECTATORS)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), m_pController->GetTeamName(m_apPlayers[ClientID]->GetTeam()));
		int Flags = CHAT_SEVEN|CHAT_SEVENDOWN;
		if (m_apPlayers[ClientID]->m_IsDummy)
			Flags |= CHAT_NO_WEBHOOK;
		SendChat(-1, CHAT_ALL, -1, aBuf, -1, Flags);
	}

	m_World.InitPlayerMap(ClientID);

	if (m_apPlayers[ClientID]->m_IsDummy) // dummies dont need these information
		return;

	m_WhoIs.AddEntry(ClientID);

	IServer::CClientInfo Info;
	Server()->GetClientInfo(ClientID, &Info);
	if(Info.m_GotDDNetVersion)
	{
		if (OnClientDDNetVersionKnown(ClientID))
			return; // kicked
	}

	SendChatTarget(ClientID, "F-DDrace Mod. Version: " GAME_VERSION ", by fokkonaut");
	if (Config()->m_SvWelcome[0] != 0)
		SendChatTarget(ClientID, Config()->m_SvWelcome);

	m_apPlayers[ClientID]->CheckClanProtection();

	SendStartMessages(ClientID);
	SendPlayerCountUpdate();
}

void CGameContext::SendStartMessages(int ClientID)
{
	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(m_VoteType, ClientID);

	// send settings
	SendSettings(ClientID);
	SendChatCommands(ClientID);

	if (!Server()->IsSevendown(ClientID))
	{
		m_pController->UpdateGameInfo(ClientID);
	}
}

void CGameContext::OnClientRejoin(int ClientID)
{
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "'%s' rejoined current session", Server()->ClientName(ClientID));
	SendChat(-1, CHAT_ALL, -1, aBuf);

	if (!m_apPlayers[ClientID])
		return;

	// send clear vote options
	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);
	// begin sending vote options
	StartResendingVotes(ClientID);

	SendStartMessages(ClientID);
	m_World.InitPlayerMap(ClientID, true);

	int Zone = GetPlayerChar(ClientID) ? GetPlayerChar(ClientID)->m_TuneZone : 0;
	SendTuningParams(ClientID, Zone);
}

void CGameContext::MapDesignChangeDone(int ClientID)
{
	if (!m_apPlayers[ClientID])
		return;

	// send clear vote options
	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);
	// begin sending vote options
	StartResendingVotes(ClientID);

	SendStartMessages(ClientID);
	m_World.InitPlayerMap(ClientID, true);

	int Zone = GetPlayerChar(ClientID) ? GetPlayerChar(ClientID)->m_TuneZone : 0;
	SendTuningParams(ClientID, Zone);

	if (Server()->GetDummy(ClientID) != -1)
		SendChatTarget(ClientID, "[WARNING] You need to reconnect your dummy after the design change is done, so it can get back it's old state.");
}

void CGameContext::OnClientConnected(int ClientID, bool Dummy, bool AsSpec)
{
	{
		bool Empty = true;
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (m_apPlayers[i])
			{
				Empty = false;
				break;
			}
		}
		if (Empty)
		{
			m_NonEmptySince = Server()->Tick();
		}
	}

	dbg_assert(!m_apPlayers[ClientID], "non-free player slot");

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, Dummy, AsSpec);

	Server()->ExpireServerInfo();

	if (Dummy)
		return;

	// send motd
	SendMotd(FormatMotd(Config()->m_SvMotd), ClientID);
}

void CGameContext::OnClientTeamChange(int ClientID)
{
	if(m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
		AbortVoteOnTeamChange(ClientID);
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	m_apPlayers[ClientID]->OnDisconnect();

	AbortVoteOnDisconnect(ClientID);

	// update clients on drop
	if(Server()->ClientIngame(ClientID) || IsClientBot(ClientID))
	{
		if(Server()->DemoRecorder_IsRecording())
		{
			CNetMsg_De_ClientLeave Msg;
			Msg.m_pName = Server()->ClientName(ClientID);
			Msg.m_pReason = pReason;
			Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
		}

		CNetMsg_Sv_ClientDrop Msg;
		Msg.m_ClientID = ClientID;
		Msg.m_pReason = pReason;
		Msg.m_Silent = 1;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, -1);

		if (!Config()->m_SvSilentSpectatorMode || m_apPlayers[ClientID]->GetTeam() != TEAM_SPECTATORS)
		{
			char aBuf[128];
			if (pReason && *pReason)
				str_format(aBuf, sizeof(aBuf), "'%s' has left the game (%s)", Server()->ClientName(ClientID), pReason);
			else
				str_format(aBuf, sizeof(aBuf), "'%s' has left the game", Server()->ClientName(ClientID));

			int Flags = CHAT_SEVEN|CHAT_SEVENDOWN;
			if (m_apPlayers[ClientID]->m_IsDummy)
				Flags |= CHAT_NO_WEBHOOK;
			SendChat(-1, CHAT_ALL, -1, aBuf, -1, Flags);
		}
	}

	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	m_VoteUpdate = true;

	Server()->ExpireServerInfo();

	if (((CServer*)Server())->m_RunServer != CServer::STOPPING)
	{
		SendPlayerCountUpdate();
	}
}

void CGameContext::OnClientEngineJoin(int ClientID)
{
	if(m_TeeHistorianActive)
	{
		m_TeeHistorian.RecordPlayerJoin(ClientID);
	}
}

void CGameContext::OnClientEngineDrop(int ClientID, const char *pReason)
{
	if(m_TeeHistorianActive)
	{
		m_TeeHistorian.RecordPlayerDrop(ClientID, pReason);
	}
}

void CGameContext::OnClientAuth(int ClientID, int Level)
{
	if(m_TeeHistorianActive)
	{
		if(Level)
		{
			m_TeeHistorian.RecordAuthLogin(ClientID, Level, Server()->AuthName(ClientID));
		}
		else
		{
			m_TeeHistorian.RecordAuthLogout(ClientID);
		}
	}
}

const char *CGameContext::GetWhisper(char *pStr, int *pTarget)
{
	char *pName;
	int Error = 0;

	pStr = str_skip_whitespaces(pStr);

	int Victim = -1;

	// add token
	if(*pStr == '"')
	{
		pStr++;

		pName = pStr; // we might have to process escape data
		while(1)
		{
			if(pStr[0] == '"')
				break;
			else if(pStr[0] == '\\')
			{
				if(pStr[1] == '\\')
					pStr++; // skip due to escape
				else if(pStr[1] == '"')
					pStr++; // skip due to escape
			}
			else if(pStr[0] == 0)
				Error = 1;

			pStr++;
		}

		// write null termination
		*pStr = 0;
		pStr++;

		for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
			if (str_comp(pName, Server()->ClientName(Victim)) == 0)
				break;

	}
	else
	{
		pName = pStr;
		while(1)
		{
			bool WasSpace = pStr[0] == ' ';
			if(WasSpace || pStr[0] == 0)
			{
				pStr[0] = 0;
				for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
					if (str_comp(pName, Server()->ClientName(Victim)) == 0)
						break;

				if (WasSpace)
					pStr[0] = ' ';
				else
					break;

				if (Victim < MAX_CLIENTS)
					break;
			}
			pStr++;
		}
	}

	if (Victim >= MAX_CLIENTS)
		Victim = -1;

	*pStr = 0;
	pStr++;

	if (Error)
		Victim = -1;
	*pTarget = Victim;
	return pStr;
}

bool CGameContext::OnClientDDNetVersionKnown(int ClientID)
{
	IServer::CClientInfo Info;
	Server()->GetClientInfo(ClientID, &Info);
	int ClientVersion = Info.m_DDNetVersion;
	if (Server()->IsSevendown(ClientID))
		dbg_msg("ddnet", "cid=%d version=%d", ClientID, ClientVersion);
	else
		dbg_msg("ddnet", "cid=%d ddnet_ver=%d version=%x", ClientID, ClientVersion, Server()->GetClientVersion(ClientID));

	//autoban known bot versions
	if(Config()->m_SvBannedVersions[0] != '\0' && IsVersionBanned(ClientVersion))
	{
		Server()->Kick(ClientID, "unsupported client");
		return true;
	}

	m_World.UpdateTeamsState(ClientID);
	if (ClientVersion >= VERSION_DDNET_CAMERA_INFO)
	{
		m_apPlayers[ClientID]->m_ZoomCursor = true;
	}
	return false;
}

void *CGameContext::PreProcessMsg(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	if (Server()->IsSevendown(ClientID))
	{
		CPlayer *pPlayer = m_apPlayers[ClientID];
		static char s_aRawMsg[1024];
		bool ProcessedMsg = true;

		if (MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReadyToEnter)
				return 0;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)s_aRawMsg;

			for (int p = 0; p < NUM_SKINPARTS; p++)
			{
				pMsg->m_apSkinPartNames[p] = "";
				pMsg->m_aUseCustomColors[p] = 0;
				pMsg->m_aSkinPartColors[p] = 0;
			}

			pMsg->m_pName = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			pMsg->m_pClan = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			pMsg->m_Country = pUnpacker->GetInt();

			char aSkinName[24];
			str_copy(aSkinName, pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(aSkinName));
			int UseCustomColor = pUnpacker->GetInt() ? 1 : 0;
			int ColorBody = pUnpacker->GetInt();
			int ColorFeet = pUnpacker->GetInt();

			CTeeInfo Info(aSkinName, UseCustomColor, ColorBody, ColorFeet);
			pPlayer->m_TeeInfos = Info;
		}
		else if (MsgID == NETMSGTYPE_CL_SAY)
		{
			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)s_aRawMsg;

			pMsg->m_Mode = pUnpacker->GetInt() ? CHAT_TEAM : CHAT_ALL;
			pMsg->m_pMessage = pUnpacker->GetString(CUnpacker::SANITIZE_CC);
			pMsg->m_Target = -1;

			if (pMsg->m_pMessage[0] == '/')
			{
				// manually catch these so we can give the correct output, not "No such command", because these commands are only "hacked in" for 0.6 clients
				if (str_comp_nocase(pMsg->m_pMessage + 1, "w") == 0 || str_comp_nocase(pMsg->m_pMessage + 1, "whisper") == 0)
				{
					SendChatTarget(ClientID, "Invalid arguments... Usage: w s[player name] r[message]");
					return 0;
				}
				else if (str_comp_nocase(pMsg->m_pMessage + 1, "c") == 0 || str_comp_nocase(pMsg->m_pMessage + 1, "converse") == 0)
				{
					SendChatTarget(ClientID, "Invalid arguments... Usage: c r[message]");
					return 0;
				}

				int WhisperOffset = -1;
				int ConverseOffset = -1;

				if (str_comp_nocase_num(pMsg->m_pMessage + 1, "w ", 2) == 0) WhisperOffset = 3;
				if (str_comp_nocase_num(pMsg->m_pMessage + 1, "whisper ", 8) == 0) WhisperOffset = 9;
				if (str_comp_nocase_num(pMsg->m_pMessage + 1, "c ", 2) == 0) ConverseOffset = 3;
				if (str_comp_nocase_num(pMsg->m_pMessage + 1, "converse ", 9) == 0) ConverseOffset = 10;

				if (WhisperOffset != -1)
				{
					static char aWhisperMsg[256];
					str_copy(aWhisperMsg, pMsg->m_pMessage + WhisperOffset, 256);
					pMsg->m_Mode = CHAT_WHISPER;
					pMsg->m_pMessage = GetWhisper(aWhisperMsg, &pMsg->m_Target);
					if (pMsg->m_Target == -1)
					{
						SendChatTarget(ClientID, "Invalid whisper");
						return 0;
					}
				}
				else if (ConverseOffset != -1)
				{
					if (pPlayer->m_LastWhisperTo >= 0)
					{
						static char aWhisperMsg[256];
						str_copy(aWhisperMsg, pMsg->m_pMessage + ConverseOffset, 256);
						pMsg->m_pMessage = aWhisperMsg;
						pMsg->m_Target = pPlayer->m_LastWhisperTo;
						pMsg->m_Mode = CHAT_WHISPER;
					}
					else
					{
						SendChatTarget(ClientID, "You do not have an ongoing conversation. Whisper to someone to start one");
						return 0; // dont process any further
					}
				}
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)s_aRawMsg;

			pMsg->m_SpectatorID = clamp(pUnpacker->GetInt(), -1, MAX_CLIENTS - 1);
			pMsg->m_SpecMode = pMsg->m_SpectatorID == -1 ? SPEC_FREEVIEW : SPEC_PLAYER;

			if (FlagsUsed() && (pMsg->m_SpectatorID == SPEC_SELECT_FLAG_RED || pMsg->m_SpectatorID == SPEC_SELECT_FLAG_BLUE))
			{
				pMsg->m_SpecMode = VANILLA_MAX_CLIENTS - pMsg->m_SpectatorID;
				pMsg->m_SpectatorID = -1;
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SKINCHANGE)
		{
			if(pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*Config()->m_SvInfoChangeDelay > Server()->Tick())
				return 0;

			CNetMsg_Cl_SkinChange *pMsg = (CNetMsg_Cl_SkinChange *)s_aRawMsg;

			for (int p = 0; p < NUM_SKINPARTS; p++)
			{
				pMsg->m_apSkinPartNames[p] = "";
				pMsg->m_aUseCustomColors[p] = 0;
				pMsg->m_aSkinPartColors[p] = 0;
			}

			const char *pName = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			const char *pClan = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			int Country = pUnpacker->GetInt();

			char aSkinName[24];
			str_copy(aSkinName, pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES), sizeof(aSkinName));
			int UseCustomColor = pUnpacker->GetInt() ? 1 : 0;
			int ColorBody = pUnpacker->GetInt();
			int ColorFeet = pUnpacker->GetInt();

			CTeeInfo Info(aSkinName, UseCustomColor, ColorBody, ColorFeet);
			pPlayer->m_TeeInfos = Info;

			bool UpdateInfo = false;

			// set infos
			if (str_comp(m_apPlayers[ClientID]->m_CurrentInfo.m_aName, Server()->ClientName(ClientID)) == 0) // check that we dont have a name on right now set by an admin
			{
				if(Server()->WouldClientNameChange(ClientID, pName) && !ProcessSpamProtection(ClientID))
				{
					char aOldName[MAX_NAME_LENGTH];
					str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
					Server()->SetClientName(ClientID, pName);

					char aChatText[256];
					str_format(aChatText, sizeof(aChatText), "'%s' changed name to '%s'", aOldName, Server()->ClientName(ClientID));
					SendChat(-1, CHAT_ALL, -1, aChatText);
					pPlayer->SetName(Server()->ClientName(ClientID));

					// reload scores
					{
						Score()->PlayerData(ClientID)->Reset();
						Score()->LoadScore(ClientID);
						Score()->PlayerData(ClientID)->m_CurrentTime = Score()->PlayerData(ClientID)->m_BestTime;
						m_apPlayers[ClientID]->m_Score = !Score()->PlayerData(ClientID)->m_BestTime ? -1 : Score()->PlayerData(ClientID)->m_BestTime;
					}

					UpdateInfo = true;

					// update whois on namechange
					m_WhoIs.AddEntry(ClientID);
				}
			}

			if(str_comp(Server()->ClientClan(ClientID), pClan)
				&& str_comp(m_apPlayers[ClientID]->m_CurrentInfo.m_aClan, Server()->ClientClan(ClientID)) == 0) // check that we dont have a clan on right now set by an admin)
			{
				Server()->SetClientClan(ClientID, str_find_nocase(pClan, "Zombie") ? "Human" : pClan);
				pPlayer->SetClan(Server()->ClientClan(ClientID));
				UpdateInfo = true;
			}

			if(Server()->ClientCountry(ClientID) != Country)
			{
				Server()->SetClientCountry(ClientID, Country);
				UpdateInfo = true;
			}

			if (UpdateInfo)
				pPlayer->UpdateInformation();
		}
		else if (MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)s_aRawMsg;
			pMsg->m_Type = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			pMsg->m_Value = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			pMsg->m_Reason = pUnpacker->GetString(CUnpacker::SANITIZE_CC|CUnpacker::SKIP_START_WHITESPACES);
			pMsg->m_Force = 0;
		}
		else
		{
			ProcessedMsg = false;
		}

		if (ProcessedMsg)
		{
			if (pUnpacker->Error())
				return 0;
			return s_aRawMsg;
		}
	}

	return m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = PreProcessMsg(MsgID, pUnpacker, ClientID);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(m_TeeHistorianActive)
	{
		if(m_NetObjHandler.TeeHistorianRecordMsg(MsgID))
		{
			m_TeeHistorian.RecordPlayerMessage(ClientID, pUnpacker->CompleteData(), pUnpacker->CompleteSize());
		}
	}

	if (!pRawMsg)
		return;

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;

			// trim right and set maximum length to 256 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(!str_utf8_is_whitespace(Code))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 255)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 32 characters per second)
			if (Length == 0 || (pMsg->m_pMessage[0] != '/' && (Config()->m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat + Server()->TickSpeed() * ((31 + Length) / 32) > Server()->Tick())))
				return;

			bool Command = pMsg->m_pMessage[0] == '/';
			if (!Command && pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_DrawEditor.TryEnterPresetName(pMsg->m_pMessage))
				return;

			// don't allow spectators to disturb players during a running game in tournament mode
			int Mode = pMsg->m_Mode;
			if((Config()->m_SvTournamentMode == 2) &&
				pPlayer->GetTeam() == TEAM_SPECTATORS &&
				!Server()->GetAuthedState(ClientID))
			{
				if(Mode != CHAT_WHISPER)
					Mode = CHAT_TEAM;
				else if(m_apPlayers[pMsg->m_Target] && m_apPlayers[pMsg->m_Target]->GetTeam() != TEAM_SPECTATORS)
					Mode = CHAT_NONE;
			}

			if (Mode == CHAT_WHISPER)
			{
				if (!Server()->IsSevendown(ClientID))
					if (!Server()->ReverseTranslate(pMsg->m_Target, ClientID))
						return;
			}
			else if (Mode == CHAT_TEAM)
			{
				if (pPlayer->m_LocalChat && GetPlayerChar(ClientID))
					Mode = CHAT_LOCAL;
			}

			if (Mode != CHAT_WHISPER)
			{
				if (Config()->m_SvLolFilter && str_comp_nocase(pMsg->m_pMessage, "lol") == 0)
				{
					pMsg->m_pMessage = "I like turtles.";
				}
				else if (Server()->GetAuthedState(ClientID) >= Config()->m_SvAtEveryoneLevel && str_find_nocase(pMsg->m_pMessage, "@everyone"))
				{
					Mode = CHAT_ATEVERYONE;
				}
				else if (Server()->GetAuthedState(ClientID) < Config()->m_SvChatAdminPingLevel && !Command)
				{
					for (int i = 0; i < MAX_CLIENTS; i++)
					{
						const char *pName = Server()->ClientName(i);
						if (!m_apPlayers[i] || !Server()->GetAuthedState(i) || !LineShouldHighlight(pMsg->m_pMessage, pName))
							continue;

						while(1)
						{
							const char *pHaystack = str_find_nocase(pMsg->m_pMessage, pName);
							if (!pHaystack)
								break;

							for (int j = 0; j < str_length(pName); j++)
							{
								*(const_cast<char *>(pHaystack)) = '*';
								pHaystack++;
							}
						}
					}
				}
			}

			if (Command && Mode != CHAT_WHISPER)
			{
				CPlayer *pPlayer = m_apPlayers[ClientID];
				if (Config()->m_SvSpamprotection && !str_startswith(pMsg->m_pMessage + 1, "timeout ")
					&& pPlayer->m_LastCommands[0] && pPlayer->m_LastCommands[0] + Server()->TickSpeed() > Server()->Tick()
					&& pPlayer->m_LastCommands[1] && pPlayer->m_LastCommands[1] + Server()->TickSpeed() > Server()->Tick()
					&& pPlayer->m_LastCommands[2] && pPlayer->m_LastCommands[2] + Server()->TickSpeed() > Server()->Tick()
					&& pPlayer->m_LastCommands[3] && pPlayer->m_LastCommands[3] + Server()->TickSpeed() > Server()->Tick()
					)
					return;

				int64 Now = Server()->Tick();
				pPlayer->m_LastCommands[pPlayer->m_LastCommandPos] = Now;
				pPlayer->m_LastCommandPos = (pPlayer->m_LastCommandPos + 1) % 4;

				m_ChatResponseTargetID = ClientID;
				Server()->RestrictRconOutput(ClientID);
				Console()->SetFlagMask(CFGFLAG_CHAT);

				int Authed = Server()->GetAuthedState(ClientID);
				if (Authed)
					Console()->SetAccessLevel(Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : Authed == AUTHED_MOD ? IConsole::ACCESS_LEVEL_MOD : IConsole::ACCESS_LEVEL_HELPER);
				else
					Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
				Console()->SetPrintOutputLevel(m_ChatPrintCBIndex, 0);

				Console()->ExecuteLine(pMsg->m_pMessage + 1, ClientID, false);
				// m_apPlayers[ClientID] can be NULL, if the player used a
				// timeout code and replaced another client.
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "%d used %s", ClientID, pMsg->m_pMessage);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "chat-command", aBuf);

				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				Console()->SetFlagMask(CFGFLAG_SERVER);
				m_ChatResponseTargetID = -1;
				Server()->RestrictRconOutput(-1);
			}
			else if(Mode != CHAT_NONE)
			{
				SendChat(ClientID, Mode, pMsg->m_Target, pMsg->m_pMessage, ClientID);
				pPlayer->UpdatePlaytime();

				if (Mode != CHAT_WHISPER)
				{
					char aLocalNames[256] = "";
					for (int i = 0; i < MAX_CLIENTS; i++)
					{
						if (m_apPlayers[i] && LineShouldHighlight(pMsg->m_pMessage, Server()->ClientName(i)) && (!CanReceiveMessage(ClientID, i) || !CanReceiveMessage(i, ClientID)))
						{
							char aBuf[128];
							str_format(aBuf, sizeof(aBuf), "%s%s", aLocalNames[0] ? ", " : "", Server()->ClientName(i));
							str_append(aLocalNames, aBuf, sizeof(aLocalNames));
						}
					}

					if (aLocalNames[0])
					{
						SendChatTarget(ClientID, "Following players can't receive your message because either you or they are in local chat mode:");
						SendChatTarget(ClientID, aLocalNames);
					}
				}
			}
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			if (str_comp_nocase(pMsg->m_Type, "option") != 0 && str_toint(pMsg->m_Value) == m_World.GetSeeOthersID(ClientID))
			{
				pPlayer->m_DoSeeOthersByVote = true;
				m_World.DoSeeOthers(ClientID);
				return;
			}
			if (m_VotingMenu.OnMessage(ClientID, pMsg))
			{
				return;
			}

			if(pMsg->m_Force)
			{
				int Authed = Server()->GetAuthedState(ClientID);
				char aCmd[1024];
				str_format(aCmd, sizeof(aCmd), "force_vote \"%s\" \"%s\" \"%s\"", pMsg->m_Type, pMsg->m_Value, pMsg->m_Reason);
				Console()->SetAccessLevel(Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : Authed == AUTHED_MOD ? IConsole::ACCESS_LEVEL_MOD : IConsole::ACCESS_LEVEL_HELPER);
				Console()->ExecuteLine(aCmd, ClientID, false);
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				return;
			}

			if(RateLimitPlayerVote(ClientID) || m_VoteCloseTime)
				return;

			m_apPlayers[ClientID]->UpdatePlaytime();

			m_VoteType = -1;
			char aChatmsg[512] = {0};
			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aSevendownDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			char aReason[VOTE_REASON_LENGTH] = "No reason given";
			if(!str_utf8_check(pMsg->m_Type) || !str_utf8_check(pMsg->m_Reason) || !str_utf8_check(pMsg->m_Value))
			{
				return;
			}
			if(pMsg->m_Reason[0])
			{
				str_copy(aReason, pMsg->m_Reason, sizeof(aReason));
			}

			if(str_comp_nocase(pMsg->m_Type, "option") == 0)
			{
				int Authed = Server()->GetAuthedState(ClientID);
				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
					{
						if(!Console()->LineIsValid(pOption->m_aCommand))
						{
							SendChatTarget(ClientID, "Invalid option");
							return;
						}
						if((str_find(pOption->m_aCommand, "sv_map ") != 0 || str_find(pOption->m_aCommand, "change_map ") != 0 || str_find(pOption->m_aCommand, "random_map") != 0 || str_find(pOption->m_aCommand, "random_unfinished_map") != 0) && RateLimitPlayerMapVote(ClientID))
						{
							return;
						}

						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
							pOption->m_aDescription, aReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);

						if((str_endswith(pOption->m_aCommand, "random_map") || str_endswith(pOption->m_aCommand, "random_unfinished_map")) && str_length(aReason) == 1 && aReason[0] >= '0' && aReason[0] <= '5')
						{
							int Stars = aReason[0] - '0';
							str_format(aCmd, sizeof(aCmd), "%s %d", pOption->m_aCommand, Stars);
						}
						else
						{
							str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						}

						m_LastMapVote = time_get();
						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
				{
					if(Authed != AUTHED_ADMIN) // allow admins to call any vote they want
					{
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
						SendChatTarget(ClientID, aChatmsg);
						return;
					}
					else
					{
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s'", Server()->ClientName(ClientID), pMsg->m_Value);
						str_format(aDesc, sizeof(aDesc), "%s", pMsg->m_Value);
						str_format(aCmd, sizeof(aCmd), "%s", pMsg->m_Value);
					}
				}

				m_VoteType = VOTE_START_OP;
			}
			else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				int Authed = Server()->GetAuthedState(ClientID);
				if(!Authed && time_get() < m_apPlayers[ClientID]->m_Last_KickVote + (time_freq() * 5))
					return;
				else if(!Authed && time_get() < m_apPlayers[ClientID]->m_Last_KickVote + (time_freq() * Config()->m_SvVoteKickDelay))
				{
					str_format(aChatmsg, sizeof(aChatmsg), "There's a %d second wait time between kick votes for each player please wait %d second(s)",
						Config()->m_SvVoteKickDelay,
						(int)(((m_apPlayers[ClientID]->m_Last_KickVote + (m_apPlayers[ClientID]->m_Last_KickVote * time_freq())) / time_freq()) - (time_get() / time_freq())));
					SendChatTarget(ClientID, aChatmsg);
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					return;
				}
				else if(!Config()->m_SvVoteKick && !Authed) // allow admins to call kick votes even if they are forbidden
				{
					SendChatTarget(ClientID, "Server does not allow voting to kick players");
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					return;
				}

				if(Config()->m_SvVoteKickMin && !GetDDRaceTeam(ClientID))
				{
					char aaAddresses[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(m_apPlayers[i])
						{
							Server()->GetClientAddr(i, aaAddresses[i], NETADDR_MAXSTRSIZE);
						}
					}
					int NumPlayers = 0;
					for(int i = 0; i < MAX_CLIENTS; ++i)
					{
						if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !GetDDRaceTeam(i))
						{
							NumPlayers++;
							for(int j = 0; j < i; j++)
							{
								if(m_apPlayers[j] && m_apPlayers[j]->GetTeam() != TEAM_SPECTATORS && !GetDDRaceTeam(j))
								{
									if(str_comp(aaAddresses[i], aaAddresses[j]) == 0)
									{
										NumPlayers--;
										break;
									}
								}
							}
						}
					}

					if(NumPlayers < Config()->m_SvVoteKickMin)
					{
						str_format(aChatmsg, sizeof(aChatmsg), "Kick voting requires %d players", Config()->m_SvVoteKickMin);
						SendChatTarget(ClientID, aChatmsg);
						return;
					}
				}

				int KickID = str_toint(pMsg->m_Value);

				if(!Server()->ReverseTranslate(KickID, ClientID))
				{
					return;
				}
				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
				{
					SendChatTarget(ClientID, "Invalid client id to kick");
					return;
				}
				if(KickID == ClientID)
				{
					SendChatTarget(ClientID, "You can't kick yourself");
					return;
				}
				if (m_apPlayers[KickID]->m_IsDummy)
				{
					SendChatTarget(ClientID, "You can't kick dummies");
					return;
				}
				int KickedAuthed = Server()->GetAuthedState(KickID);
				if(KickedAuthed > Authed)
				{
					SendChatTarget(ClientID, "You can't kick authorized players");
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					char aBufKick[128];
					str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
					SendChatTarget(KickID, aBufKick);
					return;
				}

				// Don't allow kicking if a player has no character
				if(!GetPlayerChar(ClientID) || !GetPlayerChar(KickID) || GetDDRaceTeam(ClientID) != GetDDRaceTeam(KickID))
				{
					SendChatTarget(ClientID, "You can kick only your team member");
					m_apPlayers[ClientID]->m_Last_KickVote = time_get();
					return;
				}

				str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to kick '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(KickID), aReason);
				str_format(aDesc, sizeof(aDesc), "%2d: %s", KickID, Server()->ClientName(KickID));
				if(!GetDDRaceTeam(ClientID))
				{
					if(!Config()->m_SvVoteKickBantime)
					{
						str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
						str_format(aSevendownDesc, sizeof(aSevendownDesc), "Kick '%s'", Server()->ClientName(KickID));
					}
					else
					{
						char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
						Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
						str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, Config()->m_SvVoteKickBantime);
						str_format(aSevendownDesc, sizeof(aSevendownDesc), "Ban '%s'", Server()->ClientName(KickID));
					}
				}
				else
				{
					str_format(aCmd, sizeof(aCmd), "uninvite %d %d; set_team_ddr %d 0", KickID, GetDDRaceTeam(KickID), KickID);
					str_format(aSevendownDesc, sizeof(aSevendownDesc), "Move '%s' to team 0", Server()->ClientName(KickID));
				}
				m_apPlayers[ClientID]->m_Last_KickVote = time_get();
				m_VoteType = VOTE_START_KICK;
				m_VoteClientID = KickID;
			}
			else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
			{
				if(!Config()->m_SvVoteSpectate)
				{
					SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
					return;
				}

				int SpectateID = str_toint(pMsg->m_Value);

				if(!Server()->ReverseTranslate(SpectateID, ClientID))
				{
					return;
				}
				if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
				{
					SendChatTarget(ClientID, "Invalid client id to move");
					return;
				}
				if(SpectateID == ClientID)
				{
					SendChatTarget(ClientID, "You can't move yourself");
					return;
				}
				if (m_apPlayers[SpectateID]->m_IsDummy)
				{
					SendChatTarget(ClientID, "You can't set dummies to spectator");
					return;
				}

				if(!GetPlayerChar(ClientID) || !GetPlayerChar(SpectateID) || GetDDRaceTeam(ClientID) != GetDDRaceTeam(SpectateID))
				{
					SendChatTarget(ClientID, "You can only move your team member to spectators");
					return;
				}

				str_format(aDesc, sizeof(aDesc), "%2d: %s", SpectateID, Server()->ClientName(SpectateID));
				if(Config()->m_SvPauseable && Config()->m_SvVotePause)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to pause '%s' for %d seconds (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), Config()->m_SvVotePauseTime, aReason);
					str_format(aSevendownDesc, sizeof(aSevendownDesc), "Pause '%s' (%ds)", Server()->ClientName(SpectateID), Config()->m_SvVotePauseTime);
					str_format(aCmd, sizeof(aCmd), "uninvite %d %d; force_pause %d %d", SpectateID, GetDDRaceTeam(SpectateID), SpectateID, Config()->m_SvVotePauseTime);
				}
				else
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), aReason);
					str_format(aSevendownDesc, sizeof(aSevendownDesc), "Move '%s' to spectators", Server()->ClientName(SpectateID));
					str_format(aCmd, sizeof(aCmd), "uninvite %d %d; set_team %d -1 %d", SpectateID, GetDDRaceTeam(SpectateID), SpectateID, Config()->m_SvVoteSpectateRejoindelay);
				}
				m_VoteType = VOTE_START_SPEC;
				m_VoteClientID = SpectateID;
			}

			if(aCmd[0] && str_comp(aCmd, "info") != 0)
				CallVote(ClientID, aDesc, aCmd, aReason, aChatmsg, aSevendownDesc[0] ? aSevendownDesc : 0);
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(Config()->m_SvSpamprotection && pPlayer->m_LastVote && pPlayer->m_LastVote+Server()->TickSpeed()/8 > Server()->Tick())
				return;

			pPlayer->m_LastVote = Server()->Tick();
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;

			if (pPlayer->m_VoteQuestionRunning)
			{
				pPlayer->OnEndVoteQuestion(pMsg->m_Vote);
				return;
			}

			if (m_VoteCloseTime)
			{
				if(pPlayer->m_Vote == 0)
				{
					if(!pMsg->m_Vote)
						return;

					pPlayer->m_Vote = pMsg->m_Vote;
					pPlayer->m_VotePos = ++m_VotePos;
					m_VoteUpdate = true;
				}
				else if(m_VoteCreator == pPlayer->GetCID())
				{
					CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
					if(pMsg->m_Vote != -1 || m_VoteCancelTime<time_get())
						return;

					m_VoteCloseTime = -1;
				}

				// Dont process further if a vote is running
				return;
			}

			CCharacter *pChr = pPlayer->GetCharacter();
			CCharacter *pControlledTee = pPlayer->m_pControlledTee ? pPlayer->m_pControlledTee->GetCharacter() : 0;

			if (pMsg->m_Vote == 1) //vote yes (f3)
			{
				if (pPlayer->m_TeeControlMode)
				{
					if (pControlledTee)
						pControlledTee->DropFlag();
				}
				else if (pChr)
				{
					bool InHouse = false;
					for (int i = 0; i < NUM_HOUSES; i++)
					{
						if (m_pHouses[i]->IsInside(ClientID))
						{
							m_pHouses[i]->OnKeyPress(ClientID, pMsg->m_Vote);
							InHouse = true;
						}
					}
					
					if (!InHouse)
						pChr->DropFlag();
				}
			}
			else if (pMsg->m_Vote == -1) //vote no (f4)
			{
				if (pPlayer->m_TeeControlMode)
				{
					if (pControlledTee)
						pControlledTee->DropWeapon(pControlledTee->GetActiveWeaponUnclamped(), false);
				}
				else if (pChr)
				{
					bool InHouse = false;
					for (int i = 0; i < NUM_HOUSES; i++)
					{
						if (m_pHouses[i]->IsInside(ClientID))
						{
							m_pHouses[i]->OnKeyPress(ClientID, pMsg->m_Vote);
							InHouse = true;
						}
					}

					if (!InHouse)
					{
						if (pChr->m_pHelicopter)
						{
							pChr->m_pHelicopter->Dismount();
						}
						else if (!pChr->TryMountHelicopter() && !pChr->DropGrog())
						{
							pChr->DropWeapon(pChr->GetActiveWeaponUnclamped(), false);
						}
					}
				}
			}
		}
		else if(MsgID == NETMSGTYPE_CL_SETTEAM)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

			if (pPlayer->m_HasTeeControl && !pPlayer->IsPaused())
			{
				bool SetTeeControl = pMsg->m_Team == TEAM_SPECTATORS;
				if (!SetTeeControl || (pPlayer->m_TeeControlMode && SetTeeControl))
					pPlayer->UnsetTeeControl();
				if (pPlayer->m_TeeControlMode != SetTeeControl)
				{
					pPlayer->m_TeeControlMode = SetTeeControl;
					SendChatTarget(ClientID, pPlayer->m_TeeControlMode ? "You are now using the tee controller" : "You are no longer using the tee controller");
					SendTeamChange(ClientID, SetTeeControl ? TEAM_SPECTATORS : pPlayer->GetTeam(), true, Server()->Tick(), ClientID);
					if (pPlayer->GetCharacter() && !Server()->IsSevendown(ClientID))
						SendTuningParams(ClientID, pPlayer->GetCharacter()->m_TuneZone);
					if (pPlayer->m_TeeControlForcedID != -1 && SetTeeControl)
						pPlayer->SetTeeControl(m_apPlayers[pPlayer->m_TeeControlForcedID]);
				}
				return;
			}

			if (pPlayer->GetTeam() == pMsg->m_Team
				|| (Config()->m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam + Server()->TickSpeed() * Config()->m_SvTeamChangeDelay > Server()->Tick())
				|| pPlayer->m_TeamChangeTick > Server()->Tick())
				return;

			CCharacter* pChr = pPlayer->GetCharacter();
			if (pChr)
			{
				int CurrTime = (Server()->Tick() - pChr->m_StartTime) / Server()->TickSpeed();
				if (Config()->m_SvKillProtection != 0 && CurrTime >= (60 * Config()->m_SvKillProtection) && pChr->m_DDRaceState == DDRACE_STARTED)
				{
					SendChatTarget(ClientID, "Kill Protection enabled. If you really want to join the spectators, first type /kill");
					return;
				}
				if (Config()->m_SvWalletKillProtection != 0 && pPlayer->GetWalletMoney() >= Config()->m_SvWalletKillProtection && pChr->m_FreezeTime)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Warning you have %lld money in your wallet! (see /money)", pPlayer->GetWalletMoney());
					SendChatTarget(ClientID, aBuf);
					SendChatTarget(ClientID, "Wallet kill Protection enabled. If you really want to join the spectators, first type /kill");
					return;
				}
			}

			pPlayer->m_LastSetTeam = Server()->Tick();

			if (pPlayer->m_EscapeTime)
			{
				SendChatTarget(ClientID, "You can't join the spectators while being searched by the police");
				return;
			}

			// Switch team on given client and kill/respawn him
			if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
			{
				if (pPlayer->IsPaused())
					SendChatTarget(ClientID, "Use /pause first then you can kill");
				else
				{
					if (pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
						m_VoteUpdate = true;
					pPlayer->m_TeamChangeTick = Server()->Tick() + Server()->TickSpeed() * Config()->m_SvTeamChangeDelay;
					pPlayer->SetTeam(pMsg->m_Team);
				}
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Config()->m_SvPlayerSlots);
				SendBroadcast(aBuf, ClientID);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			if(Config()->m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()/4 > Server()->Tick())
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();
			pPlayer->UpdatePlaytime();

			if (pMsg->m_SpecMode == SPEC_PLAYER && pMsg->m_SpectatorID == m_World.GetSeeOthersID(ClientID))
			{
				m_World.DoSeeOthers(ClientID);
				return;
			}

			if (pMsg->m_SpecMode == SPEC_PLAYER && pMsg->m_SpectatorID >= 0)
				if (!Server()->ReverseTranslate(pMsg->m_SpectatorID, ClientID))
					return;

			if (pPlayer->m_TeeControlMode && pPlayer->GetTeam() != TEAM_SPECTATORS && !pPlayer->IsPaused())
			{
				if (pPlayer->m_TeeControlForcedID == -1)
				{
					switch (pMsg->m_SpecMode)
					{
					case SPEC_FREEVIEW:
						pPlayer->UnsetTeeControl();
						break;
					case SPEC_PLAYER:
						pPlayer->SetTeeControl(m_apPlayers[pMsg->m_SpectatorID]);
						break;
					case SPEC_FLAGRED:
					case SPEC_FLAGBLUE:
						for (int i = 0; i < 2; i++)
						{
							CFlag* F = ((CGameControllerDDRace*)m_pController)->m_apFlags[i];
							if (!F || !F->GetCarrier())
								continue;

							if ((pMsg->m_SpecMode == SPEC_FLAGRED && i == TEAM_RED) || (pMsg->m_SpecMode == SPEC_FLAGBLUE && i == TEAM_BLUE))
								pPlayer->SetTeeControl(F->GetCarrier()->GetPlayer());
						} break;
					}
				}
			}
			else
				pPlayer->SetSpectatorID(pMsg->m_SpecMode, pMsg->m_SpectatorID);
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(Config()->m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*Config()->m_SvEmoticonDelay > Server()->Tick())
				return;

			pPlayer->UpdatePlaytime();
			pPlayer->m_LastEmote = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
			CCharacter *pChr = pPlayer->GetCharacter();
			if(pChr && Config()->m_SvEmotionalTees && pPlayer->m_EyeEmote)
			{
				switch(pMsg->m_Emoticon)
				{
				case EMOTICON_EXCLAMATION:
				case EMOTICON_GHOST:
				case EMOTICON_QUESTION:
				case EMOTICON_WTF:
						pChr->SetEmoteType(EMOTE_SURPRISE);
						break;
				case EMOTICON_DOTDOT:
				case EMOTICON_DROP:
				case EMOTICON_ZZZ:
						pChr->SetEmoteType(EMOTE_BLINK);
						break;
				case EMOTICON_EYES:
				case EMOTICON_HEARTS:
				case EMOTICON_MUSIC:
						pChr->SetEmoteType(EMOTE_HAPPY);
						break;
				case EMOTICON_OOP:
				case EMOTICON_SORRY:
				case EMOTICON_SUSHI:
						pChr->SetEmoteType(EMOTE_PAIN);
						break;
				case EMOTICON_DEVILTEE:
				case EMOTICON_SPLATTEE:
				case EMOTICON_ZOMG:
						pChr->SetEmoteType(EMOTE_ANGRY);
						break;
					default:
						pChr->SetEmoteType(EMOTE_NORMAL);
						break;
				}
				if (pPlayer->m_SpookyGhost)
					pChr->SetEmoteType(EMOTE_SURPRISE);
				else if (pChr->GetActiveWeapon() == WEAPON_HEART_GUN)
					pChr->SetEmoteType(EMOTE_HAPPY);
				pChr->SetEmoteStop(Server()->Tick() + 2 * Server()->TickSpeed());
			}
		}
		else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if (pPlayer->m_LastKill && pPlayer->m_LastKill + Server()->TickSpeed() * Config()->m_SvKillDelay > Server()->Tick())
				return;
			if (pPlayer->IsPaused())
				return;

			CCharacter* pChr = pPlayer->GetCharacter();
			if (!pChr)
				return;

			if (pChr->m_DrawEditor.Active())
			{
				pChr->m_DrawEditor.OnPlayerKill();
				return;
			}

			pPlayer->m_LastKill = Server()->Tick();

			int Fight = Arenas()->GetClientFight(ClientID);
			if (!Arenas()->FightStarted(ClientID))
			{	
				if (Fight >= 0)
				{
					Arenas()->EndFight(Fight);
					return;
				}
			}
			else if (!Arenas()->CanSelfkill(ClientID))
				return;

			if (!m_apPlayers[ClientID]->IsMinigame() && m_apPlayers[ClientID]->m_EscapeTime)
			{
				if (pPlayer->m_DieTick > Server()->Tick() - Server()->TickSpeed() * 10)
				{
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "You need to wait %d seconds before killing again due to escaping", 10 - (Server()->Tick() - pPlayer->m_DieTick) / Server()->TickSpeed());
					SendChatTarget(ClientID, aBuf);
					return;
				}
			}

			//Kill Protection
			int CurrTime = (Server()->Tick() - pChr->m_StartTime) / Server()->TickSpeed();
			if (Config()->m_SvKillProtection != 0 && CurrTime >= (60 * Config()->m_SvKillProtection) && pChr->m_DDRaceState == DDRACE_STARTED)
			{
				SendChatTarget(ClientID, "Kill Protection enabled. If you really want to kill, type /kill");
				return;
			}
			if (Config()->m_SvWalletKillProtection != 0 && pPlayer->GetWalletMoney() >= Config()->m_SvWalletKillProtection && pChr->m_FreezeTime)
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Warning you have %lld money in your wallet! (see /money)", pPlayer->GetWalletMoney());
				SendChatTarget(ClientID, aBuf);
				SendChatTarget(ClientID, "Wallet kill Protection enabled. If you really want to kill, type /kill");
				return;
			}

			CPlayer *pControlledTee = pPlayer->m_pControlledTee;
			if (pControlledTee)
			{
				if (pControlledTee->m_IsDummy)
				{
					pControlledTee->KillCharacter(WEAPON_SELF, false);
					pControlledTee->Respawn();
				}
			}
			else
			{
				if (pChr->m_LastWantedLogout && pChr->m_LastWantedLogout + Server()->TickSpeed() * Config()->m_SvKillLogout > Server()->Tick())
					Logout(pPlayer->GetAccID());

				pPlayer->m_ToggleSpawn = pPlayer->m_PlayerFlags&PLAYERFLAG_SCOREBOARD;
				pPlayer->KillCharacter(WEAPON_SELF);
				pPlayer->Respawn();
			}
		}
		else if (MsgID == NETMSGTYPE_CL_READYCHANGE)
		{
			if(pPlayer->m_LastReadyChange && pPlayer->m_LastReadyChange+Server()->TickSpeed()*1 > Server()->Tick())
				return;

			pPlayer->m_LastReadyChange = Server()->Tick();
			if (Config()->m_SvPlayerReadyMode && pPlayer->GetTeam() != TEAM_SPECTATORS)
			{
				// change players ready state
				pPlayer->m_IsReadyToPlay = !pPlayer->m_IsReadyToPlay;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_SKINCHANGE)
		{
			if(pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*Config()->m_SvInfoChangeDelay > Server()->Tick())
				return;

			pPlayer->UpdatePlaytime();
			pPlayer->m_LastChangeInfo = Server()->Tick();
			CNetMsg_Cl_SkinChange *pMsg = (CNetMsg_Cl_SkinChange *)pRawMsg;

			for(int p = 0; p < NUM_SKINPARTS; p++)
			{
				str_utf8_copy_num(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]), MAX_SKIN_LENGTH);
				pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			pPlayer->m_TeeInfos.Translate(Server()->IsSevendown(ClientID));

			// F-DDrace
			pPlayer->CheckClanProtection();
			CheckLoadPlayer(ClientID);

			Server()->ExpireServerInfo();

			if (pPlayer->m_SpookyGhost || pPlayer->m_ForcedSkin != SKIN_NONE || (pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_IsZombie))
				return;

			// update currently visable teeinfos
			// it also gets set in SendSkinChange(), but that wont get executed if no up-to-date 0.7 client is connected, so definitely do it here aswell
			m_apPlayers[ClientID]->m_CurrentInfo.m_TeeInfos = pPlayer->m_TeeInfos;

			// update all clients
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(!m_apPlayers[i] || (!Server()->ClientIngame(i) && !m_apPlayers[i]->IsDummy()) || Server()->GetClientVersion(i) < MIN_SKINCHANGE_CLIENTVERSION)
					continue;

				SendSkinChange(pPlayer->m_TeeInfos, pPlayer->GetCID(), i);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_COMMAND)
		{
			CNetMsg_Cl_Command *pMsg = (CNetMsg_Cl_Command*)pRawMsg;
			CommandManager()->OnCommand(pMsg->m_Name, pMsg->m_Arguments, ClientID);
		}
		else if (MsgID == NETMSGTYPE_CL_SHOWDISTANCE)
		{
			CNetMsg_Cl_ShowDistance *pMsg = (CNetMsg_Cl_ShowDistance *)pRawMsg;
			pPlayer->m_ShowDistance = vec2(pMsg->m_X, pMsg->m_Y);
			pPlayer->m_SentShowDistance = true;

			float Aspect = pPlayer->m_ShowDistance.x / pPlayer->m_ShowDistance.y;
			CalcScreenParams(Aspect, 1.f, &pPlayer->m_StandardShowDistance.x, &pPlayer->m_StandardShowDistance.y);
		}
		else if (MsgID == NETMSGTYPE_CL_CAMERAINFO)
		{
			CNetMsg_Cl_CameraInfo *pMsg = (CNetMsg_Cl_CameraInfo *)pRawMsg;
			pPlayer->m_CameraInfo.Write(pMsg);
		}
	}
	else
	{
		if (MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReadyToEnter)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set start infos
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, str_find_nocase(pMsg->m_pClan, "Zombie") ? "Human" : pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);

			for(int p = 0; p < NUM_SKINPARTS; p++)
			{
				str_utf8_copy_num(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]), MAX_SKIN_LENGTH);
				pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			pPlayer->m_TeeInfos.Translate(Server()->IsSevendown(ClientID));

			// send clear vote options
			CNetMsg_Sv_VoteClearOptions ClearMsg;
			Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

			// Init before sending votes
			m_VotingMenu.InitPlayer(ClientID);

			// begin sending vote options
			StartResendingVotes(ClientID);

			// send tuning parameters to client
			SendTuningParams(ClientID);

			// client is ready to enter
			pPlayer->m_IsReadyToEnter = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);

			Server()->ExpireServerInfo();
		}
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->NumArguments() == 2 ? pResult->GetFloat(1) : -1;
	char aBuf[256];
	float Value;

	if(NewValue != -1 && pSelf->Tuning()->Set(pParamName, NewValue))
	{
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else if (pSelf->Tuning()->Get(pParamName, &Value))
	{
		str_format(aBuf, sizeof(aBuf), "Value: %.2f", Value);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConToggleTuneParam(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	const char* pParamName = pResult->GetString(0);
	float OldValue;

	if (!pSelf->Tuning()->Get(pParamName, &OldValue))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
		return;
	}

	float NewValue = fabs(OldValue - pResult->GetFloat(1)) < 0.0001f
		? pResult->GetFloat(2)
		: pResult->GetFloat(1);

	pSelf->Tuning()->Set(pParamName, NewValue);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	pSelf->SendTuningParams(-1);
}

void CGameContext::ConTuneReset(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	pSelf->ResetTuning();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTunes(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	char aBuf[256];
	for (int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float Value;
		pSelf->Tuning()->Get(i, &Value);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->ms_apNames[i], Value);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConTuneZone(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	int List = pResult->GetInteger(0);
	const char* pParamName = pResult->GetString(1);
	float NewValue = pResult->NumArguments() == 3 ? pResult->GetFloat(2) : -1;
	char aBuf[256];
	float Value;

	if (List >= 0 && List < NUM_TUNEZONES)
	{
		if (NewValue != -1 && pSelf->TuningList()[List].Set(pParamName, NewValue))
		{
			str_format(aBuf, sizeof(aBuf), "%s in zone %d changed to %.2f", pParamName, List, NewValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
			pSelf->SendTuningParams(-1, List);
		}
		else if (pSelf->TuningList()[List].Get(pParamName, &Value))
		{
			str_format(aBuf, sizeof(aBuf), "Value: %.2f", Value);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
		}
		else
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
	}
}

void CGameContext::ConTuneDumpZone(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	int List = pResult->GetInteger(0);
	char aBuf[256];
	if (List >= 0 && List < NUM_TUNEZONES)
	{
		for (int i = 0; i < pSelf->TuningList()[List].Num(); i++)
		{
			float v;
			pSelf->TuningList()[List].Get(i, &v);
			str_format(aBuf, sizeof(aBuf), "zone %d: %s %.2f", List, pSelf->TuningList()[List].ms_apNames[i], v);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		}
	}
}

void CGameContext::ConTuneResetZone(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	CTuningParams TuningParams;
	if (pResult->NumArguments())
	{
		int List = pResult->GetInteger(0);
		if (List >= 0 && List < NUM_TUNEZONES)
		{
			pSelf->TuningList()[List] = TuningParams;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Tunezone %d reset", List);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
			pSelf->SendTuningParams(-1, List);
		}
	}
	else
	{
		for (int i = 0; i < NUM_TUNEZONES; i++)
		{
			*(pSelf->TuningList() + i) = TuningParams;
			pSelf->SendTuningParams(-1, i);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "All Tunezones reset");
	}
}

void CGameContext::ConTuneSetZoneMsgEnter(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	if (pResult->NumArguments())
	{
		int List = pResult->GetInteger(0);
		if (List >= 0 && List < NUM_TUNEZONES)
		{
			str_copy(pSelf->m_aaZoneEnterMsg[List], pResult->GetString(1), sizeof(pSelf->m_aaZoneEnterMsg[List]));
		}
	}
}

void CGameContext::ConTuneSetZoneMsgLeave(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	if (pResult->NumArguments())
	{
		int List = pResult->GetInteger(0);
		if (List >= 0 && List < NUM_TUNEZONES)
		{
			str_copy(pSelf->m_aaZoneLeaveMsg[List], pResult->GetString(1), sizeof(pSelf->m_aaZoneLeaveMsg[List]));
		}
	}
}

void CGameContext::ConTuneLock(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int List = pResult->GetInteger(0);
	const char *pParamName = pResult->GetString(1);
	float NewValue = pResult->GetFloat(2);

	if(List >= 0 && List < NUM_TUNEZONES)
	{
		CLockedTune LockedTune(pParamName, NewValue);
		if(pSelf->SetLockedTune(&pSelf->LockedTuning()[List], LockedTune))
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "%s for lock %d changed to %.2f", pParamName, List, NewValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
			pSelf->SendTuningParams(-1);
		}
		else
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
	}
}

void CGameContext::ConTuneLockDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int List = pResult->GetInteger(0);
	char aBuf[256];
	if(List >= 0 && List < NUM_TUNEZONES)
	{
		for(unsigned int i = 0; i < pSelf->LockedTuning()[List].size(); i++)
		{
			str_format(aBuf, sizeof(aBuf), "lock %d: %s %.2f", List, pSelf->LockedTuning()[List][i].m_aParam, pSelf->LockedTuning()[List][i].m_Value);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		}
	}
}

void CGameContext::ConTuneLockSetMsgEnter(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
	{
		int List = pResult->GetInteger(0);
		if(List >= 0 && List < NUM_TUNEZONES)
		{
			str_copy(pSelf->m_aaTuneLockMsg[List], pResult->GetString(1), sizeof(pSelf->m_aaTuneLockMsg[List]));
		}
	}
}

void CGameContext::ConSwitchOpen(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	int Switch = pResult->GetInteger(0);

	if (pSelf->Collision()->m_HighestSwitchNumber > 0 && Switch >= 0 && Switch < pSelf->Collision()->m_HighestSwitchNumber + 1)
	{
		pSelf->Collision()->m_pSwitchers[Switch].m_Initial = false;
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "switch %d opened by default", Switch);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->m_World.m_Paused ^= 1;
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->StartRound();
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CHAT_ALL, -1, pResult->GetString(0));
}

void CGameContext::ConBroadcast(IConsole::IResult* pResult, void* pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// This is done clientside in 0.7, but DDNet clients only parse aBuf[i] == '\n'
	char aBuf[1024];
	str_copy(aBuf, pResult->GetString(0), sizeof(aBuf));

	int i, j;
	for(i = 0, j = 0; aBuf[i]; i++, j++)
	{
		if(aBuf[i] == '\\' && aBuf[i + 1] == 'n')
		{
			aBuf[j] = '\n';
			i++;
		}
		else if(i != j)
		{
			aBuf[j] = aBuf[i];
		}
	}
	aBuf[j] = '\0';

	pSelf->SendBroadcast(aBuf, -1);
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS - 1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : 0;
	if (!pSelf->m_apPlayers[ClientID])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->Pause(CPlayer::PAUSE_NONE, false); // reset /spec and /pause to allow rejoin
	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick() + pSelf->Server()->TickSpeed() * Delay * 60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	if (Team == TEAM_SPECTATORS)
		pSelf->m_apPlayers[ClientID]->Pause(CPlayer::PAUSE_NONE, true);
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext* pSelf = (CGameContext*)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CHAT_ALL, -1, aBuf);

	for (int i = 0; i < MAX_CLIENTS; ++i)
		if (pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team, false);
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "authorized player forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(!pSelf->Server()->ReverseTranslate(KickID, pResult->m_ClientID))
			return;
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if(!pSelf->Config()->m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, pSelf->Config()->m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if (!pSelf->Server()->ReverseTranslate(SpectateID, pResult->m_ClientID))
			return;
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		str_format(aBuf, sizeof(aBuf), "'%s' was moved to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, pSelf->Config()->m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}


void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);
	pSelf->AddVote(pDescription, pCommand);
}

void CGameContext::AddVote(const char *pDescription, const char *pCommand)
{
	if(m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// Force, "info" is used as placeholder cmd
	if (str_comp(pCommand, "info") != 0)
	{
		// check for valid option
		if(!Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}

		pDescription = str_skip_whitespaces_const(pDescription);
		if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}

		// check for duplicate entry
		for(CVoteOptionServer *pOption = m_pVoteOptionFirst; pOption; pOption = pOption->m_pNext)
		{
			if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				return;
			}
		}
	}

	// add the option
	++m_NumVoteOptions;
	int Len = str_length(pCommand);

	CVoteOptionServer *pOption = (CVoteOptionServer *)m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	m_pVoteOptionLast = pOption;
	if(!m_pVoteOptionFirst)
		m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;

	// Reset so the votes get added again
	pSelf->m_VotingMenu.AddPlaceholderVotes();

	// reset sending of vote options
	for(int i = 0; i < MAX_CLIENTS; i++)
		pSelf->m_VotingMenu.SendPageVotes(i);
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainUpdateHidePlayers(IConsole::IResult* pResult, void* pUserData, IConsole::FCommandCallback pfnCallback, void* pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if (pResult->NumArguments())
	{
		CGameContext* pSelf = (CGameContext*)pUserData;
		pSelf->UpdateHidePlayers();
	}
}

void CGameContext::ConchainUpdateLocalChat(IConsole::IResult* pResult, void* pUserData, IConsole::FCommandCallback pfnCallback, void* pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if (pResult->NumArguments())
	{
		CGameContext* pSelf = (CGameContext*)pUserData;
		if (!pSelf->Config()->m_SvLocalChat)
		{
			for (int i = 0; i < MAX_CLIENTS; i++)
			{
				if (pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->m_LocalChat)
				{
					pSelf->m_apPlayers[i]->m_LocalChat = false;
					pSelf->SendChatTarget(pResult->m_ClientID, "Local chat mode has been disabled, automatically entered public chat");
				}
			}
		}
	}
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		char aMotd[900];
		str_copy(aMotd, pSelf->FormatMotd(pSelf->Config()->m_SvMotd), sizeof(aMotd));
		for (int i = 0; i < MAX_CLIENTS; ++i)
			if (pSelf->m_apPlayers[i])
				pSelf->SendMotd(aMotd, i);
	}
}

void CGameContext::ConchainSettingUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		pSelf->SendSettings(-1);
	}
}

void CGameContext::ConchainGameinfoUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		if(pSelf->m_pController)
			pSelf->m_pController->UpdateGameInfo(-1);
	}
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	m_ChatPrintCBIndex = Console()->RegisterPrintCallback(0, SendChatResponse, this);

	Console()->Register("tune", "s[tuning] ?i[value]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneParam, this, "Tune variable to value", AUTHED_ADMIN);
	Console()->Register("tune_reset", "", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneReset, this, "Reset all tuning variables to defaults", AUTHED_ADMIN);
	Console()->Register("tunes", "", CFGFLAG_SERVER, ConTunes, this, "List all tuning variables and their values", AUTHED_HELPER);
	Console()->Register("tune_zone", "i[zone] s[tuning] ?i[value]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneZone, this, "Tune in zone a variable to value", AUTHED_ADMIN);
	Console()->Register("tune_zone_dump", "i[zone]", CFGFLAG_SERVER, ConTuneDumpZone, this, "Dump zone tuning in zone x", AUTHED_HELPER);
	Console()->Register("tune_zone_reset", "?i[zone]", CFGFLAG_SERVER, ConTuneResetZone, this, "reset zone tuning in zone x or in all zones", AUTHED_ADMIN);
	Console()->Register("tune_zone_enter", "i[zone] s[message]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneSetZoneMsgEnter, this, "which message to display on zone enter; use 0 for normal area", AUTHED_ADMIN);
	Console()->Register("tune_zone_leave", "i[zone] s[message]", CFGFLAG_SERVER|CFGFLAG_GAME, ConTuneSetZoneMsgLeave, this, "which message to display on zone leave; use 0 for normal area", AUTHED_ADMIN);
	Console()->Register("tune_lock", "i[number] s[tuning] i[value]", CFGFLAG_SERVER | CFGFLAG_GAME, ConTuneLock, this, "Tune for lock a variable to value", AUTHED_ADMIN);
	Console()->Register("tune_lock_dump", "i[number]", CFGFLAG_SERVER, ConTuneLockDump, this, "Dump lock tuning for number x", AUTHED_ADMIN);
	Console()->Register("tune_lock_enter", "i[number] r[message]", CFGFLAG_SERVER | CFGFLAG_GAME, ConTuneLockSetMsgEnter, this, "which message to display on tune lock enter; use 0 for lock reset", AUTHED_ADMIN);
	Console()->Register("switch_open", "i[switch]", CFGFLAG_SERVER|CFGFLAG_GAME, ConSwitchOpen, this, "Whether a switch is deactivated by default (otherwise activated)", AUTHED_ADMIN);

	Console()->Register("pausegame", "?i[on/off]", CFGFLAG_SERVER|CFGFLAG_STORE, ConPause, this, "Pause/unpause game", AUTHED_ADMIN);
	Console()->Register("change_map", "?r[map]", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map", AUTHED_ADMIN);
	Console()->Register("restart", "?i[seconds]", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)", AUTHED_ADMIN);
	Console()->Register("say", "r[message]", CFGFLAG_SERVER, ConSay, this, "Say in chat", AUTHED_MOD);
	Console()->Register("broadcast", "r[message]", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message", AUTHED_MOD);
	Console()->Register("set_team", "i[id] i[team-id] ?i[delay in minutes]", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team", AUTHED_ADMIN);
	Console()->Register("set_team_all", "i[team-id]", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team", AUTHED_ADMIN);

	Console()->Register("force_vote", "s[type] s[option] ?r[reason]", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option", AUTHED_ADMIN);
	Console()->Register("add_vote", "s[name] r[command]", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option", AUTHED_ADMIN);
	Console()->Register("remove_vote", "s[name]", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option", AUTHED_ADMIN);
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options", AUTHED_ADMIN);
	Console()->Register("vote", "r['yes'|'no']", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no", AUTHED_ADMIN);
	Console()->Register("dump_antibot", "?i[id]", CFGFLAG_SERVER, ConDumpAntibot, this, "Dumps the antibot status", AUTHED_ADMIN);

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

	Console()->Chain("sv_vote_kick", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_kick_min", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_spectate", ConchainSettingUpdate, this);
	Console()->Chain("sv_player_slots", ConchainSettingUpdate, this);
	Console()->Chain("sv_max_clients", ConchainSettingUpdate, this);

	Console()->Chain("sv_scorelimit", ConchainGameinfoUpdate, this);
	Console()->Chain("sv_timelimit", ConchainGameinfoUpdate, this);

	Console()->Register("random_map", "?i[stars]", CFGFLAG_SERVER, ConRandomMap, this, "Random map", AUTHED_ADMIN);
	Console()->Register("random_unfinished_map", "?i[stars]", CFGFLAG_SERVER, ConRandomUnfinishedMap, this, "Random unfinished map", AUTHED_ADMIN);

	// F-DDrace
	Console()->Chain("sv_hide_minigame_players", ConchainUpdateHidePlayers, this);
	Console()->Chain("sv_hide_dummies", ConchainUpdateHidePlayers, this);
	Console()->Chain("sv_local_chat", ConchainUpdateLocalChat, this);

	#define CONSOLE_COMMAND(name, params, flags, callback, userdata, help, accesslevel) m_pConsole->Register(name, params, flags, callback, userdata, help, accesslevel);
	#include <game/ddracecommands.h>
	#undef CONSOLE_COMMAND

	// Keep this for backwards compatibility
	#define CHAT_COMMAND(name, params, flags, callback, userdata, help, accesslevel) m_pConsole->Register(name, params, flags, callback, userdata, help, accesslevel);
	#include "ddracechat.h"
	#undef CHAT_COMMAND

	m_VotingMenu.Init(this);
}

void CGameContext::NewCommandHook(const CCommandManager::CCommand *pCommand, void *pContext)
{
	CGameContext *pSelf = (CGameContext *)pContext;
	pSelf->SendChatCommand(pCommand, -1);
}

void CGameContext::RemoveCommandHook(const CCommandManager::CCommand *pCommand, void *pContext)
{
	CGameContext *pSelf = (CGameContext *)pContext;
	pSelf->SendRemoveChatCommand(pCommand, -1);
}

struct SLegacyCommandContext {
	CGameContext *m_pGameContext;
	IConsole::FCommandCallback m_pfnCallback;
	void *m_pOriginalContext;
};

void CGameContext::LegacyCommandCallback(IConsole::IResult *pResult, void *pContext)
{
	CCommandManager::SCommandContext *pComContext = (CCommandManager::SCommandContext *)pContext;
	SLegacyCommandContext *pLegacyContext = (SLegacyCommandContext *)pComContext->m_pContext;
	CGameContext *pSelf = pLegacyContext->m_pGameContext;

	//Do Spam protection
	CPlayer *pPlayer = pSelf->m_apPlayers[pComContext->m_ClientID];
	const int64 Now = pSelf->Server()->Tick();
	const int64 TickSpeed = pSelf->Server()->TickSpeed();

	if (pSelf->Config()->m_SvSpamprotection && !str_startswith(pComContext->m_pCommand, "timeout ")
		&& pPlayer->m_LastCommands[0] && pPlayer->m_LastCommands[0] + TickSpeed > Now
		&& pPlayer->m_LastCommands[1] && pPlayer->m_LastCommands[1] + TickSpeed > Now
		&& pPlayer->m_LastCommands[2] && pPlayer->m_LastCommands[2] + TickSpeed > Now
		&& pPlayer->m_LastCommands[3] && pPlayer->m_LastCommands[3] + TickSpeed > Now
		)
		return;

	pPlayer->m_LastCommands[pPlayer->m_LastCommandPos] = Now;
	pPlayer->m_LastCommandPos = (pPlayer->m_LastCommandPos + 1) % 4;

	// Patch up the Result
	pResult->m_ClientID = pComContext->m_ClientID;

	// Set up the console output
	pSelf->m_ChatResponseTargetID = pComContext->m_ClientID;
	pSelf->Server()->RestrictRconOutput(pComContext->m_ClientID);
	pSelf->Console()->SetFlagMask(CFGFLAG_CHAT);

	int Authed = pSelf->Server()->GetAuthedState(pComContext->m_ClientID);
	if (Authed)
		pSelf->Console()->SetAccessLevel(Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : Authed == AUTHED_MOD ? IConsole::ACCESS_LEVEL_MOD : IConsole::ACCESS_LEVEL_HELPER);
	else
		pSelf->Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
	pSelf->Console()->SetPrintOutputLevel(pSelf->m_ChatPrintCBIndex, 0);

	pLegacyContext->m_pfnCallback(pResult, pLegacyContext->m_pOriginalContext);

	// Fix the console output
	pSelf->Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
	pSelf->Console()->SetFlagMask(CFGFLAG_SERVER);
	pSelf->m_ChatResponseTargetID = -1;
	pSelf->Server()->RestrictRconOutput(-1);
}

void CGameContext::RegisterLegacyDDRaceCommands()
{
	#define CHAT_COMMAND(name, params, flags, callback, userdata, help, accesslevel) \
	{ \
		static SLegacyCommandContext Context = { this, callback, userdata}; \
		CommandManager()->AddCommand(name, help, params, LegacyCommandCallback, &Context); \
	}

	#include "ddracechat.h"
	#undef CHAT_COMMAND
}

void CGameContext::OnInit()
{
	// init everything
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pAntibot = Kernel()->RequestInterface<IAntibot>();
	m_pAntibot->RoundStart(this);
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
	m_CommandManager.Init(m_pConsole, this, NewCommandHook, RemoveCommandHook);

	m_GameUuid = RandomUuid();
	Console()->SetTeeHistorianCommandCallback(CommandCallback, this);
	Console()->SetIsDummyCallback(ConsoleIsDummyCallback, this);
	Console()->SetIsInViewCallback(ConsoleIsInViewCallback, this);

	DeleteTempfile();

	// HACK: only set static size for items, which were available in the first 0.7 release
	// so new items don't break the snapshot delta
	static const int OLD_NUM_NETOBJTYPES = 23;
	for(int i = 0; i < OLD_NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers, m_pConfig);

	// reset tune locks
	for(int i = 0; i < NUM_TUNEZONES; i++)
		LockedTuning()[i].clear();

	// Reset Tunezones
	CTuningParams TuningParams;
	for (int i = 0; i < NUM_TUNEZONES; i++)
	{
		TuningList()[i] = TuningParams;
		TuningList()[i].Set("gun_curvature", 0);
		TuningList()[i].Set("gun_speed", 1400);
		TuningList()[i].Set("shotgun_curvature", 0);
		TuningList()[i].Set("shotgun_speed", 500);
	}

	for (int i = 0; i < NUM_TUNEZONES; i++)
	{
		// Send no text by default when changing tune zones.
		m_aaZoneEnterMsg[i][0] = 0;
		m_aaZoneLeaveMsg[i][0] = 0;
		m_aaTuneLockMsg[i][0] = 0;
	}
	// Reset Tuning
	if (Config()->m_SvTuneReset)
	{
		ResetTuning();
	}
	else
	{
		Tuning()->Set("gun_speed", 1400);
		Tuning()->Set("gun_curvature", 0);
		Tuning()->Set("shotgun_speed", 500);
		Tuning()->Set("shotgun_curvature", 0);
	}

	if (Config()->m_SvDDRaceTuneReset)
	{
		Config()->m_SvHit = 1;
		Config()->m_SvEndlessDrag = 0;
		Config()->m_SvOldLaser = 0;
		Config()->m_SvOldTeleportHook = 0;
		Config()->m_SvOldTeleportWeapons = 0;
		Config()->m_SvTeleportHoldHook = 0;
		//Config()->m_SvTeam = 1;
		Config()->m_SvShowOthersDefault = 0;

		if (Collision()->m_HighestSwitchNumber > 0)
			for (int i = 0; i < Collision()->m_HighestSwitchNumber + 1; ++i)
				Collision()->m_pSwitchers[i].m_Initial = true;
	}

	Console()->ExecuteFile(Config()->m_SvResetFile, -1);

	LoadMapSettings();

	m_pController = new CGameControllerDDRace(this);
	((CGameControllerDDRace*)m_pController)->m_Teams.Reset();
	m_pController->RegisterChatCommands(CommandManager());

	RegisterLegacyDDRaceCommands();

	m_TeeHistorianActive = Config()->m_SvTeeHistorian;
	if(m_TeeHistorianActive)
	{
		char aGameUuid[UUID_MAXSTRSIZE];
		FormatUuid(m_GameUuid, aGameUuid, sizeof(aGameUuid));

		char aFilename[64];
		str_format(aFilename, sizeof(aFilename), "teehistorian/%s.teehistorian", aGameUuid);

		m_TeeHistorianFile = Kernel()->RequestInterface<IStorage>()->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!m_TeeHistorianFile)
		{
			dbg_msg("teehistorian", "failed to open '%s'", aFilename);
			exit(1);
		}
		else
		{
			dbg_msg("teehistorian", "recording to '%s'", aFilename);
		}

		char aVersion[128];
		str_format(aVersion, sizeof(aVersion), "%s", GAME_VERSION);
		CTeeHistorian::CGameInfo GameInfo;
		GameInfo.m_GameUuid = m_GameUuid;
		GameInfo.m_pServerVersion = aVersion;
		GameInfo.m_StartTime = time(0);

		GameInfo.m_pServerName = Config()->m_SvName;
		GameInfo.m_ServerPort = Config()->m_SvPort;
		GameInfo.m_pGameType = m_pController->GetGameType();

		GameInfo.m_pConfig = Config();
		GameInfo.m_pTuning = Tuning();
		GameInfo.m_pUuids = &g_UuidManager;

		char aMapName[128];
		Server()->GetMapInfo(aMapName, sizeof(aMapName), &GameInfo.m_MapSize, &GameInfo.m_MapSha256, &GameInfo.m_MapCrc);
		GameInfo.m_pMapName = aMapName;

		m_TeeHistorian.Reset(&GameInfo, TeeHistorianWrite, this);

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			int Level = Server()->GetAuthedState(i);
			if(Level)
			{
				m_TeeHistorian.RecordAuthInitial(i, Level, Server()->AuthName(i));
			}
		}
		io_flush(m_TeeHistorianFile);
	}

	if (Config()->m_SvSoloServer)
	{
		Config()->m_SvTeam = 3;
		Config()->m_SvShowOthersDefault = 1;

		Tuning()->Set("player_collision", 0);
		Tuning()->Set("player_hooking", 0);

		for (int i = 0; i < NUM_TUNEZONES; i++)
		{
			TuningList()[i].Set("player_collision", 0);
			TuningList()[i].Set("player_hooking", 0);
		}
	}

	// delete old score object
	if (m_pScore)
		delete m_pScore;

	// create score object (add sql later)
#if defined(CONF_SQL)
	if(Config()->m_SvUseSQL)
		m_pScore = new CSqlScore(this);
	else
#endif
		m_pScore = new CFileScore(this);

	// F-DDrace
	FDDraceInitPreMapInit();

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);

	CTile* pFront = 0;
	CSwitchTile* pSwitch = 0;
	if (m_Layers.FrontLayer())
		pFront = (CTile*)Kernel()->RequestInterface<IMap>()->GetData(m_Layers.FrontLayer()->m_Front);
	if (m_Layers.SwitchLayer())
		pSwitch = (CSwitchTile*)Kernel()->RequestInterface<IMap>()->GetData(m_Layers.SwitchLayer()->m_Switch);

	for (int y = 0; y < pTileMap->m_Height; y++)
	{
		for (int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y * pTileMap->m_Width + x].m_Index;
			vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);

			Collision()->m_vTiles[Index].push_back(Pos);

			if (Index == TILE_OLDLASER)
			{
				Config()->m_SvOldLaser = 1;
				dbg_msg("game layer", "found old laser tile");
			}
			else if (Index == TILE_NPC)
			{
				m_Tuning.Set("player_collision", 0);
				dbg_msg("game layer", "found no collision tile");
			}
			else if (Index == TILE_EHOOK)
			{
				Config()->m_SvEndlessDrag = 1;
				dbg_msg("game layer", "found unlimited hook time tile");
			}
			else if (Index == TILE_NOHIT)
			{
				Config()->m_SvHit = 0;
				dbg_msg("game layer", "found no weapons hitting others tile");
			}
			else if (Index == TILE_NPH)
			{
				m_Tuning.Set("player_hooking", 0);
				dbg_msg("game layer", "found no player hooking tile");
			}

			if (Index > ENTITY_OFFSET)
			{
				m_pController->OnEntity(Index, Pos, LAYER_GAME, pTiles[y * pTileMap->m_Width + x].m_Flags);
			}

			if (pFront)
			{
				Index = pFront[y * pTileMap->m_Width + x].m_Index;

				Collision()->m_vTiles[Index].push_back(vec2(x*32.0f+16.0f, y*32.0f+16.0f));

				if (Index == TILE_OLDLASER)
				{
					Config()->m_SvOldLaser = 1;
					dbg_msg("front layer", "found old laser tile");
				}
				else if (Index == TILE_NPC)
				{
					m_Tuning.Set("player_collision", 0);
					dbg_msg("front layer", "found no collision tile");
				}
				else if (Index == TILE_EHOOK)
				{
					Config()->m_SvEndlessDrag = 1;
					dbg_msg("front layer", "found unlimited hook time tile");
				}
				else if (Index == TILE_NOHIT)
				{
					Config()->m_SvHit = 0;
					dbg_msg("front layer", "found no weapons hitting others tile");
				}
				else if (Index == TILE_NPH)
				{
					m_Tuning.Set("player_hooking", 0);
					dbg_msg("front layer", "found no player hooking tile");
				}
				if (Index > ENTITY_OFFSET)
				{
					m_pController->OnEntity(Index, Pos, LAYER_FRONT, pFront[y * pTileMap->m_Width + x].m_Flags);
				}
			}
			if (pSwitch)
			{
				Index = pSwitch[y * pTileMap->m_Width + x].m_Type;
				// TODO: Add off by default door here
				// if (Index == TILE_DOOR_OFF)
				if (Index > ENTITY_OFFSET)
				{
					m_pController->OnEntity(Index, Pos, LAYER_SWITCH, pSwitch[y * pTileMap->m_Width + x].m_Flags, pSwitch[y * pTileMap->m_Width + x].m_Number);
				}
			}
		}
	}

	// clamp sv_player_slots to 0..MaxClients
	if(Config()->m_SvMaxClients < Config()->m_SvPlayerSlots)
		Config()->m_SvPlayerSlots = Config()->m_SvMaxClients;

	FDDraceInit();

#ifdef CONF_DEBUG
	// clamp dbg_dummies to 0..MAX_CLIENTS-1
	if(MAX_CLIENTS <= Config()->m_DbgDummies)
		Config()->m_DbgDummies = MAX_CLIENTS;
	if(Config()->m_DbgDummies)
	{
		for(int i = 0; i < Config()->m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1, true, false);
			OnClientEnter(MAX_CLIENTS-i-1);
		}
	}
#endif
}

void CGameContext::FDDraceInitPreMapInit()
{
	Collision()->m_vTiles.clear();
	Collision()->m_vTiles.resize(NUM_INDICES);

	Collision()->m_vRedirectTiles.clear();

	// reset plots here but load them after the map init
	for (int i = 0; i < MAX_PLOTS; i++)
	{
		m_aPlots[i].m_aOwner[0] = 0;
		m_aPlots[i].m_aDisplayName[0] = 0;
		m_aPlots[i].m_ExpireDate = 0;
		m_aPlots[i].m_Size = 0;
		m_aPlots[i].m_ToTele = vec2(-1, -1);
		m_aPlots[i].m_vObjects.clear();
		m_aPlots[i].m_DestroyEndTick = 0;
		m_aPlots[i].m_DoorHealth = Config()->m_SvPlotDoorHealth;
	}
}

void CGameContext::FDDraceInit()
{
	// Save memory. Only save those few tiles we really use when calling CCollision::GetRandomTile or CGameworld::CanSpawn (due to calling getrandomtile)
	bool aRequiredRandomTilePositions[NUM_INDICES] = { 0 };
	aRequiredRandomTilePositions[ENTITY_SPAWN] = true;
	aRequiredRandomTilePositions[ENTITY_SPAWN_RED] = true;
	aRequiredRandomTilePositions[ENTITY_SPAWN_BLUE] = true;
	// for dummy spawns
	aRequiredRandomTilePositions[ENTITY_SHOP_DUMMY_SPAWN] = true;
	aRequiredRandomTilePositions[ENTITY_PLOT_SHOP_DUMMY_SPAWN] = true;
	aRequiredRandomTilePositions[ENTITY_BANK_DUMMY_SPAWN] = true;
	aRequiredRandomTilePositions[ENTITY_TAVERN_DUMMY_SPAWN] = true;
	aRequiredRandomTilePositions[TILE_SHOP] = true;
	aRequiredRandomTilePositions[TILE_PLOT_SHOP] = true;
	aRequiredRandomTilePositions[TILE_BANK] = true;
	aRequiredRandomTilePositions[TILE_TAVERN] = true;
	// minigames
	aRequiredRandomTilePositions[TILE_MINIGAME_BLOCK] = true;
	aRequiredRandomTilePositions[TILE_SURVIVAL_LOBBY] = true;
	aRequiredRandomTilePositions[TILE_SURVIVAL_SPAWN] = true;
	aRequiredRandomTilePositions[TILE_SURVIVAL_DEATHMATCH] = true;
	aRequiredRandomTilePositions[TILE_1VS1_LOBBY] = true;
	// jail
	aRequiredRandomTilePositions[TILE_JAIL] = true;
	aRequiredRandomTilePositions[TILE_JAIL_RELEASE] = true;
	for (int i = 0; i < NUM_INDICES; i++)
	{
		Collision()->m_aTileUsed[i] = Collision()->GetRandomTile(i) != vec2(-1, -1);
		if (!aRequiredRandomTilePositions[i])
			Collision()->m_vTiles[i].clear();
	}

	CreateFolders();

	AddAccount(); // account id 0 means not logged in, so we add an unused account with id 0
	m_LogoutAccountsPort = Config()->m_SvPort; // set before calling InitAccounts
	Storage()->ListDirectory(IStorage::TYPE_ALL, Config()->m_SvAccFilePath, InitAccounts, this);

	// load plot data AFTER map init
	for (int i = 0; i < Collision()->m_NumPlots + 1; i++)
			ReadPlotStats(i);
	ExpirePlots();

	if (Config()->m_SvBansFile[0])
		Console()->ExecuteFile(Config()->m_SvBansFile);
	if (Config()->m_SvWhitelistFile[0])
		Console()->ExecuteFile(Config()->m_SvWhitelistFile);

	m_LastDataSaveTick = Server()->Tick();

	ReadMoneyListFile();
	ReadSavedPlayersFile();
	ExpireSavedIdentities();

	{
		time_t rawtime;
		struct tm* timeinfo;
		time(&rawtime);
		timeinfo = localtime(&rawtime);

		int Seconds = 60 - timeinfo->tm_sec;
		int Minutes = (60 - timeinfo->tm_min) - 1;

		m_FullHourOffsetTicks = (Seconds * Server()->TickSpeed()) + (Minutes * 60 * Server()->TickSpeed());
	}

	int64 aNeededXP[] = { 5000, 15000, 25000, 35000, 50000, 65000, 80000, 100000, 120000, 130000, 160000, 200000, 240000, 280000, 325000, 370000, 420000, 470000, 520000, 600000,
	680000, 760000, 850000, 950000, 1200000, 1400000, 1600000, 1800000, 2000000, 2210000, 2430000, 2660000, 2900000, 3150000, 3500000, 3950000, 4500000, 5250000, 6100000, 7000000,
	8000000, 9000000, 10000000, 11000000, 12000000, 13000000, 14000000, 15000000, 16000000, 17000000, 18000000, 19000000, 20000000, 21000000, 22000000, 23000000, 24000000, 25000000,
	26000000, 27000000, 28000000, 29000000, 30000000, 31000000, 32000000, 33000000, 34000000, 35000000, 36000000, 37000000, 38000000, 39000000, 40000000, 41010000, 42020000, 43030000,
	44040000, 45050000, 46060000, 47070000, 48080000, 49090000, 50100000, 51110000, 52120000, 53130000, 54140000, 55150000, 56160000, 57170000, 58180000, 59190000, 60200000, 61300000,
	62400000, 63500000, 64600000, 65700000, 66800000, 68000000 };

	for (int i = 0; i < DIFFERENCE_XP_END; i++)
		m_aNeededXP[i] = aNeededXP[i];

	int aTaserPrice[] = { 50000, 75000, 100000, 150000, 200000, 200000, 200000, 300000, 300000, 400000 };
	for (int i = 0; i < NUM_TASER_LEVELS; i++)
		m_aTaserPrice[i] = aTaserPrice[i];

	int aPoliceLevel[] = { 18, 25, 30, 40, 50 };
	for (int i = 0; i < NUM_POLICE_LEVELS; i++)
		m_aPoliceLevel[i] = aPoliceLevel[i];

	for (int i = 0; i < NUM_HOUSES; i++)
		if (m_pHouses[i])
			delete m_pHouses[i];
	m_pHouses[HOUSE_SHOP] = new CShop(this, HOUSE_SHOP);
	m_pHouses[HOUSE_PLOT_SHOP] = new CShop(this, HOUSE_PLOT_SHOP);
	m_pHouses[HOUSE_BANK] = new CBank(this);
	m_pHouses[HOUSE_TAVERN] = new CTavern(this);

	for (int i = 0; i < NUM_MINIGAMES; i++)
		if (m_pMinigames[i])
			delete m_pMinigames[i];
	m_pMinigames[MINIGAME_BLOCK] = new CMinigame(this, MINIGAME_BLOCK);
	m_pMinigames[MINIGAME_SURVIVAL] = new CMinigame(this, MINIGAME_SURVIVAL);
	m_pMinigames[MINIGAME_1VS1] = new CArenas(this, MINIGAME_1VS1);
	m_pMinigames[MINIGAME_INSTAGIB_BOOMFNG] = new CMinigame(this, MINIGAME_INSTAGIB_BOOMFNG);
	m_pMinigames[MINIGAME_INSTAGIB_FNG] = new CMinigame(this, MINIGAME_INSTAGIB_FNG);

	m_WhoIs.Init(this);
	m_RainbowName.Init(this);

	m_SurvivalGameState = SURVIVAL_OFFLINE;
	m_SurvivalBackgroundState = SURVIVAL_OFFLINE;
	m_SurvivalTick = 0;
	m_SurvivalWinner = -1;

	// check if there are minigame spawns available (survival and instagib are checked in their own ticks)
	for (int i = 0; i < NUM_MINIGAMES; i++)
		m_aMinigameDisabled[i] = false;
	m_aMinigameDisabled[MINIGAME_BLOCK] = !Collision()->TileUsed(TILE_MINIGAME_BLOCK);
	m_aMinigameDisabled[MINIGAME_1VS1] = !Collision()->TileUsed(TILE_1VS1_LOBBY);

	SetMapSpecificOptions();
	if (Config()->m_SvDefaultDummies)
	{
		ConnectDefaultDummies();
	}
	else
	{
		for (int i = 0; i < NUM_HOUSES; i++)
			ConnectHouseDummy(i, true);
	}

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/presets", Config()->m_SvPlotFilePath);
	Storage()->ListDirectory(IStorage::TYPE_ALL, aPath, LoadPresetListCallback, this);

	m_LastPlayerCountUpdate = 0;
	SendPlayerCountUpdate();
}

void CGameContext::DeleteTempfile()
{
	if (m_aDeleteTempfile[0] != 0)
	{
		Storage()->RemoveFile(m_aDeleteTempfile, IStorage::TYPE_SAVE);
		m_aDeleteTempfile[0] = 0;
	}
}

void CGameContext::OnMapChange(char* pNewMapName, int MapNameSize)
{
	char aConfig[128];
	char aTemp[128];
	str_format(aConfig, sizeof(aConfig), "maps/%s.cfg", Config()->m_SvMap);
	str_format(aTemp, sizeof(aTemp), "%s.temp.%d", pNewMapName, pid());

	IOHANDLE File = Storage()->OpenFile(aConfig, IOFLAG_READ, IStorage::TYPE_ALL);
	if (!File)
	{
		// No map-specific config, just return.
		return;
	}
	CLineReader LineReader;
	LineReader.Init(File);

	array<char*> aLines;
	char* pLine;
	int TotalLength = 0;
	while ((pLine = LineReader.Get()))
	{
		int Length = str_length(pLine) + 1;
		char* pCopy = (char*)malloc(Length);
		mem_copy(pCopy, pLine, Length);
		aLines.add(pCopy);
		TotalLength += Length;
	}
	io_close(File);

	char* pSettings = (char*)malloc(TotalLength);
	int Offset = 0;
	for (int i = 0; i < aLines.size(); i++)
	{
		int Length = str_length(aLines[i]) + 1;
		mem_copy(pSettings + Offset, aLines[i], Length);
		Offset += Length;
		free(aLines[i]);
	}

	CDataFileReader Reader;
	Reader.Open(Storage(), pNewMapName, IStorage::TYPE_ALL);

	CDataFileWriter Writer;
	Writer.Init();

	int SettingsIndex = Reader.NumData();
	bool FoundInfo = false;
	for (int i = 0; i < Reader.NumItems(); i++)
	{
		int TypeID;
		int ItemID;
		int* pData = (int*)Reader.GetItem(i, &TypeID, &ItemID);
		int Size = Reader.GetItemSize(i);
		CMapItemInfoSettings MapInfo;
		if (TypeID == MAPITEMTYPE_INFO && ItemID == 0)
		{
			FoundInfo = true;
			CMapItemInfoSettings* pInfo = (CMapItemInfoSettings*)pData;
			if (Size >= (int)sizeof(CMapItemInfoSettings))
			{
				if (pInfo->m_Settings > -1)
				{
					SettingsIndex = pInfo->m_Settings;
					char* pMapSettings = (char*)Reader.GetData(SettingsIndex);
					int DataSize = Reader.GetDataSize(SettingsIndex);
					if (DataSize == TotalLength && mem_comp(pSettings, pMapSettings, DataSize) == 0)
					{
						// Configs coincide, no need to update map.
						return;
					}
					Reader.UnloadData(pInfo->m_Settings);
				}
				else
				{
					MapInfo = *pInfo;
					MapInfo.m_Settings = SettingsIndex;
					pData = (int*)& MapInfo;
					Size = sizeof(MapInfo);
				}
			}
			else
			{
				*(CMapItemInfo*)& MapInfo = *(CMapItemInfo*)pInfo;
				MapInfo.m_Settings = SettingsIndex;
				pData = (int*)& MapInfo;
				Size = sizeof(MapInfo);
			}
		}
		Writer.AddItem(TypeID, ItemID, Size, pData);
	}

	if (!FoundInfo)
	{
		CMapItemInfoSettings Info;
		Info.m_Version = 1;
		Info.m_Author = -1;
		Info.m_MapVersion = -1;
		Info.m_Credits = -1;
		Info.m_License = -1;
		Info.m_Settings = SettingsIndex;
		Writer.AddItem(MAPITEMTYPE_INFO, 0, sizeof(Info), &Info);
	}

	for (int i = 0; i < Reader.NumData() || i == SettingsIndex; i++)
	{
		if (i == SettingsIndex)
		{
			Writer.AddData(TotalLength, pSettings);
			continue;
		}
		unsigned char* pData = (unsigned char*)Reader.GetData(i);
		int Size = Reader.GetDataSize(i);
		Writer.AddData(Size, pData);
		Reader.UnloadData(i);
	}

	dbg_msg("mapchange", "imported settings");
	Reader.Close();
	Writer.OpenFile(Storage(), aTemp);
	Writer.Finish();

	str_copy(pNewMapName, aTemp, MapNameSize);
	str_copy(m_aDeleteTempfile, aTemp, sizeof(m_aDeleteTempfile));
}

void CGameContext::OnPreShutdown()
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if (!pPlayer)
			continue;

		// Move all money from wallet to bank
		if (pPlayer->GetAccID() >= ACC_START)
		{
			pPlayer->BankTransaction(pPlayer->GetWalletMoney(), "automatic wallet to bank due to shutdown");
			pPlayer->WalletTransaction(-pPlayer->GetWalletMoney());
		}

		if (pPlayer->GetCharacter())
		{
			// Either save the character and it's money or simply drop the money so it can get loaded on next server start
			if (Config()->m_SvShutdownSaveTees)
			{
				SaveCharacter(i, SAVE_WALLET|SAVE_FLAG, Config()->m_SvShutdownSaveTeeExpire);
				// Reset, so CPlayer::OnDisconnect() will not create a jail savetee when we have this already.
				pPlayer->m_EscapeTime = 0;
				pPlayer->m_JailTime = 0;
			}
			else
			{
				pPlayer->GetCharacter()->DropMoney(pPlayer->GetWalletMoney());
			}
		}

		// properly disconnect dummies on reload/shutdown
		if (pPlayer->m_IsDummy)
		{
			Server()->DummyLeave(i);
		}
		else if (Config()->m_SvShutdownAutoReconnect && Server()->IsSevendown(i) && ((CServer*)Server())->m_RunServer == CServer::STOPPING)
		{
			// 0.7 is not supported, they would just time out, cl_reconnect_timeout is a ddnet feature.
			const char *pMsg = ((CServer *)Server())->m_NetServer.m_ShutdownMessage;
			CMsgPacker Msg(NETMSG_MAP_CHANGE, true);
			Msg.AddString(pMsg[0] != '\0' ? pMsg : "Server is restarting...", 0);
			Msg.AddInt(0);
			Msg.AddInt(1);
			/*if (!Server()->IsSevendown(i))
			{
				Msg.AddInt(0);
				Msg.AddInt(0);
				Msg.AddRaw(&SHA256_ZEROED, sizeof(SHA256_ZEROED));
			}*/
			Server()->SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, i);
		}
	}

	LogoutAllAccounts();
	for (int i = 0; i < Collision()->m_NumPlots + 1; i++)
		WritePlotStats(i);
	WriteMoneyListFile();

	if (Config()->m_SvBansFile[0])
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "bans_save \"%s\"", Config()->m_SvBansFile);
		Console()->ExecuteLine(aBuf);
	}
	Server()->SaveWhitelist();
	SendPlayerCountUpdate(true);
}

void CGameContext::OnShutdown(bool FullShutdown)
{
	Antibot()->RoundEnd();

	if (FullShutdown)
		Score()->OnShutdown();

	if(m_TeeHistorianActive)
	{
		m_TeeHistorian.Finish();
		io_close(m_TeeHistorianFile);
	}

	DeleteTempfile();
	Console()->ResetServerGameSettings();
	Collision()->Dest();
	delete m_pController;
	m_pController = 0;
	for (int i = 0; i < NUM_HOUSES; i++)
	{
		delete m_pHouses[i];
		m_pHouses[i] = 0;
	}
	Clear();
}

void CGameContext::LoadMapSettings()
{
	IMap *pMap = Kernel()->RequestInterface<IMap>();
	int Start, Num;
	pMap->GetType(MAPITEMTYPE_INFO, &Start, &Num);
	for(int i = Start; i < Start + Num; i++)
	{
		int ItemID;
		CMapItemInfoSettings *pItem = (CMapItemInfoSettings *)pMap->GetItem(i, 0, &ItemID);
		int ItemSize = pMap->GetItemSize(i);
		if(!pItem || ItemID != 0)
			continue;

		if(ItemSize < (int)sizeof(CMapItemInfoSettings))
			break;
		if(!(pItem->m_Settings > -1))
			break;

		int Size = pMap->GetDataSize(pItem->m_Settings);
		char *pSettings = (char *)pMap->GetData(pItem->m_Settings);
		char *pNext = pSettings;
		while(pNext < pSettings + Size)
		{
			int StrSize = str_length(pNext) + 1;
			Console()->ExecuteLine(pNext, IConsole::CLIENT_ID_GAME);
			pNext += StrSize;
		}
		pMap->UnloadData(pItem->m_Settings);
		break;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map.cfg", Config()->m_SvMap);
	Console()->ExecuteFile(aBuf, IConsole::CLIENT_ID_NO_GAME);
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CNetObj_De_TuneParams *pTuneParams = static_cast<CNetObj_De_TuneParams *>(Server()->SnapNewItem(NETOBJTYPE_DE_TUNEPARAMS, 0, sizeof(CNetObj_De_TuneParams)));
		if(!pTuneParams)
			return;

		mem_copy(pTuneParams->m_aTuneParams, &m_Tuning, sizeof(pTuneParams->m_aTuneParams));
	}

	// snap in this order to make sure we always send important and required gameinfo aswell as players, then the gameworld with all entities
	m_pController->Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
	if (ClientID > -1)
		m_apPlayers[ClientID]->FakeSnap();

	for (int i = 0; i < NUM_MINIGAMES; i++)
		m_pMinigames[i]->Snap(ClientID);

	m_Events.Snap(ClientID);
	m_World.Snap(ClientID);
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_World.PostSnap();
	m_Events.Clear();
}

bool CGameContext::IsClientBot(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->IsDummy();
}

bool CGameContext::IsClientReady(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReadyToEnter;
}

bool CGameContext::IsClientPlayer(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() != TEAM_SPECTATORS;
}

bool CGameContext::IsClientSpectator(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS;
}

const CUuid CGameContext::GameUuid() const { return m_GameUuid; }
const char *CGameContext::GameType() const { return m_pController && m_pController->GetGameType() ? m_pController->GetGameType() : ""; }
const char *CGameContext::Version() const { return GAME_VERSION; }
const char* CGameContext::VersionSevendown() const { return GAME_VERSION_SEVENDOWN; }
const char *CGameContext::NetVersion() const { return GAME_NETVERSION; }
const char *CGameContext::NetVersionSevendown() const { return GAME_NETVERSION_SEVENDOWN; }

IGameServer *CreateGameServer() { return new CGameContext; }

void CGameContext::SendChatResponseAll(const char* pLine, void* pUser)
{
	CGameContext* pSelf = (CGameContext*)pUser;

	static volatile int ReentryGuard = 0;
	const char* pLineOrig = pLine;

	if (ReentryGuard)
		return;
	ReentryGuard++;

	if (*pLine == '[')
		do
			pLine++;
	while ((pLine - 2 < pLineOrig || *(pLine - 2) != ':') && *pLine != 0);//remove the category (e.g. [Console]: No Such Command)

	pSelf->SendChat(-1, CHAT_ALL, -1, pLine);

	ReentryGuard--;
}

void CGameContext::SendChatResponse(const char* pLine, void* pUser, bool Highlighted)
{
	CGameContext* pSelf = (CGameContext*)pUser;
	int ClientID = pSelf->m_ChatResponseTargetID;

	if (ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	const char* pLineOrig = pLine;

	static volatile int ReentryGuard = 0;

	if (ReentryGuard)
		return;
	ReentryGuard++;

	if (pLine[0] == '[')
	{
		// Remove time and category: [20:39:00][Console]
		pLine = str_find(pLine, "]: ");
		if (pLine)
			pLine += 3;
		else
			pLine = pLineOrig;
	}

	pSelf->SendChatTarget(ClientID, pLine);

	ReentryGuard--;
}

bool CGameContext::PlayerCollision()
{
	float Temp;
	m_Tuning.Get("player_collision", &Temp);
	return Temp != 0.0f;
}

bool CGameContext::PlayerHooking()
{
	float Temp;
	m_Tuning.Get("player_hooking", &Temp);
	return Temp != 0.0f;
}

float CGameContext::PlayerJetpack()
{
	float Temp;
	m_Tuning.Get("player_jetpack", &Temp);
	return Temp;
}

int CGameContext::ProcessSpamProtection(int ClientID)
{
	if(!m_apPlayers[ClientID])
		return 0;
	if(Config()->m_SvSpamprotection && m_apPlayers[ClientID]->m_LastChat
		&& m_apPlayers[ClientID]->m_LastChat + Server()->TickSpeed() * Config()->m_SvChatDelay > Server()->Tick())
		return 1;
	else
		m_apPlayers[ClientID]->m_LastChat = Server()->Tick();
	NETADDR Addr;
	Server()->GetClientAddr(ClientID, &Addr);
	int Muted = 0;

	for(int i = 0; i < m_NumMutes && !Muted; i++)
	{
		if(!net_addr_comp(&Addr, &m_aMutes[i].m_Addr, false))
			Muted = (m_aMutes[i].m_Expire - Server()->Tick()) / Server()->TickSpeed();
	}

	if (Muted > 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof aBuf, "You are not permitted to talk for the next %d seconds.", Muted);
		SendChatTarget(ClientID, aBuf);
		return 1;
	}

	if ((m_apPlayers[ClientID]->m_ChatScore += Config()->m_SvChatPenalty) > Config()->m_SvChatThreshold)
	{
		Mute(&Addr, Config()->m_SvSpamMuteDuration, Server()->ClientName(ClientID));
		m_apPlayers[ClientID]->m_ChatScore = 0;
		return 1;
	}

	return 0;
}

int CGameContext::GetDDRaceTeam(int ClientID)
{
	CGameControllerDDRace* pController = (CGameControllerDDRace*)m_pController;
	return pController->m_Teams.m_Core.Team(ClientID);
}

void CGameContext::ResetTuning()
{
	CTuningParams TuningParams;
	m_Tuning = TuningParams;
	Tuning()->Set("gun_speed", 1400);
	Tuning()->Set("gun_curvature", 0);
	Tuning()->Set("shotgun_speed", 500);
	Tuning()->Set("shotgun_curvature", 0);
	SendTuningParams(-1);
}

bool CGameContext::IsVersionBanned(int Version)
{
	char aVersion[16];
	str_format(aVersion, sizeof(aVersion), "%d", Version);

	return str_in_list(Config()->m_SvBannedVersions, ",", aVersion);
}

void CGameContext::List(int ClientID, const char* pFilter)
{
	#define SEND(str) \
		do \
		{ \
			if (ClientID == -1) \
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", str); \
			else \
				SendChatTarget(ClientID, str); \
		} while(0)

	int Total = 0;
	int Dummies = 0;
	char aBuf[128];
	int Bufcnt = 0;
	if (pFilter[0])
		str_format(aBuf, sizeof(aBuf), "Listing players with \"%s\" in name:", pFilter);
	else
		str_format(aBuf, sizeof(aBuf), "Listing all players:");
	SEND(aBuf);
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (m_apPlayers[i])
		{
			Total++;
			if (Server()->GetDummy(i) != -1)
				Dummies++;

			const char* pName = Server()->ClientName(i);
			if (str_find_nocase(pName, pFilter) == NULL)
				continue;
			if (Bufcnt + str_length(pName) + 4 > 128)
			{
				SEND(aBuf);
				Bufcnt = 0;
			}
			if (Bufcnt != 0)
			{
				str_format(&aBuf[Bufcnt], sizeof(aBuf) - Bufcnt, ", %s", pName);
				Bufcnt += 2 + str_length(pName);
			}
			else
			{
				str_format(&aBuf[Bufcnt], sizeof(aBuf) - Bufcnt, "%s", pName);
				Bufcnt += str_length(pName);
			}
		}
	}
	if (Bufcnt != 0)
		SEND(aBuf);
	str_format(aBuf, sizeof(aBuf), "%d players online, including %d client dummies", Total, Dummies/2);
	SEND(aBuf);
	#undef SEND
}

int CGameContext::GetClientDDNetVersion(int ClientID)
{
	if (ClientID < 0)
		return 0;

	IServer::CClientInfo Info = {0};
	Server()->GetClientInfo(ClientID, &Info);
	return Info.m_DDNetVersion;
}

Mask128 CGameContext::ClientsMaskExcludeClientVersionAndHigher(int Version)
{
	Mask128 Mask = CmaskNone();
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GetClientDDNetVersion(i) >= Version)
			continue;
		Mask |= CmaskOne(i);
	}
	return Mask;
}

bool CGameContext::RateLimitPlayerVote(int ClientID)
{
	int64 Now = Server()->Tick();
	int64 TickSpeed = Server()->TickSpeed();
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(Config()->m_SvRconVote && !Server()->GetAuthedState(ClientID))
	{
		SendChatTarget(ClientID, "You can only vote after logging in.");
		return true;
	}

	if(m_VoteCloseTime)
	{
		SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
		return true;
	}

	if(Now < pPlayer->m_FirstVoteTick)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "You must wait %d seconds before making your first vote.", (int)((pPlayer->m_FirstVoteTick - Now) / TickSpeed) + 1);
		SendChatTarget(ClientID, aBuf);
		return true;
	}

	int TimeLeft = pPlayer->m_LastVoteCall + TickSpeed * Config()->m_SvVoteDelay - Now;
	if(pPlayer->m_LastVoteCall && TimeLeft > 0)
	{
		char aChatmsg[64];
		str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote.", (int)(TimeLeft / TickSpeed) + 1);
		SendChatTarget(ClientID, aChatmsg);
		return true;
	}

	NETADDR Addr;
	Server()->GetClientAddr(ClientID, &Addr);
	int VoteMuted = 0;
	for(int i = 0; i < m_NumVoteMutes && !VoteMuted; i++)
		if(!net_addr_comp(&Addr, &m_aVoteMutes[i].m_Addr, false))
			VoteMuted = (m_aVoteMutes[i].m_Expire - Server()->Tick()) / Server()->TickSpeed();
	if(VoteMuted > 0)
	{
		char aChatmsg[64];
		str_format(aChatmsg, sizeof(aChatmsg), "You are not permitted to vote for the next %d seconds.", VoteMuted);
		SendChatTarget(ClientID, aChatmsg);
		return true;
	}
	return false;
}

bool CGameContext::RateLimitPlayerMapVote(int ClientID)
{
	if(!Server()->GetAuthedState(ClientID) && time_get() < m_LastMapVote + (time_freq() * Config()->m_SvVoteMapTimeDelay))
	{
		char aChatmsg[512] = {0};
		str_format(aChatmsg, sizeof(aChatmsg), "There's a %d second delay between map-votes, please wait %d seconds.",
				Config()->m_SvVoteMapTimeDelay, (int)((m_LastMapVote + Config()->m_SvVoteMapTimeDelay * time_freq() - time_get())/time_freq()));
		SendChatTarget(ClientID, aChatmsg);
		return true;
	}
	return false;
}

void CGameContext::OnUpdatePlayerServerInfo(char *aBuf, int BufSize, int ID)
{
	if(!m_apPlayers[ID])
		return;

	char aCSkinName[64];

	CTeeInfo &TeeInfo = m_apPlayers[ID]->m_TeeInfos;

	char aJsonSkin[400];
	aJsonSkin[0] = '\0';

	// Only use 0.6 info, as only 0.6 clients handle this info right now, and it doesnt make sense to ship 0.7 skin info to 0.6 clients yet, if they wont display anything then
	//if(Server()->IsSevendown(ID))
	{
		// 0.6
		if(TeeInfo.m_Sevendown.m_UseCustomColor)
		{
			str_format(aJsonSkin, sizeof(aJsonSkin),
				"\"name\":\"%s\","
				"\"color_body\":%d,"
				"\"color_feet\":%d",
				EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_Sevendown.m_SkinName),
				TeeInfo.m_Sevendown.m_ColorBody,
				TeeInfo.m_Sevendown.m_ColorFeet);
		}
		else
		{
			str_format(aJsonSkin, sizeof(aJsonSkin),
				"\"name\":\"%s\"",
				EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_Sevendown.m_SkinName));
		}
	}
	/*else
	{
		const char *apPartNames[NUM_SKINPARTS] = {"body", "marking", "decoration", "hands", "feet", "eyes"};
		char aPartBuf[64];

		for(int i = 0; i < NUM_SKINPARTS; ++i)
		{
			str_format(aPartBuf, sizeof(aPartBuf),
				"%s\"%s\":{"
				"\"name\":\"%s\"",
				i == 0 ? "" : ",",
				apPartNames[i],
				EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_aaSkinPartNames[i]));

			str_append(aJsonSkin, aPartBuf, sizeof(aJsonSkin));

			if(TeeInfo.m_aUseCustomColors[i])
			{
				str_format(aPartBuf, sizeof(aPartBuf),
					",color:%d",
					TeeInfo.m_aSkinPartColors[i]);
				str_append(aJsonSkin, aPartBuf, sizeof(aJsonSkin));
			}
			str_append(aJsonSkin, "}", sizeof(aJsonSkin));
		}
	}*/

	str_format(aBuf, BufSize,
		",\"skin\":{"
		"%s"
		"},"
		"\"afk\":%s,"
		"\"team\":%d",
		aJsonSkin,
		JsonBool(m_apPlayers[ID]->m_Afk),
		m_apPlayers[ID]->GetTeam());
}

// DDRace

void CGameContext::CallVote(int ClientID, const char *pDesc, const char *pCmd, const char *pReason, const char *pChatmsg, const char *pSevendownDesc)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	int64 Now = Server()->Tick();
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pPlayer)
		return;

	SendChat(-1, CHAT_ALL, -1, pChatmsg, -1, CHAT_SEVENDOWN);
	if(!pSevendownDesc)
		pSevendownDesc = pDesc;

	m_VoteCreator = ClientID;
	StartVote(pDesc, pCmd, pReason, pSevendownDesc);
	pPlayer->m_Vote = 1;
	pPlayer->m_VotePos = m_VotePos = 1;
	pPlayer->m_LastVoteCall = Now;
}

void CGameContext::ConRandomMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Stars = pResult->NumArguments() ? pResult->GetInteger(0) : -1;

	pSelf->m_pScore->RandomMap(&pSelf->m_pRandomMapResult, pSelf->m_VoteCreator, Stars);
}

void CGameContext::ConRandomUnfinishedMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int Stars = pResult->NumArguments() ? pResult->GetInteger(0) : -1;

	pSelf->m_pScore->RandomUnfinishedMap(&pSelf->m_pRandomMapResult, pSelf->m_VoteCreator, Stars);
}

// F-DDrace

void CGameContext::ReadPlotStats(int ID)
{
	std::string data;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s/%s/%d.plot", Config()->m_SvPlotFilePath, Server()->GetCurrentMapName(), ID);
	std::fstream PlotFile(aBuf);
	if (!PlotFile.is_open())
		return;

	for (int i = 0; i < NUM_PLOT_VARIABLES; i++)
	{
		getline(PlotFile, data);
		const char *pData = data.c_str();

		switch (i)
		{
		case PLOT_OWNER_ACC_USERNAME:		str_copy(m_aPlots[ID].m_aOwner, pData, sizeof(m_aPlots[ID].m_aOwner)); break;
		case PLOT_DISPLAY_NAME:				str_copy(m_aPlots[ID].m_aDisplayName, pData, sizeof(m_aPlots[ID].m_aDisplayName)); break;
		case PLOT_EXPIRE_DATE:				m_aPlots[ID].m_ExpireDate = atoi(pData); break;
		case PLOT_DOOR_STATUS:				SetPlotDoorStatus(ID, atoi(pData)); break;
		case PLOT_OBJECTS:
		{
			std::vector<CEntity *> vEntities = ReadPlotObjects(pData, ID);
			for (unsigned int j = 0; j < vEntities.size(); j++)
			{
				vEntities[j]->m_PlotID = ID;
				m_aPlots[ID].m_vObjects.push_back(vEntities[j]);
			}
		} break;
		}
	}
}

void CGameContext::WritePlotStats(int ID)
{
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s/%s/%d.plot", Config()->m_SvPlotFilePath, Server()->GetCurrentMapName(), ID);
	std::ofstream PlotFile(aBuf);

	if (PlotFile.is_open())
	{
		int PlotDoorStatus = Collision()->m_pSwitchers ? Collision()->m_pSwitchers[Collision()->GetSwitchByPlot(ID)].m_Status[0] : 0;
		PlotFile << m_aPlots[ID].m_aOwner << "\n";
		PlotFile << m_aPlots[ID].m_aDisplayName << "\n";
		PlotFile << m_aPlots[ID].m_ExpireDate << "\n";
		PlotFile << PlotDoorStatus << "\n";
		
		for (unsigned int i = 0; i < m_aPlots[ID].m_vObjects.size(); i++)
			WritePlotObject(m_aPlots[ID].m_vObjects[i], &PlotFile);

		PlotFile << "\n";
	}
}

void CGameContext::WritePlotObject(CEntity *pEntity, std::ofstream *pFile, vec2 *pPos)
{
	vec2 Pos = pPos ? *pPos : pEntity->GetPos();
	char aEntry[128];
	switch (pEntity->GetObjType())
	{
		case CGameWorld::ENTTYPE_PICKUP:
		{
			CPickup *pPickup = (CPickup *)pEntity;
			str_format(aEntry, sizeof(aEntry), "%d:%.2f/%.2f:%d:%d,", CGameWorld::ENTTYPE_PICKUP, Pos.x/32.f, Pos.y/32.f, pPickup->GetType(), pPickup->GetSubtype());
			*pFile << aEntry;
			break;
		}
		case CGameWorld::ENTTYPE_DOOR:
		{
			CDoor *pDoor = (CDoor *)pEntity;
			str_format(aEntry, sizeof(aEntry), "%d:%.2f/%.2f:%.2f:%d:%d:%d:%d:%d:%d,", CGameWorld::ENTTYPE_DOOR, Pos.x/32.f, Pos.y/32.f, pDoor->GetRotation(), pDoor->GetLength(), (int)pDoor->m_Collision, pDoor->GetThickness(), pDoor->m_Number, (int)Collision()->m_pSwitchers[pDoor->m_Number].m_Status[0], pDoor->GetColor());
			*pFile << aEntry;
			break;
		}
		case CGameWorld::ENTTYPE_BUTTON:
		{
			CButton *pButton = (CButton *)pEntity;
			str_format(aEntry, sizeof(aEntry), "%d:%.2f/%.2f:%d,", CGameWorld::ENTTYPE_BUTTON, Pos.x/32.f, Pos.y/32.f, pButton->m_Number);
			*pFile << aEntry;
			break;
		}
		case CGameWorld::ENTTYPE_SPEEDUP:
		{
			CSpeedup *pSpeedup = (CSpeedup *)pEntity;
			str_format(aEntry, sizeof(aEntry), "%d:%.2f/%.2f:%d:%d:%d,", CGameWorld::ENTTYPE_SPEEDUP, Pos.x/32.f, Pos.y/32.f, pSpeedup->GetAngle(), pSpeedup->GetForce(), pSpeedup->GetMaxSpeed());
			*pFile << aEntry;
			break;
		}
		case CGameWorld::ENTTYPE_TELEPORTER:
		{
			CTeleporter *pTeleporter = (CTeleporter *)pEntity;
			str_format(aEntry, sizeof(aEntry), "%d:%.2f/%.2f:%d:%d,", CGameWorld::ENTTYPE_TELEPORTER, Pos.x/32.f, Pos.y/32.f, pTeleporter->GetType(), pTeleporter->m_Number);
			*pFile << aEntry;
			break;
		}
	}
}

std::vector<CEntity *> CGameContext::ReadPlotObjects(const char *pLine, int PlotID)
{
	const char *pData = pLine;
	std::vector<CEntity *> vEntities;
	std::vector< std::pair<int, int> > vNumbers;
	while (1)
	{
		if (!pData)
			break;

		vec2 Pos = vec2(-1, -1);
		int EntityType = -1;

		sscanf(pData, "%d", &EntityType);
		switch (EntityType)
		{
			case CGameWorld::ENTTYPE_PICKUP:
			{
				int Type = -1;
				int Subtype = -1;
				sscanf(pData, "%d:%f/%f:%d:%d", &EntityType, &Pos.x, &Pos.y, &Type, &Subtype);
				if (Type >= 0 && Subtype >= 0)
				{
					vEntities.push_back(new CPickup(&m_World, vec2(Pos.x*32.f, Pos.y*32.f), Type, Subtype));
				}
				break;
			}
			case CGameWorld::ENTTYPE_DOOR:
			{
				float Rotation = -1.f;
				int Length = -1;
				int CollisionActive = -1;
				int Thickness = -1;
				int Number = -1;
				int Status = -1;
				int Color = LASERTYPE_DOOR;
				sscanf(pData, "%d:%f/%f:%f:%d:%d:%d:%d:%d:%d", &EntityType, &Pos.x, &Pos.y, &Rotation, &Length, &CollisionActive, &Thickness, &Number, &Status, &Color);
				if (Rotation >= 0 && Length >= 0 && CollisionActive >= 0 && Thickness >= 0 && Number >= 0 && Status >= 0)
				{
					int NewNumber = -1;
					if (Number == 0)
					{
						NewNumber = 0;
					}
					else
					{
						for (unsigned int i = 0; i < vNumbers.size(); i++)
							if (vNumbers[i].first == Number)
								NewNumber = vNumbers[i].second;

						if (NewNumber == -1)
						{
							if ((int)vNumbers.size() >= Collision()->GetNumMaxDoors(PlotID))
								break;

							NewNumber = Collision()->GetSwitchByPlotLaserDoor(PlotID, vNumbers.size());
							SetPlotDrawDoorStatus(PlotID, vNumbers.size(), Status);

							std::pair<int, int> Pair;
							Pair.first = Number;
							Pair.second = NewNumber;
							vNumbers.push_back(Pair);
						}
					}

					vEntities.push_back(new CDoor(&m_World, vec2(Pos.x*32.f, Pos.y*32.f), Rotation, Length, NewNumber, CollisionActive, Thickness, Color));
				}
				break;
			}
			case CGameWorld::ENTTYPE_BUTTON:
			{
				int Number = -1;
				sscanf(pData, "%d:%f/%f:%d", &EntityType, &Pos.x, &Pos.y, &Number);
				if (Number >= 0)
				{
					int NewNumber = -1;
					for (unsigned int i = 0; i < vNumbers.size(); i++)
						if (vNumbers[i].first == Number)
							NewNumber = vNumbers[i].second;

					if (NewNumber == -1)
					{
						if ((int)vNumbers.size() >= Collision()->GetNumMaxDoors(PlotID))
							break;

						NewNumber = Collision()->GetSwitchByPlotLaserDoor(PlotID, vNumbers.size());
						std::pair<int, int> Pair;
						Pair.first = Number;
						Pair.second = NewNumber;
						vNumbers.push_back(Pair);
					}

					vEntities.push_back(new CButton(&m_World, vec2(Pos.x*32.f, Pos.y*32.f), NewNumber));
				}
				break;
			}
			case CGameWorld::ENTTYPE_SPEEDUP:
			{
				int Angle = -1;
				int Force = -1;
				int MaxSpeed = -1;
				sscanf(pData, "%d:%f/%f:%d:%d:%d", &EntityType, &Pos.x, &Pos.y, &Angle, &Force, &MaxSpeed);
				if (Angle >= 0 && Force > 0 && MaxSpeed >= 0)
				{
					vEntities.push_back(new CSpeedup(&m_World, vec2(Pos.x*32.f, Pos.y*32.f), Angle, Force, MaxSpeed));
				}
				break;
			}
			case CGameWorld::ENTTYPE_TELEPORTER:
			{
				int Type = 0;
				int Number = -1;
				sscanf(pData, "%d:%f/%f:%d:%d", &EntityType, &Pos.x, &Pos.y, &Type, &Number);
				if (Type > 0 && Number >= 0)
				{
					int NewNumber = -1;
					for (unsigned int i = 0; i < vNumbers.size(); i++)
						if (vNumbers[i].first == Number)
							NewNumber = vNumbers[i].second;

					if (NewNumber == -1)
					{
						if ((int)vNumbers.size() >= Collision()->GetNumMaxTeleporters(PlotID))
							break;

						NewNumber = Collision()->GetSwitchByPlotTeleporter(PlotID, vNumbers.size());
						std::pair<int, int> Pair;
						Pair.first = Number;
						Pair.second = NewNumber;
						vNumbers.push_back(Pair);
					}

					vEntities.push_back(new CTeleporter(&m_World, vec2(Pos.x*32.f, Pos.y*32.f), Type, NewNumber));
				}
				break;
			}
		}

		// jump to next comma, if it exists skip it so we can start the next loop run with the next data
		if ((pData = str_find(pData, ",")))
			pData++;
	}

	return vEntities;
}

int CGameContext::LoadPresetListCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	if (!IsDir && str_endswith(pName, ".plot"))
	{
		std::string Name = pName;
		Name = Name.erase(Name.size() - 5); // remove .plot
		for (unsigned int i = 0; i < pSelf->m_vPresetList.size(); i++)
			if (pSelf->m_vPresetList[i] == Name)
				return 0;

		pSelf->m_vPresetList.push_back(Name);
	}
	return 0;
}

int CGameContext::GetPlotID(int AccID)
{
	if (AccID < ACC_START)
		return 0;

	for (int i = PLOT_START; i < Collision()->m_NumPlots + 1; i++)
		if (str_comp(m_Accounts[AccID].m_Username, m_aPlots[i].m_aOwner) == 0)
			return i;
	return 0;
}

int CGameContext::GetTilePlotID(vec2 Pos, bool CheckDoor)
{
	int PlotDoor = CheckDoor ? Collision()->GetPlotBySwitch(Collision()->CheckPointDoor(Pos, 0, true, false)) : 0; // can use team 0 for checkpointdoor because closedonly = false
	return PlotDoor >= PLOT_START ? PlotDoor : Collision()->GetPlotID(Collision()->GetMapIndex(Pos));
}

void CGameContext::SetPlotInfo(int PlotID, int AccID)
{
	if (PlotID <= 0 || PlotID > Collision()->m_NumPlots || AccID < ACC_START)
		return;

	str_copy(m_aPlots[PlotID].m_aOwner, m_Accounts[AccID].m_Username, sizeof(m_aPlots[PlotID].m_aOwner));
	str_copy(m_aPlots[PlotID].m_aDisplayName, m_Accounts[AccID].m_aLastPlayerName, sizeof(m_aPlots[PlotID].m_aDisplayName));
	WritePlotStats(PlotID);
}

void CGameContext::SetPlotExpire(int PlotID)
{
	if (PlotID <= 0 || PlotID > Collision()->m_NumPlots)
		return;

	int Days = m_aPlots[PlotID].m_Size == 0 ? ITEM_EXPIRE_PLOT_SMALL : m_aPlots[PlotID].m_Size == 1 ? ITEM_EXPIRE_PLOT_BIG : 0;
	SetExpireDateDays(&m_aPlots[PlotID].m_ExpireDate, Days);
}

bool CGameContext::HasPlotByIP(int ClientID)
{
	bool HasPlot = false;
	NETADDR Addr;
	Server()->GetClientAddr(ClientID, &Addr);

	for (int i = PLOT_START; i < Collision()->m_NumPlots + 1; i++)
	{
		int ID = GetAccount(m_aPlots[i].m_aOwner);
		if (ID < ACC_START)
			continue;

		if (SameIP(ID, &Addr))
			HasPlot = true;

		if (!IsAccLoggedInThisPort(ID))
			FreeAccount(ID);

		if (HasPlot)
			break;
	}

	return HasPlot;
}

unsigned int CGameContext::GetMaxPlotObjects(int PlotID)
{
	if (PlotID < 0 || PlotID > Collision()->m_NumPlots)
		return 0;

	if (PlotID >= PLOT_START)
	{
		switch (m_aPlots[PlotID].m_Size)
		{
		case PLOT_SMALL: return Config()->m_SvMaxObjectsPlotSmall;
		case PLOT_BIG: return Config()->m_SvMaxObjectsPlotBig;
		}
	}

	return Config()->m_SvMaxObjectsFreeDraw;
}

const char *CGameContext::GetPlotSizeString(int PlotID)
{
	if (PlotID <= 0 || PlotID > Collision()->m_NumPlots)
		return "Unknown";

	switch (m_aPlots[PlotID].m_Size)
	{
	case PLOT_SMALL: return "small";
	case PLOT_BIG: return "big";
	}

	return "Unkown";
}

int CGameContext::GetMaxPlotSpeedups(int PlotID)
{
	if (PlotID <= 0 || PlotID > Collision()->m_NumPlots)
		return 0; // free draw has unlimited, so doesnt matter

	switch (m_aPlots[PlotID].m_Size)
	{
	case PLOT_SMALL: return 15;
	case PLOT_BIG: return 40;
	}
	return 0;
}

int CGameContext::GetMaxPlotTeleporters(int PlotID)
{
	if (PlotID <= 0 || PlotID > Collision()->m_NumPlots)
		return 0; // free draw has unlimited, so doesnt matter

	switch (m_aPlots[PlotID].m_Size)
	{
	case PLOT_SMALL: return 4;
	case PLOT_BIG: return 10;
	}
	return 0;
}

void CGameContext::ExpirePlots()
{
	for (int i = PLOT_START; i < Collision()->m_NumPlots + 1; i++)
	{
		if (IsExpired(m_aPlots[i].m_ExpireDate))
		{
			int AccID = GetAccIDByUsername(m_aPlots[i].m_aOwner);
			if (AccID >= ACC_START)
			{
				int ClientID = m_Accounts[AccID].m_ClientID;
				if (ClientID >= 0 && m_apPlayers[ClientID])
				{
					SendChatTarget(ClientID, "Your plot expired");
					m_apPlayers[ClientID]->CancelPlotAuction();
					m_apPlayers[ClientID]->CancelPlotSwap();
					m_apPlayers[ClientID]->StopPlotEditing();
				}
			}

			m_aPlots[i].m_aOwner[0] = 0;
			m_aPlots[i].m_aDisplayName[0] = 0;
			m_aPlots[i].m_ExpireDate = 0;
			m_aPlots[i].m_DestroyEndTick = 0;
			m_aPlots[i].m_DoorHealth = Config()->m_SvPlotDoorHealth;
			ClearPlot(i);
			SetPlotDoorStatus(i, true);
		}
	}
}

void CGameContext::SetPlotDoorStatus(int PlotID, bool Close)
{
	if (PlotID <= 0 || PlotID > Collision()->m_NumPlots || !Collision()->m_pSwitchers)
		return;

	int Switch = Collision()->GetSwitchByPlot(PlotID);
	for (int i = 0; i < VANILLA_MAX_CLIENTS; i++)
		Collision()->m_pSwitchers[Switch].m_Status[i] = Close;
}

void CGameContext::SetPlotDrawDoorStatus(int PlotID, int Door, bool Close)
{
	if (PlotID <= 0 || PlotID > Collision()->m_NumPlots || Door >= Collision()->GetNumMaxDoors(PlotID) || !Collision()->m_pSwitchers)
		return;

	int Switch = Collision()->GetSwitchByPlotLaserDoor(PlotID, Door);
	for (int i = 0; i < VANILLA_MAX_CLIENTS; i++)
		Collision()->m_pSwitchers[Switch].m_Status[i] = Close;
}

void CGameContext::SetPlotDrawDoorStatus(int Number, bool Close)
{
	if (!Collision()->IsPlotDrawDoor(Number) || !Collision()->m_pSwitchers)
		return;

	for (int i = 0; i < VANILLA_MAX_CLIENTS; i++)
		Collision()->m_pSwitchers[Number].m_Status[i] = Close;
}

void CGameContext::ClearPlot(int PlotID)
{
	if (PlotID >= 0 && PlotID <= Collision()->m_NumPlots)
	{
		for (unsigned i = 0; i < m_aPlots[PlotID].m_vObjects.size(); i++)
			m_World.DestroyEntity(m_aPlots[PlotID].m_vObjects[i]);
		m_aPlots[PlotID].m_vObjects.clear();
	}
}

int CGameContext::IntersectedLineDoor(vec2 Pos0, vec2 Pos1, int Team, bool PlotDoorOnly, bool ClosedOnly)
{
	int Number = Collision()->IntersectLineDoor(Pos0, Pos1, 0, 0, Team, PlotDoorOnly, ClosedOnly);
	return Number; // can be used as bool, -1 is plot built laser wall which would return true too
}

void CGameContext::RemovePortalsFromPlot(int PlotID)
{
	if (PlotID >= PLOT_START && PlotID <= Collision()->m_NumPlots)
	{
		CPortal *pPortal = (CPortal *)m_World.FindFirst(CGameWorld::ENTTYPE_PORTAL);
		for (; pPortal; pPortal = (CPortal *)pPortal->TypeNext())
		{
			if (GetTilePlotID(pPortal->GetPos(), true) == PlotID)
			{
				pPortal->DestroyLinkedPortal();
				pPortal->Reset();
			}
		}
	}
}

bool CGameContext::IsPlotEmpty(int PlotID)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (GetPlayerChar(i) && GetPlayerChar(i)->GetCurrentTilePlotID(true) == PlotID)
			return false;
	return true;
}

bool CGameContext::PlotCanBeRaided(int PlotID)
{
	return PlotID >= PLOT_START && m_aPlots[PlotID].m_DestroyEndTick > Server()->Tick() && Config()->m_SvPoliceTaserPlotRaid;
}

bool CGameContext::PlotDoorDestroyed(int PlotID)
{
	return m_aPlots[PlotID].m_DoorHealth <= 0;
}

bool CGameContext::OnPlotDoorTaser(int PlotID, int TaserStrength, int ClientID, vec2 Pos)
{
	if (PlotID < PLOT_START || !PlotCanBeRaided(PlotID) || m_aPlots[PlotID].m_DoorHealth <= 0)
		return false;

	int Diff = TaserStrength - max(TaserStrength - m_aPlots[PlotID].m_DoorHealth, 0);
	m_aPlots[PlotID].m_DoorHealth -= Diff;
	CreateDamage(Pos, ClientID, vec2(0, 0), Diff, 0, false);

	if (m_aPlots[PlotID].m_DoorHealth <= 0)
	{
		m_aPlots[PlotID].m_DoorHealth = 0;
		SendBroadcast("", ClientID, false);
		CreateDeath(Pos, ClientID);
		int AccID = GetAccIDByUsername(m_aPlots[PlotID].m_aOwner);
		if (AccID >= ACC_START)
		{
			int PlotOwner = m_Accounts[AccID].m_ClientID;
			if (PlotOwner >= 0 && m_apPlayers[PlotOwner])
			{
				SendChatTarget(PlotOwner, "The police have gained access to your plot in hopes of finding you");
			}
		}
		return true;
	}

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "Plot %d Door Health [%d/%d]", PlotID, m_aPlots[PlotID].m_DoorHealth, Config()->m_SvPlotDoorHealth);
	SendBroadcast(aBuf, ClientID);
	return false;
}

void CGameContext::SetExpireDateDays(time_t *pDate, float Days)
{
	SetExpireDate(pDate, Days*24.f, true);
}

void CGameContext::SetExpireDate(time_t *pDate, float Hours, bool SetMinutesZero)
{
	time_t Now;
	struct tm ExpireDate;
	time(&Now);
	ExpireDate = *localtime(&Now);

	// add another x days if we have the item already
	if (*pDate != 0)
	{
		struct tm AccDate;
		AccDate = *localtime(pDate);

		ExpireDate.tm_year = AccDate.tm_year;
		ExpireDate.tm_mon = AccDate.tm_mon;
		ExpireDate.tm_mday = AccDate.tm_mday;
		ExpireDate.tm_hour = AccDate.tm_hour;
	}

	const time_t ONE_HOUR = 60 * 60;
	time_t DateSeconds = mktime(&ExpireDate) + (Hours * ONE_HOUR);
	ExpireDate = *localtime(&DateSeconds);

	// we set minutes and seconds to 0 always :)
	if (SetMinutesZero)
		ExpireDate.tm_min = 0;
	ExpireDate.tm_sec = 0;
	
	*pDate = mktime(&ExpireDate);
}

bool CGameContext::IsExpired(time_t Date)
{
	if (!Date)
		return false;

	struct tm AccDate;
	AccDate = *localtime(&Date);

	time_t Now;
	struct tm ExpireDate;
	time(&Now);
	ExpireDate = *localtime(&Now);

	ExpireDate.tm_year = AccDate.tm_year;
	ExpireDate.tm_mon = AccDate.tm_mon;
	ExpireDate.tm_mday = AccDate.tm_mday;
	ExpireDate.tm_hour = AccDate.tm_hour;
	ExpireDate.tm_min = AccDate.tm_min;
	ExpireDate.tm_sec = AccDate.tm_sec;

	double Seconds = difftime(Now, mktime(&ExpireDate));
	int Minutes = Seconds / 60;
	return Minutes >= 0;
}

float CGameContext::MonthsPassedSinceRegister(int AccID)
{
	if (AccID < ACC_START)
		return 0;

	time_t Date = m_Accounts[AccID].m_RegisterDate;
	if (!Date)
	{
		// Register date unknown, registered before records started, set register date to 9th april 2021 when it got added for comparison
		Date = (time_t)1617919200;
	}

	time_t Now;
	time(&Now);
	double Seconds = difftime(Now, Date);
	int Days = Seconds / 60 / 60 / 24;
	return Days / 30.f;
}

void CGameContext::UpdateTopAccounts(int Type)
{
	// update top accounts with all currently online accs so we get correct and up-to-date information
	for (unsigned int i = ACC_START; i < m_Accounts.size(); i++)
		SetTopAccStats(i);

	switch (Type)
	{
	case TOP_LEVEL:		std::sort(m_TopAccounts.begin(), m_TopAccounts.end(), [](const TopAccounts& a, const TopAccounts& b) -> bool { return a.m_Level > b.m_Level; }); break;
	case TOP_POINTS:	std::sort(m_TopAccounts.begin(), m_TopAccounts.end(), [](const TopAccounts& a, const TopAccounts& b) -> bool { return a.m_Points > b.m_Points; }); break;
	case TOP_MONEY:		std::sort(m_TopAccounts.begin(), m_TopAccounts.end(), [](const TopAccounts& a, const TopAccounts& b) -> bool { return a.m_Money > b.m_Money; }); break;
	case TOP_SPREE:		std::sort(m_TopAccounts.begin(), m_TopAccounts.end(), [](const TopAccounts& a, const TopAccounts& b) -> bool { return a.m_KillStreak > b.m_KillStreak; }); break;
	case TOP_PORTAL:	std::sort(m_TopAccounts.begin(), m_TopAccounts.end(), [](const TopAccounts& a, const TopAccounts& b) -> bool { return a.m_Portal > b.m_Portal; }); break;
	}
}

int CGameContext::InitAccounts(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;

	if (!IsDir && str_endswith(pName, ".acc"))
	{
		char aUsername[64];
		str_copy(aUsername, pName, str_length(pName) - 3); // remove the .acc

		int ID = pSelf->GetAccount(aUsername);
		if (ID < ACC_START)
			return 0;

		// load all accounts into the top account list too
		pSelf->SetTopAccStats(ID);

		// logout account if needed
		if (pSelf->m_Accounts[ID].m_LoggedIn && pSelf->m_Accounts[ID].m_Port == pSelf->m_LogoutAccountsPort)
			pSelf->Logout(ID, true);
		else
			pSelf->FreeAccount(ID);
	}

	return 0;
}

void CGameContext::SetTopAccStats(int FromID)
{
	for (unsigned int i = 0; i < m_TopAccounts.size(); i++)
	{
		// update if we have it in already
		if (!str_comp(m_Accounts[FromID].m_Username, m_TopAccounts[i].m_aAccountName))
		{
			m_TopAccounts[i].m_Level = m_Accounts[FromID].m_Level;
			m_TopAccounts[i].m_Points = m_Accounts[FromID].m_BlockPoints;
			m_TopAccounts[i].m_Money = m_Accounts[FromID].m_Money;
			m_TopAccounts[i].m_KillStreak = m_Accounts[FromID].m_KillingSpreeRecord;
			m_TopAccounts[i].m_Portal = m_Accounts[FromID].m_PortalBattery;
			str_copy(m_TopAccounts[i].m_aUsername, m_Accounts[FromID].m_aLastPlayerName, sizeof(m_TopAccounts[i].m_aUsername));
			return;
		}
	}

	// if not existing in m_TopAccounts yet, add it
	CGameContext::TopAccounts Account;
	Account.m_Level = m_Accounts[FromID].m_Level;
	Account.m_Points = m_Accounts[FromID].m_BlockPoints;
	Account.m_Money = m_Accounts[FromID].m_Money;
	Account.m_KillStreak = m_Accounts[FromID].m_KillingSpreeRecord;
	Account.m_Portal = m_Accounts[FromID].m_PortalBattery;
	str_copy(Account.m_aUsername, m_Accounts[FromID].m_aLastPlayerName, sizeof(Account.m_aUsername));
	str_copy(Account.m_aAccountName, m_Accounts[FromID].m_Username, sizeof(Account.m_aAccountName));
	m_TopAccounts.push_back(Account);
}

int CGameContext::AddAccount()
{
	AccountInfo Account;
	Account.m_Port = Config()->m_SvPort;
	Account.m_LoggedIn = false;
	Account.m_Disabled = false;
	Account.m_Password.data[0] = 0;
	Account.m_Username[0] = '\0';
	Account.m_ClientID = -1;
	Account.m_Level = 0;
	Account.m_XP = 0;
	Account.m_Money = 0;
	Account.m_Kills = 0;
	Account.m_Deaths = 0;
	Account.m_PoliceLevel = 0;
	Account.m_SurvivalKills = 0;
	Account.m_SurvivalWins = 0;
	Account.m_aLastMoneyTransaction[0][0] = '\0';
	Account.m_aLastMoneyTransaction[1][0] = '\0';
	Account.m_aLastMoneyTransaction[2][0] = '\0';
	Account.m_aLastMoneyTransaction[3][0] = '\0';
	Account.m_aLastMoneyTransaction[4][0] = '\0';
	Account.m_SpookyGhost = false;
	Account.m_VIP = 0;
	Account.m_BlockPoints = 0;
	Account.m_InstagibKills = 0;
	Account.m_InstagibWins = 0;
	Account.m_SpawnWeapon[0] = 0;
	Account.m_SpawnWeapon[1] = 0;
	Account.m_SpawnWeapon[2] = 0;
	Account.m_Ninjajetpack = false;
	Account.m_aLastPlayerName[0] = '\0';
	Account.m_SurvivalDeaths = 0;
	Account.m_InstagibDeaths = 0;
	Account.m_TaserLevel = 0;
	Account.m_KillingSpreeRecord = 0;
	Account.m_Euros = 0;
	Account.m_ExpireDateVIP = 0;
	Account.m_PortalRifle = 0;
	Account.m_ExpireDatePortalRifle = 0;
	Account.m_Version = ACC_CURRENT_VERSION;
	Account.m_Addr.type = -1;
	Account.m_LastAddr.type = -1;
	Account.m_TaserBattery = 0;
	Account.m_aContact[0] = '\0';
	Account.m_aTimeoutCode[0] = '\0';
	Account.m_aSecurityPin[0] = '\0';
	Account.m_RegisterDate = 0;
	Account.m_LastLoginDate = 0;
	Account.m_Flags = 0;
	Account.m_aEmail[0] = '\0';
	Account.m_aDesign[0] = '\0';
	Account.m_PortalBattery = 0;
	Account.m_PortalBlocker = 0;
	Account.m_VoteMenuFlags = 0;

	m_Accounts.push_back(Account);
	return m_Accounts.size()-1;
}

void CGameContext::ReadAccountStats(int ID, const char *pName)
{
	std::string data;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s/%s.acc", Config()->m_SvAccFilePath, pName);
	std::fstream AccFile(aBuf);

	for (int i = 0; i < NUM_ACCOUNT_VARIABLES; i++)
	{
		getline(AccFile, data);
		const char *pData = data.c_str();
		SetAccVar(ID, i, pData);
	}
}

void CGameContext::WriteAccountStats(int ID)
{
	std::string data;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s/%s.acc", Config()->m_SvAccFilePath, m_Accounts[ID].m_Username);
	std::ofstream AccFile(aBuf);

	if (AccFile.is_open())
	{
		for (int i = 0; i < NUM_ACCOUNT_VARIABLES; i++)
		{
			AccFile << GetAccVarValue(ID, i) << "\n";
		}
		dbg_msg("acc", "saved acc '%s'", m_Accounts[ID].m_Username);
	}
	AccFile.close();
}

void CGameContext::SetAccVar(int ID, int VariableID, const char *pData)
{
	switch (VariableID)
	{
	case ACC_PORT:						m_Accounts[ID].m_Port = atoi(pData); break;
	case ACC_LOGGED_IN:					m_Accounts[ID].m_LoggedIn = atoi(pData); break;
	case ACC_DISABLED:					m_Accounts[ID].m_Disabled = atoi(pData); break;
	case ACC_PASSWORD:					sha256_from_str(&m_Accounts[ID].m_Password, pData); break;
	case ACC_USERNAME:					str_copy(m_Accounts[ID].m_Username, pData, sizeof(m_Accounts[ID].m_Username)); break;
	case ACC_CLIENT_ID:					m_Accounts[ID].m_ClientID = atoi(pData); break;
	case ACC_LEVEL:						m_Accounts[ID].m_Level = atoi(pData); break;
	case ACC_XP:						m_Accounts[ID].m_XP = atoll(pData); break;
	case ACC_MONEY:						m_Accounts[ID].m_Money = atoll(pData); break;
	case ACC_KILLS:						m_Accounts[ID].m_Kills = atoi(pData); break;
	case ACC_DEATHS:					m_Accounts[ID].m_Deaths = atoi(pData); break;
	case ACC_POLICE_LEVEL:				m_Accounts[ID].m_PoliceLevel = atoi(pData); break;
	case ACC_SURVIVAL_KILLS:			m_Accounts[ID].m_SurvivalKills = atoi(pData); break;
	case ACC_SURVIVAL_WINS:				m_Accounts[ID].m_SurvivalWins = atoi(pData); break;
	case ACC_SPOOKY_GHOST:				m_Accounts[ID].m_SpookyGhost = atoi(pData); break;
	case ACC_LAST_MONEY_TRANSACTION_0:	str_copy(m_Accounts[ID].m_aLastMoneyTransaction[0], pData, sizeof(m_Accounts[ID].m_aLastMoneyTransaction[0])); break;
	case ACC_LAST_MONEY_TRANSACTION_1:	str_copy(m_Accounts[ID].m_aLastMoneyTransaction[1], pData, sizeof(m_Accounts[ID].m_aLastMoneyTransaction[1])); break;
	case ACC_LAST_MONEY_TRANSACTION_2:	str_copy(m_Accounts[ID].m_aLastMoneyTransaction[2], pData, sizeof(m_Accounts[ID].m_aLastMoneyTransaction[2])); break;
	case ACC_LAST_MONEY_TRANSACTION_3:	str_copy(m_Accounts[ID].m_aLastMoneyTransaction[3], pData, sizeof(m_Accounts[ID].m_aLastMoneyTransaction[3])); break;
	case ACC_LAST_MONEY_TRANSACTION_4:	str_copy(m_Accounts[ID].m_aLastMoneyTransaction[4], pData, sizeof(m_Accounts[ID].m_aLastMoneyTransaction[4])); break;
	case ACC_VIP:						m_Accounts[ID].m_VIP = atoi(pData); break;
	case ACC_BLOCK_POINTS:				m_Accounts[ID].m_BlockPoints = atoi(pData); break;
	case ACC_INSTAGIB_KILLS:			m_Accounts[ID].m_InstagibKills = atoi(pData); break;
	case ACC_INSTAGIB_WINS:				m_Accounts[ID].m_InstagibWins = atoi(pData); break;
	case ACC_SPAWN_WEAPON_0:			m_Accounts[ID].m_SpawnWeapon[0] = atoi(pData); break;
	case ACC_SPAWN_WEAPON_1:			m_Accounts[ID].m_SpawnWeapon[1] = atoi(pData); break;
	case ACC_SPAWN_WEAPON_2:			m_Accounts[ID].m_SpawnWeapon[2] = atoi(pData); break;
	case ACC_NINJAJETPACK:				m_Accounts[ID].m_Ninjajetpack = atoi(pData); break;
	case ACC_LAST_PLAYER_NAME:			str_copy(m_Accounts[ID].m_aLastPlayerName, pData, sizeof(m_Accounts[ID].m_aLastPlayerName)); break;
	case ACC_SURVIVAL_DEATHS:			m_Accounts[ID].m_SurvivalDeaths = atoi(pData); break;
	case ACC_INSTAGIB_DEATHS:			m_Accounts[ID].m_InstagibDeaths = atoi(pData); break;
	case ACC_TASER_LEVEL:				m_Accounts[ID].m_TaserLevel = atoi(pData); break;
	case ACC_KILLING_SPREE_RECORD:		m_Accounts[ID].m_KillingSpreeRecord = atoi(pData); break;
	case ACC_EUROS:						m_Accounts[ID].m_Euros = atof(pData); break;
	case ACC_EXPIRE_DATE_VIP:			m_Accounts[ID].m_ExpireDateVIP = atoll(pData); break;
	case ACC_PORTAL_RIFLE:				m_Accounts[ID].m_PortalRifle = atoi(pData); break;
	case ACC_EXPIRE_DATE_PORTAL_RIFLE:	m_Accounts[ID].m_ExpireDatePortalRifle = atoll(pData); break;
	case ACC_VERSION:					m_Accounts[ID].m_Version = atoi(pData); break;
	case ACC_ADDR:						net_addr_from_str(&m_Accounts[ID].m_Addr, pData); break;
	case ACC_LAST_ADDR:					net_addr_from_str(&m_Accounts[ID].m_LastAddr, pData); break;
	case ACC_TASER_BATTERY:				m_Accounts[ID].m_TaserBattery = atoi(pData); break;
	case ACC_CONTACT:					str_copy(m_Accounts[ID].m_aContact, pData, sizeof(m_Accounts[ID].m_aContact)); break;
	case ACC_TIMEOUT_CODE:				str_copy(m_Accounts[ID].m_aTimeoutCode, pData, sizeof(m_Accounts[ID].m_aTimeoutCode)); break;
	case ACC_SECURITY_PIN:				str_copy(m_Accounts[ID].m_aSecurityPin, pData, sizeof(m_Accounts[ID].m_aSecurityPin)); break;
	case ACC_REGISTER_DATE:				m_Accounts[ID].m_RegisterDate = atoll(pData); break;
	case ACC_LAST_LOGIN_DATE:			m_Accounts[ID].m_LastLoginDate = atoll(pData); break;
	case ACC_FLAGS:						m_Accounts[ID].m_Flags = atoi(pData); break;
	case ACC_EMAIL:						str_copy(m_Accounts[ID].m_aEmail, pData, sizeof(m_Accounts[ID].m_aEmail)); break;
	case ACC_DESIGN:					str_copy(m_Accounts[ID].m_aDesign, pData, sizeof(m_Accounts[ID].m_aDesign)); break;
	case ACC_PORTAL_BATTERY:			m_Accounts[ID].m_PortalBattery = atoi(pData); break;
	case ACC_PORTAL_BLOCKER:			m_Accounts[ID].m_PortalBlocker = atoi(pData); break;
	case ACC_VOTE_MENU_FLAGS:			m_Accounts[ID].m_VoteMenuFlags = atoi(pData); break;
	}
}

const char *CGameContext::GetAccVarName(int VariableID)
{
	switch (VariableID)
	{
	case ACC_PORT:						return "port";
	case ACC_LOGGED_IN:					return "logged_in";
	case ACC_DISABLED:					return "disabled";
	case ACC_PASSWORD:					return "password";
	case ACC_USERNAME:					return "username";
	case ACC_CLIENT_ID:					return "client_id";
	case ACC_LEVEL:						return "level";
	case ACC_XP:						return "xp";
	case ACC_MONEY:						return "money";
	case ACC_KILLS:						return "kills";
	case ACC_DEATHS:					return "deaths";
	case ACC_POLICE_LEVEL:				return "police_level";
	case ACC_SURVIVAL_KILLS:			return "survival_kills";
	case ACC_SURVIVAL_WINS:				return "survival_wins";
	case ACC_SPOOKY_GHOST:				return "spooky_ghost";
	case ACC_LAST_MONEY_TRANSACTION_0:	return "last_money_transaction_0";
	case ACC_LAST_MONEY_TRANSACTION_1:	return "last_money_transaction_1";
	case ACC_LAST_MONEY_TRANSACTION_2:	return "last_money_transaction_2";
	case ACC_LAST_MONEY_TRANSACTION_3:	return "last_money_transaction_3";
	case ACC_LAST_MONEY_TRANSACTION_4:	return "last_money_transaction_4";
	case ACC_VIP:						return "vip";
	case ACC_BLOCK_POINTS:				return "block_points";
	case ACC_INSTAGIB_KILLS:			return "instagib_kills";
	case ACC_INSTAGIB_WINS:				return "instagib_wins";
	case ACC_SPAWN_WEAPON_0:			return "spawn_weapon_shotgun";
	case ACC_SPAWN_WEAPON_1:			return "spawn_weapon_grenade";
	case ACC_SPAWN_WEAPON_2:			return "spawn_weapon_rifle";
	case ACC_NINJAJETPACK:				return "ninjajetpack";
	case ACC_LAST_PLAYER_NAME:			return "last_player_name";
	case ACC_SURVIVAL_DEATHS:			return "survival_deaths";
	case ACC_INSTAGIB_DEATHS:			return "instagib_deaths";
	case ACC_TASER_LEVEL:				return "taser_level";
	case ACC_KILLING_SPREE_RECORD:		return "killing_spree_record";
	case ACC_EUROS:						return "euros";
	case ACC_EXPIRE_DATE_VIP:			return "expire_date_vip";
	case ACC_PORTAL_RIFLE:				return "portal_rifle";
	case ACC_EXPIRE_DATE_PORTAL_RIFLE:	return "expire_date_portal_rifle";
	case ACC_VERSION:					return "version";
	case ACC_ADDR:						return "addr";
	case ACC_LAST_ADDR:					return "last_addr";
	case ACC_TASER_BATTERY:				return "taser_battery";
	case ACC_CONTACT:					return "contact";
	case ACC_TIMEOUT_CODE:				return "timeout_code";
	case ACC_SECURITY_PIN:				return "security_pin";
	case ACC_REGISTER_DATE:				return "register_date";
	case ACC_LAST_LOGIN_DATE:			return "last_login_date";
	case ACC_FLAGS:						return "flags";
	case ACC_EMAIL:						return "email";
	case ACC_DESIGN:					return "design";
	case ACC_PORTAL_BATTERY:			return "portal_battery";
	case ACC_PORTAL_BLOCKER:			return "portal_blocker";
	case ACC_VOTE_MENU_FLAGS:			return "vote_menu_flags";
	}
	return "Unknown";
}

const char *CGameContext::GetAccVarValue(int ID, int VariableID)
{
	static char aBuf[128];
	str_copy(aBuf, "Unknown", sizeof(aBuf));

	switch (VariableID)
	{
	case ACC_PORT:						str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_Port); break;
	case ACC_LOGGED_IN:					str_format(aBuf, sizeof(aBuf), "%d", (int)m_Accounts[ID].m_LoggedIn); break;
	case ACC_DISABLED:					str_format(aBuf, sizeof(aBuf), "%d", (int)m_Accounts[ID].m_Disabled); break;
	case ACC_PASSWORD:					sha256_str(m_Accounts[ID].m_Password, aBuf, sizeof(aBuf)); break;
	case ACC_USERNAME:					str_copy(aBuf, m_Accounts[ID].m_Username, sizeof(aBuf)); break;
	case ACC_CLIENT_ID:					str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_ClientID); break;
	case ACC_LEVEL:						str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_Level); break;
	case ACC_XP:						str_format(aBuf, sizeof(aBuf), "%lld", m_Accounts[ID].m_XP); break;
	case ACC_MONEY:						str_format(aBuf, sizeof(aBuf), "%lld", m_Accounts[ID].m_Money); break;
	case ACC_KILLS:						str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_Kills); break;
	case ACC_DEATHS:					str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_Deaths); break;
	case ACC_POLICE_LEVEL:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_PoliceLevel); break;
	case ACC_SURVIVAL_KILLS:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_SurvivalKills); break;
	case ACC_SURVIVAL_WINS:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_SurvivalWins); break;
	case ACC_SPOOKY_GHOST:				str_format(aBuf, sizeof(aBuf), "%d", (int)m_Accounts[ID].m_SpookyGhost); break;
	case ACC_LAST_MONEY_TRANSACTION_0:	str_copy(aBuf, m_Accounts[ID].m_aLastMoneyTransaction[0], sizeof(aBuf)); break;
	case ACC_LAST_MONEY_TRANSACTION_1:	str_copy(aBuf, m_Accounts[ID].m_aLastMoneyTransaction[1], sizeof(aBuf)); break;
	case ACC_LAST_MONEY_TRANSACTION_2:	str_copy(aBuf, m_Accounts[ID].m_aLastMoneyTransaction[2], sizeof(aBuf)); break;
	case ACC_LAST_MONEY_TRANSACTION_3:	str_copy(aBuf, m_Accounts[ID].m_aLastMoneyTransaction[3], sizeof(aBuf)); break;
	case ACC_LAST_MONEY_TRANSACTION_4:	str_copy(aBuf, m_Accounts[ID].m_aLastMoneyTransaction[4], sizeof(aBuf)); break;
	case ACC_VIP:						str_format(aBuf, sizeof(aBuf), "%d", (int)m_Accounts[ID].m_VIP); break;
	case ACC_BLOCK_POINTS:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_BlockPoints); break;
	case ACC_INSTAGIB_KILLS:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_InstagibKills); break;
	case ACC_INSTAGIB_WINS:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_InstagibWins); break;
	case ACC_SPAWN_WEAPON_0:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_SpawnWeapon[0]); break;
	case ACC_SPAWN_WEAPON_1:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_SpawnWeapon[1]); break;
	case ACC_SPAWN_WEAPON_2:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_SpawnWeapon[2]); break;
	case ACC_NINJAJETPACK:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_Ninjajetpack); break;
	case ACC_LAST_PLAYER_NAME:			str_copy(aBuf, m_Accounts[ID].m_aLastPlayerName, sizeof(aBuf)); break;
	case ACC_SURVIVAL_DEATHS:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_SurvivalDeaths); break;
	case ACC_INSTAGIB_DEATHS:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_InstagibDeaths); break;
	case ACC_TASER_LEVEL:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_TaserLevel); break;
	case ACC_KILLING_SPREE_RECORD:		str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_KillingSpreeRecord); break;
	case ACC_EUROS:						str_format(aBuf, sizeof(aBuf), "%.2f", m_Accounts[ID].m_Euros); break;
	case ACC_EXPIRE_DATE_VIP:			str_format(aBuf, sizeof(aBuf), "%lld", (int64)m_Accounts[ID].m_ExpireDateVIP); break;
	case ACC_PORTAL_RIFLE:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_PortalRifle); break;
	case ACC_EXPIRE_DATE_PORTAL_RIFLE:	str_format(aBuf, sizeof(aBuf), "%lld", (int64)m_Accounts[ID].m_ExpireDatePortalRifle); break;
	case ACC_VERSION:					str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_Version); break;
	case ACC_ADDR:						net_addr_str(&m_Accounts[ID].m_Addr, aBuf, sizeof(aBuf), true); break;
	case ACC_LAST_ADDR:					net_addr_str(&m_Accounts[ID].m_LastAddr, aBuf, sizeof(aBuf), true); break;
	case ACC_TASER_BATTERY:				str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_TaserBattery); break;
	case ACC_CONTACT:					str_copy(aBuf, m_Accounts[ID].m_aContact, sizeof(aBuf)); break;
	case ACC_TIMEOUT_CODE:				str_copy(aBuf, m_Accounts[ID].m_aTimeoutCode, sizeof(aBuf)); break;
	case ACC_SECURITY_PIN:				str_copy(aBuf, m_Accounts[ID].m_aSecurityPin, sizeof(aBuf)); break;
	case ACC_REGISTER_DATE:				str_format(aBuf, sizeof(aBuf), "%lld", (int64)m_Accounts[ID].m_RegisterDate); break;
	case ACC_LAST_LOGIN_DATE:			str_format(aBuf, sizeof(aBuf), "%lld", (int64)m_Accounts[ID].m_LastLoginDate); break;
	case ACC_FLAGS:						str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_Flags); break;
	case ACC_EMAIL:						str_copy(aBuf, m_Accounts[ID].m_aEmail, sizeof(aBuf)); break;
	case ACC_DESIGN:					str_copy(aBuf, m_Accounts[ID].m_aDesign, sizeof(aBuf)); break;
	case ACC_PORTAL_BATTERY:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_PortalBattery); break;
	case ACC_PORTAL_BLOCKER:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_PortalBlocker); break;
	case ACC_VOTE_MENU_FLAGS:			str_format(aBuf, sizeof(aBuf), "%d", m_Accounts[ID].m_VoteMenuFlags); break;
	}
	return aBuf;
}

void CGameContext::Logout(int ID, bool Silent)
{
	if (ID < ACC_START)
		return;

	if (m_Accounts[ID].m_ClientID >= 0 && m_apPlayers[m_Accounts[ID].m_ClientID])
		m_apPlayers[m_Accounts[ID].m_ClientID]->OnLogout();

	m_Accounts[ID].m_LoggedIn = false;
	m_Accounts[ID].m_ClientID = -1;
	WriteAccountStats(ID);

	if (!Silent)
		dbg_msg("acc", "logged out account '%s'", m_Accounts[ID].m_Username);
	FreeAccount(ID);
}

void CGameContext::LogoutAllAccounts()
{
	unsigned int Amount = m_Accounts.size();
	for (unsigned int i = ACC_START; i < Amount; i++)
		Logout(ACC_START);
	dbg_msg("acc", "logged out all accounts");
}

bool CGameContext::Login(int ClientID, const char *pUsername, const char *pPassword, bool PasswordRequired, bool ForceDesignLoad)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (!pPlayer)
		return false;

	if (IsAccountSystemBanned(ClientID))
		return false;

	if (pPlayer->GetAccID() >= ACC_START)
	{
		SendChatTarget(ClientID, "You are already logged in");
		return false;
	}

	int ID = AddAccount();
	ReadAccountStats(ID, pUsername);

	if (m_Accounts[ID].m_Username[0] == '\0')
	{
		SendChatTarget(ClientID, "That account doesn't exist, please register first");
		FreeAccount(ID);
		return false;
	}

	if (m_Accounts[ID].m_LoggedIn)
	{
		if (m_Accounts[ID].m_Port == Config()->m_SvPort)
			SendChatTarget(ClientID, "This account is already logged in");
		else
			SendChatTarget(ClientID, "This account is already logged in on another server");
		FreeAccount(ID);
		return false;
	}

	if (m_Accounts[ID].m_Disabled)
	{
		SendChatTarget(ClientID, "This account is disabled");
		FreeAccount(ID);
		return false;
	}

	if (PasswordRequired && pPassword[0] && m_Accounts[ID].m_Version < 7)
	{
		char aPassword[SHA256_MAXSTRSIZE];
		str_copy(aPassword, pPassword, sizeof(aPassword));
		SHA256_CTX Sha256Ctx;
		sha256_init(&Sha256Ctx);
		sha256_update(&Sha256Ctx, &aPassword, sizeof(aPassword));
		SHA256_DIGEST Sha256 = sha256_finish(&Sha256Ctx);
		if (sha256_comp(m_Accounts[ID].m_Password, Sha256) == 0)
			SetPassword(ID, pPassword);
	}

	if (PasswordRequired && CheckPassword(ID, pPassword))
	{
		SendChatTarget(ClientID, "Wrong password");
		FreeAccount(ID);

		ProcessAccountSystemBan(ClientID, ACC_SYS_LOGIN);
		return false;
	}

	// set some variables and save the account with some new values
	{
		m_Accounts[ID].m_Port = Config()->m_SvPort;
		m_Accounts[ID].m_LoggedIn = true;
		m_Accounts[ID].m_ClientID = ClientID;
		m_Accounts[ID].m_Version = ACC_CURRENT_VERSION;
		str_copy(m_Accounts[ID].m_aLastPlayerName, Server()->ClientName(ClientID), sizeof(m_Accounts[ID].m_aLastPlayerName));
		if (pPlayer->m_TimeoutCode[0] != '\0')
			str_copy(m_Accounts[ID].m_aTimeoutCode, pPlayer->m_TimeoutCode, sizeof(m_Accounts[ID].m_aTimeoutCode));
		time_t Now;
		time(&Now);
		m_Accounts[ID].m_LastLoginDate = Now;

		NETADDR Addr;
		Server()->GetClientAddr(ClientID, &Addr);
		if (net_addr_comp(&Addr, &m_Accounts[ID].m_Addr, false) != 0)
		{
			// addresses are not equal, update last address and set new current address
			m_Accounts[ID].m_LastAddr = m_Accounts[ID].m_Addr;
			Server()->GetClientAddr(ClientID, &m_Accounts[ID].m_Addr);
		}
		else
		{
			// addresses are equal, just update the current address to get the possible changed port
			Server()->GetClientAddr(ClientID, &m_Accounts[ID].m_Addr);
		}

		WriteAccountStats(ID);
	}

	pPlayer->OnLogin(ForceDesignLoad);
	return true;
}

SHA256_DIGEST CGameContext::HashPassword(const char *pPassword)
{
	SHA256_CTX Sha256Ctx;
	sha256_init(&Sha256Ctx);
	sha256_update(&Sha256Ctx, pPassword, str_length(pPassword));
	return sha256_finish(&Sha256Ctx);
}

void CGameContext::SetPassword(int ID, const char *pPassword)
{
	if (ID < ACC_START)
		return;
	m_Accounts[ID].m_Password = HashPassword(pPassword);
}

bool CGameContext::CheckPassword(int ID, const char *pPassword)
{
	if (ID < ACC_START)
		return true;
	return sha256_comp(m_Accounts[ID].m_Password, HashPassword(pPassword)) != 0;
}

int64 CGameContext::GetNeededXP(int Level)
{
	if (Level < 0)
		return 0;
	if (Level < DIFFERENCE_XP_END)
		return m_aNeededXP[Level];
	return m_aNeededXP[DIFFERENCE_XP_END-1] + (OVER_LVL_100_XP * (Level+1 - DIFFERENCE_XP_END));
}

int CGameContext::GetAccIDByUsername(const char *pUsername)
{
	for (unsigned int i = ACC_START; i < m_Accounts.size(); i++)
		if (!str_comp(pUsername, m_Accounts[i].m_Username))
			return i;
	return 0;
}

int CGameContext::GetAccount(const char *pUsername)
{
	int ID = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (m_apPlayers[i] && m_apPlayers[i]->GetAccID() >= ACC_START && !str_comp(m_Accounts[m_apPlayers[i]->GetAccID()].m_Username, pUsername))
		{
			ID = m_apPlayers[i]->GetAccID();
			break;
		}
	}

	if (ID < ACC_START)
	{
		ID = AddAccount();
		ReadAccountStats(ID, pUsername);
	}

	if (m_Accounts[ID].m_Username[0] == '\0')
	{
		FreeAccount(ID);
		return 0;
	}

	return ID;
}

void CGameContext::FreeAccount(int ID)
{
	m_Accounts.erase(m_Accounts.begin() + ID);
}

bool CGameContext::IsAccLoggedInThisPort(int ID)
{
	return m_Accounts[ID].m_LoggedIn && m_Accounts[ID].m_Port == Config()->m_SvPort;
}

void CGameContext::UpdateDesignList(int ID, const char *pMapDesign)
{
	std::vector<SSavedDesignEntry> vDesigns = GetDesignList(ID);

	// Update the list
	bool Found = false;
	for (unsigned int i = 0; i < vDesigns.size(); i++)
	{
		if (str_comp(vDesigns[i].m_aMapName, Server()->GetCurrentMapName()) == 0)
		{
			// Update
			str_copy(vDesigns[i].m_aDesign, pMapDesign, sizeof(vDesigns[i].m_aDesign));
			Found = true;
			break;
		}
	}

	// Add if not found
	if (!Found)
	{
		SSavedDesignEntry Entry;
		str_copy(Entry.m_aDesign, pMapDesign, sizeof(Entry.m_aDesign));
		str_copy(Entry.m_aMapName, Server()->GetCurrentMapName(), sizeof(Entry.m_aDesign));
		vDesigns.push_back(Entry);
	}

	// Write list
	m_Accounts[ID].m_aDesign[0] = '\0';
	for (unsigned int i = 0; i < vDesigns.size(); i++)
	{
		// don't add default's to the list, waste
		if (str_comp(vDesigns[i].m_aDesign, "default") == 0)
			continue;

		char aEntry[196];
		str_format(aEntry, sizeof(aEntry), "%s:%s,", vDesigns[i].m_aMapName, vDesigns[i].m_aDesign);
		str_append(m_Accounts[ID].m_aDesign, aEntry, sizeof(m_Accounts[ID].m_aDesign));
	}
}

const char *CGameContext::GetCurrentDesignFromList(int ID)
{
	static char aBuf[128];
	str_copy(aBuf, "default", sizeof(aBuf));

	std::vector<SSavedDesignEntry> vDesigns = GetDesignList(ID);
	for (unsigned int i = 0; i < vDesigns.size(); i++)
	{
		if (str_comp(vDesigns[i].m_aMapName, Server()->GetCurrentMapName()) == 0)
		{
			str_copy(aBuf, vDesigns[i].m_aDesign, sizeof(aBuf));
			break;
		}
	}
	return aBuf;
}

std::vector<CGameContext::SSavedDesignEntry> CGameContext::GetDesignList(int ID)
{
	std::vector<SSavedDesignEntry> vDesigns;
	if (ID < ACC_START)
		return vDesigns;

	const char *pList = m_Accounts[ID].m_aDesign;
	while (1)
	{
		if (!pList || !pList[0])
			break;

		SSavedDesignEntry Entry;
		Entry.m_aMapName[0] = '\0';
		Entry.m_aDesign[0] = '\0';
		sscanf(pList, "%[^:]:%[^,]", Entry.m_aMapName, Entry.m_aDesign);
		if (Entry.m_aMapName[0] && Entry.m_aDesign[0])
		{
			vDesigns.push_back(Entry);
		}

		// jump to next comma, if it exists skip it so we can start the next loop run with the next data
		if ((pList = str_find(pList, ",")))
			pList++;
	}

	return vDesigns;
}

const char *CGameContext::GetDate(time_t Time, bool ShowTime)
{
	if (Time < 0)
		return "";

	time_t tmp = Time;
	struct tm Date = *localtime(&tmp);

	static char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%02d.%02d.%d", Date.tm_mday, Date.tm_mon+1, Date.tm_year+1900);

	if (ShowTime)
	{
		char aTime[64];
		str_format(aTime, sizeof(aTime), " (%02d:%02d)", Date.tm_hour, Date.tm_min);
		str_append(aBuf, aTime, sizeof(aBuf));
	}

	return aBuf;
}

void CGameContext::WriteDonationFile(int Type, float Amount, int ID, const char *pDescription)
{
	const char* pFrom = Type == TYPE_DONATION ? "donation" : Type == TYPE_PURCHASE ? "purchase" : "";
	char aBuf[256], aMsg[256];
	time_t Now = time(0);
	str_format(aMsg, sizeof(aMsg), "Date: %s, Euros: %.2f, Account: '%s', Description: '%s'", GetDate(Now, false), Amount, m_Accounts[ID].m_Username, pDescription);
	Console()->Format(aBuf, sizeof(aBuf), pFrom, aMsg);

	char aFile[256];
	str_format(aFile, sizeof(aFile), "%s/donations.txt", Config()->m_SvDonationFilePath);
	std::ofstream DonationsFile(aFile, std::ios_base::app | std::ios_base::out);
	DonationsFile << aBuf << "\n";
}

void CGameContext::ReadMoneyListFile()
{
	std::string data;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s/%s/moneydrops.txt", Config()->m_SvMoneyDropsFilePath, Server()->GetCurrentMapName());
	std::fstream MoneyDropsFile(aBuf);
	getline(MoneyDropsFile, data);
	const char *pStr = data.c_str();

	while (1)
	{
		if (!pStr)
			break;

		vec2 Pos = vec2(-1, -1);
		int Amount = 0;

		sscanf(pStr, "%f/%f:%d", &Pos.x, &Pos.y, &Amount);
		if (Amount > 0)
			new CMoney(&m_World, vec2(Pos.x*32.f, Pos.y*32.f), Amount);

		// jump to next comma, if it exists skip it so we can start the next loop run with the next money data
		if ((pStr = str_find(pStr, ",")))
			pStr++;
	}
}

void CGameContext::WriteMoneyListFile()
{
	char aFile[256];
	str_format(aFile, sizeof(aFile), "%s/%s/moneydrops.txt", Config()->m_SvMoneyDropsFilePath, Server()->GetCurrentMapName());
	std::ofstream MoneyDropsFile(aFile);

	CMoney *pMoney = (CMoney *)m_World.FindFirst(CGameWorld::ENTTYPE_MONEY);
	for (; pMoney; pMoney = (CMoney *)pMoney->TypeNext())
	{
		char aEntry[64];
		str_format(aEntry, sizeof(aEntry), "%.2f/%.2f:%d,", pMoney->GetPos().x/32.f, pMoney->GetPos().y/32.f, pMoney->GetAmount());
		MoneyDropsFile << aEntry;
	}
}

void CGameContext::ReadSavedPlayersFile()
{
	m_vSavedIdentities.clear();
	m_vSavedIdentitiesFiles.clear();

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "dumps/%s/%s", Config()->m_SvSavedTeesFilePath, Server()->GetCurrentMapName());
	Storage()->ListDirectory(IStorage::TYPE_ALL, aPath, LoadSavedPlayersCallback, this);

	for (unsigned int i = 0; i < m_vSavedIdentitiesFiles.size(); i++)
	{
		str_format(aPath, sizeof(aPath), "dumps/%s/%s/%s.save", Config()->m_SvSavedTeesFilePath, Server()->GetCurrentMapName(), m_vSavedIdentitiesFiles[i].c_str());
		CSaveTee SaveTee;
		if (SaveTee.LoadFile(aPath, 0, this) && SaveTee.HasSavedIdentity())
		{
			if (IsExpired(SaveTee.GetIdentity().m_ExpireDate))
			{
				RemoveSavedIdentityFile(SaveTee.GetIdentity());
				continue;
			}

			m_vSavedIdentities.push_back(SaveTee.GetIdentity());
		}
	}

	// Clear
	m_vSavedIdentitiesFiles.clear();

	// Check for redirect tile saves
	str_format(aPath, sizeof(aPath), "dumps/%s/x_redirect_tile", Config()->m_SvSavedTeesFilePath);
	Storage()->ListDirectory(IStorage::TYPE_ALL, aPath, LoadSavedPlayersCallback, this);

	for (unsigned int i = 0; i < m_vSavedIdentitiesFiles.size(); i++)
	{
		str_format(aPath, sizeof(aPath), "dumps/%s/x_redirect_tile/%s.save", Config()->m_SvSavedTeesFilePath, m_vSavedIdentitiesFiles[i].c_str());
		CSaveTee SaveTee;
		if (SaveTee.LoadFile(aPath, 0, this) && SaveTee.HasSavedIdentity())
		{
			if (IsExpired(SaveTee.GetIdentity().m_ExpireDate))
			{
				RemoveSavedIdentityFile(SaveTee.GetIdentity());
				continue;
			}

			if (SaveTee.GetIdentity().m_RedirectTilePort == Config()->m_SvPort)
			{
				m_vSavedIdentities.push_back(SaveTee.GetIdentity());
			}
		}
	}

	// Clear
	m_vSavedIdentitiesFiles.clear();
}

int CGameContext::LoadSavedPlayersCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	if (!IsDir && str_endswith(pName, ".save"))
	{
		std::string Name = pName;
		pSelf->m_vSavedIdentitiesFiles.push_back(Name.erase(Name.size()-5)); // remove .save
	}
	return 0;
}

void CGameContext::ExpireSavedIdentities()
{
	for (int i = 0; i < (int)m_vSavedIdentities.size(); i++)
	{
		if (IsExpired(m_vSavedIdentities[i].m_ExpireDate))
		{
			RemoveSavedIdentityFile(m_vSavedIdentities[i]);
			m_vSavedIdentities.erase(m_vSavedIdentities.begin() + i);
			i--;
		}
	}
}

void CGameContext::RemoveSavedIdentityFile(SSavedIdentity SavedIdentity)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if (SavedIdentity.m_RedirectTilePort)
		str_format(aPath, sizeof(aPath), "dumps/%s/x_redirect_tile/%s.save", Config()->m_SvSavedTeesFilePath, GetSavedIdentityHash(SavedIdentity));
	else
		str_format(aPath, sizeof(aPath), "dumps/%s/%s/%s.save", Config()->m_SvSavedTeesFilePath, Server()->GetCurrentMapName(), GetSavedIdentityHash(SavedIdentity));
	dbg_msg("save", "%s: removing saved identity due to expiration", SavedIdentity.m_aName);
	Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
}

void CGameContext::SaveDrop(int ClientID, float Hours, const char *pReason)
{
	if (!GetPlayerChar(ClientID) || m_apPlayers[ClientID]->m_IsDummy)
		return;

	// Save character
	SaveCharacter(ClientID, SAVE_WALLET, Hours);

	// Drop the client
	((CServer *)Server())->m_NetServer.Drop(ClientID, pReason);
}

int CGameContext::SaveCharacter(int ClientID, int Flags, float Hours)
{
	CCharacter *pChr = GetPlayerChar(ClientID);
	if (!pChr || pChr->GetPlayer()->m_IsDummy)
		return -1;

	// Pretend we leave the minigame, so that the shutdown save saves our main tee, not the minigame :D
	// We cant use SetMinigame(MINIGAME_NONE) here because that would kill the character, ending in a crash
	if (pChr->GetPlayer()->IsMinigame())
		pChr->GetPlayer()->m_MinigameTee.Load(pChr, 0);

	// if character got saved and during restart the plot expires, it would be not good if the tee keeps his editor
	pChr->GetPlayer()->StopPlotEditing();
	pChr->UnsetSpookyGhost();

	// Pretend we leave no bonus area so we can save the real values, and later override it by calling this function again
	if (pChr->m_NoBonusContext.m_InArea)
	{
		pChr->OnNoBonusArea(false);
		// Pretend we left the no bonus area on redirect. If wanted, the other map should add a tile to enable it again, same like after jail release, its not set.
		if (!(Flags & SAVE_REDIRECT))
		{
			pChr->m_NoBonusContext.m_InArea = true;
		}
	}

	if (Flags & SAVE_REDIRECT)
	{
		// reset solo so it cant be taken to another map
		pChr->SetSolo(false);
	}

	// save identity to cache
	SSavedIdentity Info;
	Server()->GetClientAddr(ClientID, &Info.m_Addr);
	str_copy(Info.m_aName, Server()->ClientName(ClientID), sizeof(Info.m_aName));
	str_copy(Info.m_aAccUsername, m_Accounts[m_apPlayers[ClientID]->GetAccID()].m_Username, sizeof(Info.m_aAccUsername));
	Info.m_TeeInfo = m_apPlayers[ClientID]->m_TeeInfos;
	str_copy(Info.m_aTimeoutCode, m_apPlayers[ClientID]->m_TimeoutCode, sizeof(Info.m_aTimeoutCode));
	Info.m_ExpireDate = 0;
	if (Hours != -1)
		SetExpireDate(&Info.m_ExpireDate, Hours);
	Info.m_RedirectTilePort = pChr->m_RedirectTilePort;
	m_vSavedIdentities.push_back(Info);

	// create file and save the character
	char aFilename[IO_MAX_PATH_LENGTH];
	if (!(Flags & SAVE_REDIRECT))
	{
		str_format(aFilename, sizeof(aFilename), "dumps/%s/%s/%s.save", Config()->m_SvSavedTeesFilePath, Server()->GetCurrentMapName(), GetSavedIdentityHash(Info));
	}
	else
	{
		str_format(aFilename, sizeof(aFilename), "dumps/%s/x_redirect_tile/%s.save", Config()->m_SvSavedTeesFilePath, GetSavedIdentityHash(Info));
	}
	CSaveTee SaveTee(Flags|SAVE_IDENTITY);
	SaveTee.SaveFile(aFilename, pChr);

	// Remove wallet money so we dont automatically drop it on disconnect because it is saved already
	if (Flags & SAVE_WALLET)
	{
		m_apPlayers[ClientID]->SetWalletMoney(0);
	}
	
	// return index of newly added identity
	return m_vSavedIdentities.size() - 1;
}

int CGameContext::FindSavedPlayer(int ClientID)
{
	if (!m_apPlayers[ClientID] || m_apPlayers[ClientID]->m_IsDummy)
		return -1;

	NETADDR Addr;
	Server()->GetClientAddr(ClientID, &Addr);

	int Found = -1;
	for (unsigned int i = 0; i < m_vSavedIdentities.size(); i++)
	{
		SSavedIdentity Info = m_vSavedIdentities[i];
		bool SameAddrAndPort = net_addr_comp(&Addr, &Info.m_Addr, true) == 0;
		if (Found != -1)
		{
			if (SameAddrAndPort)
				Found = i; // for getting the correct savedidentity when shutting down or saving a tee for save drop
			continue;
		}

		bool SameAddr = net_addr_comp(&Addr, &Info.m_Addr, false) == 0;
		bool SameTimeoutCode = Info.m_aTimeoutCode[0] != '\0' && str_comp(Info.m_aTimeoutCode, m_apPlayers[ClientID]->m_TimeoutCode) == 0;
		bool SameAcc = Info.m_aAccUsername[0] != '\0' && str_comp(Info.m_aAccUsername, m_Accounts[m_apPlayers[ClientID]->GetAccID()].m_Username) == 0;
		bool SameName = str_comp(Info.m_aName, Server()->ClientName(ClientID)) == 0;
		bool SameTeeInfo = mem_comp(&Info.m_TeeInfo, &m_apPlayers[ClientID]->m_TeeInfos, sizeof(CTeeInfo)) == 0;

		// SameTeeInfo is not really used, since players with the same skin and ip would get fucked up otherwise, in CSaveTee::Save() the identity of e.g. dummy would get saved then
		bool SameClientInfo = SameAddr && SameName;
		SameAcc = SameAcc && (SameAddr || SameName || SameTeeInfo || SameTimeoutCode);
		if (SameAddrAndPort || SameAcc || SameTimeoutCode || SameClientInfo)
		{
			Found = i;
		}
	}

	return Found;
}

const char *CGameContext::GetSavedIdentityHash(SSavedIdentity Info)
{
	SHA256_CTX Sha256Ctx;
	sha256_init(&Sha256Ctx);

	// manually update sha256 to no get bytes after 0 bytes in or so
	sha256_update(&Sha256Ctx, &Info.m_aAccUsername, str_length(Info.m_aAccUsername));
	sha256_update(&Sha256Ctx, &Info.m_Addr, sizeof(Info.m_Addr));
	sha256_update(&Sha256Ctx, &Info.m_aTimeoutCode, str_length(Info.m_aTimeoutCode));
	sha256_update(&Sha256Ctx, &Info.m_aName, str_length(Info.m_aName));
	for (int p = 0; p < NUM_SKINPARTS; p++)
	{
		sha256_update(&Sha256Ctx, Info.m_TeeInfo.GetSkinPartName(p), str_length(Info.m_TeeInfo.m_aaSkinPartNames[p]));
		sha256_update(&Sha256Ctx, &Info.m_TeeInfo.m_aUseCustomColors[p], sizeof(Info.m_TeeInfo.m_aUseCustomColors[p]));
		sha256_update(&Sha256Ctx, &Info.m_TeeInfo.m_aSkinPartColors[p], sizeof(Info.m_TeeInfo.m_aSkinPartColors[p]));
	}
	sha256_update(&Sha256Ctx, &Info.m_TeeInfo.m_Sevendown.m_SkinName, str_length(Info.m_TeeInfo.m_Sevendown.m_SkinName));
	sha256_update(&Sha256Ctx, &Info.m_TeeInfo.m_Sevendown.m_UseCustomColor, sizeof(Info.m_TeeInfo.m_Sevendown.m_UseCustomColor));
	sha256_update(&Sha256Ctx, &Info.m_TeeInfo.m_Sevendown.m_ColorBody, sizeof(Info.m_TeeInfo.m_Sevendown.m_ColorBody));
	sha256_update(&Sha256Ctx, &Info.m_TeeInfo.m_Sevendown.m_ColorFeet, sizeof(Info.m_TeeInfo.m_Sevendown.m_ColorFeet));

	static char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(sha256_finish(&Sha256Ctx), aSha256, sizeof(aSha256));
	return aSha256;
}

bool CGameContext::CheckLoadPlayer(int ClientID)
{
	// dont load two saves for one tee, because they are probably two clients with identical information, sending a info change can cause a second load, of the tee thats the 2nd client
	if (!m_apPlayers[ClientID] || m_apPlayers[ClientID]->m_LoadedSavedPlayer)
		return false;

	int Index = FindSavedPlayer(ClientID);
	if (Index == -1)
		return false;

	// Get path and load
	bool Success = TryLoadPlayer(ClientID, Index, false);
	if (!Success)
	{
		// Normal path didn't work, let's see if we can find the file in the redirect tile folder
		Success = TryLoadPlayer(ClientID, Index, true);
	}
	return Success;
}

bool CGameContext::TryLoadPlayer(int ClientID, int Index, bool RedirectTile)
{
	const char *pHash = GetSavedIdentityHash(m_vSavedIdentities[Index]);
	char aPath[IO_MAX_PATH_LENGTH];
	if (RedirectTile)
		str_format(aPath, sizeof(aPath), "dumps/%s/x_redirect_tile/%s.save", Config()->m_SvSavedTeesFilePath, pHash);
	else
		str_format(aPath, sizeof(aPath), "dumps/%s/%s/%s.save", Config()->m_SvSavedTeesFilePath, Server()->GetCurrentMapName(), pHash);
	CSaveTee SaveTee;
	if (SaveTee.LoadFile(aPath, m_apPlayers[ClientID]->GetCharacter()))
	{
		Server()->SendRedirectSaveTeeRemove(SaveTee.GetPreviousPort(), pHash);
		// Remove file, this save has been used now
		dbg_msg("save", "%d:%s used his save, removing save file", ClientID, Server()->ClientName(ClientID));
		Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
		m_vSavedIdentities.erase(m_vSavedIdentities.begin() + Index);
		m_apPlayers[ClientID]->m_LoadedSavedPlayer = true;
		return true;
	}
	return false;
}

void CGameContext::OnRedirectSaveTeeAdd(const char *pHash)
{
	if (!pHash[0])
		return;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "dumps/%s/x_redirect_tile/%s.save", Config()->m_SvSavedTeesFilePath, pHash);
	CSaveTee SaveTee;
	if (SaveTee.LoadFile(aPath, 0, this) && SaveTee.HasSavedIdentity() && SaveTee.GetIdentity().m_RedirectTilePort == Config()->m_SvPort)
	{
		int Index = GetIdentityIndexByHash(pHash);
		if (Index == -1)
			m_vSavedIdentities.push_back(SaveTee.GetIdentity());
	}
}

void CGameContext::OnRedirectSaveTeeRemove(const char *pHash)
{
	int Index = GetIdentityIndexByHash(pHash);
	if (Index == -1)
		return;
	m_vSavedIdentities.erase(m_vSavedIdentities.begin() + Index);
}

int CGameContext::GetIdentityIndexByHash(const char *pHash)
{
	if (pHash[0])
		for (unsigned int i = 0; i < m_vSavedIdentities.size(); i++)
			if (str_comp(GetSavedIdentityHash(m_vSavedIdentities[i]), pHash) == 0)
				return i;
	return -1;
}

void CGameContext::OnPlayerCountUpdate(int Port, int PlayerCount)
{
	CPlayerCounter *pEnt = (CPlayerCounter *)m_World.FindFirst(CGameWorld::ENTTYPE_PLAYER_COUNTER);
	for (; pEnt; pEnt = (CPlayerCounter *)pEnt->TypeNext())
	{
		pEnt->OnUpdate(Port, PlayerCount);
	}
}

void CGameContext::SendPlayerCountUpdate(bool Shutdown)
{
	m_LastPlayerCountUpdate = Server()->Tick();
	Server()->SendPlayerCountUpdate(Shutdown);
}

int CGameContext::GetRediretListPort(int WantedSwitchNumber)
{
	const char *pList = Config()->m_SvRedirectServerTilePorts;
	char aBuf[16];
	while ((pList = str_next_token(pList, ",", aBuf, sizeof(aBuf))))
	{
		int Switch = 0;
		int Port = 0;
		if (sscanf(aBuf, "%d:%d", &Switch, &Port) == 2 && Switch == WantedSwitchNumber)
		{
			return Port;
		}
	}
	return 0;
}

int CGameContext::GetRediretListSwitch(int WantedPort)
{
	const char *pList = Config()->m_SvRedirectServerTilePorts;
	char aBuf[16];
	while ((pList = str_next_token(pList, ",", aBuf, sizeof(aBuf))))
	{
		int Switch = 0;
		int Port = 0;
		if (sscanf(aBuf, "%d:%d", &Switch, &Port) == 2 && Port == WantedPort)
		{
			return Switch;
		}
	}
	return 0;
}

void CGameContext::CreateFolders()
{
	char aBuf[IO_MAX_PATH_LENGTH] = { 0 };
	fs_makedir(Storage()->GetBinaryPath(Config()->m_SvAccFilePath, aBuf, sizeof(aBuf)));
	fs_makedir(Storage()->GetBinaryPath(Config()->m_SvDonationFilePath, aBuf, sizeof(aBuf)));

	char aPath[IO_MAX_PATH_LENGTH];

	// plots
	fs_makedir(Storage()->GetBinaryPath(Config()->m_SvPlotFilePath, aBuf, sizeof(aBuf)));
	str_format(aPath, sizeof(aPath), "%s/%s", Config()->m_SvPlotFilePath, Server()->GetMapName());
	fs_makedir(Storage()->GetBinaryPath(aPath, aBuf, sizeof(aBuf)));

	str_format(aPath, sizeof(aPath), "%s/presets", Config()->m_SvPlotFilePath);
	fs_makedir(Storage()->GetBinaryPath(aPath, aBuf, sizeof(aBuf)));

	// money drops
	fs_makedir(Storage()->GetBinaryPath(Config()->m_SvMoneyDropsFilePath, aBuf, sizeof(aBuf)));
	str_format(aPath, sizeof(aPath), "%s/%s", Config()->m_SvMoneyDropsFilePath, Server()->GetMapName());
	fs_makedir(Storage()->GetBinaryPath(aPath, aBuf, sizeof(aBuf)));

	// map designs
	fs_makedir(Storage()->GetBinaryPath(Config()->m_SvMapDesignPath, aBuf, sizeof(aBuf)));
	str_format(aPath, sizeof(aPath), "%s/%s", Config()->m_SvMapDesignPath, Server()->GetMapName());
	fs_makedir(Storage()->GetBinaryPath(aPath, aBuf, sizeof(aBuf)));

	// money history
	str_format(aPath, sizeof(aPath), "dumps/%s", Config()->m_SvMoneyHistoryFilePath);
	Storage()->CreateFolder(aPath, IStorage::TYPE_SAVE);

	// saved tee
	str_format(aPath, sizeof(aPath), "dumps/%s", Config()->m_SvSavedTeesFilePath);
	Storage()->CreateFolder(aPath, IStorage::TYPE_SAVE);
	str_format(aPath, sizeof(aPath), "dumps/%s/%s", Config()->m_SvSavedTeesFilePath, Server()->GetMapName());
	Storage()->CreateFolder(aPath, IStorage::TYPE_SAVE);
	str_format(aPath, sizeof(aPath), "dumps/%s/x_redirect_tile", Config()->m_SvSavedTeesFilePath);
	Storage()->CreateFolder(aPath, IStorage::TYPE_SAVE);
}

bool CGameContext::SameIP(int ClientID1, int ClientID2)
{
	if (ClientID1 < 0 || ClientID1 >= MAX_CLIENTS || ClientID2 < 0 || ClientID2 >= MAX_CLIENTS || !m_apPlayers[ClientID1] || !m_apPlayers[ClientID2])
		return false;

	NETADDR Addr1, Addr2;
	Server()->GetClientAddr(ClientID1, &Addr1);
	Server()->GetClientAddr(ClientID2, &Addr2);

	bool Same = (net_addr_comp(&Addr1, &Addr2, false) == 0);

	int AccID1 = m_apPlayers[ClientID1]->GetAccID();
	if (AccID1 >= ACC_START)
		Same = Same || (net_addr_comp(&Addr2, &m_Accounts[AccID1].m_LastAddr, false) == 0);

	int AccID2 = m_apPlayers[ClientID2]->GetAccID();
	if (AccID2 >= ACC_START)
		Same = Same || (net_addr_comp(&Addr1, &m_Accounts[AccID2].m_LastAddr, false) == 0);

	return Same;
}

bool CGameContext::SameIP(int AccID, const NETADDR *pAddr)
{
	if (AccID < ACC_START)
		return false;

	return (net_addr_comp(pAddr, &m_Accounts[AccID].m_Addr, false) == 0
		|| net_addr_comp(pAddr, &m_Accounts[AccID].m_LastAddr, false) == 0);
}

int CGameContext::GetNextClientID()
{
	for (int i = 0; i < Config()->m_SvMaxClients; i++)
		if (((CServer *)Server())->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			return i;
	return -1;
}

int CGameContext::GetCIDByName(const char *pName)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (m_apPlayers[i] && !str_comp(pName, Server()->ClientName(i)))
			return i;
	return -1;
}

void CGameContext::CreateSoundGlobal(int Sound)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (m_apPlayers[i])
		{
			if (Server()->IsSevendown(i))
			{
				CMsgPacker Msg(5 + NUM_NETMSGTYPES); // NETMSGTYPE_SV_SOUNDGLOBAL
				Msg.AddInt(Sound);
				Server()->SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
			}
			else
				CreateSoundPlayer(Sound, i);
		}
}

void CGameContext::CreateSoundPlayer(int Sound, int ClientID)
{
	CreateSound(m_apPlayers[ClientID]->m_ViewPos, Sound, CmaskOne(ClientID));
}

void CGameContext::CreateSoundPlayerAt(vec2 Pos, int Sound, int ClientID)
{
	CreateSound(Pos, Sound, CmaskOne(ClientID));
}

bool CGameContext::IsLocal(int ClientID1, int ClientID2)
{
	if (ClientID1 == ClientID2 || ClientID1 < 0 || ClientID2 < 0)
		return true;

	CCharacter *p1 = GetPlayerChar(ClientID1);
	CCharacter *p2 = GetPlayerChar(ClientID2);

	if (!p1 || !p2 || p1->Team() != p2->Team())
		return false;

	float dx = p1->GetPos().x-p2->GetPos().x;
	float dy = p1->GetPos().y-p2->GetPos().y;

	if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
		return false;

	if(distance(p1->GetPos(), p2->GetPos()) > 4000.0f)
		return false;

	return true;
}

bool CGameContext::CanReceiveMessage(int Sender, int Receiver)
{
	return m_apPlayers[Receiver] && (!m_apPlayers[Receiver]->m_LocalChat || IsLocal(Sender, Receiver));
}

bool CGameContext::IsMuted(int Sender, int Receiver)
{
	return m_apPlayers[Receiver] && Sender >= 0 && m_apPlayers[Receiver]->m_aMuted[Sender];
}

bool CGameContext::LineShouldHighlight(const char *pLine, const char *pName)
{
	const char *pHL = str_utf8_find_nocase(pLine, pName);
	if (pHL)
	{
		int Length = str_length(pName);
		if(Length > 0 && (pLine == pHL || pHL[-1] == ' ') && (pHL[Length] == 0 || pHL[Length] == ' ' || pHL[Length] == '.' || pHL[Length] == '!' || pHL[Length] == ',' || pHL[Length] == '?' || pHL[Length] == ':'))
			return true;
	}
	return false;
}

void CGameContext::SendChatPolice(const char *pMessage)
{
	SendChat(-1, CHAT_POLICE_CHANNEL, -1, pMessage);
}

bool CGameContext::JailPlayer(int ClientID, int Seconds)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (!pPlayer || Seconds <= 0)
		return false;

	// make sure we are not saved as killer for someone else after we got arrested, so we cant take the flag to the jail
	UnsetKiller(ClientID);

	pPlayer->m_JailTime = Server()->TickSpeed() * Seconds;
	pPlayer->m_EscapeTime = 0;
	if(pPlayer->GetCharacter())
		pPlayer->KillCharacter(WEAPON_GAME);

	// Force destroyendtick to be 1, so it can get resetted in the next tick and the owner gets the message aswell
	int PlotID = GetPlotID(pPlayer->GetAccID());
	if (PlotID >= PLOT_START)
		m_aPlots[PlotID].m_DestroyEndTick = 1;
	return true;
}

bool CGameContext::ForceJailRelease(int ClientID)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (!pPlayer || !pPlayer->m_JailTime)
		return false;

	pPlayer->m_JailTime = 1;
	SendChatTarget(ClientID, "You were released from jail");
	pPlayer->KillCharacter(WEAPON_GAME);
	return true;
}

void CGameContext::ProcessSpawnBlockProtection(int ClientID)
{
	CCharacter *pChr = GetPlayerChar(ClientID);
	if (!pChr)
		return;

	int Killer = pChr->Core()->m_Killer.m_ClientID;
	if (Killer < 0 || Killer == ClientID)
		return;

	CPlayer *pKiller = m_apPlayers[Killer];
	if (!pKiller || !pKiller->GetCharacter() || pKiller->m_IsDummy)
		return;

	if (IsSpawnArea(pKiller->GetCharacter()->GetPos()) && !Arenas()->FightStarted(Killer)) // if killer is in spawn area
	{
		pKiller->m_SpawnBlockScore++;
		if (Config()->m_SvSpawnBlockProtection)
		{
			if (pKiller->m_SpawnBlockScore > 5)
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "'%s' is spawnblocking. Catch him!", Server()->ClientName(Killer));
				SendChatPolice(aBuf);
				SendChatTarget(Killer, "Police is searching you because of spawnblocking");
				pKiller->m_EscapeTime += Server()->TickSpeed() * 120; // + 2 minutes escape time
			}
			else
			{
				SendChatTarget(Killer, "[WARNING] Spawnblocking is illegal");
			}
		}
	}
}

bool CGameContext::IsSpawnArea(vec2 Pos)
{
	return (Pos.x >= Config()->m_SvSpawnAreaLowX * 32
		&& Pos.x <= Config()->m_SvSpawnAreaHighX * 32
		&& Pos.y >= Config()->m_SvSpawnAreaLowY * 32
		&& Pos.y <= Config()->m_SvSpawnAreaHighY * 32);
}

vec2 CGameContext::RoundPos(vec2 Pos)
{
	Pos = vec2((int)Pos.x, (int)Pos.y);
	Pos.x -= (int)Pos.x % 32 - 16;
	Pos.y -= (int)Pos.y % 32 - 16;
	return Pos;
}

bool CGameContext::TryAccountSystemBan(const NETADDR *pAddr, int Type, int Secs)
{
	// find a matching register ban for this ip, update expiration time if found
	for(int i = 0; i < m_NumAccountSystemBans; i++)
	{
		if(net_addr_comp(&m_aAccountSystemBans[i].m_Addr, pAddr, false) == 0)
		{
			m_aAccountSystemBans[i].m_LastAttempt = Server()->Tick();

			bool Ban = false;
			switch (Type)
			{
			case ACC_SYS_REGISTER:
			{
				m_aAccountSystemBans[i].m_NumRegistrations++;
				Ban = m_aAccountSystemBans[i].m_NumRegistrations > Config()->m_SvAccSysBanRegistrations;
				break;
			}
			case ACC_SYS_LOGIN:
			{
				m_aAccountSystemBans[i].m_NumFailedLogins++;
				Ban = m_aAccountSystemBans[i].m_NumFailedLogins > Config()->m_SvAccSysBanPwFails;
				break;
			}
			case ACC_SYS_PIN:
			{
				m_aAccountSystemBans[i].m_NumFailedPins++;
				Ban = m_aAccountSystemBans[i].m_NumFailedPins > Config()->m_SvAccSysBanPinFails;
				break;
			}
			}

			if (Ban)
			{
				m_aAccountSystemBans[i].m_Expire = Server()->Tick() + Secs * Server()->TickSpeed();
				return true;
			}
			return false;
		}
	}

	// nothing to update create new one
	if(m_NumAccountSystemBans < MAX_ACC_SYS_BANS)
	{
		m_aAccountSystemBans[m_NumAccountSystemBans].m_Addr = *pAddr;
		m_aAccountSystemBans[m_NumAccountSystemBans].m_Expire = 0;
		m_aAccountSystemBans[m_NumAccountSystemBans].m_LastAttempt = Server()->Tick();

		switch (Type)
		{
		case ACC_SYS_REGISTER: m_aAccountSystemBans[m_NumAccountSystemBans].m_NumRegistrations = 1; break;
		case ACC_SYS_LOGIN: m_aAccountSystemBans[m_NumAccountSystemBans].m_NumFailedLogins = 1; break;
		case ACC_SYS_PIN: m_aAccountSystemBans[m_NumAccountSystemBans].m_NumFailedPins = 1; break;
		}

		m_NumAccountSystemBans++;
		return false;
	}
	// no free slot found
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "acc", "account system ban array is full");
	return false;
}

int CGameContext::ProcessAccountSystemBan(int ClientID, int Type)
{
	if(!m_apPlayers[ClientID])
		return 0;

	if (IsAccountSystemBanned(ClientID, true))
		return 1;

	NETADDR Addr;
	Server()->GetClientAddr(ClientID, &Addr);
	if (TryAccountSystemBan(&Addr, Type, ACC_SYS_BAN_DELAY))
	{
		const char *pReason = "";
		switch (Type)
		{
		case ACC_SYS_REGISTER: pReason = "Registration spam"; break;
		case ACC_SYS_LOGIN: pReason = "Too many login fails"; break;
		case ACC_SYS_PIN: pReason = "Too many pin fails"; break;
		}

		char aBuf[128];
		str_format(aBuf, sizeof aBuf, "You have been banned from the account system for %d seconds (%s)", ACC_SYS_BAN_DELAY, pReason);
		SendChatTarget(ClientID, aBuf);
		return 1;
	}

	return 0;
}

bool CGameContext::IsAccountSystemBanned(int ClientID, bool ChatMsg)
{
	if(!m_apPlayers[ClientID])
		return false;

	NETADDR Addr;
	Server()->GetClientAddr(ClientID, &Addr);
	int AccountSystemBanned = 0;

	for(int i = 0; i < m_NumAccountSystemBans; i++)
	{
		if(net_addr_comp(&Addr, &m_aAccountSystemBans[i].m_Addr, false) == 0)
		{
			AccountSystemBanned = (m_aAccountSystemBans[i].m_Expire - Server()->Tick()) / Server()->TickSpeed();
			break;
		}
	}

	if (AccountSystemBanned > 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof aBuf, "You are banned from the account system for the next %d seconds.", AccountSystemBanned);
		SendChatTarget(ClientID, aBuf);
		return true;
	}

	return false;
}

const char *CGameContext::AppendMotdFooter(const char *pMsg, const char *pFooter)
{
	static char aRet[900] = "";
	if (pMsg[0])
		str_format(aRet, sizeof(aRet), "%s\n\n%s", pMsg, pFooter);
	return aRet;

	/*MOTD_MAX_LINES ist jetzt 24, war vorher aber 22. Weiß nicht wieso das dann überhaupt alles funktioniert... Muss auf jedenfall angepasst werden. Needs a Rewrite :D
	static char aRet[900] = "";
	if (!pFooter[0])
	{
		str_copy(aRet, pMsg, sizeof(aRet));
		return aRet;
	}

	int FooterLines = 0;
	int MaxLinesWithoutFooter = MOTD_MAX_LINES;
	for (int i = 0, s = 0; i < str_length(pFooter) + 1; i++)
	{
		s++;
		if ((pFooter[i] == '\\' && pFooter[i+1] == 'n') || pFooter[i] == '\n' || s >= 35)
		{
			FooterLines++;
			MaxLinesWithoutFooter--;
			s = 0;
		}
	}

	char aMotd[900];
	str_copy(aMotd, pMsg, sizeof(aMotd));
	if (!aMotd[0])
		return "";

	int Lines = 0;
	int MotdLen = str_length(aMotd) + 1;
	for (int i = 0, s = 0; i < MotdLen; i++)
	{
		s++;
		if ((aMotd[i] == '\\' && aMotd[i+1] == 'n') || aMotd[i] == '\n' || s >= 35)
		{
			Lines++;
			s = 0;
		}
	}

	for (int i = MotdLen; i > 0; i--)
	{
		if ((aMotd[i-1] == '\\' && aMotd[i] == 'n') || aMotd[i] == '\n' || Lines > MaxLinesWithoutFooter)
		{
			aMotd[i] = '\0';
			aMotd[i - 1] = '\0';
			Lines--;
		}
		else
			break;
	}

	Lines = clamp(Lines, 0, MaxLinesWithoutFooter);

	char aNewLines[64] = "";
	for (int i = 0; i < MOTD_MAX_LINES-Lines; i++)
		str_append(aNewLines, "\n", sizeof(aNewLines));

	str_format(aRet, sizeof(aRet), "%s%s%s", aMotd, aNewLines, pFooter);
	return aRet;*/
}

const char *CGameContext::FormatMotd(const char *pMsg)
{
	char aFooter[128];
	str_format(aFooter, sizeof(aFooter), "F-DDrace is a mod by fokkonaut\nF-DDrace Mod. Ver.: %s", GAME_VERSION);
	return AppendMotdFooter(pMsg, aFooter);
}

const char *CGameContext::FormatURL(const char *pURL)
{
	static char aURL[256];
	for (int i = 0, s = 0; i < str_length(pURL) + 1; i++)
	{
#ifdef CONF_FAMILY_WINDOWS
		if (pURL[i] == '&')
			aURL[s++] = '^';
#endif
		aURL[s++] = pURL[i];
	}

	return aURL;
}

const char *CGameContext::FormatExperienceBroadcast(const char *pMsg, int ClientID)
{
	if (Server()->IsSevendown(ClientID))
		return pMsg;

	char pTextColor[5] = { '^', Config()->m_SvExpMsgColorText[0], Config()->m_SvExpMsgColorText[1], Config()->m_SvExpMsgColorText[2] };
	char pSymbolColor[5] = { '^', Config()->m_SvExpMsgColorSymbol[0], Config()->m_SvExpMsgColorSymbol[1], Config()->m_SvExpMsgColorSymbol[2] };
	char pValueColor[5] = { '^', Config()->m_SvExpMsgColorValue[0], Config()->m_SvExpMsgColorValue[1], Config()->m_SvExpMsgColorValue[2] };

	const int ColorOffset = 4;
	int s = ColorOffset;

	static char aRet[512];
	str_copy(aRet, pTextColor, sizeof(aRet));

	int BroadcastLen = str_length(pMsg) + 1;
	for (int i = 0; i < BroadcastLen; i++)
	{
		aRet[s] = pMsg[i];
		s++;

		int Found = 0;
		if (pMsg[i+1] == '[' || pMsg[i+1] == ']' || pMsg[i+1] == '/')
			Found = 1;
		else if (pMsg[i] == '[' || pMsg[i] == '/')
			Found = 2;
		else if (pMsg[i] == ']')
			Found = 3;

		if (Found)
		{
			s += ColorOffset;
			const char *pColorCode = Found == 1 ? pSymbolColor : Found == 2 ? pValueColor : pTextColor;
			str_append(aRet, pColorCode, sizeof(aRet));
		}
	}

	return aRet;
}

void CGameContext::CalcScreenParams(float Aspect, float Zoom, float *w, float *h)
{
	const float Amount = 1150 * 1000;
	const float WMax = 1500;
	const float HMax = 1050;

	float f = sqrtf(Amount) / sqrtf(Aspect);
	*w = f * Aspect;
	*h = f;

	// limit the view
	if(*w > WMax)
	{
		*w = WMax;
		*h = *w / Aspect;
	}

	if(*h > HMax)
	{
		*h = HMax;
		*w = *h * Aspect;
	}

	*w *= Zoom;
	*h *= Zoom;
}

void CGameContext::SnapSelectedArea(CSelectedArea *pSelectedArea)
{
	vec2 TopLeft = pSelectedArea->TopLeft();
	vec2 BottomRight = pSelectedArea->BottomRight();
	vec2 aPoints[4] = { TopLeft, vec2(BottomRight.x, TopLeft.y), BottomRight, vec2(TopLeft.x, BottomRight.y) };

	for (int i = 0; i < 4; i++)
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, pSelectedArea->m_aID[i], sizeof(CNetObj_Laser)));
		if (!pObj)
			return;

		int To = i == 3 ? 0 : i+1;
		pObj->m_X = round_to_int(aPoints[i].x);
		pObj->m_Y = round_to_int(aPoints[i].y);
		pObj->m_FromX = round_to_int(aPoints[To].x);
		pObj->m_FromY = round_to_int(aPoints[To].y);
		pObj->m_StartTick = Server()->Tick() - 2;
	}
}

void CGameContext::ConnectDummy(int DummyMode, vec2 Pos)
{
	int DummyID = GetNextClientID();
	if (DummyID < 0 || DummyID >= MAX_CLIENTS || m_apPlayers[DummyID])
		return;

	CPlayer *pDummy = m_apPlayers[DummyID] = new(DummyID) CPlayer(this, DummyID, false, false, true);
	Server()->DummyJoin(DummyID);
	pDummy->SetDummyMode(DummyMode);
	pDummy->m_ForceSpawnPos = Pos;
	pDummy->m_Afk = false; // players are marked as afk when they first enter. dummies dont send real inputs, thats why we need to make them non-afk again

	if (DummyMode == DUMMYMODE_V3_BLOCKER && Collision()->TileUsed(TILE_MINIGAME_BLOCK))
		pDummy->m_Minigame = MINIGAME_BLOCK;
	else if ((DummyMode == DUMMYMODE_SHOP_DUMMY && Collision()->TileUsed(ENTITY_SHOP_DUMMY_SPAWN))
		|| (DummyMode == DUMMYMODE_PLOT_SHOP_DUMMY && Collision()->TileUsed(ENTITY_PLOT_SHOP_DUMMY_SPAWN))
		|| (DummyMode == DUMMYMODE_BANK_DUMMY && Collision()->TileUsed(ENTITY_BANK_DUMMY_SPAWN))
		|| (DummyMode == DUMMYMODE_TAVERN_DUMMY && Collision()->TileUsed(ENTITY_TAVERN_DUMMY_SPAWN))
		)
		pDummy->m_Minigame = MINIGAME_NONE;

	if (DummyMode == DUMMYMODE_TAVERN_DUMMY)
	{
		pDummy->m_TeeInfos = CTeeInfo(SKIN_TWINBOP);
	}
	else
	{
		pDummy->m_TeeInfos = CTeeInfo(SKIN_DUMMY);
	}

	dbg_msg("dummy", "Dummy connected: %d, Dummymode: %d", DummyID, DummyMode);
	OnClientEnter(DummyID);
}

bool CGameContext::IsHouseDummy(int ClientID, int Type)
{
	if (Type == -1)
	{
		for (int i = 0; i < NUM_HOUSES; i++)
			if (IsHouseDummy(ClientID, i))
				return true;
		return false;
	}

	int Mode = 0;
	switch (Type)
	{
	case HOUSE_SHOP: Mode = DUMMYMODE_SHOP_DUMMY; break;
	case HOUSE_PLOT_SHOP: Mode = DUMMYMODE_PLOT_SHOP_DUMMY; break;
	case HOUSE_BANK: Mode = DUMMYMODE_BANK_DUMMY; break;
	case HOUSE_TAVERN: Mode = DUMMYMODE_TAVERN_DUMMY; break;
	}
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetDummyMode() == Mode;
}

int CGameContext::GetHouseDummy(int Type)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (IsHouseDummy(i, Type))
			return i;
	return -1;
}

void CGameContext::ConnectHouseDummy(int Type, bool SpawnTileOnly)
{
	int Index, SpawnTile, Dummymode;
	switch (Type)
	{
	case HOUSE_SHOP: Index = TILE_SHOP; SpawnTile = ENTITY_SHOP_DUMMY_SPAWN; Dummymode = DUMMYMODE_SHOP_DUMMY; break;
	case HOUSE_PLOT_SHOP: Index = TILE_PLOT_SHOP; SpawnTile = ENTITY_PLOT_SHOP_DUMMY_SPAWN; Dummymode = DUMMYMODE_PLOT_SHOP_DUMMY; break;
	case HOUSE_BANK: Index = TILE_BANK; SpawnTile = ENTITY_BANK_DUMMY_SPAWN; Dummymode = DUMMYMODE_BANK_DUMMY; break;
	case HOUSE_TAVERN: Index = TILE_TAVERN; SpawnTile = ENTITY_TAVERN_DUMMY_SPAWN; Dummymode = DUMMYMODE_TAVERN_DUMMY; break;
	default: return;
	}

	if (GetHouseDummy(Type) == -1 && Collision()->TileUsed(Index))
	{
		vec2 Pos = vec2(-1, -1);
		if (Collision()->TileUsed(SpawnTile))
		{
			Pos = Collision()->GetRandomTile(SpawnTile);
		}
		else if (SpawnTileOnly)
		{
			// Don't automatically connect house bots if no spawn tile has been found, only with sv_default_dummies enabled
			return;
		}
		ConnectDummy(Dummymode, Pos);
	}
}

void CGameContext::ConnectDefaultDummies()
{
	if (!str_comp(Server()->GetMapName(), "ChillBlock5"))
	{
		ConnectDummy(DUMMYMODE_CHILLBLOCK5_POLICE);
		ConnectDummy(DUMMYMODE_CHILLBLOCK5_BLOCKER);
		ConnectDummy(DUMMYMODE_CHILLBLOCK5_BLOCKER);
		ConnectDummy(DUMMYMODE_CHILLBLOCK5_RACER);
	}
	else if (!str_comp(Server()->GetMapName(), "BlmapChill"))
	{
		ConnectDummy(DUMMYMODE_BLMAPCHILL_POLICE);
	}
	else if (!str_comp(Server()->GetMapName(), "blmapV3RoyalX"))
	{
		ConnectDummy(DUMMYMODE_V3_BLOCKER);
	}

	if (Collision()->TileUsed(TILE_MINIGAME_BLOCK))
		ConnectDummy(DUMMYMODE_V3_BLOCKER);

	for (int i = 0; i < NUM_HOUSES; i++)
		ConnectHouseDummy(i);
}

void CGameContext::ConsoleIsDummyCallback(int ClientID, bool *pIsDummy, void *pUser)
{
	CGameContext* pSelf = (CGameContext*)pUser;
	*pIsDummy = pSelf->m_apPlayers[ClientID] && pSelf->m_apPlayers[ClientID]->m_IsDummy;
}

void CGameContext::ConsoleIsInViewCallback(int ClientID, int CallerID, bool *pIsInView, void *pUser)
{
	CGameContext* pSelf = (CGameContext*)pUser;
	*pIsInView = false;
	if (CallerID < 0 || !pSelf->m_apPlayers[CallerID] || !pSelf->GetPlayerChar(ClientID))
		return;

	vec2 CheckPos = pSelf->GetPlayerChar(ClientID)->GetPos();
	vec2 ShowDistance = pSelf->m_apPlayers[CallerID]->m_ShowDistance;
	float dx = pSelf->m_apPlayers[CallerID]->m_ViewPos.x-CheckPos.x;
	if (absolute(dx) > ShowDistance.x / 2.f)
	{
		return;
	}
	float dy = pSelf->m_apPlayers[CallerID]->m_ViewPos.y-CheckPos.y;
	if (absolute(dy) > ShowDistance.y / 2.f)
	{
		return;
	}
	*pIsInView = true;
}

void CGameContext::SetMapSpecificOptions()
{
	if (!str_comp(Server()->GetMapName(), "ChillBlock5"))
	{
		Config()->m_SvV3OffsetX = 374;
		Config()->m_SvV3OffsetY = 59;
	}
	else if (!str_comp(Server()->GetMapName(), "blmapV3RoyalX"))
	{
		Config()->m_SvV3OffsetX = 97;
		Config()->m_SvV3OffsetY = 19;
	}
	else if (!str_comp(Server()->GetMapName(), "BlmapChill"))
	{
		Config()->m_SvV3OffsetX = 696;
		Config()->m_SvV3OffsetY = 617;

		Config()->m_SvSpawnAreaLowX = 5;
		Config()->m_SvSpawnAreaLowY = 4;
		Config()->m_SvSpawnAreaHighX = 48;
		Config()->m_SvSpawnAreaHighY = 48;
	}
}

CLaserText *CGameContext::CreateLaserText(vec2 Pos, int Owner, const char *pText, int Seconds, bool AboveTee)
{
	if (AboveTee)
	{
		Pos.y -= 70.f;
	}
	Pos.y -= 32.f;
	Pos.x -= 16.f;
	return new CLaserText(&m_World, Pos, Owner, Seconds > 0 ? Server()->TickSpeed() * Seconds : -1, pText, (int)(strlen(pText)));
}

void CGameContext::SpawnHelicopter(vec2 Pos)
{
	Pos.y -= 64.f;
	new CHelicopter(&m_World, Pos);
}

void CGameContext::UpdateHidePlayers(int UpdateID)
{
	if (UpdateID == -1)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
			UpdateHidePlayers(i);
		return;
	}

	if (!m_apPlayers[UpdateID])
		return;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (i == UpdateID || !m_apPlayers[i] || m_apPlayers[UpdateID]->GetTeam() == TEAM_SPECTATORS || m_apPlayers[i]->m_IsDummy)
			continue;

		int Team = m_apPlayers[UpdateID]->GetHidePlayerTeam(i);

		// only update the team when its not the same as before
		if (m_apPlayers[i]->m_HidePlayerTeam[UpdateID] == Team)
			continue;

		m_apPlayers[i]->m_HidePlayerTeam[UpdateID] = Team;

		SendTeamChange(UpdateID, Team, true, Server()->Tick(), i);
	}
}

void CGameContext::UnsetTelekinesis(CEntity *pEntity)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacter *pChr = GetPlayerChar(i);
		if (pChr && pChr->m_pTelekinesisEntity == pEntity)
		{
			pChr->m_pTelekinesisEntity = 0;
			break; // can break here, every entity can only be picked by one player using telekinesis at the time
		}
	}
}

void CGameContext::UnsetKiller(int ClientID)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacter *pChr = GetPlayerChar(i);
		if (ClientID != i && pChr && pChr->Core()->m_Killer.m_ClientID == ClientID)
		{
			pChr->Core()->m_Killer.m_ClientID = -1;
			pChr->Core()->m_Killer.m_Weapon = -1;
		}
	}
}

void CGameContext::OnSetTimedOut(int ClientID, int OrigID)
{
	m_World.InitPlayerMap(ClientID, true);
}

bool CGameContext::FlagsUsed()
{
	return (m_pController->GetGameFlags()&GAMEFLAG_FLAGS);
}

void CGameContext::SendMotd(const char *pMsg, int ClientID)
{
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = pMsg;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendTeamChange(int ClientID, int Team, bool Silent, int CooldownTick, int ToClientID)
{
	CNetMsg_Sv_Team Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Team = Team;
	Msg.m_Silent = (int)Silent;
	Msg.m_CooldownTick = CooldownTick;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ToClientID);
}

const char *CGameContext::GetWeaponName(int Weapon)
{
	switch (Weapon)
	{
	case -2:
		return "Heart";
	case -1:
		return "Armor";
	case WEAPON_HAMMER:
		return "Hammer";
	case WEAPON_GUN:
		return "Gun";
	case WEAPON_SHOTGUN:
		return "Shotgun";
	case WEAPON_GRENADE:
		return "Grenade";
	case WEAPON_LASER:
		return "Rifle";
	case WEAPON_NINJA:
		return "Ninja";
	case WEAPON_TASER:
		return "Taser";
	case WEAPON_HEART_GUN:
		return "Heart Gun";
	case WEAPON_PLASMA_RIFLE:
		return "Plasma Rifle";
	case WEAPON_STRAIGHT_GRENADE:
		return "Straight Grenade";
	case WEAPON_TELEKINESIS:
		return "Telekinesis";
	case WEAPON_LIGHTSABER:
		return "Lightsaber";
	case WEAPON_PORTAL_RIFLE:
		return "Portal Rifle";
	case WEAPON_PROJECTILE_RIFLE:
		return "Projectile Rifle";
	case WEAPON_BALL_GRENADE:
		return "Ball Greande";
	case WEAPON_DRAW_EDITOR:
		return "Draw Editor";
	case WEAPON_TELE_RIFLE:
		return "Tele Rifle";
	case WEAPON_LIGHTNING_LASER:
		return "Lightning Laser";
	}
	return "Unknown";
}

int CGameContext::GetWeaponType(int Weapon)
{
	switch (Weapon)
	{
	case WEAPON_TASER:
		return WEAPON_LASER;
	case WEAPON_HEART_GUN:
		return WEAPON_GUN;
	case WEAPON_PLASMA_RIFLE:
		return WEAPON_LASER;
	case WEAPON_STRAIGHT_GRENADE:
		return WEAPON_GRENADE;
	case WEAPON_TELEKINESIS:
		return WEAPON_NINJA;
	case WEAPON_LIGHTSABER:
		return WEAPON_GUN;
	case WEAPON_PORTAL_RIFLE:
		return WEAPON_LASER;
	case WEAPON_PROJECTILE_RIFLE:
		return WEAPON_LASER;
	case WEAPON_BALL_GRENADE:
		return WEAPON_GRENADE;
	case WEAPON_DRAW_EDITOR:
		return WEAPON_NINJA;
	case WEAPON_TELE_RIFLE:
		return WEAPON_LASER;
	case WEAPON_LIGHTNING_LASER:
		return WEAPON_LASER;
	}
	return Weapon;
}

int CGameContext::GetProjectileType(int Weapon)
{
	switch (Weapon)
	{
	case WEAPON_STRAIGHT_GRENADE:
		return WEAPON_GRENADE;
	case WEAPON_PROJECTILE_RIFLE:
		return WEAPON_GUN;
	case WEAPON_BALL_GRENADE:
		return WEAPON_GRENADE;
	}
	return Weapon;
}

int CGameContext::GetPickupType(int Type, int Subtype)
{
	if (Type == POWERUP_BATTERY)
		return PICKUP_LASER;
	if (Type == POWERUP_NINJA)
		return PICKUP_NINJA;
	if (Type != POWERUP_WEAPON)
		return Type;

	Subtype = GetWeaponType(Subtype);
	switch (Subtype)
	{
	case WEAPON_GUN:
		return PICKUP_GUN;
	case WEAPON_HAMMER:
		return PICKUP_HAMMER;
	case WEAPON_SHOTGUN:
		return PICKUP_SHOTGUN;
	case WEAPON_GRENADE:
		return PICKUP_GRENADE;
	case WEAPON_LASER:
		return PICKUP_LASER;
	case WEAPON_NINJA:
		return PICKUP_NINJA;
	}
	return Subtype;
}

void CGameContext::SendExtraMessage(int Extra, int ToID, bool Set, int FromID, bool Silent, int Special)
{
	if (Silent)
		return;

	char aMsg[128];
	str_copy(aMsg, CreateExtraMessage(Extra, Set, FromID, ToID, Special), sizeof(aMsg));
	SendChatTarget(ToID, aMsg);
	if (FromID >= 0 && FromID != ToID)
		SendChatTarget(FromID, aMsg);
}

const char *CGameContext::CreateExtraMessage(int Extra, bool Set, int FromID, int ToID, int Special)
{
	char aInfinite[16];
	char aItem[64];
	static char aMsg[128];

	// infinite
	if (Set && (Extra == INF_RAINBOW || Extra == INF_METEOR))
		str_format(aInfinite, sizeof(aInfinite), "Infinite ");
	else
		aInfinite[0] = 0;

	// get item name
	char aTemp[64];
	str_copy(aTemp, GetExtraName(Extra, Special), sizeof(aItem));
	str_format(aItem, sizeof(aItem), "%s%s", aInfinite, aTemp);

	// message without a sender
	if (FromID == -1 || FromID == ToID)
	{
		if (Extra == JETPACK || Extra == ATOM || Extra == TRAIL || Extra == METEOR || Extra == INF_METEOR || Extra == SCROLL_NINJA || Extra == HOOK_POWER|| Extra == SPREAD_WEAPON
			|| Extra == FREEZE_HAMMER || Extra == ITEM || Extra == TELE_WEAPON || Extra == DOOR_HAMMER || Extra == ROTATING_BALL || Extra == EPIC_CIRCLE || Extra == STAFF_IND)
			str_format(aMsg, sizeof(aMsg), "You %s %s", Set ? "have a" : "lost your", aItem);
		else if (Extra == VANILLA_MODE || Extra == DDRACE_MODE)
			str_format(aMsg, sizeof(aMsg), "You are now in %s", aItem);
		else if (Extra == PASSIVE || Extra == SNAKE)
			str_format(aMsg, sizeof(aMsg), "You are %s in %s", Set ? "now" : "no longer", aItem);
		else if (Extra == ENDLESS_HOOK || Extra == INFINITE_JUMPS)
			str_format(aMsg, sizeof(aMsg), "%s %s been %s", aItem, Extra == INFINITE_JUMPS ? "have" : "has", Set ? "activated" : "deactivated");
		else if (Extra == TEE_CONTROL)
			str_format(aMsg, sizeof(aMsg), "You are %s permitted to use the tee controller", Set ? "now" : "no longer");
		else
			str_format(aMsg, sizeof(aMsg), "You %s %s", Set ? "have" : "lost", aItem);
	}
	// message with a sender
	else if (FromID >= 0)
		str_format(aMsg, sizeof(aMsg), "%s was %s '%s' by '%s'", aItem, Set ? "given to" : "removed from", Server()->ClientName(ToID), Server()->ClientName(FromID));

	return aMsg;
}

const char *CGameContext::GetExtraName(int Extra, int Special)
{
	switch (Extra)
	{
	case HOOK_NORMAL:
		return "Normal";
	case JETPACK:
		return "Jetpack Gun";
	case RAINBOW:
		return "Rainbow";
	case INF_RAINBOW:
		return "Rainbow";
	case ATOM:
		return "Atom";
	case TRAIL:
		return "Trail";
	case SPOOKY_GHOST:
		return "Spooky Ghost";
	case METEOR:
		return "Meteor";
	case INF_METEOR:
		return "Meteor";
	case PASSIVE:
		return "Passive Mode";
	case VANILLA_MODE:
		return "Vanilla Mode";
	case DDRACE_MODE:
		return "DDrace Mode";
	case BLOODY:
		return "Bloody";
	case STRONG_BLOODY:
		return "Strong Bloody";
	case SCROLL_NINJA:
		return "Scroll Ninja";
	case HOOK_POWER:
		{
			static char aPower[64];
			str_format(aPower, sizeof(aPower), "%s Hook", GetExtraName(Special));
			return aPower;
		}
	case ENDLESS_HOOK:
		return "Endless Hook";
	case INFINITE_JUMPS:
		return "Infinite Jumps";
	case SPREAD_WEAPON:
		{
			static char aWeapon[64];
			str_format(aWeapon, sizeof(aWeapon), "Spread %s", GetWeaponName(Special));
			return aWeapon;
		}
	case FREEZE_HAMMER:
		return "Freeze Hammer";
	case INVISIBLE:
		return "Invisibility";
	case ITEM:
		{
			static char aItem[64];
			str_format(aItem, sizeof(aItem), "%s%sItem", Special > -3 ? GetWeaponName(Special) : "", Special > -3 ? " " : "");
			return aItem;
		}
	case TELE_WEAPON:
		{
			static char aWeapon[64];
			str_format(aWeapon, sizeof(aWeapon), "Tele %s", GetWeaponName(Special));
			return aWeapon;
		}
	case ALWAYS_TELE_WEAPON:
		return "Always Tele Weapon";
	case DOOR_HAMMER:
		return "Door Hammer";
	case TEE_CONTROL:
		return "Tee Control";
	case SNAKE:
		return "Snake Mode";
	case LOVELY:
		return "Lovely";
	case ROTATING_BALL:
		return "Rotating Ball";
	case EPIC_CIRCLE:
		return "Epic Circle";
	case STAFF_IND:
		return "Staff Indicator";
	case RAINBOW_NAME:
		return "Rainbow Name";
	case CONFETTI:
		return "Confetti";
	case SPARKLE:
		return "Sparkle";
	}
	return "Unknown";
}

int CGameContext::CountConnectedPlayers(bool CountSpectators, bool ExcludeDummies)
{
	int Count = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (((CServer*)Server())->m_aClients[i].m_State != CServer::CClient::STATE_EMPTY)
		{
			if (m_apPlayers[i])
			{
				if (ExcludeDummies && m_apPlayers[i]->m_IsDummy)
					continue;
				if (!CountSpectators && m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)
					continue;
			}
			Count++;
		}
	}
	return Count;
}

bool CGameContext::IsValidHookPower(int HookPower)
{
	return HookPower == HOOK_NORMAL
		|| HookPower == RAINBOW
		|| HookPower == BLOODY
		|| HookPower == ATOM
		|| HookPower == TRAIL;
}

const char *CGameContext::GetScoreModeName(int ScoreMode)
{
	switch (ScoreMode)
	{
	case SCORE_TIME:
		return "Time";
	case SCORE_LEVEL:
		return "Level";
	case SCORE_BLOCK_POINTS:
		return "Block Points";
	case SCORE_BONUS:
		return "No-Bonus Score";
	}
	return "Unknown";
}

const char *CGameContext::GetScoreModeCommand(int ScoreMode)
{
	switch (ScoreMode)
	{
	case SCORE_TIME:
		return "time";
	case SCORE_LEVEL:
		return "level";
	case SCORE_BLOCK_POINTS:
		return "points";
	case SCORE_BONUS:
		return "bonus";
	}
	return "Unknown";
}

const char *CGameContext::GetMinigameName(int Minigame)
{
	switch (Minigame)
	{
	case MINIGAME_NONE:
		return "None";
	case MINIGAME_BLOCK:
		return "Block";
	case MINIGAME_SURVIVAL:
		return "Survival";
	case MINIGAME_INSTAGIB_BOOMFNG:
		return "Instagib Boom FNG";
	case MINIGAME_INSTAGIB_FNG:
		return "Instagib FNG";
	case MINIGAME_1VS1:
		return "1vs1";
	}
	return "Unknown";
}

const char* CGameContext::GetMinigameCommand(int Minigame)
{
	switch (Minigame)
	{
	case MINIGAME_NONE:
		return "none";
	case MINIGAME_BLOCK:
		return "block";
	case MINIGAME_SURVIVAL:
		return "survival";
	case MINIGAME_INSTAGIB_BOOMFNG:
		return "boomfng";
	case MINIGAME_INSTAGIB_FNG:
		return "fng";
	case MINIGAME_1VS1:
		return "1vs1";
	}
	return "unknown";
}

void CGameContext::SetMinigame(int ClientID, int Minigame, bool Force)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (!pPlayer)
		return;

	char aMsg[128];

	// check whether minigame is disabled
	if (Minigame != MINIGAME_NONE && m_aMinigameDisabled[Minigame])
	{
		SendChatTarget(ClientID, "This minigame is disabled");
		return;
	}

	// check if we are already in a minigame
	if (pPlayer->m_Minigame == Minigame)
	{
		// you can't leave when you're not in a minigame
		if (Minigame == MINIGAME_NONE)
			SendChatTarget(ClientID, "You are not in a minigame");
		else
		{
			str_format(aMsg, sizeof(aMsg), "You are already in minigame '%s'", GetMinigameName(Minigame));
			SendChatTarget(ClientID, aMsg);
		}
		return;
	}

	if (!Force && pPlayer->RequestMinigameChange(Minigame))
		return;

	// leave minigame
	if (Minigame == MINIGAME_NONE)
	{
		str_format(aMsg, sizeof(aMsg), "'%s' left the minigame '%s'", Server()->ClientName(ClientID), GetMinigameName(pPlayer->m_Minigame));
		SendChat(-1, CHAT_ALL, -1, aMsg);

		//reset everything
		if (pPlayer->m_Minigame == MINIGAME_SURVIVAL)
		{
			pPlayer->m_Gamemode = pPlayer->m_SavedGamemode = GAMEMODE_DDRACE;
			pPlayer->m_SurvivalState = SURVIVAL_OFFLINE;
			pPlayer->m_ShowName = true;
		}
		else if (pPlayer->m_Minigame == MINIGAME_1VS1)
		{
			Arenas()->OnPlayerLeave(ClientID);
		}
	}
	// join minigame
	else if (!pPlayer->IsMinigame())
	{
		str_format(aMsg, sizeof(aMsg), "'%s' joined the minigame '%s', use '/%s' to join aswell", Server()->ClientName(ClientID), GetMinigameName(Minigame), GetMinigameCommand(Minigame));
		SendChat(-1, CHAT_ALL, -1, aMsg);
		SendChatTarget(ClientID, "Say '/leave' to join the normal area again");

		// Save character stats to reload them after leaving
		pPlayer->SaveMinigameTee();

		//set minigame required stuff
		((CGameControllerDDRace *)m_pController)->m_Teams.SetForceCharacterTeam(ClientID, 0);

		if (Minigame == MINIGAME_SURVIVAL)
		{
			pPlayer->m_Gamemode = pPlayer->m_SavedGamemode = GAMEMODE_VANILLA;
			pPlayer->m_SurvivalState = SURVIVAL_LOBBY;
		}
		else if (Minigame == MINIGAME_1VS1)
		{
			SendChatTarget(ClientID, "Type '/1vs1 <playername>' to start a fight with someone");
			SendChatTarget(ClientID, "For custom scorelimits or a kill-border use '/1vs1 <playername> <scorelimit> <killborder>'");
		}
	}
	else
	{
		// you can't join minigames if you are already in another mingame
		SendChatTarget(ClientID, "You have to leave first in order to join another minigame");
		return;
	}

	pPlayer->KillCharacter(WEAPON_MINIGAME_CHANGE);
	pPlayer->m_Minigame = Minigame;
	pPlayer->SetPlaying();
	pPlayer->m_LastMovementTick = Server()->Tick();

	UpdateHidePlayers();

	// Update the gameinfo, add or remove GAMEFLAG_RACE as wanted (in minigames we disable it to properly show the scores)
	m_pController->UpdateGameInfo(ClientID);
}

void CGameContext::SurvivalTick()
{
	// if there are no spawn tiles, we cant play the game
	if (!m_aMinigameDisabled[MINIGAME_SURVIVAL] && (!Collision()->TileUsed(TILE_SURVIVAL_LOBBY) || !Collision()->TileUsed(TILE_SURVIVAL_SPAWN) || !Collision()->TileUsed(TILE_SURVIVAL_DEATHMATCH)))
	{
		m_aMinigameDisabled[MINIGAME_SURVIVAL] = true;
		return;
	}

	// set the mode to lobby, if the game is offline and there are now players
	if (m_SurvivalGameState == SURVIVAL_OFFLINE)
		m_SurvivalGameState = SURVIVAL_LOBBY;

	// check if we dont have any players in the current state
	if (!CountSurvivalPlayers(m_SurvivalGameState))
	{
		m_SurvivalGameState = SURVIVAL_OFFLINE;
		m_SurvivalBackgroundState = SURVIVAL_OFFLINE;
		return;
	}

	// decrease the tick at any time if it exists (its a timer)
	if (m_SurvivalTick)
		m_SurvivalTick--;

	int Remaining = m_SurvivalTick / Server()->TickSpeed();

	// main part
	char aBuf[128];

	if (m_SurvivalGameState > SURVIVAL_LOBBY && CountSurvivalPlayers(m_SurvivalGameState) == 1)
	{
		// if there is only one survival player left, before the time is over, we have a winner
		m_SurvivalWinner = GetRandomSurvivalPlayer(m_SurvivalGameState);

		if (m_apPlayers[m_SurvivalWinner])
		{
			str_format(aBuf, sizeof(aBuf), "The winner is '%s'", Server()->ClientName(m_SurvivalWinner));
			SendSurvivalBroadcast(aBuf);

			// send message to winner
			SendChatTarget(m_SurvivalWinner, "You are the winner");

			// add a win to the winners' accounts
			if (m_apPlayers[m_SurvivalWinner]->GetAccID() >= ACC_START)
				m_Accounts[m_apPlayers[m_SurvivalWinner]->GetAccID()].m_SurvivalWins++;
			m_apPlayers[m_SurvivalWinner]->GiveXP(250, "for winning a survival round");
		}

		// sending back to lobby
		m_SurvivalGameState = SURVIVAL_LOBBY;
		m_SurvivalBackgroundState = SURVIVAL_OFFLINE;
		SetPlayerSurvivalState(SURVIVAL_LOBBY);
	}


	// checking for foreground states
	switch (m_SurvivalGameState)
	{
		case SURVIVAL_LOBBY:
		{
			// check whether we have something running in the background
			if (m_SurvivalBackgroundState != SURVIVAL_OFFLINE)
				break;

			// count the lobby players, if they are fewer than the minimum amount, set the waiting mode in the background
			if (CountSurvivalPlayers(SURVIVAL_LOBBY) < Config()->m_SvSurvivalMinPlayers)
			{
				m_SurvivalBackgroundState = BACKGROUND_LOBBY_WAITING;
			}
			// if we are more than the minimum players waiting, the countdown will start in the background (30 seconds until the game starts)
			else
			{
				m_SurvivalBackgroundState = BACKGROUND_LOBBY_COUNTDOWN;
				m_SurvivalTick = Server()->TickSpeed() * (Config()->m_SvSurvivalLobbyCountdown + 1);
			}
			break;
		}

		case SURVIVAL_PLAYING:
		{
			// the game is running
			break;
		}

		case SURVIVAL_DEATHMATCH:
		{
			if (!m_SurvivalTick)
			{
				// if the deathmatch is over, reset the survival game, sending players back to lobby
				if (CountSurvivalPlayers(SURVIVAL_DEATHMATCH) > 1)
					SendSurvivalBroadcast("There is no winner this round!");
				m_SurvivalGameState = SURVIVAL_OFFLINE;
				m_SurvivalBackgroundState = BACKGROUND_IDLE;
				SetPlayerSurvivalState(SURVIVAL_LOBBY);
			}
			else
			{
				// before its over, send some broadcasts until its finally over
				if (Server()->Tick() % 50 == 0)
				{
					if (Remaining % 30 == 0 || Remaining <= 10)
					{
						str_format(aBuf, sizeof(aBuf), "Deathmatch will end in %d seconds", Remaining);
						SendSurvivalBroadcast(aBuf, true);
					}
				}
			}
			break;
		}
	}

	// checking for background states
	switch (m_SurvivalBackgroundState)
	{
		case BACKGROUND_LOBBY_WAITING:
		{
			// send the waiting for players broadcast to all survival players
			if (Server()->Tick() % 50 == 0)
			{
				str_format(aBuf, sizeof(aBuf), "[%d/%d] players to start a round", CountSurvivalPlayers(SURVIVAL_LOBBY), Config()->m_SvSurvivalMinPlayers);
				SendSurvivalBroadcast(aBuf, false, false);
			}
			break;
		}

		case BACKGROUND_LOBBY_COUNTDOWN:
		{
			if (!m_SurvivalTick)
			{
				// timer is over, the round starts
				str_format(aBuf, sizeof(aBuf), "Round started, you have %d minutes to kill each other", Config()->m_SvSurvivalRoundTime);
				SendSurvivalBroadcast(aBuf);

				// set a new tick, this time for the round to end after its up
				m_SurvivalTick = Server()->TickSpeed() * 60 * Config()->m_SvSurvivalRoundTime;
				// set the foreground state
				m_SurvivalGameState = SURVIVAL_PLAYING;
				// change background state
				m_SurvivalBackgroundState = BACKGROUND_DEATHMATCH_COUNTDOWN;
				// set the player's survival state
				SetPlayerSurvivalState(SURVIVAL_PLAYING);
			}
			else if (CountSurvivalPlayers(SURVIVAL_LOBBY) >= Config()->m_SvSurvivalMinPlayers)
			{
				// if we are more than the minimum players, the countdown will start
				if (Server()->Tick() % 50 == 0)
				{
					str_format(aBuf, sizeof(aBuf), "Round will start in %d seconds", Remaining);
					SendSurvivalBroadcast(aBuf, Remaining <= 10, false);
				}
			}
			// if someone left the lobby, the countdown stops and we return to the lobby state (waiting for players again)
			else
			{
				SendSurvivalBroadcast("Start failed, too few players");
				m_SurvivalGameState = SURVIVAL_LOBBY;
				m_SurvivalBackgroundState = SURVIVAL_OFFLINE;
			}
			break;
		}

		case BACKGROUND_DEATHMATCH_COUNTDOWN:
		{
			if (!m_SurvivalTick)
			{
				// deathmatch countdown is over, we will start the deathmatch now
				SendSurvivalBroadcast("Deathmatch started, you have 2 minutes to kill the last survivors");

				//sending to deathmatch arena
				m_SurvivalGameState = SURVIVAL_DEATHMATCH;
				SetPlayerSurvivalState(SURVIVAL_DEATHMATCH);
				m_SurvivalBackgroundState = BACKGROUND_IDLE;

				// deathmatch will be 2 minutes
				m_SurvivalTick = Server()->TickSpeed() * 60 * Config()->m_SvSurvivalDeathmatchTime;
			}
			else
			{
				// printing broadcast until deathmatch starts
				if (Server()->Tick() % 50 == 0)
				{
					if (Remaining % 60 == 0 || Remaining == 30 || Remaining <= 10)
					{
						str_format(aBuf, sizeof(aBuf), "Deathmatch will start in %d %s%s", Remaining > 30 ? Remaining / 60 : Remaining, (Remaining % 60 == 0 && Remaining != 0) ? "minute" : "second", (Remaining == 1 || Remaining == 60) ? "" : "s");
						SendSurvivalBroadcast(aBuf, true);
					}
				}
			}
			break;
		}
	}
}

int CGameContext::CountSurvivalPlayers(int State)
{
	int count = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (m_apPlayers[i] && m_apPlayers[i]->m_Minigame == MINIGAME_SURVIVAL && (m_apPlayers[i]->m_SurvivalState == State || State == -1))
			count++;
	return count;
}

void CGameContext::SetPlayerSurvivalState(int State)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (m_apPlayers[i] && m_apPlayers[i]->m_Minigame == MINIGAME_SURVIVAL)
		{
			// only send playing people to deathmatch
			if (State == SURVIVAL_DEATHMATCH && m_apPlayers[i]->m_SurvivalState != SURVIVAL_PLAYING)
				continue;

			// unset spectator mode and pause
			m_apPlayers[i]->SetPlaying();
			// kill the character
			m_apPlayers[i]->KillCharacter(WEAPON_GAME);
			// set its new survival state
			m_apPlayers[i]->m_SurvivalState = State;
			// hide name in every state except lobby
			m_apPlayers[i]->m_ShowName = State == SURVIVAL_LOBBY;
		}
}

int CGameContext::GetRandomSurvivalPlayer(int State, int NotThis)
{
	std::vector<int> SurvivalPlayers;
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (i != NotThis && m_apPlayers[i] && m_apPlayers[i]->m_Minigame == MINIGAME_SURVIVAL && (m_apPlayers[i]->m_SurvivalState == State || State == -1))
			SurvivalPlayers.push_back(i);
	if (SurvivalPlayers.size())
	{
		int Rand = rand() % SurvivalPlayers.size();
		return SurvivalPlayers[Rand];
	}
	return -1;
}

void CGameContext::SendSurvivalBroadcast(const char *pMsg, bool Sound, bool IsImportant)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (m_apPlayers[i] && m_apPlayers[i]->m_Minigame == MINIGAME_SURVIVAL)
		{
			if (Sound)
				CreateSoundPlayer(SOUND_HOOK_NOATTACH, i);

			// show money broadcast instead of the wanted one if we are on a money tile
			if (m_apPlayers[i]->GetCharacter() && m_apPlayers[i]->GetCharacter()->m_MoneyTile)
				continue;
			SendBroadcast(pMsg, i, IsImportant);
		}
	}
}

void CGameContext::InstagibTick(int Type)
{
	Type = Type == 0 ? MINIGAME_INSTAGIB_BOOMFNG : MINIGAME_INSTAGIB_FNG;
	m_aMinigameDisabled[Type] = true;

	// if there are no spawn tiles, we cant play the game
	if (!m_aMinigameDisabled[Type] && !Collision()->TileUsed(Type == MINIGAME_INSTAGIB_BOOMFNG ? ENTITY_SPAWN_RED : ENTITY_SPAWN_BLUE))
	{
		m_aMinigameDisabled[Type] = true;
		return;
	}

	//m_apPlayers[Winner]->GiveXP(250, "for winning an instagib round");

	// add instagib here
}
