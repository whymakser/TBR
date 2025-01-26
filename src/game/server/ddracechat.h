/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */

// This file can be included several times.

#ifndef CHAT_COMMAND
#define CHAT_COMMAND(name, params, flags, callback, userdata, help, accesslevel)
#endif

CHAT_COMMAND("credits", "", CFGFLAG_CHAT, ConCredits, this, "Shows the credits of the F-DDrace mod", AUTHED_NO)
CHAT_COMMAND("emote", "?s[emote name] i[duration in seconds]", CFGFLAG_CHAT, ConEyeEmote, this, "Sets your tee's eye emote", AUTHED_NO)
CHAT_COMMAND("settings", "?s[configname]", CFGFLAG_CHAT, ConSettings, this, "Shows gameplay information for this server", AUTHED_NO)
CHAT_COMMAND("help", "?r[command]", CFGFLAG_CHAT, ConHelp, this, "Shows help to command r, general help if left blank", AUTHED_NO)
CHAT_COMMAND("info", "", CFGFLAG_CHAT, ConInfo, this, "Shows info about this server", AUTHED_NO)
CHAT_COMMAND("list", "?r[filter]", CFGFLAG_CHAT, ConList, this, "List connected players with optional case-insensitive substring matching filter", AUTHED_NO)
CHAT_COMMAND("me", "r[message]", CFGFLAG_CHAT|CFGFLAG_NONTEEHISTORIC, ConMe, this, "Like the famous irc command '/me says hi' will display '<yourname> says hi'", AUTHED_NO)
CHAT_COMMAND("pause", "?r[player name]", CFGFLAG_CHAT, ConTogglePause, this, "Toggles pause", AUTHED_NO)
CHAT_COMMAND("spec", "?r[player name]", CFGFLAG_CHAT, ConToggleSpec, this, "Toggles spec (if not available behaves as /pause)", AUTHED_NO)
CHAT_COMMAND("pausevoted", "", CFGFLAG_CHAT, ConTogglePauseVoted, this, "Toggles pause on the currently voted player", AUTHED_NO)
CHAT_COMMAND("specvoted", "", CFGFLAG_CHAT, ConToggleSpecVoted, this, "Toggles spec on the currently voted player", AUTHED_NO)
CHAT_COMMAND("mapinfo", "?r[map]", CFGFLAG_CHAT, ConMapInfo, this, "Show info about the map with name r gives (current map by default)", AUTHED_NO)
CHAT_COMMAND("practice", "?i['0'|'1']", CFGFLAG_CHAT, ConPractice, this, "Enable cheats (currently only /rescue) for your current team's run, but you can't earn a rank", AUTHED_NO)
CHAT_COMMAND("timeout", "?s[code]", CFGFLAG_CHAT, ConTimeout, this, "Set timeout protection code s", AUTHED_NO)
CHAT_COMMAND("save", "r[code]", CFGFLAG_CHAT, ConSave, this, "Save team with code r to current server. To save to another server, use '/save s r' where s = server (case-sensitive: GER, RUS, etc) and r = code", AUTHED_NO)
CHAT_COMMAND("load", "r[code]", CFGFLAG_CHAT, ConLoad, this, "Load with code r", AUTHED_NO)
CHAT_COMMAND("map", "?r[map]", CFGFLAG_CHAT|CFGFLAG_NONTEEHISTORIC, ConMap, this, "Vote a map by name", AUTHED_NO)
CHAT_COMMAND("rankteam", "?r", CFGFLAG_CHAT, ConTeamRank, this, "Shows the team rank of player with name r (your team rank by default)", AUTHED_ADMIN)
CHAT_COMMAND("rank", "?r[player name]", CFGFLAG_CHAT, ConRank, this, "Shows the rank of player with name r (your rank by default)", AUTHED_NO)
CHAT_COMMAND("top5team", "?i", CFGFLAG_CHAT, ConTeamTop5, this, "Shows five team ranks of the ladder beginning with rank i (1 by default)", AUTHED_ADMIN)
CHAT_COMMAND("top5", "?i[rank to start with]", CFGFLAG_CHAT, ConTop5, this, "Shows five ranks of the ladder beginning with rank i (1 by default)", AUTHED_NO)
CHAT_COMMAND("team", "?i[id]", CFGFLAG_CHAT, ConJoinTeam, this, "Lets you join team i (shows your team if left blank)", AUTHED_NO)
CHAT_COMMAND("lock", "?i['0'|'1']", CFGFLAG_CHAT, ConLockTeam, this, "Lock team so no-one else can join it", AUTHED_NO)
CHAT_COMMAND("invite", "r[player name]", CFGFLAG_CHAT, ConInviteTeam, this, "Invite a person to a locked team", AUTHED_NO)

