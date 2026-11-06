#pragma once

#include <fiction/synthesis/truth_tables.hpp>
#include <fiction/technology/sidb/io/read_sqd_layout.hpp>
#include <fiction/technology/sidb/model/simulation_parameters.hpp>
#include <fiction/technology/sidb/simulation/engine.hpp>
#include <fiction/technology/sidb/simulation/logic/operational_domain.hpp>

#include <fmt/format.h>
#include <kitty/dynamic_truth_table.hpp>
#include <vector>
#include <string>

struct DataPoint {
    double epsilon_r;
    double lambda_tf;
    int status;
    float mu;
};

std::vector<DataPoint> ComputeOperationalData(double mu, const std::string& gate, const std::string& algorithm, double start_eps, double start_lambda);