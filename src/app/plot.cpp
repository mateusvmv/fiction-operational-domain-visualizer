#include "../app.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <fmt/format.h>

namespace fs = std::filesystem;

// Convert a full algorithm name to the shorthand used in plot labels.
std::string GetAlgorithmShorthand(const std::string& algorithm)
{
    if (algorithm == "Contour Tracing") return "CT";
    if (algorithm == "Grid Search") return "GS";
    if (algorithm == "Flood Fill") return "FF";
    if (algorithm == "Random Sampling") return "RS";
    if (algorithm == "Radial Search") return "RadS";
    return algorithm;
}

// With a flat cache, "missing" is the only miss state.
enum class CacheMiss { None, Missing };

struct CacheLookup {
    const std::vector<DataPoint>* points = nullptr;
    CacheMiss miss = CacheMiss::None;
};

CacheLookup FindExperimentData(const AppState& state, const ExperimentConfig& experiment)
{
    // ExperimentConfig slices implicitly to its ExperimentKey base for the lookup.
    const auto it = state.cache.find(experiment);
    if (it == state.cache.end()) {
        return {nullptr, CacheMiss::Missing};
    }
    return {&it->second, CacheMiss::None};
}

struct CacheDiagnostics {
    std::size_t enabled = 0;
    std::size_t duplicate_cache_keys = 0;
    std::size_t missing_cache = 0;
    std::size_t empty_data = 0;
    std::size_t no_matching_status = 0;
};

void RecordCacheMiss(CacheDiagnostics& diagnostics, CacheMiss miss)
{
    if (miss == CacheMiss::Missing) ++diagnostics.missing_cache;
}

// The cache's identity is the ExperimentKey (gate/algorithm/mu/start point/
// truth table). Detect repeated keys so fully overlapping series are not
// mistaken for an ImPlot ID collision.
void RecordDuplicateCacheKey(
    CacheDiagnostics& diagnostics,
    std::unordered_set<ExperimentKey, ExperimentKeyHash>& seen_keys,
    const ExperimentConfig& experiment)
{
    if (!seen_keys.insert(experiment).second) {
        ++diagnostics.duplicate_cache_keys;
    }
}

void DrawCacheDiagnostics(const CacheDiagnostics& diagnostics)
{
    if (diagnostics.enabled == 0) {
        ImGui::TextDisabled("No enabled experiments.");
        return;
    }

    if (diagnostics.missing_cache == 0 &&
        diagnostics.empty_data == 0 &&
        diagnostics.no_matching_status == 0 &&
        diagnostics.duplicate_cache_keys == 0) {
        return;
    }

    ImGui::TextDisabled(
        "Enabled: %zu | repeated cache keys: %zu (shared/overlapping data) | missing cache entries: %zu | empty data: %zu | no points for this status: %zu",
        diagnostics.enabled,
        diagnostics.duplicate_cache_keys,
        diagnostics.missing_cache,
        diagnostics.empty_data,
        diagnostics.no_matching_status);
}

struct ActiveExperiment {
    std::size_t index = 0;
    const ExperimentConfig* config = nullptr;
};

