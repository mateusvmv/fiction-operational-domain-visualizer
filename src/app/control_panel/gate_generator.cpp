#include "../control_panel.hpp"

#include <fiction/technology/sidb/generators/design_gates.hpp>
#include <fiction/technology/sidb/io/read_sqd_layout.hpp>
#include <fiction/technology/sidb/io/write_sqd_layout.hpp>
#include <fiction/technology/sidb/simulation/analysis/critical_temperature.hpp>
#include <fiction/technology/sidb/simulation/logic/operational_domain.hpp>

#include <fmt/format.h>
#include <kitty/dynamic_truth_table.hpp>
#include <mockturtle/utils/stopwatch.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cmath>
#include <filesystem>
#include <random>
#include <stop_token>
#include <thread>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

struct GateGeneratorTabState {
    // Skeleton & Truth Table
    char skeleton_path[512] = "fiction/experiments/gate_skeletons/skeleton_bestagons_with_tags/skeleton_hex_inputsdbp_2i1o.sqd";
    char tt_buffer[2048]    = "8"; // Default 2-input AND

    // File browser state for skeleton selection
    bool skeleton_browser_should_open = false;

    // TT Editor modal state
    bool tt_editor_should_open = false;
    int  tt_nin = 2, tt_nout = 1;
    std::vector<std::vector<bool>> tt_bits;

    // Generator selections
    bool run_exhaustive = true;
    bool run_quickcell  = true;
    int  min_dbs        = 1;
    int  max_dbs        = 4;

