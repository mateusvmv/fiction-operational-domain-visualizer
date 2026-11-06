#pragma once
#include "app.hpp"
#include <string>
#include <vector>

struct CacheTask
{
    std::string gate;
    std::string algorithm;
    double mu_val;
    double start_epsilon_r;
    double start_lambda_tf;
};

struct WindowState
{
    ImGuiID dockspace_id = 0;
    bool should_close = false;
    bool first_time = true;
    bool rebuilding_cache = false;
    bool show_version_mismatch_modal = false;
    bool show_exit_confirmation = false;
    int loaded_version = 0;
    size_t total_cache_items = 0;
    size_t completed_cache_items = 0;
    std::vector<CacheTask> cache_tasks;
};

void SetupDock(WindowState &win_state);
void DrawToolbar(AppState &app_state, const std::string &state_filepath, uint64_t &baseline_hash);

void DrawVersionMismatchModal(WindowState &win_state, AppState &app_state, const std::string &state_filepath, uint64_t &baseline_hash);
void DrawCacheRebuildingModal(WindowState &win_state, AppState &app_state);
void DrawExitConfirmationModal(WindowState &win_state, AppState &app_state, const std::string &state_filepath);