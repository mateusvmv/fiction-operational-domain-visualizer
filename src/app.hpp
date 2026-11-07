#pragma once

#include "operational_data.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstddef>
#include <cstdint>

#include "raylib.h"
#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"

struct ExperimentConfig {
    std::string gate_filepath;
    std::string algorithm = "Contour Tracing"; // "Contour Tracing", "Grid Search", "Flood Fill", "Random Sampling"
    double mu_val = -0.32;
    double start_epsilon_r = 5.0;
    double start_lambda_tf = 5.0;
    ImVec4 color = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
    bool enabled = true;
};


// Exact cache key for the starting coordinates. Using a formatted string here
// (for example "{:.1f}_{:.1f}") silently merged distinct start points.
struct StartPointKey {
    double epsilon_r = 0.0;
    double lambda_tf = 0.0;

    bool operator==(const StartPointKey& other) const noexcept {
        return epsilon_r == other.epsilon_r && lambda_tf == other.lambda_tf;
    }
};

struct StartPointKeyHash {
    std::size_t operator()(const StartPointKey& key) const noexcept {
        std::size_t seed = std::hash<double>{}(key.epsilon_r);
        const std::size_t value = std::hash<double>{}(key.lambda_tf);
        seed ^= value + static_cast<std::size_t>(0x9e3779b9U) +
                (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

inline StartPointKey MakeStartPointKey(double epsilon_r, double lambda_tf) noexcept {
    return StartPointKey{epsilon_r, lambda_tf};
}

// Full logical cache identity, used to avoid scheduling the same computation
// more than once when multiple experiment rows have exactly the same settings.
struct ExperimentCacheKey {
    std::string gate_filepath;
    std::string algorithm;
    double mu_val = 0.0;
    StartPointKey start_point;

    bool operator==(const ExperimentCacheKey& other) const noexcept {
        return gate_filepath == other.gate_filepath &&
               algorithm == other.algorithm &&
               mu_val == other.mu_val &&
               start_point == other.start_point;
    }
};

struct ExperimentCacheKeyHash {
    std::size_t operator()(const ExperimentCacheKey& key) const noexcept {
        std::size_t seed = std::hash<std::string>{}(key.gate_filepath);
        auto combine = [&seed](std::size_t value) {
            seed ^= value + static_cast<std::size_t>(0x9e3779b9U) +
                    (seed << 6U) + (seed >> 2U);
        };
        combine(std::hash<std::string>{}(key.algorithm));
        combine(std::hash<double>{}(key.mu_val));
        combine(std::hash<double>{}(key.start_point.epsilon_r));
        combine(std::hash<double>{}(key.start_point.lambda_tf));
        return seed;
    }
};

inline ExperimentCacheKey MakeExperimentCacheKey(const ExperimentConfig& experiment) {
    return ExperimentCacheKey{
        experiment.gate_filepath,
        experiment.algorithm,
        experiment.mu_val,
        MakeStartPointKey(experiment.start_epsilon_r, experiment.start_lambda_tf)
    };
}

using StartPointCache = std::unordered_map<
    StartPointKey, std::vector<DataPoint>, StartPointKeyHash>;
using MuCache = std::unordered_map<double, StartPointCache>;
using AlgorithmCache = std::unordered_map<std::string, MuCache>;
using GateCache = std::unordered_map<std::string, AlgorithmCache>;

struct AppState {
    int version = 4;

    // Gate discovery state
    std::vector<std::string> search_directories = {"fiction/experiments/sidb_gate_libraries/siqad_gates"};
    std::vector<std::string> discovered_gates;
    std::vector<std::string> included_gates;

    // Independent experiment configurations
    std::vector<ExperimentConfig> experiments;
    std::size_t selected_experiment_index = 0; // Selected for results window

    // Cache: gate -> algorithm -> mu -> exact (epsilon_r, lambda_tf) -> DataPoints
    GateCache cache;
};

void DrawControlPanel(AppState& state);

void SaveDataToCsv(const std::string& filepath, const std::vector<DataPoint>& plot_data);
void DrawExperimentResultsWindow(AppState& state);
void DrawOperationalPlot(AppState& state);

bool BeginFileBrowserPopup(const std::string &name);
void DrawFileBrowserBody(char* save_path_buffer, std::size_t buffer_size);

// State persistence and validation functions
void SaveAppState(const AppState& state, const std::string& filepath);
bool LoadAppState(AppState& state, const std::string& filepath, int& loaded_version_out);
uint64_t HashAppState(const AppState& state);

void RemoveExperiment(AppState& state, std::size_t index_to_remove);
void RemoveGate(AppState& state, std::size_t index_to_remove);