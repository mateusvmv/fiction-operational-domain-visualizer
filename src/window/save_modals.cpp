#include "../window.hpp"
#include "../app.hpp"
#include "imgui.h"

#include <cstdio>
#include <filesystem>

void DrawVersionMismatchModal(AppContext& ctx)
{
    WindowState&       win_state      = ctx.win;
    AppState&          app_state      = ctx.app;
    const std::string& state_filepath = ctx.state_filepath;
    uint64_t&          baseline_hash  = ctx.baseline_hash;

    if (!win_state.show_version_mismatch_modal) return;

    ImGui::OpenPopup("Version Mismatch");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 180), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Version Mismatch", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Saved state version mismatch detected!");
        ImGui::Text("Saved Version: %d | Current Version: %d",
                    win_state.loaded_version, app_state.version);
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

void DrawCacheRebuildingModal(AppContext& ctx)
{
    WindowState& win_state = ctx.win;
    AppState&    app_state = ctx.app;

    if (!win_state.rebuilding_cache) return;

    ImGui::OpenPopup("Running Saved Experiments");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Running Saved Experiments", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Running saved experiments from previous state...");
        ImGui::Spacing();

        const std::size_t total = app_state.experiments.size();
        const std::size_t done  = CacheCompletion(app_state);

        const float progress = total > 0
            ? static_cast<float>(done) / static_cast<float>(total)
            : 1.0f;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%zu / %zu", done, total);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), buf);

        ImGui::Spacing();

        if (done < total) {
            ImGui::TextWrapped("Please wait while operational data is recomputed.");
            SyncCache(app_state);
        } else {
            if (ImGui::Button("Start")) {
                win_state.rebuilding_cache = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

void DrawExitConfirmationModal(AppContext& ctx)
{
    WindowState&       win_state      = ctx.win;
    AppState&          app_state      = ctx.app;
    const std::string& state_filepath = ctx.state_filepath;

    if (!win_state.show_exit_confirmation) return;

    ImGui::OpenPopup("Unsaved Changes Confirmation");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(450, 180), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Unsaved Changes Confirmation",
                               &win_state.show_exit_confirmation,
                               ImGuiWindowFlags_NoResize)) {
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
