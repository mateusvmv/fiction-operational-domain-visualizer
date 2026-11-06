#include "app.hpp"
#include "window.hpp"
#include <string>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <filesystem>
#include <fmt/format.h>

#include "raylib.h"
#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"

int main()
try
{
    AppState app_state;
    WindowState win_state;

    const std::string state_filepath = "app_state.json";
    bool has_previous_state = std::filesystem::exists(state_filepath);
    bool version_mismatch = false;
    win_state.loaded_version = app_state.version;

    if (has_previous_state) {
        AppState temp_state;
        if (LoadAppState(temp_state, state_filepath, win_state.loaded_version)) {
            if (win_state.loaded_version != app_state.version) {
                version_mismatch = true;
            } else {
                app_state = temp_state;
            }
        } else {
            version_mismatch = true; // Corrupted or unreadable state file
        }
    }
    uint64_t baseline_hash = HashAppState(app_state);

    const int screenWidth = 1300;
    const int screenHeight = 850;
    InitWindow(screenWidth, screenHeight, "SiDB Operational Domain Visualizer");
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ImPlot::CreateContext();

    // Enable ImGui Docking and Multi-Viewports features
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename  = nullptr;

    win_state.rebuilding_cache = has_previous_state && !version_mismatch;
    win_state.show_version_mismatch_modal = version_mismatch;

    if (win_state.rebuilding_cache) {
        for (const auto& exp : app_state.experiments) {
            win_state.cache_tasks.push_back(CacheTask{exp.gate_filepath, exp.algorithm, exp.mu_val, exp.start_epsilon_r, exp.start_lambda_tf});
        }
        win_state.total_cache_items = win_state.cache_tasks.size();
    }

    while (!win_state.should_close)
    {
        if (WindowShouldClose())
        {
            if (HashAppState(app_state) != baseline_hash) {
                win_state.show_exit_confirmation = true;
            } else {
                win_state.should_close = true;
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        rlImGuiBegin();

        // Create a full-screen dockspace container covering the main viewport
        win_state.dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        if (win_state.first_time)
        {
            win_state.first_time = false;
            SetupDock(win_state);
        }

        DrawToolbar(app_state, state_filepath, baseline_hash);

        // --- Modals ---
        DrawVersionMismatchModal(win_state, app_state, state_filepath, baseline_hash);
        DrawCacheRebuildingModal(win_state, app_state);
        DrawExitConfirmationModal(win_state, app_state, state_filepath);

        DrawControlPanel(app_state);

        if (!app_state.experiments.empty()) {
            if (app_state.selected_experiment_index >= app_state.experiments.size()) {
                app_state.selected_experiment_index = 0;
            }
            const auto& active_exp = app_state.experiments[app_state.selected_experiment_index];
            std::string start_key = fmt::format("{:.1f}_{:.1f}", active_exp.start_epsilon_r, active_exp.start_lambda_tf);

            if (app_state.cache[active_exp.gate_filepath][active_exp.algorithm][active_exp.mu_val].find(start_key) == 
                app_state.cache[active_exp.gate_filepath][active_exp.algorithm][active_exp.mu_val].end()) {
                app_state.cache[active_exp.gate_filepath][active_exp.algorithm][active_exp.mu_val][start_key] = 
                    ComputeOperationalData(active_exp.mu_val, active_exp.gate_filepath, active_exp.algorithm, active_exp.start_epsilon_r, active_exp.start_lambda_tf);
            }

            const auto& active_experiment_data = app_state.cache[active_exp.gate_filepath][active_exp.algorithm][active_exp.mu_val][start_key];

            DrawExperimentResultsWindow(active_experiment_data, active_exp);
            DrawOperationalPlot(active_experiment_data, active_exp);
        } else {
            ImGui::Begin("Experiment Results");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Add and select an experiment to view results.");
            ImGui::End();

            ImGui::Begin("Operational Domain Plot");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No active data to plot.");
            ImGui::End();
        }

        rlImGuiEnd();

        EndDrawing();
    }

    ImPlot::DestroyContext();
    rlImGuiShutdown();
    CloseWindow();

    return EXIT_SUCCESS;
}
catch (const std::exception& exception)
{
    std::fputs(exception.what(), stderr);
    std::fputc('\n', stderr);
    return EXIT_FAILURE;
}