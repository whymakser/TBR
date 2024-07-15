// made by fokkonaut

#include "votingmenu.h"
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/teams.h>
#include <engine/shared/config.h>
#include <game/server/gamemodes/DDRace.h>
#include <engine/server/server.h>

CGameContext *CVotingMenu::GameServer() const { return m_pGameServer; }
IServer *CVotingMenu::Server() const { return GameServer()->Server(); }

// Font: https://fsymbols.com/generators/smallcaps/
// Not a normal space: https://www.cogsci.ed.ac.uk/~richard/utf-8.cgi?input=%E2%80%8A&mode=char
static const char *PLACEHOLDER_DESC = " ";
// Acc
static const char *COLLAPSE_HEADER_ACC_INFO = "Aᴄᴄᴏᴜɴᴛ Iɴғᴏ";
static const char *COLLAPSE_HEADER_ACC_STATS = "Aᴄᴄᴏᴜɴᴛ Sᴛᴀᴛs";
static const char *COLLAPSE_HEADER_PLOT_INFO = "Pʟᴏᴛ";
// Acc Misc
static const char *ACC_MISC_SILENTFARM = "Silent Farm";
static const char *ACC_MISC_NINJAJETPACK = "Ninjajetpack";
// Acc Plot
static const char *ACC_PLOT_SPAWN = "Plot Spawn";
static const char *ACC_PLOT_EDIT = "Edit Plot";
static const char *ACC_PLOT_CLEAR = "Clear Plot";
// VIP
static const char *ACC_VIP_RAINBOW = "Rainbow";
static const char *ACC_VIP_BLOODY = "Bloody";
static const char *ACC_VIP_ATOM = "Atom";
static const char *ACC_VIP_TRAIL = "Trail";
static const char *ACC_VIP_SPREADGUN = "Spread Gun";
// VIP Plus
static const char *ACC_VIP_PLUS_RAINBOWHOOK = "Rainbow Hook";
static const char *ACC_VIP_PLUS_ROTATINGBALL = "Rotating Ball";
static const char *ACC_VIP_PLUS_EPICCIRCLE = "Epic Circle";
static const char *ACC_VIP_PLUS_LOVELY = "Lovely";
static const char *ACC_VIP_PLUS_RAINBOWNAME = "Rainbow Name";
// Misc
static const char *MISC_HIDEDRAWINGS = "Hide Drawings";
static const char *MISC_WEAPONINDICATOR = "Weapon Indicator";
static const char *MISC_ZOOMCURSOR = "Zoom Cursor";
static const char *MISC_RESUMEMOVED = "Resume Moved";
static const char *MISC_HIDEBROADCASTS = "Hide Broadcasts";

void CVotingMenu::Init(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;

	m_NumCollapseEntries = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		Reset(i);
	}

	str_copy(m_aPages[PAGE_VOTES].m_aName, "Vᴏᴛᴇs", sizeof(m_aPages[PAGE_VOTES].m_aName));
	str_copy(m_aPages[PAGE_ACCOUNT].m_aName, "Aᴄᴄᴏᴜɴᴛ", sizeof(m_aPages[PAGE_ACCOUNT].m_aName));
	str_copy(m_aPages[PAGE_MISCELLANEOUS].m_aName, "Mɪsᴄᴇʟʟᴀɴᴇᴏᴜs", sizeof(m_aPages[PAGE_MISCELLANEOUS].m_aName));

	for (int i = 0; i < NUM_PAGES; i++)
		for (int j = 0; j < NUM_PAGE_MAX_OPTIONS; j++)
			m_aPages[i].m_aaTempDesc[j][0] = 0;

	// Don't add the placeholders again when the map changes. The voteOptionHeap in CGameContext is not destroyed, so we do not have to add placeholders another time
	if (!m_Initialized)
	{
		AddPlaceholderVotes();
		m_Initialized = true;
	}
}

void CVotingMenu::Reset(int ClientID)
{
	m_aClients[ClientID].m_Page = PAGE_VOTES;
	m_aClients[ClientID].m_LastVoteMsg = 0;
	m_aClients[ClientID].m_NextVoteSendTick = 0;
	m_aClients[ClientID].m_ShowAccountInfo = false;
	m_aClients[ClientID].m_ShowPlotInfo = false;
	m_aClients[ClientID].m_ShowAccountStats = false;
	m_aClients[ClientID].m_PrevStats.m_Flags = 0;
	m_aClients[ClientID].m_PrevStats.m_Minigame = MINIGAME_NONE;
	m_aClients[ClientID].m_PrevStats.m_ScoreMode = GameServer()->Config()->m_SvDefaultScoreMode;
	m_aClients[ClientID].m_PrevStats.m_JailTime = 0;
	m_aClients[ClientID].m_PrevStats.m_EscapeTime = 0;
}

void CVotingMenu::AddPlaceholderVotes()
{
	for (int i = 0; i < NUM_PAGES + NUM_PAGE_SEPERATORS; i++)
	{
		GameServer()->AddVote("placeholder_voting_menu", "info");
	}
}

