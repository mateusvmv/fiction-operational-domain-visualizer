#include "../app.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <fmt/format.h>

namespace fs = std::filesystem;

void ScanDirectories(AppState& state) {
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
    state.discovered_gates.erase(std::unique(state.discovered_gates.begin(), state.discovered_gates.end()), state.discovered_gates.end());
}

template <typename T, typename LabelFunc, typename ExtraFunc, typename RemoveFunc>
inline void DrawItemRadioList(
    std::vector<T>& items,
    size_t& selected_index,
    LabelFunc&& get_label,
    ExtraFunc&& draw_extra,
    RemoveFunc&& on_remove,
    bool allow_remove = true
)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        bool is_selected = (selected_index == i);
        std::string label = get_label(items[i]);
        
        if (ImGui::RadioButton(label.c_str(), is_selected)) {
            selected_index = i;
        }
        
        draw_extra(items[i], i);
        
        if (allow_remove && items.size() > 1) {
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                on_remove(i);
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }
}

enum class AddGateState { SelectDirectory, ChooseGates };

void DrawControlPanel(AppState& state) {
    ImGui::Begin("SiDB Plot Control Panel");

    static AddGateState add_gate_state = AddGateState::SelectDirectory;
    static std::unordered_map<std::string, bool> selector_states;

    if (ImGui::BeginTabBar("ControlPanelTabs")) {
        
        // Tab 1: Gates
        if (ImGui::BeginTabItem("Gates")) {
            if (BeginFileBrowserPopup("AddNewGatesPopup")) {
                if (add_gate_state == AddGateState::SelectDirectory) {
                    ImGui::Text("Select Directory to Scan");
                    ImGui::Separator();
                    
                    static char dir_buffer[256] = "fiction/experiments/sidb_gate_libraries/siqad_gates";
                    DrawFileBrowserBody(dir_buffer, sizeof(dir_buffer));

                    ImGui::SameLine();
                    if (ImGui::Button("Scan .sqd")) {
                        std::string new_dir(dir_buffer);
                        if (std::find(state.search_directories.begin(), state.search_directories.end(), new_dir) == state.search_directories.end()) {
                            state.search_directories.push_back(new_dir);
                        }
                        ScanDirectories(state);

                        selector_states.clear();
                        for (const auto& g : state.discovered_gates) {
                            bool included = (std::find(state.included_gates.begin(), state.included_gates.end(), g) != state.included_gates.end());
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
                            auto it = std::find(state.included_gates.begin(), state.included_gates.end(), g_path);
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

            ImGui::Separator();
            ImGui::Text("Added .sqd Gates:");
            if (state.included_gates.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No gates added yet.");
            } else {
                if (ImGui::BeginChild("AddedGatesList", ImVec2(0, 200), true)) {
                    DrawItemRadioList(
                        state.included_gates,
                        state.selected_experiment_index, // placeholder index
                        [](const std::string& path) { return path; },
                        [](std::string&, size_t) {},
                        [&state](std::size_t idx) { RemoveGate(state, idx); }
                    );
                }
                ImGui::EndChild();
            }

            if (ImGui::Button("Add New Gates")) {
                add_gate_state = AddGateState::SelectDirectory;
                ImGui::OpenPopup("AddNewGatesPopup");
            }

            ImGui::EndTabItem();
        }

        // Tab 2: Experiments
        if (ImGui::BeginTabItem("Experiments")) {
            ImGui::Text("Configured Experiments:");
            ImGui::Separator();

            if (state.experiments.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No experiments created yet.");
            } else {
                if (ImGui::BeginChild("ExperimentsList", ImVec2(0, 220), true)) {
                    DrawItemRadioList(
                        state.experiments,
                        state.selected_experiment_index,
                        [](const ExperimentConfig& exp) { return fmt::format("{} [{}] (mu={:.2f})", fs::path(exp.gate_filepath).filename().string(), exp.algorithm, exp.mu_val); },
                        [](ExperimentConfig& exp, size_t) {
                            ImGui::SameLine();
                            ImGui::ColorEdit3("##Color", reinterpret_cast<float*>(&exp.color), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                        },
                        [&state](std::size_t idx) { RemoveExperiment(state, idx); }
                    );
                }
                ImGui::EndChild();
            }

            ImGui::Separator();
            ImGui::Text("Add / Edit Experiment:");

            static int selected_gate_combo = 0;
            if (!state.included_gates.empty()) {
                if (selected_gate_combo >= static_cast<int>(state.included_gates.size())) selected_gate_combo = 0;
                ImGui::Combo("Gate", &selected_gate_combo, [](void* data, int idx) -> const char* {
                    auto* vec = static_cast<std::vector<std::string>*>(data);
                    return (*vec)[static_cast<std::size_t>(idx)].c_str();
                }, static_cast<void*>(&state.included_gates), static_cast<int>(state.included_gates.size()));
            }

            static int alg_combo = 0;
            const char* algorithms[] = { "Contour Tracking", "Grid Search", "Flood Fill", "Random Sampling" };
            ImGui::Combo("Algorithm", &alg_combo, algorithms, IM_ARRAYSIZE(algorithms));

            static double new_mu = -0.32;
            ImGui::InputDouble("mu (eV)", &new_mu, 0.01, 0.1, "%.2f");

            static double new_start_eps = 5.0;
            static double new_start_lambda = 5.0;
            ImGui::InputDouble("Start Epsilon (r)", &new_start_eps, 0.1, 1.0, "%.1f");
            ImGui::InputDouble("Start Lambda (tf)", &new_start_lambda, 0.1, 1.0, "%.1f");

            if (ImGui::Button("Add Experiment") && !state.included_gates.empty()) {
                ExperimentConfig new_exp;
                new_exp.gate_filepath = state.included_gates[selected_gate_combo];
                new_exp.algorithm = algorithms[alg_combo];
                new_exp.mu_val = new_mu;
                new_exp.start_epsilon_r = new_start_eps;
                new_exp.start_lambda_tf = new_start_lambda;
                new_exp.color = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
                state.experiments.push_back(new_exp);
                state.selected_experiment_index = state.experiments.size() - 1;
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}