#include "gate_generator.hpp"
#include "../control_panel.hpp"

#include <fiction/technology/sidb/io/read_sqd_layout.hpp>
#include <fiction/technology/sidb/io/write_sqd_layout.hpp>
#include <fiction/technology/sidb/lattice.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

namespace fs = std::filesystem;

int CompareDoubles(const double lhs, const double rhs)
{
    const bool lhs_nan = std::isnan(lhs);
    const bool rhs_nan = std::isnan(rhs);
    if (lhs_nan || rhs_nan) {
        return lhs_nan == rhs_nan ? 0 : (lhs_nan ? 1 : -1);
    }
    return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
}

std::vector<std::string> CollectCandidatePreloadedPaths(const AppState& state)
{
    std::vector<std::string> candidates = state.included_gates;

    for (const auto& dir : {"generated_gates", "portas"}) {
        try {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                for (const auto& entry : fs::directory_iterator(dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".sqd") {
                        candidates.push_back(entry.path().string());
                    }
                }
            }
        } catch (const fs::filesystem_error&) {
            // Ignore unreadable directories in candidate discovery
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}

std::size_t CountAddedSidbs(const fiction::sidb::layout& gate_lyt,
                                          const fiction::sidb::layout& skeleton_lyt) noexcept
{
    const std::size_t gate_count     = gate_lyt.sidbs().size();
    const std::size_t skeleton_count = skeleton_lyt.sidbs().size();
    return gate_count > skeleton_count ? (gate_count - skeleton_count) : 0;
}

void DrawPreloadedGatesPopup(AppState& state,
                             GateGeneratorTabState& tab,
                             bool& should_open,
                             std::map<std::string, bool>& selection_map)
{
    if (should_open) {
        selection_map.clear();
        for (const auto& path : CollectCandidatePreloadedPaths(state)) {
            selection_map[path] = true;
        }
        ImGui::OpenPopup("Select Preloaded Gates");
        should_open = false;
    }

    if (!ImGui::BeginPopupModal("Select Preloaded Gates", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Select which preloaded .sqd gates to evaluate and load into the results set:");
    ImGui::Separator();

    const bool all_selected = !selection_map.empty() &&
                              std::all_of(selection_map.begin(), selection_map.end(),
                                          [](const auto& kv) { return kv.second; });

    if (ImGui::Button(all_selected ? "Deselect All" : "Select All")) {
        const bool next_state = !all_selected;
        for (auto& [path, checked] : selection_map) {
            checked = next_state;
        }
    }

    ImGui::Spacing();

    if (ImGui::BeginChild("PreloadedGatesScroll", ImVec2(520, 240), true)) {
        if (selection_map.empty()) {
            ImGui::TextWrapped("No candidate .sqd files found.");
        } else {
            for (auto& [path, checked] : selection_map) {
                ImGui::Checkbox(path.c_str(), &checked);
            }
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Load & Evaluate Selected")) {
        try {
            const auto spec = ParseTruthTablesFromHex(tab.tt_buffer);
            if (spec.empty()) {
                throw std::invalid_argument("Truth table is empty or invalid.");
            }

            std::vector<GeneratedGateResult> evaluated;
            for (const auto& [path, checked] : selection_map) {
                if (!checked) {
                    continue;
                }
                if (!fs::exists(path)) {
                    throw std::runtime_error(fmt::format("Preloaded gate file not found: '{}'", path));
                }

                const auto lyt = fiction::sidb::io::read_sqd_layout(path);

                std::size_t canvas_sidb_count = lyt.dots_with_tag(fiction::sidb::dot_tag::LOGIC).size();
                if (!std::string_view{tab.skeleton_path}.empty() && fs::exists(tab.skeleton_path))
                {
                    const auto skeleton_lyt = fiction::sidb::io::read_sqd_layout(tab.skeleton_path);
                    canvas_sidb_count = CountAddedSidbs(lyt, skeleton_lyt);
                }

                auto res = EvaluateGateLayout(
                    lyt, spec, tab.mu_ev, tab.epsilon_r, tab.lambda_tf,
                    "Preloaded", canvas_sidb_count, std::nullopt);

                if (res.has_value()) {
                    res->saved_sqd_path = path;
                    evaluated.push_back(std::move(*res));
                }
            }

            const std::size_t count = evaluated.size();
            {
                std::scoped_lock lock(tab.results_mutex);
                tab.results        = std::move(evaluated);
                tab.status_message = fmt::format("Loaded and evaluated {} preloaded gates.", count);
                tab.has_error      = false;
            }
            selection_map.clear();
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& e) {
            std::scoped_lock lock(tab.results_mutex);
            tab.status_message = fmt::format("Preload error: {}", e.what());
            tab.has_error      = true;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        selection_map.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void DrawGateGeneratorTab(AppState& state) {
    static GateGeneratorTabState tab;
    static bool preloaded_popup_should_open = false;
    static std::map<std::string, bool> preloaded_selection;

    const bool running = tab.is_running.load();

    ImGui::BeginDisabled(running);

    // --- 1. Skeleton & Truth Table ---
    ImGui::Text("Skeleton & Function");
    ImGui::Separator();

    ImGui::InputText("Skeleton (.sqd)", tab.skeleton_path, sizeof(tab.skeleton_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        tab.skeleton_browser_should_open = true;
    }

    if (tab.skeleton_browser_should_open) {
        ImGui::OpenPopup("Select Skeleton File");
        tab.skeleton_browser_should_open = false;
    }

    if (BeginFileBrowserPopup("Select Skeleton File")) {
        DrawFileBrowserBody(tab.skeleton_path, sizeof(tab.skeleton_path));
        ImGui::SameLine();
        if (ImGui::Button("Select")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::InputText("Truth Table", tab.tt_buffer, sizeof(tab.tt_buffer), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Edit as Table...")) {
        auto editor_state = BuildTTEditorFromBuffer(tab.tt_buffer);
        tab.tt_nin  = editor_state.nin;
        tab.tt_nout = editor_state.nout;
        tab.tt_bits = std::move(editor_state.bits);
        tab.tt_editor_should_open = true;
    }

    if (tab.tt_editor_should_open) {
        ImGui::OpenPopup("Generator TT Editor");
        tab.tt_editor_should_open = false;
    }
    if (ImGui::BeginPopupModal("Generator TT Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (RenderTTEditorBody(tab.tt_nin, tab.tt_nout, tab.tt_bits, tab.tt_buffer, sizeof(tab.tt_buffer))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- 2. Generator Configuration ---
    ImGui::Spacing();
    ImGui::Text("Generators");
    ImGui::Separator();
    ImGui::Checkbox("Exhaustive Gate Designer", &tab.run_exhaustive);
    ImGui::SameLine();
    ImGui::Checkbox("QuickCell", &tab.run_quickcell);
    ImGui::SliderInt("Min Canvas SiDBs", &tab.min_dbs, 1, 8);
    ImGui::SliderInt("Max Canvas SiDBs", &tab.max_dbs, tab.min_dbs, 8);

    // --- 3. Physical & Quality Filter Parameters ---
    ImGui::Spacing();
    ImGui::Text("Simulation & Quality Filter");
    ImGui::Separator();
    ImGui::InputDouble("mu (eV)", &tab.mu_ev, 0.01, 0.1, "%.2f");
    ImGui::InputDouble("Start Epsilon (r)", &tab.epsilon_r, 0.1, 1.0, "%.2f");
    ImGui::InputDouble("Start Lambda (tf)", &tab.lambda_tf, 0.1, 1.0, "%.2f");
    ImGui::InputDouble("Min Critical Temp (K)", &tab.min_crit_temp_k, 0.5, 1.0, "%.1f");

    ImGui::EndDisabled();

    // --- 4. Run Trigger, Preloaded Selection & Status / Error Display ---
    ImGui::Spacing();
    ImGui::BeginDisabled(running || (!tab.run_exhaustive && !tab.run_quickcell));
    if (ImGui::Button("Generate & Evaluate Gates")) {
        while (!tab.workers.empty() && tab.workers.back().joinable())
        {
            tab.workers.back().join();
            tab.workers.pop_back();
        }

        tab.barrier.~barrier();
        new (&tab.barrier) std::barrier<>(static_cast<std::ptrdiff_t>(tab.num_threads));

        tab.is_running = true;
        for (size_t i = 0; i < tab.num_threads; ++i)
        {
            tab.workers.emplace_back(std::jthread([i](const std::stop_token stop_token) {
                RunGenerationPipeline(tab, stop_token, i);
            }));
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Load Preloaded Gates")) {
        preloaded_popup_should_open = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Export All Results")) {
        std::vector<GeneratedGateResult> results_copy;
        {
            std::scoped_lock lock(tab.results_mutex);
            if (!tab.results.empty()) results_copy = tab.results;
        }
        if (!results_copy.empty()) {
            try {
                const auto spec = ParseTruthTablesFromHex(tab.tt_buffer);
                {
                    std::scoped_lock lock(tab.results_mutex);
                    tab.status_message = fmt::format(
                        "Exporting {} gates with per-pattern snapshots (this runs {} QuickExact "
                        "simulations per gate — expect this to take a while)...",
                        results_copy.size(), std::size_t{1} << spec.front().num_vars());
                }
                ExportAllResults(results_copy, tab.mu_ev, tab.epsilon_r, tab.lambda_tf, spec,
                                 "generated_gates_export");
                std::scoped_lock lock(tab.results_mutex);
                tab.status_message = fmt::format("Exported {} gates with patterns to generated_gates_export/.",
                                                 results_copy.size());
                tab.has_error = false;
            } catch (const std::exception& e) {
                std::scoped_lock lock(tab.results_mutex);
                tab.status_message = fmt::format("Export error: {}", e.what());
                tab.has_error = true;
            }
        }
    }
    ImGui::EndDisabled();

    DrawPreloadedGatesPopup(state, tab, preloaded_popup_should_open, preloaded_selection);

    {
        std::scoped_lock lock(tab.results_mutex);
        ImGui::Spacing();
        if (tab.has_error) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", tab.status_message.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextWrapped("%s", tab.status_message.c_str());
        }
    }

    // --- 5. Results Table ---
    ImGui::Spacing();
    ImGui::Text("Qualified Gates (Tc >= %.1f K)", tab.min_crit_temp_k);
    ImGui::Separator();

    std::scoped_lock results_lock(tab.results_mutex);
    if (tab.results.empty()) {
        ImGui::TextWrapped("No qualified gates generated yet.");
        return;
    }

    const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("GeneratedGatesTable", 6, table_flags, ImVec2(0, 260))) {
        ImGui::TableSetupColumn("Generator");
        ImGui::TableSetupColumn("#DBs");
        ImGui::TableSetupColumn("CT (K)");
        ImGui::TableSetupColumn("Domain Dist (Ra)");
        ImGui::TableSetupColumn("Op. Samples");
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();

        std::vector<std::size_t> display_order(tab.results.size());
        std::iota(display_order.begin(), display_order.end(), std::size_t{0});

        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs();
            sort_specs != nullptr && sort_specs->SpecsCount > 0) {
            const auto compare_column = [](const std::size_t lhs_index,
                                                const std::size_t rhs_index,
                                                const ImGuiTableColumnSortSpecs& spec) {
                const auto& lhs = tab.results[lhs_index];
                const auto& rhs = tab.results[rhs_index];
                switch (spec.ColumnIndex) {
                    case 0: return lhs.generator_name.compare(rhs.generator_name);
                    case 1: return lhs.num_canvas_dbs < rhs.num_canvas_dbs ? -1 :
                                   (lhs.num_canvas_dbs > rhs.num_canvas_dbs ? 1 : 0);
                    case 2: return CompareDoubles(lhs.critical_temp_k, rhs.critical_temp_k);
                    case 3: return CompareDoubles(lhs.domain_distance, rhs.domain_distance);
                    case 4: return lhs.num_op_samples < rhs.num_op_samples ? -1 :
                                   (lhs.num_op_samples > rhs.num_op_samples ? 1 : 0);
                    default: return 0;
                }
            };

            std::stable_sort(display_order.begin(), display_order.end(),
                             [&compare_column, sort_specs](const std::size_t lhs, const std::size_t rhs) {
                for (int sort_index = 0; sort_index < sort_specs->SpecsCount; ++sort_index) {
                    const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[sort_index];
                    const int comparison = compare_column(lhs, rhs, spec);
                    if (comparison == 0) {
                        continue;
                    }
                    return spec.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0 : comparison > 0;
                }
                return lhs < rhs;
            });
            sort_specs->SpecsDirty = false;
        }

        for (const std::size_t result_index : display_order) {
            auto& result = tab.results[result_index];
            ImGui::PushID(static_cast<int>(result_index));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(result.generator_name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu", result.num_canvas_dbs);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", result.critical_temp_k);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.4f", result.domain_distance);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%zu", result.num_op_samples);

            ImGui::TableSetColumnIndex(5);
            if (ImGui::Button("Export & Add")) {
                fs::create_directories("generated_gates");
                const std::string output_path = fmt::format(
                    "generated_gates/{}_{}db_tc{:.1f}_dist{:.2f}_{}.sqd",
                    result.generator_name, result.num_canvas_dbs,
                    result.critical_temp_k, result.domain_distance, result_index);

                fiction::sidb::io::write_sqd_layout(result.layout, output_path);
                result.saved_sqd_path = output_path;

                if (std::find(state.included_gates.begin(), state.included_gates.end(), output_path) ==
                    state.included_gates.end()) {
                    state.included_gates.push_back(output_path);
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