const char *CVotingMenu::GetPageDescription(int ClientID, int Page)
{
	static char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%s %s", Page == GetPage(ClientID) ? "☒" : "☐", m_aPages[Page].m_aName);
	return aBuf;
}

bool CVotingMenu::SetPage(int ClientID, int Page)
{
	if (Page < 0 || Page >= NUM_PAGES || Page == GetPage(ClientID))
		return false;

	m_aClients[ClientID].m_Page = Page;
	// Update dummy page, because otherwise dummy could be in vote page, so if you switch page, swap to dummy, and vote smth, it'll actually call a vote
	int DummyID = Server()->GetDummy(ClientID);
	if (DummyID != -1)
		m_aClients[DummyID].m_Page = Page;

	// Don't process normal votes, e.g. when another vote get's added
	if (Page != PAGE_VOTES)
	{
		GameServer()->m_apPlayers[ClientID]->m_SendVoteIndex = -1;
		if (DummyID != -1)
			GameServer()->m_apPlayers[DummyID]->m_SendVoteIndex = -1;
	}
	SendPageVotes(ClientID);
	return true;
}

void CVotingMenu::OnProgressVoteOptions(int ClientID, CMsgPacker *pPacker, int *pCurIndex, CVoteOptionServer **ppCurrent)
{
	// Just started sending votes, add the pages
	bool IsPageVotes = GetPage(ClientID) == PAGE_VOTES;
	int Index = IsPageVotes ? GameServer()->m_apPlayers[ClientID]->m_SendVoteIndex : 0;
	if (IsPageVotes && (Index < 0 || Index >= NUM_OPTION_START_OFFSET))
		return;

	// Depending on votesPerTick... That's why we start with an offset maybe
	for (int i = Index; i < NUM_OPTION_START_OFFSET; i++)
	{
		if (i < NUM_PAGES)
		{
			pPacker->AddString(GetPageDescription(ClientID, i), VOTE_DESC_LENGTH);
		}
		else
		{
			pPacker->AddString(PLACEHOLDER_DESC, VOTE_DESC_LENGTH);
		}
		if (ppCurrent && *ppCurrent)
			*ppCurrent = (*ppCurrent)->m_pNext;
		(*pCurIndex)++;
	}
}

bool CVotingMenu::OnMessage(int ClientID, CNetMsg_Cl_CallVote *pMsg)
{
	if (m_aClients[ClientID].m_LastVoteMsg + Server()->TickSpeed() / 2 > Server()->Tick())
		return true;
	m_aClients[ClientID].m_LastVoteMsg = Server()->Tick();

	if(!str_utf8_check(pMsg->m_Type) || !str_utf8_check(pMsg->m_Value) || !str_utf8_check(pMsg->m_Reason))
	{
		return true;
	}

	bool VoteOption = str_comp_nocase(pMsg->m_Type, "option") == 0;
	//bool VoteKick = str_comp_nocase(pMsg->m_Type, "kick") == 0;
	//bool VoteSpectate = str_comp_nocase(pMsg->m_Type, "spectate") == 0;

	int WantedPage = -1;
	if (VoteOption)
	{
		for (int i = 0; i < NUM_PAGES; i++)
		{
			if (str_comp(pMsg->m_Value, GetPageDescription(ClientID, i)) == 0)
			{
				WantedPage = i;
				break;
			}
		}
	}

	if (WantedPage != -1)
	{
		SetPage(ClientID, WantedPage);
		return true;
	}
	if (GetPage(ClientID) == PAGE_VOTES)
	{
		// placeholder
		if (!pMsg->m_Value[0])
			return true;
		// Process normal voting
		return false;
	}

	PrepareTempDescriptions(ClientID);

	const char *pDesc = 0;
	for (int i = 0; i < NUM_PAGE_MAX_OPTIONS; i++)
	{
		const char *pFullDesc = str_skip_voting_menu_prefixes(m_aPages[GetPage(ClientID)].m_aaTempDesc[i]);
		if (!pFullDesc || !pFullDesc[0])
			continue;

		const char *pValue = str_skip_voting_menu_prefixes(pMsg->m_Value);
		if (pValue && pValue[0] && str_comp(pFullDesc, pValue) == 0)
		{
			pDesc = pFullDesc;
			break;
		}
	}

	// shouldnt happen, silent ignore
	if (!pDesc)
		return true;

	// Process voting option
	if (OnMessageSuccess(ClientID, pDesc))
	{
		SendPageVotes(ClientID);
	}
	return true;
}

