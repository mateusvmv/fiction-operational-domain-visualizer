#include "../window.hpp"
#include "imgui.h"

void DrawToolbar(AppContext& ctx)
{
    AppState&          app_state      = ctx.app;
    const std::string& state_filepath = ctx.state_filepath;
    uint64_t&          baseline_hash  = ctx.baseline_hash;

    // Force the toolbar to span the entire top width of the viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 35.0f)); // Fixed height for a toolbar
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags toolbar_flags = ImGuiWindowFlags_NoDecoration | 
                                        ImGuiWindowFlags_NoScrollbar | 
                                        ImGuiWindowFlags_NoMove | 
                                        ImGuiWindowFlags_NoSavedSettings | 
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | 
                                        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

    ImGui::Begin("Toolbar", nullptr, toolbar_flags);
    ImGui::PopStyleVar(3);

    if (ImGui::Button("Save State")) {
        SaveAppState(app_state, state_filepath);
        baseline_hash = HashAppState(app_state);
    }
    if (HashAppState(app_state) != baseline_hash) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "(Unsaved Changes)");
    }
    ImGui::End();
}
