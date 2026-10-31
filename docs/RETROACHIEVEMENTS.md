# RetroAchievements (casual)

Jet Force Gemini has a [RetroAchievements set](https://retroachievements.org/game/30) (63 achievements). The game can
earn them in **casual (softcore)** mode, with every port feature available: modern controls, 60 FPS, cutscene skip and
the rest. Hardcore isn't offered: RetroAchievements only accepts hardcore unlocks from emulators it has approved, and
a recompilation isn't one.

Status: experimental. Game identification is tested (the US ROM is recognised as game 30); unlocking in play has not
been tested yet.

## Logging in

Nothing runs and nothing is sent to RetroAchievements until you log in. For now there is no login form in the
launcher, so:

1. Make a text file called `retroachievements.txt` in the game's folder (on Android:
   `Android/data/io.github.jfgrecomp/files`), with two lines:

   ```
   username=YourName
   password=YourPassword
   ```

2. Start the game. It logs in once, keeps only the login token (in `retroachievements.json`) and deletes
   `retroachievements.txt`, so your password isn't stored.
3. Load your game: a note at the bottom of the screen shows how many achievements you have. Unlocks show there too.

To log out, delete `retroachievements.json`.

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