bool CVotingMenu::OnMessageSuccess(int ClientID, const char *pDesc)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	CCharacter *pChr = pPlayer->GetCharacter();

	if (GetPage(ClientID) == PAGE_ACCOUNT)
	{
		// Acc info
		if (IsCollapseHeader(pDesc, COLLAPSE_HEADER_ACC_INFO))
		{
			m_aClients[ClientID].m_ShowAccountInfo = !m_aClients[ClientID].m_ShowAccountInfo;
			return true;
		}
		if (IsCollapseHeader(pDesc, COLLAPSE_HEADER_ACC_STATS))
		{
			m_aClients[ClientID].m_ShowAccountStats = !m_aClients[ClientID].m_ShowAccountStats;
			return true;
		}
		if (IsCollapseHeader(pDesc, COLLAPSE_HEADER_PLOT_INFO))
		{
			m_aClients[ClientID].m_ShowPlotInfo = !m_aClients[ClientID].m_ShowPlotInfo;
			return true;
		}

		// Acc Misc
		if (IsOption(pDesc, ACC_MISC_SILENTFARM))
		{
			pPlayer->SetSilentFarm(!pPlayer->m_SilentFarm);
			return true;
		}
		if (IsOption(pDesc, ACC_MISC_NINJAJETPACK))
		{
			pPlayer->SetNinjaJetpack(!pPlayer->m_NinjaJetpack);
			return true;
		}

		// Plot
		if (IsOption(pDesc, ACC_PLOT_SPAWN))
		{
			pPlayer->SetPlotSpawn(!pPlayer->m_PlotSpawn);
			return true;
		}
		if (IsOption(pDesc, ACC_PLOT_EDIT))
		{
			pPlayer->StartPlotEdit();
			return true;
		}
		if (IsOption(pDesc, ACC_PLOT_CLEAR))
		{
			pPlayer->ClearPlot();
			return true;
		}

		// VIP
		if (IsOption(pDesc, ACC_VIP_RAINBOW))
		{
			if (pChr) pChr->OnRainbowVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_BLOODY))
		{
			if (pChr) pChr->OnBloodyVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_ATOM))
		{
			if (pChr) pChr->OnAtomVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_TRAIL))
		{
			if (pChr) pChr->OnTrailVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_SPREADGUN))
		{
			if (pChr) pChr->OnSpreadGunVIP();
			return true;
		}
		// VIP Plus
		else if (IsOption(pDesc, ACC_VIP_PLUS_RAINBOWHOOK))
		{
			if (pChr) pChr->OnRainbowHookVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_PLUS_ROTATINGBALL))
		{
			if (pChr) pChr->OnRotatingBallVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_PLUS_EPICCIRCLE))
		{
			if (pChr) pChr->OnEpicCircleVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_PLUS_LOVELY))
		{
			if (pChr) pChr->OnLovelyVIP();
			return true;
		}
		else if (IsOption(pDesc, ACC_VIP_PLUS_RAINBOWNAME))
		{
			if (pChr) pChr->OnRainbowNameVIP();
			return true;
		}
	}
	else if (GetPage(ClientID) == PAGE_MISCELLANEOUS)
	{
		if (IsOption(pDesc, MISC_HIDEDRAWINGS))
		{
			pPlayer->SetHideDrawings(!pPlayer->m_HideDrawings);
			return true;
		}
		else if (IsOption(pDesc, MISC_WEAPONINDICATOR))
		{
			pPlayer->SetWeaponIndicator(!pPlayer->m_WeaponIndicator);
			return true;
		}
		else if (IsOption(pDesc, MISC_ZOOMCURSOR))
		{
			pPlayer->SetZoomCursor(!pPlayer->m_ZoomCursor);
			return true;
		}
		else if (IsOption(pDesc, MISC_RESUMEMOVED))
		{
			pPlayer->SetResumeMoved(!pPlayer->m_ResumeMoved);
			return true;
		}
		else if (IsOption(pDesc, MISC_HIDEBROADCASTS))
		{
			pPlayer->SetHideBroadcasts(!pPlayer->m_HideBroadcasts);
			return true;
		}
		else
		{
			for (int i = -1; i < CServer::NUM_MAP_DESIGNS; i++)
			{
				const char *pDesign = Server()->GetMapDesignName(i);
				if (!pDesign[0] || str_comp(pDesc, pDesign) != 0)
					continue;
				Server()->ChangeMapDesign(ClientID, pDesign);
				return true;
			}

			for (int i = -1; i < NUM_MINIGAMES; i++)
			{
				const char *pMinigame = i == -1 ? "No minigame" : GameServer()->GetMinigameName(i);
				if (str_comp(pDesc, pMinigame) != 0)
					continue;
				GameServer()->SetMinigame(ClientID, i);
				return true;
			}

			for (int i = 0; i < NUM_SCORE_MODES; i++)
			{
				if (i == SCORE_BONUS && !GameServer()->Config()->m_SvAllowBonusScoreMode)
					continue;
				if (str_comp(pDesc, GameServer()->GetScoreModeName(i)) != 0)
					continue;
				pPlayer->ChangeScoreMode(i);
				return true;
			}
		}
	}

	return false;
}

int CVotingMenu::PrepareTempDescriptions(int ClientID)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	if (!pPlayer)
		return 0;

	m_NumCollapseEntries = 0;
	int NumOptions = 0;

	if (pPlayer->m_HideBroadcasts)
	{
		char aBuf[VOTE_DESC_LENGTH];
		bool DoAnnouncement = false;
		if (pPlayer->m_JailTime)
		{
			DoAnnouncement = true;
			str_format(aBuf, sizeof(aBuf), "You are arrested for %lld seconds", pPlayer->m_JailTime / Server()->TickSpeed());
		}
		else if (pPlayer->m_EscapeTime)
		{
			DoAnnouncement = true;
			str_format(aBuf, sizeof(aBuf), "You are wanted by the police for %lld seconds", pPlayer->m_EscapeTime / Server()->TickSpeed());
		}

		if (DoAnnouncement)
		{
			const int Page = GetPage(ClientID);
			DoLineText(Page, &NumOptions, aBuf, BULLET_POINT);
			DoLineSeperator(Page, &NumOptions);
		}
	}

	if (GetPage(ClientID) == PAGE_ACCOUNT)
	{
		DoPageAccount(ClientID, &NumOptions);
	}
	else if (GetPage(ClientID) == PAGE_MISCELLANEOUS)
	{
		DoPageMiscellaneous(ClientID, &NumOptions);
	}

	return NumOptions;
}

