#pragma once

#include "operational_data.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "raylib.h"
#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"

struct ExperimentConfig {
    std::string gate_filepath;
    std::string algorithm = "Contour Tracking"; // "Contour Tracking", "Grid Search", "Flood Fill", "Random Sampling"
    double mu_val = -0.32;
    double start_epsilon_r = 5.0;
    double start_lambda_tf = 5.0;
    ImVec4 color = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
};

struct AppState {
    int version = 3; // Version bump for experiment configs

    // Gate discovery state
    std::vector<std::string> search_directories = {"fiction/experiments/sidb_gate_libraries/siqad_gates"};
    std::vector<std::string> discovered_gates;
    std::vector<std::string> included_gates;

    // Independent experiment configurations
    std::vector<ExperimentConfig> experiments;
    std::size_t selected_experiment_index = 0;

    // Cache: gate -> algorithm -> mu -> start_point_key -> DataPoints
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<double, std::unordered_map<std::string, std::vector<DataPoint>>>>> cache;
};

void DrawControlPanel(AppState& state);

void SaveDataToCsv(const std::string& filepath, const std::vector<DataPoint>& plot_data);
void DrawExperimentResultsWindow(const std::vector<DataPoint>& current_experiment_data, const ExperimentConfig& active_exp);
void DrawOperationalPlot(const std::vector<DataPoint>& active_data, const ExperimentConfig& active_exp);

bool BeginFileBrowserPopup(const std::string &name);
void DrawFileBrowserBody(char* save_path_buffer, std::size_t buffer_size);

// State persistence and validation functions
void SaveAppState(const AppState& state, const std::string& filepath);
bool LoadAppState(AppState& state, const std::string& filepath, int& loaded_version_out);
uint64_t HashAppState(const AppState& state);

void RemoveExperiment(AppState& state, std::size_t index_to_remove);
void RemoveGate(AppState& state, std::size_t index_to_remove);