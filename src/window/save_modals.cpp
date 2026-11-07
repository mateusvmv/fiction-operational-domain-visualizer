#include "../window.hpp"
#include "../app.hpp"
#include "imgui.h"
#include <filesystem>

void DrawVersionMismatchModal(WindowState &win_state, AppState &app_state, const std::string &state_filepath, uint64_t &baseline_hash)
{
    if (!win_state.show_version_mismatch_modal) return;

    ImGui::OpenPopup("Version Mismatch");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 180), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Version Mismatch", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Saved state version mismatch detected!");
        ImGui::Text("Saved Version: %d | Current Version: %d", win_state.loaded_version, app_state.version);
        ImGui::Spacing();
        ImGui::TextWrapped("The saved state file is incompatible with this version of the application. Please choose how to proceed:");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Delete and Start Fresh", ImVec2(160, 0))) {
            std::filesystem::remove(state_filepath);
            win_state.show_version_mismatch_modal = false;
            baseline_hash = HashAppState(app_state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(100, 0))) {
            win_state.should_close = true;
        }

        ImGui::EndPopup();
    }
}

void DrawCacheRebuildingModal(WindowState &win_state, AppState &app_state)
{
    if (!win_state.rebuilding_cache) return;

    ImGui::OpenPopup("Running Saved Experiments");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Running Saved Experiments", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Running saved experiments from previous state...");
        ImGui::Spacing();

        const float progress = win_state.total_cache_items > 0
            ? static_cast<float>(win_state.completed_cache_items) /
              static_cast<float>(win_state.total_cache_items)
            : 1.0f;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%zu / %zu",
                      win_state.completed_cache_items, win_state.total_cache_items);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), buf);

        ImGui::Spacing();

        if (win_state.completed_cache_items < win_state.total_cache_items) {
            ImGui::TextWrapped("Please wait while operational data is recomputed.");

            const auto& task = win_state.cache_tasks[win_state.completed_cache_items];
            const StartPointKey start_key = MakeStartPointKey(
                task.start_epsilon_r, task.start_lambda_tf);
            auto& start_cache = app_state.cache[task.gate][task.algorithm][task.mu_val];
            if (start_cache.find(start_key) == start_cache.end()) {
                start_cache.emplace(start_key, ComputeOperationalData(
                    task.mu_val, task.gate, task.algorithm,
                    task.start_epsilon_r, task.start_lambda_tf));
            }
            ++win_state.completed_cache_items;
        } else {
            if (ImGui::Button("Start")) {
                win_state.rebuilding_cache = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

void DrawExitConfirmationModal(WindowState &win_state, AppState &app_state, const std::string &state_filepath)
{
    if (!win_state.show_exit_confirmation) return;

    ImGui::OpenPopup("Unsaved Changes Confirmation");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(450, 180), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Unsaved Changes Confirmation", &win_state.show_exit_confirmation, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("You have unsaved changes to your application state.");
        ImGui::Text("Do you want to save before exiting?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save and Exit", ImVec2(120, 0))) {
            SaveAppState(app_state, state_filepath);
            win_state.show_exit_confirmation = false;
            win_state.should_close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard and Exit", ImVec2(130, 0))) {
            win_state.show_exit_confirmation = false;
            win_state.should_close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            win_state.show_exit_confirmation = false;
        }

        ImGui::EndPopup();
    }
}
