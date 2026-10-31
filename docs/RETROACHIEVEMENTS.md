# RetroAchievements (casual)

Jet Force Gemini has a [RetroAchievements set](https://retroachievements.org/game/30) (63 achievements). The game can
earn them in **casual (softcore)** mode, with every port feature available: modern controls, 60 FPS, cutscene skip and
the rest. Hardcore isn't offered: RetroAchievements only accepts hardcore unlocks from emulators it has approved, and
a recompilation isn't one.

Status: experimental. Game identification is tested (the US ROM is recognised as game 30); unlocking in play has not
been tested yet.

## Logging in

Open the launcher's **Achievements** page, enter your RetroAchievements username and password and choose **Log in**.
The password is used once and isn't stored: the game keeps only RetroAchievements' login token (in
`retroachievements.json` in the game's folder) and logs in with it next time. **Log out** removes it. Nothing runs and
nothing is sent to RetroAchievements until you log in.

Once the game is running, the page shows how many of the set's achievements you have, a note at the bottom of the
screen says the same when the game loads, and unlocks show there too.

## How it works

- `src/jfg/jfg_achievements.cpp` runs rcheevos' `rc_client` (lib/rcheevos, v12.5.0, MIT licence).
- The game is identified from the ROM the launcher stored (`jfg-us.z64`), with RetroAchievements' N64 hash.
- Achievements read the game's RDRAM directly. N64Recomp keeps it in the layout RetroAchievements' N64 sets are made
  against (offsets 0-0x7FFFFF, 32-bit words in host byte order, as mupen64plus stores it).
- The set is checked once per game frame. Network requests run on their own threads; their replies are handled on
  the game thread.
- Requests use `jfg_http_request` (Android: the app's Java HTTPS helpers; Linux: curl). Windows is not done yet, so
  RetroAchievements doesn't connect there.
- The port gives the game a larger heap than the N64 had (docs/MEMORY.md). Achievements that follow pointers are
  unaffected; any that read fixed heap addresses could behave differently.