    // Simulation & Filter parameters
    double mu_ev           = -0.32;
    double epsilon_r       = 5.0;
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

static int CompareDoubles(const double lhs, const double rhs) {
    const bool lhs_nan = std::isnan(lhs);
    const bool rhs_nan = std::isnan(rhs);
    if (lhs_nan || rhs_nan) {
        return lhs_nan == rhs_nan ? 0 : (lhs_nan ? 1 : -1);
    }
    return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
}

static std::vector<kitty::dynamic_truth_table> ParseTruthTablesFromHex(const char* buffer) {
    const auto tokens = ParseHexTokens(buffer);
    std::vector<kitty::dynamic_truth_table> tts;
    tts.reserve(tokens.size());

    for (const auto& hex : tokens) {
        if (hex.empty()) {
            throw std::invalid_argument("Truth table contains an empty hexadecimal token.");
        }
        const unsigned num_vars = static_cast<unsigned>(std::log2(static_cast<double>(hex.size()) * 4.0));
        kitty::dynamic_truth_table tt(num_vars);
        kitty::create_from_hex_string(tt, hex);
        tts.push_back(std::move(tt));
    }
    return tts;
}

static void RunGenerationPipeline(GateGeneratorTabState& tab, const std::stop_token stop_token, size_t thread_id)
{
    struct RunningReset {
        std::atomic<bool>& running;
        ~RunningReset() { running.store(false); }
    } running_reset{tab.is_running};

    const auto publish_cancelled = [&tab]() {
        std::scoped_lock lock(tab.results_mutex);
        tab.status_message = "Cancelled. Generation worker stopped.";
    };

    try
    {
        if (stop_token.stop_requested())
        {
            publish_cancelled();
            tab.barrier.arrive_and_drop();
            return;
        }

        // Snapshot the tab's config into file-local statics so all workers see
        // a consistent set of values for the duration of this run.
        static std::string skeleton_path_cache;
        static std::string tt_buffer_cache;
        static double mu_ev_cache;
        static double epsilon_r_cache;
        static double lambda_tf_cache;
        static double min_crit_temp_k_cache;
        static bool run_exhaustive_cache;
        static bool run_quickcell_cache;
        static int min_dbs_cache;
        static int max_dbs_cache;

        if (thread_id == 0)
        {
            std::scoped_lock lock(tab.results_mutex);
            tab.results.clear();
            tab.status_message = "Loading skeleton and truth table...";
            tab.has_error = false;

            skeleton_path_cache = tab.skeleton_path;
            tt_buffer_cache = tab.tt_buffer;
            mu_ev_cache = tab.mu_ev;
            epsilon_r_cache = tab.epsilon_r;
            lambda_tf_cache = tab.lambda_tf;
            min_crit_temp_k_cache = tab.min_crit_temp_k;
            run_exhaustive_cache = tab.run_exhaustive;
            run_quickcell_cache = tab.run_quickcell;
            min_dbs_cache = tab.min_dbs;
            max_dbs_cache = tab.max_dbs;
        }
        tab.barrier.arrive_and_wait();

        const auto skeleton = fiction::sidb::io::read_sqd_layout(skeleton_path_cache);
        const auto spec     = ParseTruthTablesFromHex(tt_buffer_cache.c_str());
        if (spec.empty())
        {
            throw std::invalid_argument("Truth table is empty or invalid.");
        }

        const fiction::sidb::model::simulation_parameters sim_params{3, static_cast<float>(mu_ev_cache)};

        fiction::sidb::simulation::logic::operational_domain_params op_domain_params{
            .operational_params = { .sim_params = sim_params }};

        const fiction::sidb::simulation::analysis::critical_temperature_params ct_params{
            .operational_params = op_domain_params.operational_params};

        using DesignMode = fiction::sidb::generators::design_gates_params::design_gates_mode;
        std::vector<std::pair<DesignMode, std::string>> modes;
        if (run_exhaustive_cache) {
            modes.emplace_back(DesignMode::AUTOMATIC_EXHAUSTIVE_GATE_DESIGNER, "Exhaustive");
        }
        if (run_quickcell_cache) {
            modes.emplace_back(DesignMode::QUICKCELL, "QuickCell");
        }

        std::size_t total_candidates                 = 0;
        std::size_t passed_temperature_filter        = 0;
        std::size_t quickcell_candidates             = 0;
        std::size_t quickcell_layouts_enumerated     = 0;
        std::size_t quickcell_first_prune_survivors  = 0;
        std::size_t quickcell_second_prune_survivors = 0;
        std::size_t quickcell_third_prune_survivors  = 0;

        using GeneratedLayoutsType = std::decay_t<decltype(fiction::sidb::generators::design_gates(
            skeleton, spec, fiction::sidb::generators::design_gates_params{}, nullptr))>;
        static GeneratedLayoutsType generated_layouts;
        static fiction::sidb::generators::design_gates_stats gen_stats;
        static std::vector<GeneratedGateResult> scratch_results;
        static std::atomic<std::size_t> result_write_index{0};
        static std::atomic<std::size_t> next_layout_index{0};

        for (const auto& [mode, mode_label] : modes) {
            for (int dbs = min_dbs_cache; dbs <= max_dbs_cache; ++dbs) {
                if (stop_token.stop_requested())
                {
                    publish_cancelled();
                    tab.barrier.arrive_and_drop();
                    return;
                }

                std::size_t num_generated = 0;

                // Thread 0 performs generation and setup OUTSIDE the mutex so
                // the UI thread keeps rendering while design_gates runs.
                if (thread_id == 0)
                {
                    {
                        std::scoped_lock lock(tab.results_mutex);
                        tab.status_message = fmt::format(
                            "Running {} ({} canvas SiDBs)...", mode_label, dbs);
                    }

                    fiction::sidb::generators::design_gates_params gen_params{};
                    gen_params.design_mode            = mode;
                    gen_params.number_of_canvas_sidbs = static_cast<std::size_t>(dbs);
                    gen_params.termination_cond       =
                        fiction::sidb::generators::design_gates_params::termination_condition::ALL_COMBINATIONS_ENUMERATED;
                    gen_params.operational_params     = op_domain_params.operational_params;

                    gen_stats = fiction::sidb::generators::design_gates_stats{};
                    generated_layouts = fiction::sidb::generators::design_gates(skeleton, spec, gen_params, &gen_stats);

                    num_generated = generated_layouts.size();
                    if (num_generated > 10000)
                    {
                        std::random_device rd;
                        std::mt19937 g(rd());
                        std::shuffle(generated_layouts.begin(), generated_layouts.end(), g);
                        generated_layouts.resize(10000);
                    }

                    scratch_results.resize(generated_layouts.size());
                    result_write_index.store(0);
                    next_layout_index.store(0);

                    total_candidates += num_generated;
                    if (mode == DesignMode::QUICKCELL) {
                        quickcell_candidates             += num_generated;
                        quickcell_layouts_enumerated     += gen_stats.number_of_layouts;
                        quickcell_first_prune_survivors  += gen_stats.number_of_layouts_after_first_pruning;
                        quickcell_second_prune_survivors += gen_stats.number_of_layouts_after_second_pruning;
                        quickcell_third_prune_survivors  += gen_stats.number_of_layouts_after_third_pruning;
                    }

                    std::scoped_lock lock(tab.results_mutex);
                    if (mode == DesignMode::QUICKCELL) {
                        tab.status_message = fmt::format(
                            "QuickCell ({} canvas SiDBs): returned {} gates (sampled {}); canvas layouts {}; "
                            "after pruning {}/{}/{}.",
                            dbs, num_generated, generated_layouts.size(), gen_stats.number_of_layouts,
                            gen_stats.number_of_layouts_after_first_pruning,
                            gen_stats.number_of_layouts_after_second_pruning,
                            gen_stats.number_of_layouts_after_third_pruning);
                    } else {
                        tab.status_message = fmt::format(
                            "Evaluating {} candidates (sampled {}) from {} ({} canvas SiDBs)...",
                            num_generated, generated_layouts.size(), mode_label, dbs);
                    }
                }
                tab.barrier.arrive_and_wait();     // setup published

                if (stop_token.stop_requested())
                {
                    publish_cancelled();
                    tab.barrier.arrive_and_drop();
                    return;
                }

                // Evaluate candidates in parallel
                constexpr std::size_t batch_size = 32;
                std::size_t thread_passed_temp = 0;

                while (!stop_token.stop_requested())
                {
                    const std::size_t start = next_layout_index.fetch_add(batch_size, std::memory_order_relaxed);
                    if (start >= generated_layouts.size()) {
                        break;
                    }
                    const std::size_t end = std::min(start + batch_size, generated_layouts.size());

                    if (thread_id == 0) {
                        std::scoped_lock lock(tab.results_mutex);
                        tab.status_message = fmt::format(
                            "Calculating CT and distances (processed ~{} / {})",
                            start, generated_layouts.size());
                    }

                    for (std::size_t i = start; i < end; ++i)
                    {
                        if (stop_token.stop_requested()) {
                            break;
                        }

                        const auto& lyt = generated_layouts[i];
                        const double crit_temp = fiction::sidb::simulation::analysis::critical_temperature_gate_based(
                            lyt, spec, ct_params);
                        if (stop_token.stop_requested()) {
                            break;
                        }
                        if (crit_temp < min_crit_temp_k_cache) {
                            continue;
                        }
                        ++thread_passed_temp;

                        const fiction::sidb::simulation::logic::parameter_point center_point{
                            std::vector<double>{epsilon_r_cache, lambda_tf_cache}};

                        fiction::sidb::simulation::logic::operational_domain_stats ra_stats{};
                        const auto op_domain_ra = fiction::sidb::simulation::logic::operational_domain_radial_search(
                            lyt, spec, center_point, op_domain_params, &ra_stats);
                        if (stop_token.stop_requested()) {
                            break;
                        }
                        const double dist_ra = op_domain_ra.domain_distance(center_point);

                        GeneratedGateResult result{};
                        result.generator_name  = mode_label;
                        result.num_canvas_dbs  = static_cast<std::size_t>(dbs);
                        result.critical_temp_k = crit_temp;
                        result.domain_distance = dist_ra;
                        result.num_op_samples  = ra_stats.num_operational_parameter_combinations;
                        result.runtime_sec     = mockturtle::to_seconds(ra_stats.time_total)
                                               + mockturtle::to_seconds(ra_stats.time_total);
                        result.layout          = lyt;

                        const std::size_t write_idx = result_write_index.fetch_add(1, std::memory_order_relaxed);
                        if (write_idx < scratch_results.size()) {
                            scratch_results[write_idx] = std::move(result);
                        }
                    }
                }

                if (stop_token.stop_requested())
                {
                    publish_cancelled();
                    tab.barrier.arrive_and_drop();
                    return;
                }

                {
                    std::scoped_lock lock(tab.results_mutex);
                    passed_temperature_filter += thread_passed_temp;
                }
                tab.barrier.arrive_and_wait();     // all evaluation writes published

                // Single-thread append of this iteration's survivors into the UI vector.
                // Existing entries (and their saved_sqd_path values) are untouched.
                if (thread_id == 0)
                {
                    const std::size_t final_count = std::min(
                        result_write_index.load(std::memory_order_relaxed),
                        scratch_results.size());

                    std::scoped_lock lock(tab.results_mutex);
                    for (std::size_t i = 0; i < final_count; ++i) {
                        tab.results.push_back(std::move(scratch_results[i]));
                    }
                }
                // No barrier here: next iteration's setup barrier synchronises.
            }
        }

        if (thread_id == 0)
        {
            std::scoped_lock lock(tab.results_mutex);
            if (run_quickcell_cache) {
                tab.status_message = fmt::format(
                    "Done. Found {} qualified gates. QuickCell: {} returned gates from {} canvas layouts "
                    "(pruning survivors {}/{}/{}); all generators returned {}; {} passed Tc >= {:.1f} K.",
                    tab.results.size(), quickcell_candidates, quickcell_layouts_enumerated,
                    quickcell_first_prune_survivors, quickcell_second_prune_survivors,
                    quickcell_third_prune_survivors, total_candidates,
                    passed_temperature_filter, min_crit_temp_k_cache);
            } else {
                tab.status_message = fmt::format(
                    "Done. Found {} qualified gates. Generated candidates: {}; passed Tc filter: {}.",
                    tab.results.size(), total_candidates, passed_temperature_filter);
            }
        }
    }
    catch (const std::exception& e)
    {
        std::scoped_lock lock(tab.results_mutex);
        tab.status_message = fmt::format("Error: {}", e.what());
        tab.has_error = true;
        tab.barrier.arrive_and_drop();
    }
    catch (...)
    {
        std::scoped_lock lock(tab.results_mutex);
        tab.status_message = "Error: unknown exception during gate generation.";
        tab.has_error = true;
        tab.barrier.arrive_and_drop();
    }
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
    ImGui::SliderInt("Min Canvas SiDBs", &tab.min_dbs, 1, 4);
    ImGui::SliderInt("Max Canvas SiDBs", &tab.max_dbs, tab.min_dbs, 4);

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
        new (&tab.barrier) std::barrier<>(tab.num_threads);

        tab.is_running = true;
        for (size_t i = 0; i < tab.num_threads; ++i)
        {
            tab.workers.emplace_back(std::jthread([&tab, i](const std::stop_token stop_token) {
                RunGenerationPipeline(tab, stop_token, i);
            }));
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
                std::filesystem::create_directories("generated_gates");
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