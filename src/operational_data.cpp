#include "operational_data.hpp"
#include <filesystem>

namespace fs = std::filesystem;

std::vector<DataPoint> ComputeOperationalData(double mu, const std::string& gate_filepath, const std::string& algorithm, double start_eps, double start_lambda) {
    static_cast<void>(start_eps);
    static_cast<void>(start_lambda);

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
    } else {
        // Contour Tracking
        fiction::sidb::simulation::logic::operational_domain_stats op_domain_stats_ct{};
        const auto op_domain_ct =
            fiction::sidb::simulation::logic::operational_domain_contour_tracing(lyt, truth_table, 100, op_domain_params, &op_domain_stats_ct);
        process_domain(op_domain_ct);
    }

    return points;
}