CHAT_COMMAND("showothers", "?i['0'|'1']", CFGFLAG_CHAT, ConShowOthers, this, "Whether to show players from other teams or not (off by default), optional i = 0 for off else for on", AUTHED_NO)
CHAT_COMMAND("showall", "?i['0'|'1']", CFGFLAG_CHAT, ConShowAll, this, "Whether to show players at any distance (off by default), optional i = 0 for off else for on", AUTHED_NO)
CHAT_COMMAND("specteam", "?i['0'|'1']", CFGFLAG_CHAT, ConSpecTeam, this, "Whether to show players from other teams when spectating (on by default), optional i = 0 for off else for on", AUTHED_NO)
CHAT_COMMAND("ninjajetpack", "?i['0'|'1']", CFGFLAG_CHAT, ConNinjaJetpack, this, "Whether to use ninja jetpack or not. Makes jetpack look more awesome", AUTHED_NO)
CHAT_COMMAND("saytime", "?r[player name]", CFGFLAG_CHAT|CFGFLAG_NONTEEHISTORIC, ConSayTime, this, "Privately messages someone's current time in this current running race (your time by default)", AUTHED_NO)
CHAT_COMMAND("saytimeall", "", CFGFLAG_CHAT|CFGFLAG_NONTEEHISTORIC, ConSayTimeAll, this, "Publicly messages everyone your current time in this current running race", AUTHED_NO)
CHAT_COMMAND("time", "", CFGFLAG_CHAT, ConTime, this, "Privately shows you your current time in this current running race in the broadcast message", AUTHED_NO)
CHAT_COMMAND("r", "", CFGFLAG_CHAT, ConRescue, this, "Teleport yourself out of freeze (use sv_rescue 1 to enable this feature)", AUTHED_NO)
CHAT_COMMAND("rescue", "", CFGFLAG_CHAT, ConRescue, this, "Teleport yourself out of freeze (use sv_rescue 1 to enable this feature)", AUTHED_NO)

CHAT_COMMAND("kill", "", CFGFLAG_CHAT, ConProtectedKill, this, "Kill yourself when kill-protected during a long game (use f1, kill for regular kill)", AUTHED_NO)

// F-DDrace

//score
CHAT_COMMAND("score", "?s[mode]", CFGFLAG_CHAT, ConScore, this, "Changes the displayed score in scoreboard", AUTHED_NO)

//stats
CHAT_COMMAND("stats", "?r[player name]", CFGFLAG_CHAT, ConStats, this, "Shows stats of player r", AUTHED_NO)
CHAT_COMMAND("account", "", CFGFLAG_CHAT, ConAccount, this, "Shows information about your account", AUTHED_NO)

//info
CHAT_COMMAND("helptoggle", "", CFGFLAG_CHAT, ConHelpToggle, this, "Shows help to enable spookyghost or portal blocker", AUTHED_NO)
CHAT_COMMAND("police", "?i[page]", CFGFLAG_CHAT, ConPoliceInfo, this, "Shows information about police", AUTHED_NO)
CHAT_COMMAND("vip", "", CFGFLAG_CHAT, ConVIPInfo, this, "Shows information about VIP and VIP+", AUTHED_NO)
CHAT_COMMAND("spawnweapons", "", CFGFLAG_CHAT, ConSpawnWeaponsInfo, this, "Shows information about spawn weapons", AUTHED_NO)

