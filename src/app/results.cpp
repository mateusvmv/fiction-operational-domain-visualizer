#include "../app.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <fmt/format.h>

namespace fs = std::filesystem;

void SaveDataToCsv(const std::string& filepath, const std::vector<DataPoint>& plot_data) {
    std::ofstream file(filepath);
    if (!file.is_open()) return;

    file << "epsilon_r,lambda_tf,status,mu\n";
    for (const auto& pt : plot_data) {
        file << pt.epsilon_r << "," << pt.lambda_tf << "," << pt.status << "," << pt.mu << "\n";
    }
}

void DrawExperimentResultsWindow(AppState& state) {
    ImGui::Begin("Experiment Results");

    if (state.experiments.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No experiments available.");
        ImGui::End();
        return;
    }

    if (state.selected_experiment_index >= state.experiments.size()) {
        state.selected_experiment_index = 0;
    }

    // Selector dropdown for results view
    if (ImGui::BeginCombo("Select Experiment", fmt::format("{} [{}] (mu={:.2f})", fs::path(state.experiments[state.selected_experiment_index].gate_filepath).filename().string(), state.experiments[state.selected_experiment_index].algorithm, state.experiments[state.selected_experiment_index].mu_val).c_str())) {
        for (size_t i = 0; i < state.experiments.size(); ++i) {
            bool is_selected = (state.selected_experiment_index == i);
            std::string combo_label = fmt::format("{} [{}] (mu={:.2f})", fs::path(state.experiments[i].gate_filepath).filename().string(), state.experiments[i].algorithm, state.experiments[i].mu_val);
            if (ImGui::Selectable(combo_label.c_str(), is_selected)) {
                state.selected_experiment_index = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    const auto& active_exp = state.experiments[state.selected_experiment_index];
    std::string start_key = fmt::format("{:.1f}_{:.1f}", active_exp.start_epsilon_r, active_exp.start_lambda_tf);
    
    std::vector<DataPoint> current_experiment_data;
    if (state.cache.count(active_exp.gate_filepath) &&
        state.cache[active_exp.gate_filepath].count(active_exp.algorithm) &&
        state.cache[active_exp.gate_filepath][active_exp.algorithm].count(active_exp.mu_val) &&
        state.cache[active_exp.gate_filepath][active_exp.algorithm][active_exp.mu_val].count(start_key)) {
        current_experiment_data = state.cache[active_exp.gate_filepath][active_exp.algorithm][active_exp.mu_val][start_key];
    }

    if (ImGui::BeginTable("ExperimentSummaryTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Gate");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", fs::path(active_exp.gate_filepath).filename().string().c_str());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Algorithm");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%s", active_exp.algorithm.c_str());

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Current mu");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f eV", active_exp.mu_val);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Start Point (eps, lambda)");
        ImGui::TableSetColumnIndex(1); ImGui::Text("(%.1f, %.1f)", active_exp.start_epsilon_r, active_exp.start_lambda_tf);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Total Records");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", current_experiment_data.size());

        size_t operational_count = 0;
        for (const auto& pt : current_experiment_data) {
            if (pt.status == 1) operational_count++;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Operational Points");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", operational_count);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Non-Operational Points");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", current_experiment_data.size() - operational_count);

        ImGui::EndTable();
    }

    ImGui::Separator();
    
    static char save_path_buffer[256] = "./output/operational_results.csv";
    ImGui::InputText("##SaveFilePath", save_path_buffer, sizeof(save_path_buffer));

    if (ImGui::Button("Browse...")) {
        ImGui::OpenPopup("FileBrowserPopup");
    }

    if (BeginFileBrowserPopup("FileBrowserPopup")) {
        DrawFileBrowserBody(save_path_buffer, sizeof(save_path_buffer));

        ImGui::SameLine();
        if (ImGui::Button("Select")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        fs::create_directories(fs::path(save_path_buffer).parent_path());
        SaveDataToCsv(save_path_buffer, current_experiment_data);
    }

    ImGui::End();
}