#include "app.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>

namespace nlohmann {
template <>
struct adl_serializer<ImVec4> {
    static void to_json(json& j, const ImVec4& v) {
        j = json{v.x, v.y, v.z, v.w};
    }
    static void from_json(const json& j, ImVec4& v) {
        j.at(0).get_to(v.x);
        j.at(1).get_to(v.y);
        j.at(2).get_to(v.z);
        j.at(3).get_to(v.w);
    }
};

template <>
struct adl_serializer<ExperimentConfig> {
    static void to_json(json& j, const ExperimentConfig& c) {
        j = json{
            {"gate_filepath", c.gate_filepath},
            {"algorithm", c.algorithm},
            {"mu_val", c.mu_val},
            {"start_epsilon_r", c.start_epsilon_r},
            {"start_lambda_tf", c.start_lambda_tf},
            {"color", c.color}
        };
    }
    static void from_json(const json& j, ExperimentConfig& c) {
        if (j.contains("gate_filepath")) j.at("gate_filepath").get_to(c.gate_filepath);
        if (j.contains("algorithm")) j.at("algorithm").get_to(c.algorithm);
        if (j.contains("mu_val")) j.at("mu_val").get_to(c.mu_val);
        if (j.contains("start_epsilon_r")) j.at("start_epsilon_r").get_to(c.start_epsilon_r);
        if (j.contains("start_lambda_tf")) j.at("start_lambda_tf").get_to(c.start_lambda_tf);
        if (j.contains("color")) j.at("color").get_to(c.color);
    }
};
}

void SaveAppState(const AppState& state, const std::string& filepath) {
    nlohmann::json j;
    j["version"] = state.version;
    j["search_directories"] = state.search_directories;
    j["discovered_gates"] = state.discovered_gates;
    j["included_gates"] = state.included_gates;
    j["experiments"] = state.experiments;
    j["selected_experiment_index"] = state.selected_experiment_index;

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
    }
}

bool LoadAppState(AppState& state, const std::string& filepath, int& loaded_version_out) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("version")) {
            j["version"].get_to(loaded_version_out);
        } else {
            loaded_version_out = 0;
        }

        if (j.contains("search_directories")) j["search_directories"].get_to(state.search_directories);
        if (j.contains("discovered_gates")) j["discovered_gates"].get_to(state.discovered_gates);
        if (j.contains("included_gates")) j["included_gates"].get_to(state.included_gates);
        if (j.contains("experiments")) j["experiments"].get_to(state.experiments);
        if (j.contains("selected_experiment_index")) j["selected_experiment_index"].get_to(state.selected_experiment_index);

        state.cache.clear();
        return true;
    } catch (...) {
        return false;
    }
}

uint64_t HashAppState(const AppState& state) {
    nlohmann::json j;
    j["version"] = state.version;
    j["search_directories"] = state.search_directories;
    j["included_gates"] = state.included_gates;
    j["selected_experiment_index"] = state.selected_experiment_index;

    nlohmann::json exp_arr = nlohmann::json::array();
    for (const auto& exp : state.experiments) {
        exp_arr.push_back({
            exp.gate_filepath,
            exp.algorithm,
            exp.mu_val,
            exp.start_epsilon_r,
            exp.start_lambda_tf,
            exp.color.x, exp.color.y, exp.color.z, exp.color.w
        });
    }
    j["experiments"] = exp_arr;

    std::string s = j.dump();
    uint64_t hash = 14695981039346656037ULL;
    for (char c : s) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}