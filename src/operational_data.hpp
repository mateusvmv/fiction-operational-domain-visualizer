#pragma once

#include <fiction/synthesis/truth_tables.hpp>
#include <fiction/technology/sidb/io/read_sqd_layout.hpp>
#include <fiction/technology/sidb/model/simulation_parameters.hpp>
#include <fiction/technology/sidb/simulation/engine.hpp>
#include <fiction/technology/sidb/simulation/logic/operational_domain.hpp>

#include <fmt/format.h>
#include <kitty/dynamic_truth_table.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct DataPoint {
    double epsilon_r;
    double lambda_tf;
    double runtime_ms;
    double min_distance_to_non_operational;
    float  mu;
    int    status;
};

// A point in the (epsilon_r, lambda_tf) sweep parameter space.
struct StartPoint {
    double epsilon_r = 5.0;
    double lambda_tf = 5.0;
    bool operator==(const StartPoint&) const = default;
};

struct StartPointHash {
    std::size_t operator()(const StartPoint& k) const noexcept {
        std::size_t seed = std::hash<double>{}(k.epsilon_r);
        const std::size_t v = std::hash<double>{}(k.lambda_tf);
        seed ^= v + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

// Everything needed to define (and cache) one operational-domain computation.
// Serves three roles with one definition:
//   * the identity used for cache dedup and lookup,
//   * the argument struct for ComputeOperationalData,
//   * the base of ExperimentConfig (which only adds color + enabled).
struct ExperimentKey {
    std::string              gate_filepath;
    std::string              algorithm = "Contour Tracing";
    double                   mu_val = -0.32;
    StartPoint               start_point;
    std::vector<std::string> truth_table_hex;

    bool operator==(const ExperimentKey&) const = default;
};

struct ExperimentKeyHash {
    std::size_t operator()(const ExperimentKey& k) const noexcept {
        std::size_t seed = std::hash<std::string>{}(k.gate_filepath);
        auto combine = [&seed](std::size_t v) {
            seed ^= v + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
        };
        combine(std::hash<std::string>{}(k.algorithm));
        combine(std::hash<double>{}(k.mu_val));
        combine(std::hash<double>{}(k.start_point.epsilon_r));
        combine(std::hash<double>{}(k.start_point.lambda_tf));
        for (const auto& s : k.truth_table_hex) combine(std::hash<std::string>{}(s));
        return seed;
    }
};

// Sweep the operational domain for one ExperimentKey.
// An empty truth_table_hex falls back to zero-truth-tables of the layout's
// detected arity, so the sweep won't crash on undeclared gates — but the
// resulting status column is meaningless until a real spec is supplied.
std::vector<DataPoint> ComputeOperationalData(const ExperimentKey& experiment);