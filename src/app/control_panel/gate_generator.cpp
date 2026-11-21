#include "gate_generator.hpp"
#include "../control_panel.hpp"

#include <fiction/technology/sidb/io/write_sqd_layout.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

namespace fs = std::filesystem;

static int CompareDoubles(const double lhs, const double rhs)
{
    const bool lhs_nan = std::isnan(lhs);
    const bool rhs_nan = std::isnan(rhs);
    if (lhs_nan || rhs_nan) {
        return lhs_nan == rhs_nan ? 0 : (lhs_nan ? 1 : -1);
    }
    return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
}

void DrawGateGeneratorTab(AppState& state) {
    static GateGeneratorTabState tab;
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

    // --- 4. Run Trigger & Status / Error Display ---
    ImGui::Spacing();
    ImGui::BeginDisabled(running || (!tab.run_exhaustive && !tab.run_quickcell));
    if (ImGui::Button("Generate & Evaluate Gates")) {
        while (!tab.workers.empty() && tab.workers.back().joinable())
        {
            tab.workers.back().join();
            tab.workers.pop_back();
        }

        // Reset the barrier to full thread count to prevent inter-run deadlocks
        // from prior cancellations/errors that reduced the expected count.
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

    {
        std::scoped_lock lock(tab.results_mutex);
        ImGui::SameLine();
        if (tab.has_error) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", tab.status_message.c_str());
        } else {
            ImGui::TextUnformatted(tab.status_message.c_str());
        }
    }

    // --- 5. Results Table ---
    ImGui::Spacing();
    ImGui::Text("Qualified Gates (Tc >= %.1f K)", tab.min_crit_temp_k);
    ImGui::Separator();

    std::scoped_lock results_lock(tab.results_mutex);
    if (tab.results.empty()) {
        ImGui::TextDisabled("No qualified gates generated yet.");
        return;
    }

    const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("GeneratedGatesTable", 6, table_flags, ImVec2(0, 260))) {
        ImGui::TableSetupColumn("Generator");
        ImGui::TableSetupColumn("#DBs");
        ImGui::TableSetupColumn("Tc (K)");
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