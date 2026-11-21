#include "gate_generator.hpp"

#include "../control_panel.hpp"

#include <fiction/technology/sidb/generators/design_gates.hpp>
#include <fiction/technology/sidb/io/read_sqd_layout.hpp>
#include <fiction/technology/sidb/lattice.hpp>
#include <fiction/technology/sidb/simulation/analysis/critical_temperature.hpp>
#include <fiction/technology/sidb/simulation/logic/operational_domain.hpp>

#include <fmt/format.h>
#include <mockturtle/utils/stopwatch.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

[[nodiscard]] bool is_within_canvas(
    const fiction::sidb::lattice_site&                                    site,
    const std::pair<fiction::sidb::lattice_site, fiction::sidb::lattice_site>& canvas) noexcept
{
    const auto min_x   = std::min(canvas.first.x, canvas.second.x);
    const auto max_x   = std::max(canvas.first.x, canvas.second.x);
    const auto min_row = std::min(fiction::sidb::row_of(canvas.first), fiction::sidb::row_of(canvas.second));
    const auto max_row = std::max(fiction::sidb::row_of(canvas.first), fiction::sidb::row_of(canvas.second));
    const auto row     = fiction::sidb::row_of(site);

    return site.x >= min_x && site.x <= max_x && row >= min_row && row <= max_row;
}

[[nodiscard]] std::string format_logic_dots(const fiction::sidb::layout& lyt)
{
    const auto logic_dots = lyt.dots_with_tag(fiction::sidb::dot_tag::LOGIC);
    if (logic_dots.empty())
    {
        return "none";
    }

    std::string out{};
    for (std::size_t i = 0; i < logic_dots.size(); ++i)
    {
        if (i > 0)
        {
            out += ", ";
        }
        out += fmt::format("{}", logic_dots[i]);
    }
    return out;
}

std::optional<GeneratedGateResult> EvaluateGateLayout(
    const fiction::sidb::layout&                    lyt,
    const std::vector<kitty::dynamic_truth_table>& spec,
    const double                                    mu_ev,
    const double                                    epsilon_r,
    const double                                    lambda_tf,
    std::string                                     generator_name,
    const std::size_t                               num_canvas_dbs,
    const std::optional<double>                     min_crit_temp_k)
{
    const auto op_domain_params = MakeOperationalDomainParams(mu_ev);

    const fiction::sidb::simulation::analysis::critical_temperature_params ct_params{
        .operational_params = op_domain_params.operational_params};

    const double crit_temp = fiction::sidb::simulation::analysis::critical_temperature_gate_based(
        lyt, spec, ct_params);

    if (min_crit_temp_k.has_value() && crit_temp < *min_crit_temp_k) {
        return std::nullopt;
    }

    const fiction::sidb::simulation::logic::parameter_point center_point{
        std::vector<double>{epsilon_r, lambda_tf}};

    fiction::sidb::simulation::logic::operational_domain_stats ra_stats{};
    const auto op_domain_ra = fiction::sidb::simulation::logic::operational_domain_radial_search(
        lyt, spec, center_point, op_domain_params, &ra_stats);

    const double dist_ra = op_domain_ra.domain_distance(center_point);

    std::vector<RadialSearchPoint> radial_points;
    op_domain_ra.for_each([&](const auto& pp, const auto& op_tuple) {
        const auto& pv = pp.get_parameters();
        if (pv.size() >= 2) {
            radial_points.push_back({
                pv[0], pv[1],
                std::get<0>(op_tuple) ==
                    fiction::sidb::simulation::logic::operational_status::OPERATIONAL ? 1 : 0
            });
        }
    });

    GeneratedGateResult result{};
    result.generator_name  = std::move(generator_name);
    result.num_canvas_dbs  = num_canvas_dbs;
    result.critical_temp_k = crit_temp;
    result.domain_distance = dist_ra;
    result.num_op_samples  = ra_stats.num_operational_parameter_combinations;
    result.runtime_sec     = mockturtle::to_seconds(ra_stats.time_total);
    result.radial_points   = std::move(radial_points);
    result.layout          = lyt;
    return result;
}

