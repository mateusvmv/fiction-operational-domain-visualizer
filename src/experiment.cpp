#include "app.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

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
    std::size_t remaining = 0;
    bool computed_one = false;

    for (const auto& exp : state.experiments) {
        if (state.cache.find(exp) != state.cache.end()) continue;

        if (!computed_one) {
            state.cache.emplace(exp, ComputeOperationalData(exp));
            computed_one = true;
        } else {
            ++remaining;
        }
    }
    return remaining;
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

    // Experiments for this gate lose their cache entries via PruneCache at
    // the end of the frame, but erasing explicitly here keeps RemoveGate
    // self-contained regardless of when it's called.
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
