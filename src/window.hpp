#pragma once
#include "app.hpp"
#include <string>
#include <vector>

struct WindowState {
    ImGuiID dockspace_id = 0;
    bool should_close = false;
    bool first_time = true;
    bool rebuilding_cache = false;
    bool show_version_mismatch_modal = false;
    bool show_exit_confirmation = false;
    int loaded_version = 0;
    size_t total_cache_items = 0;
    size_t completed_cache_items = 0;
    std::vector<ExperimentKey> cache_tasks;
};

// Shared draw-time context: the two long-lived state objects, the state file
// path, and the baseline hash for unsaved-change detection. Every top-level
// draw helper that needs any of these takes one AppContext&.
//
// Reference members make this non-assignable; create it once in main and let
// the lifetime of the referenced locals outlive it.
struct AppContext {
    AppState&          app;
    WindowState&       win;
    const std::string& state_filepath;
    uint64_t&          baseline_hash;
};

void SetupDock(WindowState& win_state);
void DrawToolbar(AppContext& ctx);
void DrawVersionMismatchModal(AppContext& ctx);
void DrawCacheRebuildingModal(AppContext& ctx);
void DrawExitConfirmationModal(AppContext& ctx);