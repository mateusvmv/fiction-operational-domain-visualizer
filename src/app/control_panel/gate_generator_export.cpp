#include "gate_generator.hpp"

#include <fiction/technology/sidb/io/write_sqd_layout.hpp>
#include <fiction/technology/sidb/simulation/engines/quickexact.hpp>
#include <fiction/technology/sidb/simulation/logic/bdl_input_iterator.hpp>
#include <fiction/technology/sidb/simulation/logic/is_operational.hpp>

#include <fmt/format.h>
#include <kitty/dynamic_truth_table.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
namespace fs = std::filesystem;

// Serialize per-pattern charge states + expected outputs at the source point.
// One QuickExact simulation per input pattern per gate.
static void ExportPatternSnapshot(
    const GeneratedGateResult&                      r,
    const double                                    mu_ev,
    const double                                    source_epsilon_r,
    const double                                    source_lambda_tf,
    const std::vector<kitty::dynamic_truth_table>&  spec,
    const std::string&                              patterns_path)
{
    const auto& lyt       = r.layout;
    const auto& lat       = lyt.get_lattice();
    const auto& all_sites = lyt.sidbs();

    // ---- dots ----
    json dots = json::array();
    for (const auto& site : all_sites) {
        const auto nm  = lat.nm_position(site);
        const auto tag = lyt.get_dot_tag(site);
        std::string type_str = "normal";
        if (tag == fiction::sidb::dot_tag::INPUT)       type_str = "input";
        else if (tag == fiction::sidb::dot_tag::OUTPUT) type_str = "output";
        else if (tag == fiction::sidb::dot_tag::LOGIC)  type_str = "logic";

        dots.push_back({
            {"x_nm", nm.first},
            {"y_nm", nm.second},
            {"x",    site.x},
            {"y",    site.y},
            {"z",    static_cast<int>(site.z)},
            {"type", type_str}
        });
    }

    json out;
    out["num_inputs"]  = spec.empty() ? 0 : static_cast<int>(spec.front().num_vars());
    out["num_outputs"] = static_cast<int>(spec.size());
    out["dots"]        = std::move(dots);
    out["patterns"]    = json::array();

    if (spec.empty() || all_sites.empty()) {
        std::ofstream(patterns_path) << out.dump(2);
        return;
    }

    const auto num_inputs   = spec.front().num_vars();
    const std::uint64_t num_patterns = std::uint64_t{1} << num_inputs;

    // ---- operational set at the source point ----
    std::set<std::uint64_t> op_set;
    try {
        fiction::sidb::model::simulation_parameters sp{};
        sp.base       = 2;
        sp.mu_minus   = static_cast<float>(mu_ev);
        sp.epsilon_r  = source_epsilon_r;
        sp.lambda_tf  = source_lambda_tf;

        fiction::sidb::simulation::logic::is_operational_params op_params{};
        op_params.sim_params  = sp;
        op_params.sim_engine  = fiction::sidb::simulation::engine::QUICKEXACT;
        op_params.op_condition =
            fiction::sidb::simulation::logic::is_operational_params::operational_condition::REJECT_KINKS;

        op_set = fiction::sidb::simulation::logic::operational_input_patterns(lyt, spec, op_params);
    } catch (const std::exception&) {
        // leave op_set empty
    }

    // ---- per-pattern simulation & charge states ----
    try {
        fiction::sidb::simulation::logic::bdl_input_iterator bii{lyt};
        if (!bii.is_valid()) {
            std::ofstream(patterns_path) << out.dump(2);
            return;
        }

        fiction::sidb::simulation::engines::quickexact_params qe{};
        qe.sim_params.base       = 2;
        qe.sim_params.mu_minus   = static_cast<float>(mu_ev);
        qe.sim_params.epsilon_r  = source_epsilon_r;
        qe.sim_params.lambda_tf  = source_lambda_tf;
        qe.base_number_detection =
            fiction::sidb::simulation::engines::quickexact_params::automatic_base_number_detection::OFF;
        qe.deadline = std::chrono::steady_clock::time_point::max();

        for (std::uint64_t p = 0; p < num_patterns; ++p) {
            bii = p;
            const auto& pattern_lyt = *bii;

            json entry;
            entry["input_pattern"] = p;

            std::vector<int> charges(all_sites.size(), 127); // 127 = NONE
            try {
                const auto res    = fiction::sidb::simulation::engines::quickexact(pattern_lyt, qe);
                const auto ground = res.groundstates();
                if (!ground.empty()) {
                    const auto& gs = ground.front();
                    for (std::size_t i = 0; i < all_sites.size(); ++i) {
                        charges[i] = static_cast<int>(gs.get_charge_state(all_sites[i]));
                    }
                }
            } catch (const std::exception&) {
                // keep 127s
            }
            entry["charge_states"] = charges;

            json expected = json::array();
            for (const auto& tt : spec) {
                expected.push_back(static_cast<int>(kitty::get_bit(tt, p)));
            }
            entry["expected_output"] = std::move(expected);
            entry["operational"]     = (op_set.find(p) != op_set.end());

            out["patterns"].push_back(std::move(entry));
        }
    } catch (const std::exception&) {
        // leave patterns empty if iteration could not be set up
    }

    std::ofstream(patterns_path) << out.dump(2);
}

