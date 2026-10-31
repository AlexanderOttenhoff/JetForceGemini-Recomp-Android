// Jet Force Gemini port: RetroAchievements, casual (softcore) only.
//
// RetroAchievements (RA) has a set for Jet Force Gemini (game 30). This runs RA's client library, rcheevos' rc_client,
// against the recompiled game: it reads the game's RDRAM, which N64Recomp keeps in the layout RA's sets were made
// against (the N64's 8 MB at offsets 0-0x7FFFFF, as 32-bit words in host byte order, the way mupen64plus stores it),
// and evaluates the set once per game frame (jfg_debug_frame).
//
// - Casual only. RA turns unlocks from a client it hasn't approved into softcore unlocks, and a recompilation is not
//   an approved emulator, so hardcore is never asked for. Everything the port adds (modern controls, 60 FPS, cutscene
//   skip, cheats) stays available.
// - Nothing runs and nothing is sent until the player logs in on the launcher's Achievements page. Logging in uses the
//   password once: RA hands back a token, which is kept in retroachievements.json in the app folder; the password is
//   not stored. Logging out deletes it.
// - Requests go to RA on worker threads (jfg_http_request); their replies are handed to rc_client on the game thread.
// - Built only when rcheevos is in lib/rcheevos (JFG_HAS_RCHEEVOS); otherwise these functions do nothing.
//
// See docs/RETROACHIEVEMENTS.md.
#include "jfg_achievements.h"

#ifdef JFG_HAS_RCHEEVOS

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "json/json.hpp"
#include "rc_client.h"
#include "rc_consoles.h"

#include "jfg/jfg_update_check.h"

namespace fs = std::filesystem;

namespace {
constexpr uint32_t RDRAM_SIZE = 0x800000; // RA's N64 map: 8 MB of RDRAM (with the expansion pak)

rc_client_t* g_client = nullptr;
fs::path g_folder;                 // the app folder
std::string g_version;
std::string g_user_agent;
std::atomic<uint8_t*> g_rdram{ nullptr };
std::atomic<bool> g_game_requested{ false };

// Replies from the worker threads, handed to rc_client on the game thread.
std::mutex g_reply_mutex;
std::deque<std::function<void()>> g_replies;

// What the launcher's Achievements page shows (jfg_achievements_status).
std::mutex g_status_mutex;
JfgRaStatus g_status;

void set_status(JfgRaState state, const std::string& message) {
    std::lock_guard lock(g_status_mutex);
    g_status.state = state;
    g_status.message = message;
    g_status.serial++;
}

// The last unlock, for the overlay (jfg_tick.cpp).
std::mutex g_toast_mutex;
std::string g_toast_text;
std::chrono::steady_clock::time_point g_toast_time{};

void log_line(const std::string& text) {
    std::fprintf(stderr, "[ra] %s\n", text.c_str());
}

void toast(const std::string& text) {
    std::lock_guard lock(g_toast_mutex);
    g_toast_text = text;
    g_toast_time = std::chrono::steady_clock::now();
}

uint32_t RC_CCONV read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t*) {
    uint8_t* rdram = g_rdram.load();
    if (rdram == nullptr || address >= RDRAM_SIZE) return 0;
    if (num_bytes > RDRAM_SIZE - address) num_bytes = RDRAM_SIZE - address;
    std::memcpy(buffer, rdram + address, num_bytes); // already in the byte order RA's N64 sets expect
    return num_bytes;
}

void RC_CCONV server_call(const rc_api_request_t* request, rc_client_server_callback_t callback, void* callback_data, rc_client_t*) {
    std::string url = request->url;
    const bool post = request->post_data != nullptr;
    std::string post_data = post ? request->post_data : "";
    std::string content_type = request->content_type != nullptr ? request->content_type : "";
    std::thread([=]() {
        int status = 0;
        std::string body;
        const bool ok = jfg_http_request(url, post ? &post_data : nullptr, content_type, g_user_agent, status, body);
        std::lock_guard lock(g_reply_mutex);
        g_replies.emplace_back([=]() {
            rc_api_server_response_t response{};
            response.body = body.c_str();
            response.body_length = body.size();
            response.http_status_code = ok ? status : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            callback(&response, callback_data);
        });
    }).detach();
}

void run_replies() {
    std::deque<std::function<void()>> replies;
    {
        std::lock_guard lock(g_reply_mutex);
        replies.swap(g_replies);
    }
    for (auto& reply : replies) reply();
}

void RC_CCONV event_handler(const rc_client_event_t* event, rc_client_t*) {
    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if (event->achievement != nullptr) {
                toast(std::string("Achievement unlocked: ") + event->achievement->title + " (" +
                      std::to_string(event->achievement->points) + ")");
                log_line(std::string("unlocked: ") + event->achievement->title);
            }
            break;
        case RC_CLIENT_EVENT_GAME_COMPLETED:
            toast("All achievements unlocked!");
            break;
        case RC_CLIENT_EVENT_SERVER_ERROR:
            if (event->server_error != nullptr && event->server_error->error_message != nullptr)
                log_line(std::string("server error: ") + event->server_error->error_message);
            break;
        case RC_CLIENT_EVENT_DISCONNECTED:
            log_line("offline: unlocks are kept and sent later");
            break;
        case RC_CLIENT_EVENT_RECONNECTED:
            log_line("back online: pending unlocks sent");
            break;
        default:
            break;
    }
}

void RC_CCONV log_message(const char* message, const rc_client_t*) {
    log_line(message);
}

fs::path token_file() { return g_folder / "retroachievements.json"; }

void save_token(const char* username, const char* token) {
    nlohmann::json j;
    j["username"] = username;
    j["token"] = token;
    std::ofstream out(token_file(), std::ios::trunc);
    out << j.dump(2) << "\n";
}

