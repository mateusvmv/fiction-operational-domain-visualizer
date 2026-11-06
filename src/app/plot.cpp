#include "../app.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <fmt/format.h>

void DrawOperationalPlot(const std::vector<DataPoint>& active_data, const ExperimentConfig& active_exp) {
    ImGui::SetNextWindowSize(ImVec2(800, 650), ImGuiCond_FirstUseEver);
    ImGui::Begin("Operational Domain Plot");
    
    if (ImPlot::BeginPlot("SiDB Operational Domain", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Relative Permittivity (epsilon_r)", "Thomas-Fermi Screening Length (lambda_tf)");
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.5, 10.5, ImGuiCond_Once);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.5, 10.5, ImGuiCond_Once);

        std::vector<double> x_vals, y_vals;
        for (const auto& pt : active_data) {
            if (pt.status == 1) {
                x_vals.push_back(pt.epsilon_r);
                y_vals.push_back(pt.lambda_tf);
            }
        }

        if (!x_vals.empty()) {
            std::string label = fmt::format("{} | mu = {:.2f} eV", active_exp.algorithm, active_exp.mu_val);
            
            // Plot scatter using ImPlot v1.1 inline spec configuration
            ImPlot::PlotScatter(label.c_str(), x_vals.data(), y_vals.data(), static_cast<int>(x_vals.size()), {
                ImPlotProp_Marker, ImPlotMarker_Square,
                ImPlotProp_MarkerSize, 3.5f,
                ImPlotProp_MarkerFillColor, active_exp.color,
                ImPlotProp_MarkerLineColor, active_exp.color
            });
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}