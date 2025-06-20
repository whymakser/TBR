# F-DDrace [![](https://github.com/fokkonaut/F-DDrace/workflows/Build/badge.svg)](https://github.com/fokkonaut/F-DDrace/actions?query=workflow%3ABuild+event%3Apush+branch%3AF-DDrace)

F-DDrace is a modification of Teeworlds, developed by fokkonaut.

## About

F-DDrace is an "open world" game type where you can do whatever you want - chat, hang out, farm money, play minigames, block people, race the map, collect stuff, buy things, annoy others, form clans, and more. The idea was to not have a specific goal, just play and enjoy doing exactly what you want.

## Core Features

**Architecture:** The code base mixes Vanilla 0.7 and DDNet. F-DDrace manually ports various DDNet parts to Vanilla 0.7 and builds on top of that. Originally only 0.7 clients could connect, but DDNet client support was added via a translation layer. So F-DDrace is basically a DDNet modification with extra steps.

**Multi version support:** Works with both 0.7 and DDNet clients perfectly with 100% feature support.

**128 player support:** F-DDrace was the first mod to fully support more than 64 players, implementing it perfectly and covering all edge cases. It was also the first server to actually reach such high player counts during the COVID pandemic, testing and improving performance with live tests.

**Account system:** Save your stats like level, experience, money, kills, deaths and much more. Use `/register`, `/login`, `/stats`, `/account`. Protect your account with `/pin` for security and `/contact` for recovery.

**Shop system:** Buy cosmetics, upgrades and more with farmed money. Shop is integrated into the map.

**Custom votes menu:** The callvote menu has 3 categories: Votes, Account, Miscellaneous. Switch between them effortlessly and interact with toggle options, value options via reason, and collapse elements. You can combine elements or add prefixes like bullet points.

**Plot system & innovative editor:** One of F-DDrace's biggest features. Rent your own plot and design it with the self-made ingame plot editor. The editor lets you place pickups (hearts, shields, weapons), create laser walls (fully customizable color, length, angle, thickness, collision), build laser doors with matching toggle buttons, add speedups (configurable angle, force, max speed), and set up teleporters (multiple modes: In/Out, From, To, Weapon From, Hook From, with evil mode for red/blue teleporters). Advanced transformation tools let you select whole areas to move, copy, erase, or save for later loading.

**Tavern & Grog:** Sit in a tavern and buy grog. Yes, really.

**Minigames:** List with `/minigames`. When joining, your complete tee status (position, weapons, extras, etc.) gets saved and restored when you `/leave`. Nothing gets missed.

**1vs1 system:** Create a 1vs1 lobby with `/1vs1 <name>` and fight anywhere on the map. What's special is you can take ANY place from the whole map to play a 1vs1 there. Enlarge the area using zoom and adjust weapons for the round.

**Durák card game:** Play by mouse or keyboard (`+left`, `+right`, `+jump`, `+showhookcoll`). Requires: `cl_show_direction 0; cl_nameplates_size 50; cl_nameplates_offset 30; cl_show_others_alpha 40;` Keyboard controls: select cards using A/D, submit using SPACE, go back using HOOK-COLLISION. When you select a card with SPACE, you can choose what to do with it using A/D again and submit with SPACE again. Attacker drags cards to table, defender can defend by dragging cards onto offense cards or place same rank to push attack to next player.

**Server-side translation:** Use `/language` to switch languages. Set default with `sv_default_language` or enable suggestions with `sv_language_suggestion`. Language files are in [`datasrc/languages`](https://github.com/fokkonaut/F-DDrace/tree/F-DDrace/datasrc/languages) and moved to `data/languages` upon compiling.

**Flags and weapon drops:** Drop flags with `F3`, weapons with `F4`. They interact with explosions, shotgun, speedups, teleporters, doors, portals. Flags can be hooked smoothly by players and you can spectate them using the spectate menu (`bind x +spectate`).

**Persistent data:** Money drops and plots survive server restarts. Use `sv_shutdown_save_tees 1` to save and load all players automatically - everything is stored in files and matched when you rejoin.

**Extra weapons:** Extended inventory beyond regular weapons. Most are admin-only if not placed in map.

## Building

F-DDrace uses the same build system as DDNet. For detailed building instructions, dependencies, and platform-specific setup, see the [DDNet README](https://github.com/ddnet/ddnet/blob/master/README.md):

- **Dependencies:** [Linux/macOS](https://github.com/ddnet/ddnet/blob/master/README.md#dependencies-on-linux--macos)
- **Building on Linux/macOS:** [Instructions](https://github.com/ddnet/ddnet/blob/master/README.md#building-on-linux-and-macos)  
- **Building on Windows:** [Instructions](https://github.com/ddnet/ddnet/blob/master/README.md#building-on-windows-with-visual-studio)

## Common Chat Commands

Most commands show help when used without parameters.

- `/register name pw pw` - Create account
- `/login name pw` - Log in
- `/stats [player]` - View stats
- `/money` - Check balance
- `/pay amount name` - Send money
- `/plot` - Manage your plot
- `/minigames` - List available minigames
- `/1vs1` - Join/create 1vs1
- `/leave` - Leave current minigame
- `/language [lang]` - Change language
- `/design [name]` - Change map design

## Development, Configuration & Commands

F-DDrace has tons of settings and commands. Instead of maintaining duplicate documentation here, check the source files:

- **All settings:** [`src/game/variables.h`](https://github.com/fokkonaut/F-DDrace/blob/F-DDrace/src/game/variables.h)
- **Admin commands:** [`src/game/ddracecommands.h`](https://github.com/fokkonaut/F-DDrace/blob/F-DDrace/src/game/ddracecommands.h)  
- **Chat commands:** [`src/game/ddracechat.h`](https://github.com/fokkonaut/F-DDrace/blob/F-DDrace/src/game/server/ddracechat.h)

These files contain the complete and up-to-date documentation for everything.

Source code is in [`src/`](https://github.com/fokkonaut/F-DDrace/tree/F-DDrace/src) and [`scripts/`](https://github.com/fokkonaut/F-DDrace/tree/F-DDrace/scripts).
Pull requests and suggestions welcome!

## Credits

- **Lead Developer:** [fokkonaut](https://github.com/fokkonaut)
- **Major Contributors:** [ChillerDragon](https://github.com/ChillerDragon), [Matq](https://github.com/Matqyou), and the Teeworlds/DDNet community
- **Based on:** [Teeworlds](https://github.com/teeworlds/teeworlds), [DDNet](https://github.com/ddnet/ddnet)

## License

See `license.txt` in the repository root.

---

For help or to contribute, open an issue or make a pull request.