//account
CHAT_COMMAND("register", "s[name] s[password] s[password]", CFGFLAG_CHAT, ConRegister, this, "Register an account", AUTHED_NO)
CHAT_COMMAND("login", "s[name] s[password]", CFGFLAG_CHAT, ConLogin, this, "Log into an account", AUTHED_NO)
CHAT_COMMAND("logout", "", CFGFLAG_CHAT, ConLogout, this, "Log out of an account", AUTHED_NO)
CHAT_COMMAND("changepassword", "s[old-pw] s[new-pw] s[new-pw]", CFGFLAG_CHAT, ConChangePassword, this, "Changes account password", AUTHED_NO)
CHAT_COMMAND("contact", "?r[option]", CFGFLAG_CHAT, ConContact, this, "Sets contact information for your account", AUTHED_NO)
CHAT_COMMAND("pin", "?s[pin]", CFGFLAG_CHAT, ConPin, this, "Sets a new security pin for your account", AUTHED_NO)

CHAT_COMMAND("pay", "i[amount] r[name]", CFGFLAG_CHAT, ConPayMoney, this, "Pays i money to player r", AUTHED_NO)
CHAT_COMMAND("money", "?s['drop'] ?i[amount]", CFGFLAG_CHAT, ConMoney, this, "Shows your current balance and last transactions, or drops i amount", AUTHED_NO)
CHAT_COMMAND("portal", "?s['drop'] ?i[amount]", CFGFLAG_CHAT, ConPortal, this, "Shows your current portal battery amount, or drops i amount", AUTHED_NO)
CHAT_COMMAND("taser", "?s['drop'] ?i[amount]", CFGFLAG_CHAT, ConTaserInfo, this, "Shows your current taser stats, or drops i amount", AUTHED_NO)

CHAT_COMMAND("room", "s['invite'|'kick'] r[name]", CFGFLAG_CHAT, ConRoom, this, "Invite or kick player r from the room", AUTHED_NO)
CHAT_COMMAND("spawn", "", CFGFLAG_CHAT, ConSpawn, this, "Teleport to spawn (-50.000 money)", AUTHED_NO)
CHAT_COMMAND("silentfarm", "?i['0'|'1']", CFGFLAG_CHAT, ConSilentFarm, this, "Mute sounds while on moneytile", AUTHED_NO)

//plots
CHAT_COMMAND("plot", "?s[command] ?s[price|helpcmd|swapname] ?r[playername]", CFGFLAG_CHAT, ConPlot, this, "Plot command", AUTHED_NO)
CHAT_COMMAND("hidedrawings", "?i['0'|'1']", CFGFLAG_CHAT, ConHideDrawings, this, "Whether drawings are hidden", AUTHED_NO)

//extras
CHAT_COMMAND("weaponindicator", "?i['0'|'1']", CFGFLAG_CHAT, ConWeaponIndicator, this, "Tells you which weapon you are holding under the heart and armor bar", AUTHED_NO)
CHAT_COMMAND("zoomcursor", "?i['0'|'1']", CFGFLAG_CHAT, ConZoomCursor, this, "Whether to zoom the cursor aswell, do not use with dynamic camera", AUTHED_NO)

//other
CHAT_COMMAND("resumemoved", "?i['0'|'1']", CFGFLAG_CHAT, ConResumeMoved, this, "Whether to resume from pause when someone moved your tee (off by default), optional i = 0 for off else for on", AUTHED_NO)
CHAT_COMMAND("mute", "r[playername]", CFGFLAG_CHAT, ConMutePlayer, this, "Mutes player r until disconnect", AUTHED_NO)
CHAT_COMMAND("design", "?s[name]", CFGFLAG_CHAT, ConDesign, this, "Changes map design or shows a list of available designs", AUTHED_NO)
CHAT_COMMAND("language", "?s[language]", CFGFLAG_CHAT, ConLanguage, this, "Changes language or shows a list of availables languages", AUTHED_NO)
CHAT_COMMAND("discord", "", CFGFLAG_CHAT, ConDiscord, this, "Sends Discord invite link", AUTHED_NO)
CHAT_COMMAND("shrug", "", CFGFLAG_CHAT, ConShrug, this, "¯\\_(ツ)_/¯", AUTHED_NO)
CHAT_COMMAND("hidebroadcasts", "?i['0'|'1']", CFGFLAG_CHAT, ConHideBroadcasts, this, "Whether to hide money, jail, escape broadcasts and show them in vote menu instead", AUTHED_NO)