void CVotingMenu::DoPageAccount(int ClientID, int *pNumOptions)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	CCharacter *pChr = pPlayer->GetCharacter();
	const int Page = GetPage(ClientID);

	char aBuf[128];
	time_t tmp;
	int AccID = pPlayer->GetAccID();
	CGameContext::AccountInfo *pAccount = &GameServer()->m_Accounts[AccID];

	if (AccID < ACC_START)
	{
		DoLineText(Page, pNumOptions, "You're not logged in.");
		DoLineText(Page, pNumOptions, "Use '/login' to see more information about your account.");
		return;
	}

	bool ShowEuros = GameServer()->Config()->m_SvEuroMode || pAccount->m_Euros > 0;
	bool ShowPortalDate = GameServer()->Config()->m_SvPortalRifleShop || pAccount->m_PortalRifle;
	if (DoLineCollapse(Page, pNumOptions, COLLAPSE_HEADER_ACC_INFO, m_aClients[ClientID].m_ShowAccountInfo, 5 + (int)ShowEuros + (int)ShowPortalDate))
	{
		str_format(aBuf, sizeof(aBuf), "Account Name: %s", pAccount->m_Username);
		DoLineText(Page, pNumOptions, aBuf);

		if (pAccount->m_RegisterDate != 0)
		{
			tmp = pAccount->m_RegisterDate;
			str_format(aBuf, sizeof(aBuf), "Registered on: %s", GameServer()->GetDate(tmp, false));
			DoLineText(Page, pNumOptions, aBuf);
		}
		else
			DoLineText(Page, pNumOptions, "Registered on: unknown");

		if (ShowEuros)
		{
			str_format(aBuf, sizeof(aBuf), "Euros: %d", pAccount->m_Euros);
			DoLineText(Page, pNumOptions, aBuf);
		}

		if (pAccount->m_VIP)
		{
			tmp = pAccount->m_ExpireDateVIP;
			str_format(aBuf, sizeof(aBuf), "VIP%s: until %s", pAccount->m_VIP == VIP_PLUS ? "+" : "", GameServer()->GetDate(tmp));
			DoLineText(Page, pNumOptions, aBuf);
		}
		else
			DoLineText(Page, pNumOptions, "VIP: not bought");

		if (pAccount->m_PortalRifle)
		{
			tmp = pAccount->m_ExpireDatePortalRifle;
			str_format(aBuf, sizeof(aBuf), "Portal Rifle: until %s", GameServer()->GetDate(tmp));
			DoLineText(Page, pNumOptions, aBuf);
		}
		else if (GameServer()->Config()->m_SvPortalRifleShop)
			DoLineText(Page, pNumOptions, "Portal Rifle: not bought");

		str_format(aBuf, sizeof(aBuf), "Contact: %s", pAccount->m_aContact);
		DoLineText(Page, pNumOptions, aBuf);
		str_format(aBuf, sizeof(aBuf), "E-Mail: %s", pAccount->m_aEmail);
		DoLineText(Page, pNumOptions, aBuf);

		// This does not count to NumEntries anymore, s_NumCollapseEntries is 0 by now again. We wanna add a seperator only if this thing is opened
		DoLineSeperator(Page, pNumOptions);
	}

	int PlotID = GameServer()->GetPlotID(AccID);
	if (DoLineCollapse(Page, pNumOptions, COLLAPSE_HEADER_ACC_STATS, m_aClients[ClientID].m_ShowAccountStats, 12))
	{
		str_format(aBuf, sizeof(aBuf), "Level [%d]", pAccount->m_Level);
		DoLineText(Page, pNumOptions, aBuf);
		if (pChr && pChr->m_aLineExp[0] != '\0')
		{
			DoLineText(Page, pNumOptions, pChr->m_aLineExp);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "XP [%lld/%lld]", pAccount->m_XP, GameServer()->GetNeededXP(pAccount->m_Level));
			DoLineText(Page, pNumOptions, aBuf);
		}
		str_format(aBuf, sizeof(aBuf), "Bank [%lld]", pAccount->m_Money);
		DoLineText(Page, pNumOptions, aBuf);
		if (pChr && pChr->m_aLineMoney[0] != '\0')
		{
			DoLineText(Page, pNumOptions, pChr->m_aLineMoney);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Wallet [%lld]", pPlayer->GetWalletMoney());
			DoLineText(Page, pNumOptions, aBuf);
		}
		str_format(aBuf, sizeof(aBuf), "Police [%d]", pAccount->m_PoliceLevel);
		DoLineText(Page, pNumOptions, aBuf);

		DoLineTextSubheader(Page, pNumOptions, "Cᴏʟʟᴇᴄᴛᴀʙʟᴇs");
		str_format(aBuf, sizeof(aBuf), "Taser Battery [%d]", pAccount->m_TaserBattery);
		DoLineText(Page, pNumOptions, aBuf);
		str_format(aBuf, sizeof(aBuf), "Portal Battery [%d]", pAccount->m_PortalBattery);
		DoLineText(Page, pNumOptions, aBuf);

		DoLineTextSubheader(Page, pNumOptions, "Bʟᴏᴄᴋ");
		str_format(aBuf, sizeof(aBuf), "Points: %d", pAccount->m_BlockPoints);
		DoLineText(Page, pNumOptions, aBuf);
		str_format(aBuf, sizeof(aBuf), "Kills: %d", pAccount->m_Kills);
		DoLineText(Page, pNumOptions, aBuf);
		str_format(aBuf, sizeof(aBuf), "Deaths: %d", pAccount->m_Deaths);
		DoLineText(Page, pNumOptions, aBuf);

		// only when we have the next category below
		if (PlotID >= PLOT_START)
		{
			// This does not count to NumEntries anymore, s_NumCollapseEntries is 0 by now again. We wanna add a seperator only if this thing is opened
			DoLineSeperator(Page, pNumOptions);
		}
	}

	if (PlotID >= PLOT_START)
	{
		char aPlotHeader[32];
		str_format(aPlotHeader, sizeof(aPlotHeader), "%s %d", COLLAPSE_HEADER_PLOT_INFO, PlotID);
		if (DoLineCollapse(Page, pNumOptions, aPlotHeader, m_aClients[ClientID].m_ShowPlotInfo, 4))
		{
			str_format(aBuf, sizeof(aBuf), "Rented until: %s", GameServer()->GetDate(GameServer()->m_aPlots[PlotID].m_ExpireDate));
			DoLineText(Page, pNumOptions, aBuf, BULLET_POINT);

			DoLineToggleOption(Page, pNumOptions, ACC_PLOT_SPAWN, pPlayer->m_PlotSpawn);
			DoLineText(Page, pNumOptions, ACC_PLOT_EDIT, BULLET_ARROW);
			DoLineText(Page, pNumOptions, ACC_PLOT_CLEAR, BULLET_ARROW);
		}
	}

	DoLineSeperator(Page, pNumOptions);
	DoLineTextSubheader(Page, pNumOptions, "Oᴘᴛɪᴏɴs");
	DoLineToggleOption(Page, pNumOptions, ACC_MISC_SILENTFARM, pPlayer->m_SilentFarm);
	if (pAccount->m_Ninjajetpack)
	{
		DoLineToggleOption(Page, pNumOptions, ACC_MISC_NINJAJETPACK, pPlayer->m_NinjaJetpack);
	}

	if (pAccount->m_VIP)
	{
		DoLineSeperator(Page, pNumOptions);
		DoLineTextSubheader(Page, pNumOptions, pAccount->m_VIP == VIP_PLUS ? "ᴠɪᴘ+" : "ᴠɪᴘ");
		DoLineToggleOption(Page, pNumOptions, ACC_VIP_RAINBOW, (pChr && pChr->m_Rainbow) || pPlayer->m_InfRainbow);
		DoLineToggleOption(Page, pNumOptions, ACC_VIP_BLOODY, pChr && (pChr->m_Bloody || pChr->m_StrongBloody));
		DoLineToggleOption(Page, pNumOptions, ACC_VIP_ATOM, pChr && pChr->m_Atom);
		DoLineToggleOption(Page, pNumOptions, ACC_VIP_TRAIL, pChr && pChr->m_Trail);
		DoLineToggleOption(Page, pNumOptions, ACC_VIP_SPREADGUN, pChr && pChr->m_aSpreadWeapon[WEAPON_GUN]);

		if (pAccount->m_VIP == VIP_PLUS)
		{
			DoLineToggleOption(Page, pNumOptions, ACC_VIP_PLUS_RAINBOWHOOK, pChr && pChr->m_HookPower == RAINBOW);
			DoLineToggleOption(Page, pNumOptions, ACC_VIP_PLUS_ROTATINGBALL, pChr && pChr->m_RotatingBall);
			DoLineToggleOption(Page, pNumOptions, ACC_VIP_PLUS_EPICCIRCLE, pChr && pChr->m_EpicCircle);
			DoLineToggleOption(Page, pNumOptions, ACC_VIP_PLUS_LOVELY, pChr && pChr->m_Lovely);
			DoLineToggleOption(Page, pNumOptions, ACC_VIP_PLUS_RAINBOWNAME, pPlayer->m_RainbowName);
		}
	}
}