// Use pixel-space distance so tooltip hit-testing behaves consistently when
// the user zooms or changes the aspect ratio of the plot.
void DrawScatterTooltip(const ExperimentConfig& experiment,
                        std::size_t experiment_index,
                        const std::vector<DataPoint>& plotted_points)
{
    if (plotted_points.empty() || !ImPlot::IsPlotHovered()) {
        return;
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    float nearest_distance_sq = std::numeric_limits<float>::max();
    std::size_t nearest_index = 0;

    for (std::size_t i = 0; i < plotted_points.size(); ++i) {
        const ImVec2 pixel = ImPlot::PlotToPixels(
            plotted_points[i].epsilon_r, plotted_points[i].lambda_tf);
        const float dx = pixel.x - mouse.x;
        const float dy = pixel.y - mouse.y;
        const float distance_sq = dx * dx + dy * dy;
        if (distance_sq < nearest_distance_sq) {
            nearest_distance_sq = distance_sq;
            nearest_index = i;
        }
    }

    // Ten pixels gives a useful target without depending on data-axis units.
    if (nearest_distance_sq > 100.0f) {
        return;
    }

    const DataPoint& point = plotted_points[nearest_index];
    const std::string algorithm = GetAlgorithmShorthand(experiment.algorithm);
    const std::string gate_name = fs::path(experiment.gate_filepath).filename().string();

    ImGui::BeginTooltip();
    ImGui::Text("%s %s (experiment %zu) (%.2f, %.2f) [%.1f ms]",
        algorithm.c_str(), gate_name.c_str(), experiment_index,
        point.epsilon_r, point.lambda_tf, point.runtime_ms);
    ImGui::EndTooltip();
}

void DrawBarTooltip(const std::vector<ActiveExperiment>& active_experiments,
                    const std::vector<double>& counts)
{
    // IsItemHovered() here only tests the last PlotBars item. Test the plot
    // itself and identify the bar under the mouse instead.
    if (!ImPlot::IsPlotHovered() || active_experiments.size() != counts.size()) {
        return;
    }

    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    const int hovered_index = static_cast<int>(std::round(mouse.x));
    if (hovered_index < 0 || static_cast<std::size_t>(hovered_index) >= counts.size()) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(hovered_index);
    if (mouse.y < 0.0 || mouse.y > counts[index] ||
        std::abs(mouse.x - static_cast<double>(hovered_index)) > 0.25) {
        return;
    }

    const ExperimentConfig& experiment = *active_experiments[index].config;
    const std::string algorithm = GetAlgorithmShorthand(experiment.algorithm);
    const std::string gate_name = fs::path(experiment.gate_filepath).filename().string();

    ImGui::BeginTooltip();
    ImGui::Text("%s %s (experiment %zu) (mu: %.2f, points: %.0f)",
        algorithm.c_str(), gate_name.c_str(), active_experiments[index].index,
        experiment.mu_val, counts[index]);
    ImGui::EndTooltip();
}

void DrawRuntimeBarTooltip(const std::vector<ActiveExperiment>& active_experiments,
                           const std::vector<double>& runtimes)
{
    if (!ImPlot::IsPlotHovered() || active_experiments.size() != runtimes.size()) {
        return;
    }

    const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    const int hovered_index = static_cast<int>(std::round(mouse.x));
    if (hovered_index < 0 || static_cast<std::size_t>(hovered_index) >= runtimes.size()) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(hovered_index);
    if (mouse.y < 0.0 || mouse.y > runtimes[index] ||
        std::abs(mouse.x - static_cast<double>(hovered_index)) > 0.25) {
        return;
    }

    const ExperimentConfig& experiment = *active_experiments[index].config;
    const std::string algorithm = GetAlgorithmShorthand(experiment.algorithm);
    const std::string gate_name = fs::path(experiment.gate_filepath).filename().string();

    ImGui::BeginTooltip();
    ImGui::Text("%s %s (experiment %zu) (mu: %.2f, time: %.2f ms)",
        algorithm.c_str(), gate_name.c_str(), active_experiments[index].index,
        experiment.mu_val, runtimes[index]);
    ImGui::EndTooltip();
}

// Operational Domain scatter plot.
void DrawOperationalDomainTab(const AppState& state, int desired)
{
    std::vector<ActiveExperiment> active_experiments;
    CacheDiagnostics diagnostics;
    std::unordered_set<ExperimentKey, ExperimentKeyHash> seen_cache_keys;

    // Build the filtered point arrays once. They remain alive until the plot
    // has consumed them, and the diagnostics explain why entries were skipped.
    std::vector<std::vector<DataPoint>> scatter_points;

    for (std::size_t i = 0; i < state.experiments.size(); ++i) {
        const ExperimentConfig& experiment = state.experiments[i];
        if (!experiment.enabled) {
            continue;
        }

        ++diagnostics.enabled;
        RecordDuplicateCacheKey(diagnostics, seen_cache_keys, experiment);
        const CacheLookup lookup = FindExperimentData(state, experiment);
        if (lookup.points == nullptr) {
            RecordCacheMiss(diagnostics, lookup.miss);
            continue;
        }
        if (lookup.points->empty()) {
            ++diagnostics.empty_data;
            continue;
        }

        std::vector<DataPoint> matching_points;
        matching_points.reserve(lookup.points->size());
        for (const DataPoint& point : *lookup.points) {
            if (point.status == desired) {
                matching_points.push_back(point);
            }
        }

        if (matching_points.empty()) {
            ++diagnostics.no_matching_status;
            continue;
        }

        scatter_points.push_back(std::move(matching_points));
        active_experiments.push_back({i, &experiment});
    }

    DrawCacheDiagnostics(diagnostics);
    if (ImGui::SmallButton("Fit axes")) {
        ImPlot::SetNextAxisToFit(ImAxis_X1);
        ImPlot::SetNextAxisToFit(ImAxis_Y1);
    }

    if (ImPlot::BeginPlot("SiDB Operational Domain", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Relative Permittivity (epsilon_r)",
                          "Thomas-Fermi Screening Length (lambda_tf)");
        // Do not clamp the axes to 0.5..10.5: valid experiments outside that
        // range were silently clipped. ImPlot can fit the plotted data.

        for (std::size_t k = 0; k < active_experiments.size(); ++k) {
            const ActiveExperiment& active = active_experiments[k];
            const ExperimentConfig& experiment = *active.config;
            const std::vector<DataPoint>& points = scatter_points[k];

            std::vector<double> x_values;
            std::vector<double> y_values;
            x_values.reserve(points.size());
            y_values.reserve(points.size());
            for (const DataPoint& point : points) {
                x_values.push_back(point.epsilon_r);
                y_values.push_back(point.lambda_tf);
            }

            const std::string algorithm = GetAlgorithmShorthand(experiment.algorithm);
            const std::string gate_name = fs::path(experiment.gate_filepath).filename().string();
            const std::string visible_label = fmt::format(
                "{} | {} (experiment {}) | mu = {:.2f} eV",
                gate_name, algorithm, active.index, experiment.mu_val);
            // The ## suffix is an explicit, unique ImPlot item ID. The text
            // before it remains the visible legend label.
            const std::string plot_label = fmt::format(
                "{}##scatter_experiment_{}", visible_label, active.index);

            ImPlot::PlotScatter(plot_label.c_str(), x_values.data(), y_values.data(),
                static_cast<int>(x_values.size()), {
                    ImPlotProp_Marker, ImPlotMarker_Square,
                    ImPlotProp_MarkerSize, 3.5f,
                    ImPlotProp_MarkerFillColor, experiment.color,
                    ImPlotProp_MarkerLineColor, experiment.color
                });

            DrawScatterTooltip(experiment, active.index, points);
        }

        ImPlot::EndPlot();
    }
}

// Calculated-points statistics bar chart.
void DrawCalculatedPointsStatisticsTab(const AppState& state)
{
    std::vector<double> positions;
    std::vector<double> counts;
    std::vector<std::string> labels;
    std::vector<ActiveExperiment> active_experiments;
    CacheDiagnostics diagnostics;
    std::unordered_set<ExperimentKey, ExperimentKeyHash> seen_cache_keys;

    for (std::size_t i = 0; i < state.experiments.size(); ++i) {
        const ExperimentConfig& experiment = state.experiments[i];
        if (!experiment.enabled) {
            continue;
        }

        ++diagnostics.enabled;
        RecordDuplicateCacheKey(diagnostics, seen_cache_keys, experiment);
        const CacheLookup lookup = FindExperimentData(state, experiment);
        if (lookup.points == nullptr) {
            RecordCacheMiss(diagnostics, lookup.miss);
            continue;
        }
        if (lookup.points->empty()) {
            ++diagnostics.empty_data;
        }

        positions.push_back(static_cast<double>(positions.size()));
        counts.push_back(static_cast<double>(lookup.points->size()));
        const std::string algorithm = GetAlgorithmShorthand(experiment.algorithm);
        const std::string gate_name = fs::path(experiment.gate_filepath).filename().string();
        labels.push_back(fmt::format("{} [{}] (experiment {})",
            gate_name, algorithm, i));
        active_experiments.push_back({i, &experiment});
    }

    DrawCacheDiagnostics(diagnostics);

    if (ImPlot::BeginPlot("Total Points per Experiment", ImVec2(-1, -1), ImPlotFlags_None)) {
        ImPlot::SetupAxes("Experiments", "Total Points Calculated",
                          ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);

        if (!counts.empty()) {
            std::vector<const char*> label_pointers;
            label_pointers.reserve(labels.size());
            for (const std::string& label : labels) {
                label_pointers.push_back(label.c_str());
            }

            ImPlot::SetupAxisTicks(ImAxis_X1, positions.data(),
                static_cast<int>(positions.size()), label_pointers.data());

            for (std::size_t k = 0; k < counts.size(); ++k) {
                const double position = positions[k];
                const double value = counts[k];
                const std::string item_id = fmt::format(
                    "##points_experiment_{}", active_experiments[k].index);

                ImPlot::PlotBars(item_id.c_str(), &position, &value, 1, 0.4f, {
                    ImPlotProp_MarkerFillColor, active_experiments[k].config->color,
                    ImPlotProp_MarkerLineColor, active_experiments[k].config->color
                });
            }

            DrawBarTooltip(active_experiments, counts);
        }

        ImPlot::EndPlot();
    }
}

// Runtime statistics bar chart.
void DrawRuntimeStatisticsTab(const AppState& state)
{
    std::vector<double> positions;
    std::vector<double> runtimes;
    std::vector<std::string> labels;
    std::vector<ActiveExperiment> active_experiments;
    CacheDiagnostics diagnostics;
    std::unordered_set<ExperimentKey, ExperimentKeyHash> seen_cache_keys;

    for (std::size_t i = 0; i < state.experiments.size(); ++i) {
        const ExperimentConfig& experiment = state.experiments[i];
        if (!experiment.enabled) {
            continue;
        }

        ++diagnostics.enabled;
        RecordDuplicateCacheKey(diagnostics, seen_cache_keys, experiment);
        const CacheLookup lookup = FindExperimentData(state, experiment);
        if (lookup.points == nullptr) {
            RecordCacheMiss(diagnostics, lookup.miss);
            continue;
        }
        if (lookup.points->empty()) {
            ++diagnostics.empty_data;
            continue;
        }

        positions.push_back(static_cast<double>(positions.size()));
        // Runtime is stored on every point from the same run. Use the first
        // point as the experiment-level runtime, as the original code did.
        runtimes.push_back(lookup.points->front().runtime_ms);
        const std::string algorithm = GetAlgorithmShorthand(experiment.algorithm);
        const std::string gate_name = fs::path(experiment.gate_filepath).filename().string();
        labels.push_back(fmt::format("{} [{}] (experiment {})",
            gate_name, algorithm, i));
        active_experiments.push_back({i, &experiment});
    }

    DrawCacheDiagnostics(diagnostics);

    if (ImPlot::BeginPlot("Algorithm Runtime per Experiment", ImVec2(-1, -1), ImPlotFlags_None)) {
        ImPlot::SetupAxes("Experiments", "Runtime (ms)",
                          ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);

        if (!runtimes.empty()) {
            std::vector<const char*> label_pointers;
            label_pointers.reserve(labels.size());
            for (const std::string& label : labels) {
                label_pointers.push_back(label.c_str());
            }

            ImPlot::SetupAxisTicks(ImAxis_X1, positions.data(),
                static_cast<int>(positions.size()), label_pointers.data());

            for (std::size_t k = 0; k < runtimes.size(); ++k) {
                const double position = positions[k];
                const double value = runtimes[k];
                const std::string item_id = fmt::format(
                    "##runtime_experiment_{}", active_experiments[k].index);

                ImPlot::PlotBars(item_id.c_str(), &position, &value, 1, 0.4f, {
                    ImPlotProp_MarkerFillColor, active_experiments[k].config->color,
                    ImPlotProp_MarkerLineColor, active_experiments[k].config->color
                });
            }

            DrawRuntimeBarTooltip(active_experiments, runtimes);
        }

        ImPlot::EndPlot();
    }
}

// Main window orchestrator.
void DrawOperationalPlot(AppState& state)
{
    ImGui::SetNextWindowSize(ImVec2(800, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Operational Domain Plot")) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("PlotTabs")) {
        if (ImGui::BeginTabItem("Operational Domain")) {
            DrawOperationalDomainTab(state, 1);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Non-Operational Domain")) {
            DrawOperationalDomainTab(state, 0);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Calculated Points Statistics")) {
            DrawCalculatedPointsStatisticsTab(state);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Runtime Statistics")) {
            DrawRuntimeStatisticsTab(state);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
