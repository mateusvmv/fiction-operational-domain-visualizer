#include "app.hpp"
#include <algorithm>

void RemoveExperiment(AppState& state, std::size_t index_to_remove)
{
    if (index_to_remove >= state.experiments.size()) return;
    
    state.experiments.erase(state.experiments.begin() + static_cast<std::ptrdiff_t>(index_to_remove));
    
    if (state.selected_experiment_index >= state.experiments.size()) {
        state.selected_experiment_index = state.experiments.empty() ? 0 : state.experiments.size() - 1;
    }
}

void RemoveGate(AppState& state, std::size_t index_to_remove) {
    if (index_to_remove >= state.included_gates.size()) return;
    std::string removed_gate = state.included_gates[index_to_remove];
    
    // Remove any experiments associated with this gate filepath
    state.experiments.erase(
        std::remove_if(state.experiments.begin(), state.experiments.end(),
            [&removed_gate](const ExperimentConfig& exp) {
                return exp.gate_filepath == removed_gate;
            }),
        state.experiments.end()
    );

    state.cache.erase(removed_gate);
    state.included_gates.erase(state.included_gates.begin() + static_cast<std::ptrdiff_t>(index_to_remove));
    
    if (state.selected_experiment_index >= state.experiments.size()) {
        state.selected_experiment_index = state.experiments.empty() ? 0 : state.experiments.size() - 1;
    }
}