void CVotingMenu::DoPageMiscellaneous(int ClientID, int *pNumOptions)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	int Page = GetPage(ClientID);

	DoLineTextSubheader(Page, pNumOptions, "Oᴘᴛɪᴏɴs");
	DoLineToggleOption(Page, pNumOptions, MISC_HIDEDRAWINGS, pPlayer->m_HideDrawings);
	DoLineToggleOption(Page, pNumOptions, MISC_WEAPONINDICATOR, pPlayer->m_WeaponIndicator);
	DoLineToggleOption(Page, pNumOptions, MISC_ZOOMCURSOR, pPlayer->m_ZoomCursor);
	DoLineToggleOption(Page, pNumOptions, MISC_RESUMEMOVED, pPlayer->m_ResumeMoved);
	DoLineToggleOption(Page, pNumOptions, MISC_HIDEBROADCASTS, pPlayer->m_HideBroadcasts);

	std::vector<const char *> vpDesigns;
	for (int i = -1; i < CServer::NUM_MAP_DESIGNS; i++)
	{
		const char *pDesign = Server()->GetMapDesignName(i);
		if (pDesign[0])
		{
			vpDesigns.push_back(pDesign);
		}
	}

	// Default is always there, thats why 1
	if (vpDesigns.size() > 1)
	{
		DoLineSeperator(Page, pNumOptions);
		DoLineTextSubheader(Page, pNumOptions, "Dᴇsɪɢɴs");
		// I have no idea what happens when a design has the same name as another vote, but it'll probably take the first and it'll be bugged out. try to avoid that xd
		const char *pOwnDesign = Server()->GetMapDesign(ClientID);
		for (unsigned int i = 0; i < vpDesigns.size(); i++)
		{
			bool CurDesign = str_comp(pOwnDesign, vpDesigns[i]) == 0;
			DoLineToggleOption(Page, pNumOptions, vpDesigns[i], CurDesign);
		}
	}

	DoLineSeperator(Page, pNumOptions);
	DoLineTextSubheader(Page, pNumOptions, "Mɪɴɪɢᴀᴍᴇs");
	for (int i = -1; i < NUM_MINIGAMES; i++)
	{
		if (i != -1 && GameServer()->m_aMinigameDisabled[i])
			continue;
		bool CurMinigame = i == pPlayer->m_Minigame;
		const char *pMinigame = i == -1 ? "No minigame" : GameServer()->GetMinigameName(i);
		DoLineToggleOption(Page, pNumOptions, pMinigame, CurMinigame);
	}

	DoLineSeperator(Page, pNumOptions);
	DoLineTextSubheader(Page, pNumOptions, "Sᴄᴏʀᴇ Mᴏᴅᴇ");
	for (int i = 0; i < NUM_SCORE_MODES; i++)
	{
		if (i == SCORE_BONUS && !GameServer()->Config()->m_SvAllowBonusScoreMode)
			continue;
		bool CurScoreMode = i == pPlayer->m_ScoreMode;
		DoLineToggleOption(Page, pNumOptions, GameServer()->GetScoreModeName(i), CurScoreMode);
	}
}

