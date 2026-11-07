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

// User-facing experiment: an ExperimentKey plus presentation and enabled state.
// Slices implicitly to ExperimentKey for cache lookups and ComputeOperationalData.
struct ExperimentConfig : ExperimentKey {
    ImVec4 color   = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
    bool   enabled = true;
};

// Flat cache: one entry per distinct ExperimentKey (gate + algorithm + mu +
// start point + truth table). Lookup, sync and prune are all single find/
// insert/erase operations.
using ExperimentDataCache =
    std::unordered_map<ExperimentKey, std::vector<DataPoint>, ExperimentKeyHash>;

struct AppState {
    int version = 6;

    // Gate discovery state
    std::vector<std::string> search_directories = {"fiction/experiments/sidb_gate_libraries/siqad_gates"};
    std::vector<std::string> discovered_gates;
    std::vector<std::string> included_gates;

    // Independent experiment configurations
    std::vector<ExperimentConfig> experiments;
    std::size_t selected_experiment_index = 0;

    // ExperimentKey -> DataPoints
    ExperimentDataCache cache;
};

void DrawControlPanel(AppState& state);

void SaveDataToCsv(const std::string& filepath, const std::vector<DataPoint>& plot_data);
void DrawExperimentResultsWindow(AppState& state);
void DrawOperationalPlot(AppState& state);

bool BeginFileBrowserPopup(const std::string& name);
void DrawFileBrowserBody(char* save_path_buffer, std::size_t buffer_size);

void SaveAppState(const AppState& state, const std::string& filepath);
bool LoadAppState(AppState& state, const std::string& filepath, int& loaded_version_out);
uint64_t HashAppState(const AppState& state);

void RemoveExperiment(AppState& state, std::size_t index_to_remove);
void RemoveGate(AppState& state, std::size_t index_to_remove);

// --- Cache maintenance -----------------------------------------------------

// Number of experiments whose cache entry is present.
std::size_t CacheCompletion(const AppState& state);

// Fill in missing cache entries; at most ONE computation per call so a
// frame stays responsive. Returns how many experiments still need computing.
std::size_t SyncCache(AppState& state);

// Drop cache entries not referenced by any experiment.
void PruneCache(AppState& state);
