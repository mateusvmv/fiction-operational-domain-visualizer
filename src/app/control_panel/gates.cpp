#include "../control_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

#include "imgui.h"

namespace fs = std::filesystem;

static void ScanDirectories(AppState& state) {
    state.discovered_gates.clear();
    for (const auto& dir : state.search_directories) {
        try {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".sqd") {
                        state.discovered_gates.push_back(entry.path().string());
                    }
                }
            }
        } catch (...) {}
    }
    std::sort(state.discovered_gates.begin(), state.discovered_gates.end());
    state.discovered_gates.erase(
        std::unique(state.discovered_gates.begin(), state.discovered_gates.end()),
        state.discovered_gates.end());
}

enum class AddGateState { SelectDirectory, ChooseGates };

static void DrawAddNewGatesPopup(AppState& state,
                                 AddGateState& add_gate_state,
                                 std::unordered_map<std::string, bool>& selector_states) {
    if (!BeginFileBrowserPopup("AddNewGatesPopup")) return;

    if (add_gate_state == AddGateState::SelectDirectory) {
        ImGui::Text("Select Directory to Scan");
        ImGui::Separator();

        static char dir_buffer[256] = "fiction/experiments/sidb_gate_libraries/siqad_gates";
        DrawFileBrowserBody(dir_buffer, sizeof(dir_buffer));

        ImGui::SameLine();
        if (ImGui::Button("Scan .sqd")) {
            std::string new_dir(dir_buffer);
            if (std::find(state.search_directories.begin(), state.search_directories.end(), new_dir)
                    == state.search_directories.end()) {
                state.search_directories.push_back(new_dir);
            }
            ScanDirectories(state);

            selector_states.clear();
            for (const auto& g : state.discovered_gates) {
                bool included = (std::find(state.included_gates.begin(),
                                           state.included_gates.end(), g)
                                 != state.included_gates.end());
                selector_states[g] = included;
            }
            add_gate_state = AddGateState::ChooseGates;
        }
    } else {
        ImGui::Text("Select .sqd Files to Add");
        ImGui::Separator();

        if (ImGui::BeginChild("SqdSelectorChild", ImVec2(0, 300), true)) {
            if (state.discovered_gates.empty()) {
                ImGui::Text("No .sqd files found.");
            } else {
                for (const auto& g_path : state.discovered_gates) {
                    bool is_selected = selector_states[g_path];
                    if (ImGui::Checkbox(g_path.c_str(), &is_selected)) {
                        selector_states[g_path] = is_selected;
                    }
                }
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Select")) {
            for (const auto& [g_path, selected] : selector_states) {
                auto it = std::find(state.included_gates.begin(),
                                    state.included_gates.end(), g_path);
                if (selected && it == state.included_gates.end()) {
                    state.included_gates.push_back(g_path);
                } else if (!selected && it != state.included_gates.end()) {
                    state.included_gates.erase(it);
                }
            }
            selector_states.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Back")) {
            add_gate_state = AddGateState::SelectDirectory;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        selector_states.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawGatesTab(AppState& state) {
    static AddGateState add_gate_state = AddGateState::SelectDirectory;
    static std::unordered_map<std::string, bool> selector_states;

    DrawAddNewGatesPopup(state, add_gate_state, selector_states);

    ImGui::Separator();
    ImGui::Text("Added .sqd Gates:");

    if (state.included_gates.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No gates added yet.");
    } else {
        if (ImGui::BeginChild("AddedGatesList", ImVec2(0, 200), true)) {
            for (std::size_t i = 0; i < state.included_gates.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s", state.included_gates[i].c_str());
                ImGui::SameLine();
                if (ImGui::Button("Remove")) {
                    RemoveGate(state, i);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    if (ImGui::Button("Add New Gates")) {
        add_gate_state = AddGateState::SelectDirectory;
        ImGui::OpenPopup("AddNewGatesPopup");
    }
}