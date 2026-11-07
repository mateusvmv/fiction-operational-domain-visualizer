#include "operational_data.hpp"

#include <fiction/technology/sidb/simulation/logic/detect_bdl_wires.hpp>
#include <kitty/constructors.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;

using fiction::sidb::simulation::logic::bdl_wire_selection;
using fiction::sidb::simulation::logic::detect_bdl_wires;

static std::vector<kitty::dynamic_truth_table> BuildTruthTable(
    const fiction::sidb::layout&                                       lyt,
    const fiction::sidb::simulation::logic::operational_domain_params& op_domain_params,
    const std::vector<std::string>&                                    truth_table_hex)
{
    const auto& wire_params =
        op_domain_params.operational_params.input_bdl_iterator_params.bdl_wire_params;

    const auto input_wires  = detect_bdl_wires(lyt, wire_params, bdl_wire_selection::INPUT);
    const auto output_wires = detect_bdl_wires(lyt, wire_params, bdl_wire_selection::OUTPUT);

    if (input_wires.empty() || output_wires.empty()) {
        throw std::invalid_argument(
            "Could not detect BDL input/output wires in the layout; "
            "cannot size the truth table.");
    }

    const auto num_vars    = static_cast<uint32_t>(input_wires.size());
    const auto num_outputs = output_wires.size();

    std::vector<kitty::dynamic_truth_table> spec;
    spec.reserve(num_outputs);

    if (truth_table_hex.empty()) {
        for (std::size_t i = 0; i < num_outputs; ++i) {
            spec.emplace_back(num_vars);
        }
        return spec;
    }

    if (truth_table_hex.size() != num_outputs) {
        throw std::invalid_argument(fmt::format(
            "Truth table declares {} output(s), but the layout has {} output wire(s).",
            truth_table_hex.size(), num_outputs));
    }

    for (std::size_t i = 0; i < truth_table_hex.size(); ++i) {
        std::string h = truth_table_hex[i];
        h.erase(std::remove_if(h.begin(), h.end(),
                               [](unsigned char c) { return std::isspace(c); }),
                h.end());

        kitty::dynamic_truth_table tt(num_vars);
        kitty::create_from_hex_string<kitty::dynamic_truth_table>(tt, h);
        spec.push_back(std::move(tt));
    }
    return spec;
}

std::vector<DataPoint> ComputeOperationalData(const ExperimentKey& experiment)
{
    const double mu           = experiment.mu_val;
    const double start_eps    = experiment.start_point.epsilon_r;
    const double start_lambda = experiment.start_point.lambda_tf;

    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<DataPoint> points;

    fs::path p(experiment.gate_filepath);
    std::string gate_name = p.stem().string();
    auto lyt = fiction::sidb::io::read_sqd_layout(experiment.gate_filepath, gate_name);

    const fiction::sidb::model::simulation_parameters sim_params{2, static_cast<float>(mu)};

    fiction::sidb::simulation::logic::operational_domain_params op_domain_params{
        .operational_params = {.sim_params                = sim_params,
                               .sim_engine                = fiction::sidb::simulation::engine::QUICKEXACT,
                               .input_bdl_iterator_params = {.bdl_wire_params = {.threshold_bdl_interdistance = 1.5}}},
        .sweep_dimensions   = {{.dimension = fiction::sidb::simulation::logic::sweep_parameter::EPSILON_R, .min = 1.0, .max = 10.0, .step = 0.2},
                               {.dimension = fiction::sidb::simulation::logic::sweep_parameter::LAMBDA_TF, .min = 1.0, .max = 10.0, .step = 0.2}}};

    std::vector<kitty::dynamic_truth_table> truth_table =
        BuildTruthTable(lyt, op_domain_params, experiment.truth_table_hex);

    auto process_domain = [&](const auto& op_dom) {
        op_dom.for_each([&](const fiction::sidb::simulation::logic::parameter_point& pp, const auto& op_tuple) {
            const auto& params_vec = pp.get_parameters();
            if (params_vec.size() >= 2) {
                DataPoint pt;
                pt.epsilon_r = params_vec[0];
                pt.lambda_tf = params_vec[1];
                auto status  = std::get<0>(op_tuple);
                pt.status    = (status == fiction::sidb::simulation::logic::operational_status::OPERATIONAL) ? 1 : 0;
                pt.mu        = static_cast<float>(mu);
                points.push_back(pt);
            }
        });
    };

    if (experiment.algorithm == "Grid Search") {
        process_domain(fiction::sidb::simulation::logic::operational_domain_grid_search(lyt, truth_table, op_domain_params));
    } else if (experiment.algorithm == "Flood Fill") {
        process_domain(fiction::sidb::simulation::logic::operational_domain_flood_fill(lyt, truth_table, 250, op_domain_params));
    } else if (experiment.algorithm == "Random Sampling") {
        process_domain(fiction::sidb::simulation::logic::operational_domain_random_sampling(lyt, truth_table, 2500, op_domain_params));
    } else if (experiment.algorithm == "Radial Search") {
        fiction::sidb::simulation::logic::parameter_point start_pp{std::vector<double>{start_eps, start_lambda}};
        process_domain(fiction::sidb::simulation::logic::operational_domain_radial_search(lyt, truth_table, start_pp, op_domain_params));
    } else {
        process_domain(fiction::sidb::simulation::logic::operational_domain_contour_tracing(lyt, truth_table, 100, op_domain_params));
    }

    auto end_time     = std::chrono::high_resolution_clock::now();
    double runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    double min_dist_nop = std::numeric_limits<double>::infinity();
    double min_dist_op  = std::numeric_limits<double>::infinity();
    for (const auto& pt : points) {
        double dist = std::hypot(pt.epsilon_r - start_eps, pt.lambda_tf - start_lambda);
        if (pt.status == 0) {
            if (dist < min_dist_nop) min_dist_nop = dist;
        } else {
            if (dist < min_dist_op) min_dist_op = dist;
        }
    }
    double min_dist = min_dist_nop - min_dist_op;

    for (auto& pt : points) {
        pt.runtime_ms                      = runtime_ms;
        pt.min_distance_to_non_operational = min_dist;
    }

    return points;
}