void RunGenerationPipeline(GateGeneratorTabState& tab, const std::stop_token stop_token, size_t thread_id)
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

            skeleton_path_cache   = tab.skeleton_path;
            tt_buffer_cache       = tab.tt_buffer;
            mu_ev_cache           = tab.mu_ev;
            epsilon_r_cache       = tab.epsilon_r;
            lambda_tf_cache       = tab.lambda_tf;
            min_crit_temp_k_cache = tab.min_crit_temp_k;
            run_exhaustive_cache  = tab.run_exhaustive;
            run_quickcell_cache   = tab.run_quickcell;
            min_dbs_cache         = tab.min_dbs;
            max_dbs_cache         = tab.max_dbs;
        }
        tab.barrier.arrive_and_wait();

        if (skeleton_path_cache.empty())
        {
            throw std::invalid_argument("Skeleton path must not be empty.");
        }
        if (min_dbs_cache < 0 || max_dbs_cache < min_dbs_cache)
        {
            throw std::invalid_argument("Invalid canvas SiDB count range (min_dbs / max_dbs).");
        }

        const auto skeleton = fiction::sidb::io::read_sqd_layout(skeleton_path_cache);
        const auto spec     = ParseTruthTablesFromHex(tt_buffer_cache.c_str());
        if (spec.empty())
        {
            throw std::invalid_argument("Truth table is empty or invalid.");
        }
        if (spec.front().num_vars() == 0)
        {
            throw std::invalid_argument("Truth table must have at least one input variable.");
        }

        const std::unordered_set<fiction::sidb::lattice_site> skeleton_logic_dots{
            skeleton.dots_with_tag(fiction::sidb::dot_tag::LOGIC).cbegin(),
            skeleton.dots_with_tag(fiction::sidb::dot_tag::LOGIC).cend()};

        const auto op_domain_params = MakeOperationalDomainParams(mu_ev_cache);

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

                    fiction::sidb::generators::design_gates_params gen_params
                    {
                        .operational_params     = op_domain_params.operational_params,
                        .design_mode            = mode,
                        .canvas                 = {{0, 1, 1}, {10, 3, 1}},
                        .number_of_canvas_sidbs = static_cast<std::size_t>(dbs),
                        .termination_cond       = fiction::sidb::generators::design_gates_params::termination_condition::ALL_COMBINATIONS_ENUMERATED
                    };

                    gen_stats         = fiction::sidb::generators::design_gates_stats{};
                    generated_layouts = fiction::sidb::generators::design_gates(skeleton, spec, gen_params, &gen_stats);

                    for (const auto& cand : generated_layouts)
                    {
                        std::size_t added_logic_dots = 0;
                        for (const auto& logic_site : cand.dots_with_tag(fiction::sidb::dot_tag::LOGIC))
                        {
                            if (!skeleton_logic_dots.contains(logic_site))
                            {
                                ++added_logic_dots;
                                if (!is_within_canvas(logic_site, gen_params.canvas))
                                {
                                    const auto msg = fmt::format(
                                        "Canvas SiDB {} placed outside configured canvas ({} to {}, row {} to {}).",
                                        logic_site,
                                        gen_params.canvas.first,
                                        gen_params.canvas.second,
                                        fiction::sidb::row_of(gen_params.canvas.first),
                                        fiction::sidb::row_of(gen_params.canvas.second));
                                    std::cerr << "[error] " << msg << "\n";
                                    throw std::runtime_error(msg);
                                }
                            }
                        }
                        if (added_logic_dots != static_cast<std::size_t>(dbs))
                        {
                            const auto msg = fmt::format(
                                "Unexpected number of newly added canvas LOGIC SiDBs: got {}, expected {}.",
                                added_logic_dots, dbs);
                            std::cerr << "[error] " << msg << "\n";
                            throw std::runtime_error(msg);
                        }
                    }

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

                        std::vector<RadialSearchPoint> radial_points;
                        op_domain_ra.for_each([&](const auto& pp, const auto& op_tuple) {
                            const auto& pv = pp.get_parameters();
                            if (pv.size() >= 2) {
                                radial_points.push_back({
                                    pv[0], pv[1],
                                    std::get<0>(op_tuple) ==
                                        fiction::sidb::simulation::logic::operational_status::OPERATIONAL ? 1 : 0
                                });
                            }
                        });

                        GeneratedGateResult result{};
                        result.generator_name  = mode_label;
                        result.num_canvas_dbs  = static_cast<std::size_t>(dbs);
                        result.critical_temp_k = crit_temp;
                        result.domain_distance = dist_ra;
                        result.num_op_samples  = ra_stats.num_operational_parameter_combinations;
                        result.runtime_sec     = mockturtle::to_seconds(ra_stats.time_total);
                        result.radial_points   = std::move(radial_points);
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
        std::cerr << fmt::format("[error] RunGenerationPipeline: {}\n", e.what());
        std::scoped_lock lock(tab.results_mutex);
        tab.status_message = fmt::format("Error: {}", e.what());
        tab.has_error = true;
        tab.barrier.arrive_and_drop();
    }
    catch (...)
    {
        std::cerr << "[error] RunGenerationPipeline: unknown exception\n";
        std::scoped_lock lock(tab.results_mutex);
        tab.status_message = "Error: unknown exception during gate generation.";
        tab.has_error = true;
        tab.barrier.arrive_and_drop();
    }
}
