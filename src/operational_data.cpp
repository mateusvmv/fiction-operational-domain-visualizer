#include "operational_data.hpp"
#include <filesystem>
#include <chrono>
#include <cmath>
#include <limits>

namespace fs = std::filesystem;

std::vector<DataPoint> ComputeOperationalData(double mu, const std::string& gate_filepath, const std::string& algorithm, double start_eps, double start_lambda) {
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<DataPoint> points;
    
    fs::path p(gate_filepath);
    std::string gate_name = p.stem().string();

    auto lyt = fiction::sidb::io::read_sqd_layout(gate_filepath, gate_name);
    std::vector<kitty::dynamic_truth_table> truth_table = {fiction::synthesis::create_and_tt()};

    const fiction::sidb::model::simulation_parameters sim_params{2, static_cast<float>(mu)};
    
    fiction::sidb::simulation::logic::operational_domain_params op_domain_params{
        .operational_params = {.sim_params                = sim_params,
                               .sim_engine                = fiction::sidb::simulation::engine::QUICKEXACT,
                               .input_bdl_iterator_params = {.bdl_wire_params = {.threshold_bdl_interdistance = 1.5}}},
        .sweep_dimensions   = {{.dimension = fiction::sidb::simulation::logic::sweep_parameter::EPSILON_R, .min = 1.0, .max = 10.0, .step = 0.2},
                               {.dimension = fiction::sidb::simulation::logic::sweep_parameter::LAMBDA_TF, .min = 1.0, .max = 10.0, .step = 0.2}}};

    auto process_domain = [&](const auto& op_dom) {
        op_dom.for_each([&](const fiction::sidb::simulation::logic::parameter_point& pp, const auto& op_tuple) {
            const auto& params_vec = pp.get_parameters();
            if (params_vec.size() >= 2) {
                DataPoint pt;
                pt.epsilon_r = params_vec[0];
                pt.lambda_tf = params_vec[1];
                auto status = std::get<0>(op_tuple);
                pt.status = (status == fiction::sidb::simulation::logic::operational_status::OPERATIONAL) ? 1 : 0;
                pt.mu = static_cast<float>(mu);
                points.push_back(pt);
            }
        });
    };

    if (algorithm == "Grid Search") {
        process_domain(fiction::sidb::simulation::logic::operational_domain_grid_search(lyt, truth_table, op_domain_params));
    } else if (algorithm == "Flood Fill") {
        process_domain(fiction::sidb::simulation::logic::operational_domain_flood_fill(lyt, truth_table, 250, op_domain_params));
    } else if (algorithm == "Random Sampling") {
        process_domain(fiction::sidb::simulation::logic::operational_domain_random_sampling(lyt, truth_table, 2500, op_domain_params));
    } else if (algorithm == "Radial Search") {
        fiction::sidb::simulation::logic::parameter_point start_pp{std::vector<double>{start_eps, start_lambda}};
        process_domain(fiction::sidb::simulation::logic::operational_domain_radial_search(lyt, truth_table, start_pp, op_domain_params));
    } else {
        // Contour Tracing
        process_domain(fiction::sidb::simulation::logic::operational_domain_contour_tracing(lyt, truth_table, 100, op_domain_params));
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    // Calculate the smallest distance from the start point to any non-operational point (status == 0)
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

    // Attach runtime and minimum distance statistic to all data points for caching and table retrieval
    for (auto& pt : points) {
        pt.runtime_ms = runtime_ms;
        pt.min_distance_to_non_operational = min_dist;
    }

    return points;
}
