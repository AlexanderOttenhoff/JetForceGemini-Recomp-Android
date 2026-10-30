#pragma once
// Jet Force Gemini port: RetroAchievements, casual only (jfg_achievements.cpp).
#include <cstdint>
#include <filesystem>
#include <string>

// At start-up: does nothing until the player has logged in once (see jfg_achievements.cpp).
void jfg_achievements_init(const std::filesystem::path& app_folder, const std::string& version);
// Once per game frame, on the game thread (jfg_debug_frame).
void jfg_achievements_frame(uint8_t* rdram);
// While the game isn't running frames (launcher, pause): keeps requests moving.
void jfg_achievements_idle();
// The latest on-screen note (an unlock), and how long ago it came; false when there has been none.
bool jfg_achievements_toast(std::string& text, float& seconds);