void CVotingMenu::Tick()
{
	// Check once per second if we have to auto update
	if (Server()->Tick() % Server()->TickSpeed() != 0)
		return;

	CVotingMenu::SClientVoteInfo::SPrevStats Stats;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if (!pPlayer)
			continue;
		if (m_aClients[i].m_NextVoteSendTick > Server()->Tick())
			continue;

		// 0.7 doesnt have playerflag_in_menu anymore, so we update them automatically every 3s if something changed, even when not in menu /shrug
		bool Update = (pPlayer->m_PlayerFlags&PLAYERFLAG_IN_MENU) || !Server()->IsSevendown(i);
		if (!Update || m_aClients[i].m_Page == PAGE_VOTES || !FillStats(i, &Stats))
			continue;

		// Design doesn't have to be checked, because on design loading finish it will resend the votes anyways so it will be up to date
		if (mem_comp(&Stats, &m_aClients[i].m_PrevStats, sizeof(Stats)) != 0)
		{
			SendPageVotes(i);
			m_aClients[i].m_PrevStats = Stats;
		}
	}
}

bool CVotingMenu::FillStats(int ClientID, CVotingMenu::SClientVoteInfo::SPrevStats *pStats)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	CCharacter *pChr = GameServer()->GetPlayerChar(ClientID);
	if (!pPlayer || !pChr || !pStats)
		return false;

	const int Page = GetPage(ClientID);

	int AccID = pPlayer->GetAccID();
	CGameContext::AccountInfo *pAccount = &GameServer()->m_Accounts[AccID];
	CVotingMenu::SClientVoteInfo::SPrevStats Stats = *pStats;
	int Flags = 0;

	// Misc
	if (Page == PAGE_MISCELLANEOUS)
	{
		if (pPlayer->m_HideDrawings)
			Flags |= PREVFLAG_MISC_HIDEDRAWINGS;
		if (pPlayer->m_WeaponIndicator)
			Flags |= PREVFLAG_MISC_WEAPONINDICATOR;
		if (pPlayer->m_ZoomCursor)
			Flags |= PREVFLAG_MISC_ZOOMCURSOR;
		if (pPlayer->m_ResumeMoved)
			Flags |= PREVFLAG_MISC_RESUMEMOVED;
		if (pPlayer->m_HideBroadcasts)
			Flags |= PREVFLAG_MISC_HIDEBROADCASTS;
		Stats.m_Minigame = pPlayer->m_Minigame;
		Stats.m_ScoreMode = pPlayer->m_ScoreMode;
	}
	else if (Page == PAGE_ACCOUNT)
	{
		// VIP
		if (pAccount->m_VIP)
		{
			if ((pChr && pChr->m_Rainbow) || pPlayer->m_InfRainbow)
				Flags |= PREVFLAG_ACC_VIP_RAINBOW;
			if (pChr && (pChr->m_Bloody || pChr->m_StrongBloody))
				Flags |= PREVFLAG_ACC_VIP_BLOODY;
			if (pChr && pChr->m_Atom)
				Flags |= PREVFLAG_ACC_VIP_ATOM;
			if (pChr && pChr->m_Trail)
				Flags |= PREVFLAG_ACC_VIP_TRAIL;
			if (pChr && pChr->m_aSpreadWeapon[WEAPON_GUN])
				Flags |= PREVFLAG_ACC_VIP_SPREADGUN;

			// VIP Plus
			if (pAccount->m_VIP == VIP_PLUS)
			{
				if (pChr && pChr->m_HookPower == RAINBOW)
					Flags |= PREVFLAG_ACC_VIP_PLUS_RAINBOWHOOK;
				if (pChr && pChr->m_RotatingBall)
					Flags |= PREVFLAG_ACC_VIP_PLUS_ROTATINGBALL;
				if (pChr && pChr->m_EpicCircle)
					Flags |= PREVFLAG_ACC_VIP_PLUS_EPICCIRCLE;
				if (pChr && pChr->m_Lovely)
					Flags |= PREVFLAG_ACC_VIP_PLUS_LOVELY;
				if (pPlayer->m_RainbowName)
					Flags |= PREVFLAG_ACC_VIP_PLUS_RAINBOWNAME;
			}
		}
		
		// Acc Misc
		if (pPlayer->m_SilentFarm)
			Flags |= PREVFLAG_ACC_MISC_SILENTFARM;
		if (pAccount->m_Ninjajetpack && pPlayer->m_NinjaJetpack)
			Flags |= PREVFLAG_ACC_NINJAJETPACK;
		// Plot
		if (m_aClients[ClientID].m_ShowPlotInfo && pPlayer->m_PlotSpawn && GameServer()->GetPlotID(AccID) >= PLOT_START)
		{
			Flags |= PREVFLAG_PLOT_SPAWN;
		}

		if (m_aClients[ClientID].m_ShowAccountStats)
		{
			Stats.m_Acc.m_XP = pAccount->m_XP;
			Stats.m_Acc.m_Money = pAccount->m_Money;
			Stats.m_Acc.m_WalletMoney = pPlayer->GetWalletMoney();
			Stats.m_Acc.m_PoliceLevel = pAccount->m_PoliceLevel;
			Stats.m_Acc.m_TaserBattery = pAccount->m_TaserBattery;
			Stats.m_Acc.m_PortalBattery = pAccount->m_PortalBattery;
			Stats.m_Acc.m_Points = pAccount->m_BlockPoints;
			Stats.m_Acc.m_Kills = pAccount->m_Kills;
			Stats.m_Acc.m_Deaths = pAccount->m_Deaths;
			Stats.m_Acc.m_Euros = pAccount->m_Euros;
			str_copy(Stats.m_Acc.m_aContact, pAccount->m_aContact, sizeof(Stats.m_Acc.m_aContact));
			if (pChr && pChr->m_aLineExp[0] != '\0')
				Flags |= PREVFLAG_ISPLUSXP;
		}
	}

	if (Page != PAGE_VOTES && pPlayer->m_HideBroadcasts)
	{
		Stats.m_JailTime = pPlayer->m_JailTime;
		Stats.m_EscapeTime = pPlayer->m_EscapeTime;
	}

	Stats.m_Flags = Flags;
	
	// fill
	*pStats = Stats;
	return true;
}