void RC_CCONV login_done(int result, const char* error, rc_client_t* client, void*) {
    if (result != RC_OK) {
        const std::string why = error != nullptr ? error : "unknown error";
        log_line("login failed: " + why);
        set_status(JfgRaState::Failed, why);
        return;
    }
    const rc_client_user_t* user = rc_client_get_user_info(client);
    if (user != nullptr) {
        save_token(user->username, user->token);
        log_line(std::string("logged in as ") + user->display_name);
        {
            std::lock_guard lock(g_status_mutex);
            g_status.user = user->display_name;
            g_status.points = user->score_softcore;
        }
        set_status(JfgRaState::LoggedIn, "");
    }
}

void RC_CCONV game_loaded(int result, const char* error, rc_client_t* client, void*) {
    if (result != RC_OK) {
        log_line(std::string("game not loaded: ") + (error != nullptr ? error : "unknown error"));
        return;
    }
    const rc_client_game_t* game = rc_client_get_game_info(client);
    rc_client_user_game_summary_t summary{};
    rc_client_get_user_game_summary(client, &summary);
    if (game != nullptr) {
        log_line(std::string("game: ") + game->title + ", " + std::to_string(summary.num_unlocked_achievements) + "/" +
                 std::to_string(summary.num_core_achievements) + " unlocked");
        {
            std::lock_guard lock(g_status_mutex);
            g_status.game_loaded = true;
            g_status.unlocked = summary.num_unlocked_achievements;
            g_status.total = summary.num_core_achievements;
            g_status.serial++;
        }
        toast(std::string("RetroAchievements: ") + std::to_string(summary.num_unlocked_achievements) + "/" +
              std::to_string(summary.num_core_achievements) + " unlocked (casual)");
    }
}

void ensure_client() {
    if (g_client != nullptr) return;
    g_client = rc_client_create(read_memory, server_call);
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_WARN, log_message);
    rc_client_set_event_handler(g_client, event_handler);
    rc_client_set_hardcore_enabled(g_client, 0); // casual only
    char clause[128] = {};
    rc_client_get_user_agent_clause(g_client, clause, sizeof(clause));
    g_user_agent = "JetForceGeminiRecompiled/" + g_version + " " + clause;
}
} // namespace

void jfg_achievements_init(const fs::path& app_folder, const std::string& version) {
    g_folder = app_folder;
    g_version = version;
    // A login from an earlier run: its token. Without one nothing runs until the player logs in on the launcher.
    std::ifstream tokens(token_file());
    if (!tokens) return;
    nlohmann::json j = nlohmann::json::parse(tokens, nullptr, false);
    if (!j.is_object() || !j.contains("username") || !j.contains("token")) return;
    const std::string user = j["username"].get<std::string>(), token = j["token"].get<std::string>();
    ensure_client();
    set_status(JfgRaState::LoggingIn, "");
    rc_client_begin_login_with_token(g_client, user.c_str(), token.c_str(), login_done, nullptr);
}

void jfg_achievements_login(const std::string& user, const std::string& password) {
    if (user.empty() || password.empty()) {
        set_status(JfgRaState::Failed, "Enter your username and password.");
        return;
    }
    ensure_client();
    set_status(JfgRaState::LoggingIn, "");
    rc_client_begin_login_with_password(g_client, user.c_str(), password.c_str(), login_done, nullptr);
}

void jfg_achievements_logout() {
    if (g_client != nullptr) rc_client_logout(g_client);
    std::error_code ec;
    fs::remove(token_file(), ec);
    g_game_requested = false;
    {
        std::lock_guard lock(g_status_mutex);
        g_status = JfgRaStatus{ JfgRaState::LoggedOut, "", "", 0, 0, 0, false, g_status.serial + 1 };
    }
}

JfgRaStatus jfg_achievements_status() {
    std::lock_guard lock(g_status_mutex);
    return g_status;
}

void jfg_achievements_frame(uint8_t* rdram) {
    if (g_client == nullptr) return;
    g_rdram = rdram;
    run_replies();
    // The game: once logged in, identified from the ROM the launcher stored (jfg-us.z64 in the app folder).
    if (!g_game_requested && rc_client_get_user_info(g_client) != nullptr) {
        g_game_requested = true;
        static std::vector<uint8_t> rom;
        std::ifstream in(g_folder / "jfg-us.z64", std::ios::binary);
        rom.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!rom.empty()) {
            rc_client_begin_identify_and_load_game(g_client, RC_CONSOLE_NINTENDO_64, nullptr, rom.data(), rom.size(),
                                                   game_loaded, nullptr);
        }
    }
    rc_client_do_frame(g_client);
}

void jfg_achievements_idle() {
    if (g_client == nullptr) return;
    run_replies();
    rc_client_idle(g_client);
}

bool jfg_achievements_toast(std::string& text, float& seconds) {
    std::lock_guard lock(g_toast_mutex);
    if (g_toast_text.empty()) return false;
    seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_toast_time).count();
    text = g_toast_text;
    return true;
}

#else // no rcheevos: RetroAchievements is off

void jfg_achievements_init(const std::filesystem::path&, const std::string&) {}
void jfg_achievements_login(const std::string&, const std::string&) {}
void jfg_achievements_logout() {}
JfgRaStatus jfg_achievements_status() {
    JfgRaStatus status;
    status.state = JfgRaState::Failed;
    status.message = "This build has no RetroAchievements support.";
    return status;
}
void jfg_achievements_frame(uint8_t*) {}
void jfg_achievements_idle() {}
bool jfg_achievements_toast(std::string&, float&) { return false; }

#endif
