#include "../app.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <fmt/format.h>

namespace fs = std::filesystem;

void DrawOperationalPlot(const AppState& state) {
    ImGui::SetNextWindowSize(ImVec2(800, 650), ImGuiCond_FirstUseEver);
    ImGui::Begin("Operational Domain Plot");
    
    if (ImPlot::BeginPlot("SiDB Operational Domain", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Relative Permittivity (epsilon_r)", "Thomas-Fermi Screening Length (lambda_tf)");
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.5, 10.5, ImGuiCond_Once);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.5, 10.5, ImGuiCond_Once);

        for (const auto& exp : state.experiments) {
            if (!exp.enabled) continue;

            std::string start_key = fmt::format("{:.1f}_{:.1f}", exp.start_epsilon_r, exp.start_lambda_tf);
            
            // Ensure data exists in cache or compute it
            // (Assumes cache has been populated or handled in main loop)
            auto gate_it = state.cache.find(exp.gate_filepath);
            if (gate_it == state.cache.end()) continue;
            auto alg_it = gate_it->second.find(exp.algorithm);
            if (alg_it == gate_it->second.end()) continue;
            auto mu_it = alg_it->second.find(exp.mu_val);
            if (mu_it == alg_it->second.end()) continue;
            auto pt_it = mu_it->second.find(start_key);
            if (pt_it == mu_it->second.end()) continue;

            const auto& active_data = pt_it->second;

            std::vector<double> x_vals, y_vals;
            for (const auto& pt : active_data) {
                if (pt.status == 1) {
                    x_vals.push_back(pt.epsilon_r);
                    y_vals.push_back(pt.lambda_tf);
                }
            }

            if (!x_vals.empty()) {
                std::string label = fmt::format("{} | {} | mu = {:.2f} eV", fs::path(exp.gate_filepath).filename().string(), exp.algorithm, exp.mu_val);
                
                ImPlot::PlotScatter(label.c_str(), x_vals.data(), y_vals.data(), static_cast<int>(x_vals.size()), {
                    ImPlotProp_Marker, ImPlotMarker_Square,
                    ImPlotProp_MarkerSize, 3.5f,
                    ImPlotProp_MarkerFillColor, exp.color,
                    ImPlotProp_MarkerLineColor, exp.color
                });
            }
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}