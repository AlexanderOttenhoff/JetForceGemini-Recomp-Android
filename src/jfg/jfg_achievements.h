#pragma once
// Jet Force Gemini port: RetroAchievements, casual only (jfg_achievements.cpp).
#include <cstdint>
#include <filesystem>
#include <string>

enum class JfgRaState { LoggedOut, LoggingIn, LoggedIn, Failed };

// What the launcher's Achievements page shows.
struct JfgRaStatus {
    JfgRaState state = JfgRaState::LoggedOut;
    std::string user;      // display name, when logged in
    std::string message;   // why a login failed
    uint32_t points = 0;   // the user's softcore points
    uint32_t unlocked = 0; // this game's achievements: unlocked and in total, once the game is loaded
    uint32_t total = 0;
    bool game_loaded = false;
    uint32_t serial = 0;   // changes whenever anything above does
};

// At start-up: logs in again with the token of an earlier login, if there is one; otherwise nothing runs.
void jfg_achievements_init(const std::filesystem::path& app_folder, const std::string& version);
// The launcher's Achievements page. The password is used once and not kept; logging out forgets the login.
void jfg_achievements_login(const std::string& user, const std::string& password);
void jfg_achievements_logout();
JfgRaStatus jfg_achievements_status();
// Once per game frame, on the game thread (jfg_debug_frame).
void jfg_achievements_frame(uint8_t* rdram);
// While the game isn't running frames (launcher, pause): keeps requests moving.
void jfg_achievements_idle();
// The latest on-screen note (an unlock), and how long ago it came; false when there has been none.
bool jfg_achievements_toast(std::string& text, float& seconds);
