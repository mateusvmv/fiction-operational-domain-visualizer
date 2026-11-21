#pragma once

#include "../../app.hpp"

#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

// Full lifetime state kept alive by the tab instance. Held by a file-local
// `static` in gate_generator.cpp; the pipeline worker accesses it by reference.
struct GateGeneratorTabState {
    // Skeleton & Truth Table
    char skeleton_path[512] = "portas/skeleton.sqd";
    char tt_buffer[2048]    = "8"; // Default 2-input AND

    // File browser state for skeleton selection
    bool skeleton_browser_should_open = false;

    // TT Editor modal state
    bool tt_editor_should_open = false;
    int  tt_nin = 2, tt_nout = 1;
    std::vector<std::vector<bool>> tt_bits;

    // Generator selections
    bool run_exhaustive = false;
    bool run_quickcell  = true;
    int  min_dbs        = 1;
    int  max_dbs        = 3;

    // Simulation & Filter parameters
    double mu_ev           = -0.32;
    double epsilon_r       = 5.6;
    double lambda_tf       = 5.0;
    double min_crit_temp_k = 5.0;

    // Async execution state
    std::atomic<bool> is_running{false};
    std::mutex        results_mutex;
    std::string       status_message = "Idle";
    bool              has_error      = false;
    std::vector<GeneratedGateResult> results;

    std::vector<std::jthread> workers;
    std::barrier<> barrier;
    size_t num_threads;

    GateGeneratorTabState():
        barrier(std::max(1u, std::thread::hardware_concurrency())),
        num_threads(std::max(1u, std::thread::hardware_concurrency()))
    {}
};

// Convert a comma-separated hex string (e.g. "8,6") into kitty truth tables.
// Throws std::invalid_argument on an empty token.
std::vector<kitty::dynamic_truth_table> ParseTruthTablesFromHex(const char* buffer);

// Worker-thread body. Called once per hardware thread by DrawGateGeneratorTab.
void RunGenerationPipeline(GateGeneratorTabState& tab,
                           std::stop_token stop_token,
                           size_t thread_id);

// Batch export to <output_dir>/ {index.json, circuits/*, radial/*, patterns/*}.
// Runs per-pattern QuickExact simulations at the source point; can take a while.
void ExportAllResults(const std::vector<GeneratedGateResult>& results,
                      double mu_ev,
                      double source_epsilon_r,
                      double source_lambda_tf,
                      const std::vector<kitty::dynamic_truth_table>& spec,
                      const std::string& output_dir = "generated_gates_export");

[[nodiscard]] std::optional<GeneratedGateResult> EvaluateGateLayout(
    const fiction::sidb::layout&                    lyt,
    const std::vector<kitty::dynamic_truth_table>& spec,
    double                                          mu_ev,
    double                                          epsilon_r,
    double                                          lambda_tf,
    std::string                                     generator_name,
    std::size_t                                     num_canvas_dbs,
    std::optional<double>                           min_crit_temp_k = std::nullopt);