//minigames
CHAT_COMMAND("minigames", "", CFGFLAG_CHAT, ConMinigames, this, "Shows a list of all available minigames", AUTHED_NO)
CHAT_COMMAND("leave", "", CFGFLAG_CHAT, ConLeaveMinigame, this, "Leaves the current minigame", AUTHED_NO)
CHAT_COMMAND("block", "", CFGFLAG_CHAT, ConJoinBlock, this, "Joins the block minigame", AUTHED_NO)
CHAT_COMMAND("survival", "", CFGFLAG_CHAT, ConJoinSurvival, this, "Joins the survival minigame", AUTHED_NO)
CHAT_COMMAND("boomfng", "", CFGFLAG_CHAT, ConJoinBoomFNG, this, "Joins the boom fng minigame", AUTHED_NO)
CHAT_COMMAND("fng", "", CFGFLAG_CHAT, ConJoinFNG, this, "Joins the fng minigame", AUTHED_NO)
CHAT_COMMAND("1vs1", "?s[playername] ?i[scorelimit] ?i[killborder]", CFGFLAG_CHAT, Con1VS1, this, "Joins the 1vs1 minigame or accepts/creates a fight/new arena", AUTHED_NO)

//account top5s
CHAT_COMMAND("top5level", "?i[rank to start with]", CFGFLAG_CHAT, ConTop5Level, this, "Shows top5 accounts sorted by level", AUTHED_NO)
CHAT_COMMAND("top5points", "?i[rank to start with]", CFGFLAG_CHAT, ConTop5Points, this, "Shows top5 accounts sorted by block points", AUTHED_NO)
CHAT_COMMAND("top5money", "?i[rank to start with]", CFGFLAG_CHAT, ConTop5Money, this, "Shows top5 accounts sorted by money", AUTHED_NO)
CHAT_COMMAND("top5spree", "?i[rank to start with]", CFGFLAG_CHAT, ConTop5Spree, this, "Shows top5 accounts sorted by killing spree", AUTHED_NO)
CHAT_COMMAND("top5portal", "?i[rank to start with]", CFGFLAG_CHAT, ConTop5Portal, this, "Shows top5 accounts sorted by portal batteries", AUTHED_NO)

//police
CHAT_COMMAND("policehelper", "s[add|remove] r[name]", CFGFLAG_CHAT, ConPoliceHelper, this, "Adds/removes player r to/from policehelpers", AUTHED_NO)
CHAT_COMMAND("wanted", "", CFGFLAG_CHAT, ConWanted, this, "Shows a list of all players being wanted by police", AUTHED_NO)

//vip
CHAT_COMMAND("rainbow", "", CFGFLAG_CHAT, ConRainbowVIP, this, "Toggles rainbow for yourself", AUTHED_NO)
CHAT_COMMAND("bloody", "", CFGFLAG_CHAT, ConBloodyVIP, this, "Toggles bloody for yourself", AUTHED_NO)
CHAT_COMMAND("atom", "", CFGFLAG_CHAT, ConAtomVIP, this, "Toggles atom for yourself", AUTHED_NO)
CHAT_COMMAND("trail", "", CFGFLAG_CHAT, ConTrailVIP, this, "Toggles trail for yourself", AUTHED_NO)
CHAT_COMMAND("spreadgun", "", CFGFLAG_CHAT, ConSpreadGunVIP, this, "Toggles spread gun for yourself", AUTHED_NO)
CHAT_COMMAND("rotatingball", "", CFGFLAG_CHAT, ConRotatingBallVIP, this, "Toggles rotating ball for yourself", AUTHED_NO)
CHAT_COMMAND("epiccircle", "", CFGFLAG_CHAT, ConEpicCircleVIP, this, "Toggles epic circle for yourself", AUTHED_NO)
CHAT_COMMAND("lovely", "", CFGFLAG_CHAT, ConLovelyVIP, this, "Toggles lovely for yourself", AUTHED_NO)
CHAT_COMMAND("rainbowhook", "", CFGFLAG_CHAT, ConRainbowHookVIP, this, "Toggles rainbow hook for yourself", AUTHED_NO)
CHAT_COMMAND("rainbowname", "", CFGFLAG_CHAT, ConRainbowNameVIP, this, "Toggles rainbow name for yourself", AUTHED_NO)
CHAT_COMMAND("rainbowspeed", "?i[speed]", CFGFLAG_CHAT, ConRainbowSpeedVIP, this, "Sets rainbow speed for yourself", AUTHED_NO)
#undef CHAT_COMMAND
