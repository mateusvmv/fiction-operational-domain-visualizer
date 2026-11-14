#include "app.hpp"
#include "window.hpp"
#include <string>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <filesystem>
#include <utility>
#include <fmt/format.h>

#include "raylib.h"
#include "imgui.h"
#include "imgui_internal.h"
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
            version_mismatch = true;
        }
    }
    uint64_t baseline_hash = HashAppState(app_state);

    const int screenWidth = 1300;
    const int screenHeight = 850;
    InitWindow(screenWidth, screenHeight, "SiDB Operational Domain Visualizer");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;

    win_state.rebuilding_cache = has_previous_state && !version_mismatch
                                 && !app_state.experiments.empty();
    win_state.show_version_mismatch_modal = version_mismatch;

    AppContext ctx{app_state, win_state, state_filepath, baseline_hash};

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

        // ESC closes the topmost ImGui popup.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            if (g.OpenPopupStack.Size > 0) {
                ImGui::ClosePopupToLevel(g.OpenPopupStack.Size - 1, false);
            }
        }

        win_state.dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        if (win_state.first_time) {
            win_state.first_time = false;
            SetupDock(win_state);
        }

        DrawToolbar(ctx);
        DrawVersionMismatchModal(ctx);
        DrawCacheRebuildingModal(ctx);
        DrawExitConfirmationModal(ctx);
        
        DrawExperimentProgressModal(app_state);
        DrawControlPanel(app_state);

        if (!app_state.experiments.empty()) {
            if (!win_state.rebuilding_cache) {
                SyncCache(app_state);
            }
            DrawExperimentResultsWindow(app_state);
            DrawOperationalPlot(app_state);
        } else {
            ImGui::Begin("Experiment Results");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                "Add and select an experiment to view results.");
            ImGui::End();

            ImGui::Begin("Operational Domain Plot");
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                "No active data to plot.");
            ImGui::End();
        }

        // End-of-frame: drop cache entries no experiment references.
        PruneCache(app_state);

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
