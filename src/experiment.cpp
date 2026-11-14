#include "app.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <stop_token>
#include <algorithm>
#include <unordered_set>
#include <utility>

struct ExperimentWorkerState {
    std::atomic<bool>     is_running{false};
    std::atomic<bool>     is_finished{false};
    std::string           status_message = "Computing operational domain...";
    std::jthread          worker_thread;
    std::size_t           computing_index = 0;
    ExperimentConfig      computing_exp;
    std::vector<DataPoint> computed_result;
};

static ExperimentWorkerState g_exp_worker;

std::size_t CacheCompletion(const AppState& state)
{
    std::size_t done = 0;
    for (const auto& exp : state.experiments) {
        if (state.cache.find(exp) != state.cache.end()) ++done;
    }
    return done;
}

std::size_t SyncCache(AppState& state)
{
    // If worker is running, check if it has finished computing
    if (g_exp_worker.is_running.load())
    {
        if (g_exp_worker.is_finished.load())
        {
            if (g_exp_worker.worker_thread.joinable()) {
                g_exp_worker.worker_thread.join();
            }

            // Insert computed result into cache safely on the main thread
            state.cache.emplace(g_exp_worker.computing_exp, std::move(g_exp_worker.computed_result));

            g_exp_worker.is_running.store(false);
            g_exp_worker.is_finished.store(false);
        }
        else
        {
            // Return remaining uncomputed count while running
            std::size_t remaining = 0;
            for (const auto& exp : state.experiments) {
                if (state.cache.find(exp) == state.cache.end()) ++remaining;
            }
            return remaining;
        }
    }

    // Find the first uncomputed experiment in the list
    auto it = std::find_if(state.experiments.begin(), state.experiments.end(),
        [&state](const ExperimentConfig& exp) {
            return state.cache.find(exp) == state.cache.end();
        });

    if (it == state.experiments.end()) {
        return 0; // All experiments cached
    }

    // Start background computation for this experiment
    g_exp_worker.computing_index = std::distance(state.experiments.begin(), it);
    g_exp_worker.computing_exp = *it;
    g_exp_worker.is_running.store(true);
    g_exp_worker.is_finished.store(false);
    g_exp_worker.status_message = "Computing operational domain for " + it->gate_filepath + "...";

    ExperimentConfig target_exp = *it;
    g_exp_worker.worker_thread = std::jthread([target_exp](std::stop_token st) {
        auto data = ComputeOperationalData(target_exp);
        if (!st.stop_requested()) {
            g_exp_worker.computed_result = std::move(data);
            g_exp_worker.is_finished.store(true);
        }
    });

    std::size_t remaining = 0;
    for (const auto& exp : state.experiments) {
        if (state.cache.find(exp) == state.cache.end()) ++remaining;
    }
    return remaining;
}

void DrawExperimentProgressModal(AppState& state)
{
    if (g_exp_worker.is_running.load()) {
        ImGui::OpenPopup("Computing Experiment...");
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (ImGui::BeginPopupModal("Computing Experiment...", nullptr, flags)) {
        ImGui::Text("Running background experiment computation...");
        ImGui::Separator();
        
        ImGui::Text("%s", g_exp_worker.status_message.c_str());

        ImGui::Spacing();
        if (g_exp_worker.worker_thread.joinable()) {
            g_exp_worker.worker_thread.join();
            ImGui::CloseCurrentPopup();
        }
        else if (ImGui::Button("Cancel & Remove Experiment", ImVec2(220, 0)))
        {
            g_exp_worker.worker_thread.request_stop();
            g_exp_worker.is_running.store(false);
            g_exp_worker.is_finished.store(false);

            // Remove the active experiment so it doesn't fail without data
            RemoveExperiment(state, g_exp_worker.computing_index);

            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void PruneCache(AppState& state)
{
    std::unordered_set<ExperimentKey, ExperimentKeyHash> active;
    active.reserve(state.experiments.size() * 2);
    for (const auto& exp : state.experiments) {
        active.insert(static_cast<const ExperimentKey&>(exp));
    }

    for (auto it = state.cache.begin(); it != state.cache.end(); ) {
        if (active.find(it->first) == active.end()) {
            it = state.cache.erase(it);
        } else {
            ++it;
        }
    }
}

void RemoveExperiment(AppState& state, std::size_t index_to_remove)
{
    if (index_to_remove >= state.experiments.size()) return;
    state.experiments.erase(state.experiments.begin() + static_cast<std::ptrdiff_t>(index_to_remove));
    if (state.selected_experiment_index >= state.experiments.size()) {
        state.selected_experiment_index = state.experiments.empty() ? 0 : state.experiments.size() - 1;
    }
}

void RemoveGate(AppState& state, std::size_t index_to_remove)
{
    if (index_to_remove >= state.included_gates.size()) return;
    const std::string removed_gate = state.included_gates[index_to_remove];

    for (auto it = state.cache.begin(); it != state.cache.end(); ) {
        if (it->first.gate_filepath == removed_gate) {
            it = state.cache.erase(it);
        } else {
            ++it;
        }
    }

    state.experiments.erase(
        std::remove_if(state.experiments.begin(), state.experiments.end(),
            [&removed_gate](const ExperimentConfig& exp) {
                return exp.gate_filepath == removed_gate;
            }),
        state.experiments.end());

    state.included_gates.erase(state.included_gates.begin() + static_cast<std::ptrdiff_t>(index_to_remove));

    if (state.selected_experiment_index >= state.experiments.size()) {
        state.selected_experiment_index = state.experiments.empty() ? 0 : state.experiments.size() - 1;
    }
}

CacheLookup FindExperimentData(const AppState& state, const ExperimentConfig& experiment)
{
    const auto it = state.cache.find(experiment);
    if (it == state.cache.end()) {
        return {nullptr};
    }
    return {&it->second};
}