void CVotingMenu::SendPageVotes(int ClientID, bool ResendVotesPage)
{
	if (!GameServer()->m_apPlayers[ClientID])
		return;

	int Seconds = Server()->IsSevendown(ClientID) ? 1 : 3;
	m_aClients[ClientID].m_NextVoteSendTick = Server()->Tick() + Server()->TickSpeed() * Seconds;
	const int Page = GetPage(ClientID);
	if (Page == PAGE_VOTES)
	{
		// No need to resend votes when we login, we only want to update the other pages with new values
		if (ResendVotesPage)
		{
			CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
			Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, ClientID);

			// begin sending vote options
			GameServer()->m_apPlayers[ClientID]->m_SendVoteIndex = 0;
		}
		return;
	}

	CVotingMenu::SClientVoteInfo::SPrevStats Stats;
	if (FillStats(ClientID, &Stats))
	{
		m_aClients[ClientID].m_PrevStats = Stats;
	}

	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, ClientID);

	CNetMsg_Sv_VoteOptionGroupStart StartMsg;
	Server()->SendPackMsg(&StartMsg, MSGFLAG_VITAL, ClientID);

	const int NumVotesPage = PrepareTempDescriptions(ClientID);
	const int NumVotesToSend = NUM_OPTION_START_OFFSET + NumVotesPage;
	int TotalVotesSent = 0;
	CMsgPacker Msg(NETMSGTYPE_SV_VOTEOPTIONLISTADD);
	while (TotalVotesSent < NumVotesToSend)
	{
		const int VotesLeft = min(NumVotesToSend - TotalVotesSent, (int)MAX_VOTES_PER_PACKET);
		Msg.AddInt(VotesLeft);

		int CurIndex = 0;
		if (TotalVotesSent == 0)
		{
			OnProgressVoteOptions(ClientID, &Msg, &TotalVotesSent);
			CurIndex = TotalVotesSent;
		}

		while(CurIndex < VotesLeft)
		{
			Msg.AddString(m_aPages[Page].m_aaTempDesc[TotalVotesSent], VOTE_DESC_LENGTH);
			TotalVotesSent++;
			CurIndex++;
		}

		if (Server()->IsSevendown(ClientID))
		{
			while (CurIndex < MAX_VOTES_PER_PACKET)
			{
				Msg.AddString("", VOTE_DESC_LENGTH);
				CurIndex++;
			}
		}
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
		// Reset for next potentional run
		Msg.Reset();
	}

	CNetMsg_Sv_VoteOptionGroupEnd EndMsg;
	Server()->SendPackMsg(&EndMsg, MSGFLAG_VITAL, ClientID);
}