void ExportAllResults(const std::vector<GeneratedGateResult>& results,
                      const double                             mu_ev,
                      const double                             source_epsilon_r,
                      const double                             source_lambda_tf,
                      const std::vector<kitty::dynamic_truth_table>& spec,
                      const std::string&                       output_dir)
{
    fs::create_directories(output_dir + "/circuits");
    fs::create_directories(output_dir + "/radial");
    fs::create_directories(output_dir + "/patterns");

    json index = {
        {"source_point", {
            {"epsilon_r", source_epsilon_r},
            {"lambda_tf", source_lambda_tf}
        }},
        {"sweep", {
            {"epsilon_r", {{"min", 1.0}, {"max", 10.0}}},
            {"lambda_tf", {{"min", 1.0}, {"max", 10.0}}}
        }},
        {"gates", json::array()}
    };

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];

        const std::string sqd_name = fmt::format(
            "{}_{}db_tc{:.1f}_dist{:.2f}_{}.sqd",
            r.generator_name, r.num_canvas_dbs,
            r.critical_temp_k, r.domain_distance, i);
        fiction::sidb::io::write_sqd_layout(r.layout, output_dir + "/circuits/" + sqd_name);

        // radial json
        json rad;
        rad["generator"]       = r.generator_name;
        rad["num_canvas_dbs"]  = r.num_canvas_dbs;
        rad["critical_temp_k"] = r.critical_temp_k;
        rad["domain_distance"] = r.domain_distance;
        rad["num_op_samples"]  = r.num_op_samples;
        rad["runtime_sec"]     = r.runtime_sec;
        rad["points"]          = json::array();
        for (const auto& p : r.radial_points) {
            json pt;
            pt["epsilon_r"]   = p.epsilon_r;
            pt["lambda_tf"]   = p.lambda_tf;
            pt["operational"] = p.operational;
            rad["points"].push_back(std::move(pt));
        }
        const std::string rad_name = fmt::format("radial_{}.json", i);
        std::ofstream(output_dir + "/radial/" + rad_name) << rad.dump(2);

        // patterns json
        const std::string pat_name = fmt::format("patterns_{}.json", i);
        ExportPatternSnapshot(r, mu_ev, source_epsilon_r, source_lambda_tf, spec,
                              output_dir + "/patterns/" + pat_name);

        json entry;
        entry["id"]              = i;
        entry["generator"]       = r.generator_name;
        entry["num_canvas_dbs"]  = r.num_canvas_dbs;
        entry["critical_temp_k"] = r.critical_temp_k;
        entry["domain_distance"] = r.domain_distance;
        entry["num_op_samples"]  = r.num_op_samples;
        entry["runtime_sec"]     = r.runtime_sec;
        entry["sqd_file"]        = "circuits/" + sqd_name;
        entry["radial_json"]     = "radial/"   + rad_name;
        entry["patterns_json"]   = "patterns/" + pat_name;
        index["gates"].push_back(std::move(entry));
    }

    std::ofstream(output_dir + "/index.json") << index.dump(2);
}