#define ADDLINE_IMPL(desc, prefix, collapseHeader) do \
{ \
	if (!pNumOptions || *pNumOptions >= NUM_PAGE_MAX_OPTIONS) \
		break; \
	char aLine[VOTE_DESC_LENGTH]; \
	if ((prefix)[0]) \
	{ \
		str_format(aLine, sizeof(aLine), "%s %s", (prefix), (desc)); \
	} \
	else \
	{ \
		str_copy(aLine, (desc), sizeof(aLine)); \
	} \
	bool AddCollapseFooter = false; \
	if (m_NumCollapseEntries > 0 && !(collapseHeader)) \
	{ \
		m_NumCollapseEntries--; \
		if (m_NumCollapseEntries == 0) \
		{ \
			AddCollapseFooter = true; \
		} \
		char aTemp[VOTE_DESC_LENGTH]; \
		str_format(aTemp, sizeof(aTemp), "│ %s", aLine); \
		str_copy(aLine, aTemp, sizeof(aLine)); \
	} \
	str_copy(m_aPages[Page].m_aaTempDesc[NUM_OPTION_START_OFFSET + *pNumOptions], aLine, VOTE_DESC_LENGTH); \
	(*pNumOptions)++; \
	if (AddCollapseFooter) \
	{ \
		str_copy(m_aPages[Page].m_aaTempDesc[NUM_OPTION_START_OFFSET + *pNumOptions], "╰───────────────────────", VOTE_DESC_LENGTH); \
		(*pNumOptions)++; \
	} \
} while(0)

#define ADDLINE(desc) ADDLINE_IMPL(desc, "", false)
#define ADDLINE_PREFIX(desc, prefix) ADDLINE_IMPL(desc, prefix, false)
#define ADDLINE_COLLAPSE(desc) ADDLINE_IMPL(desc, "", true)

void CVotingMenu::DoLineToggleOption(int Page, int *pNumOptions, const char *pDescription, bool Value)
{
	ADDLINE_PREFIX(pDescription, Value ? "☒" : "☐");
}

void CVotingMenu::DoLineSeperator(int Page, int *pNumOptions)
{
	ADDLINE(PLACEHOLDER_DESC);
}

void CVotingMenu::DoLineTextSubheader(int Page, int *pNumOptions, const char *pDescription)
{
	char aBuf[VOTE_DESC_LENGTH];
	str_format(aBuf, sizeof(aBuf), "─── %s ───", pDescription);
	ADDLINE(aBuf);
}

void CVotingMenu::DoLineTextValue(int Page, int *pNumOptions, const char *pDescription, int Value)
{
	char aBuf[VOTE_DESC_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s: %d", pDescription, Value);
	DoLineText(Page, pNumOptions, aBuf, true);
}

void CVotingMenu::DoLineText(int Page, int *pNumOptions, const char *pDescription, int BulletPoint)
{
	const char *pPrefix = "";
	if (BulletPoint == BULLET_POINT) pPrefix = "• ";
	else if (BulletPoint == BULLET_ARROW) pPrefix = "⇨ ";
	ADDLINE_PREFIX(pDescription, pPrefix);
}

bool CVotingMenu::DoLineCollapse(int Page, int *pNumOptions, const char *pDescription, bool ShowContent, int NumEntries)
{
	char aBuf[VOTE_DESC_LENGTH];
	if (ShowContent)
	{
		// Add one for the bottom line
		m_NumCollapseEntries = NumEntries;
		str_format(aBuf, sizeof(aBuf), "╭─           %s     [‒]", pDescription);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), ">            %s     [+]", pDescription);
	}
	ADDLINE_COLLAPSE(aBuf);
	return ShowContent;
}

#undef ADDLINE
