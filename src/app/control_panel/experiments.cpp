#include "../control_panel.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "imgui.h"
#include <fmt/format.h>

namespace fs = std::filesystem;

static const char* const kAlgorithms[] = {
    "Contour Tracing", "Grid Search", "Flood Fill", "Random Sampling", "Radial Search"
};

struct ExperimentsTabState {
    // List interaction
    int  edit_index       = -1;
    bool edit_should_open = false;

    // Edit-modal working copy
    ExperimentConfig edit_exp;
    char edit_tt_buffer[2048] = "";
    int  edit_gate_idx = 0;
    int  edit_alg_idx  = 0;

    // Add form
    char   tt_buffer[2048]     = "";
    int    selected_gate_combo = 0;
    int    alg_combo           = 0;
    double new_mu              = -0.32;
    double new_start_eps       = 5.0;
    double new_start_lambda    = 5.0;

    // Add TT editor
    bool tt_add_should_open = false;
    int  tt_add_nin = 2, tt_add_nout = 1;
    std::vector<std::vector<bool>> tt_add_bits;

    // Edit TT editor
    bool tt_edit_should_open = false;
    int  tt_edit_nin = 2, tt_edit_nout = 1;
    std::vector<std::vector<bool>> tt_edit_bits;
};

// Copy an experiment's settings into the Add form. Used by click-to-fill.
static void FillAddFormFromExperiment(ExperimentsTabState& tab,
                                      const ExperimentConfig& exp,
                                      const std::vector<std::string>& included_gates) {
    tab.selected_gate_combo = 0;
    for (std::size_t k = 0; k < included_gates.size(); ++k) {
        if (included_gates[k] == exp.gate_filepath) {
            tab.selected_gate_combo = static_cast<int>(k);
            break;
        }
    }
    tab.alg_combo = 0;
    for (int k = 0; k < IM_ARRAYSIZE(kAlgorithms); ++k) {
        if (exp.algorithm == kAlgorithms[k]) { tab.alg_combo = k; break; }
    }
    tab.new_mu           = exp.mu_val;
    tab.new_start_eps    = exp.start_point.epsilon_r;
    tab.new_start_lambda = exp.start_point.lambda_tf;
    FormatHexTokens(tab.tt_buffer, sizeof(tab.tt_buffer),
                                   exp.truth_table_hex);
}

// ----------------------------------------------------------------------------
// Experiment list (drag to reorder; click to fill Add form)
// ----------------------------------------------------------------------------

static void DrawExperimentList(AppState& state, ExperimentsTabState& tab) {
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_NoBordersInBody;
    if (!ImGui::BeginTable("##exp_rows", 6, flags)) return;

    ImGui::TableSetupColumn("##grip", ImGuiTableColumnFlags_WidthFixed,   20.0f);
    ImGui::TableSetupColumn("##chk",  ImGuiTableColumnFlags_WidthFixed,   25.0f);
    ImGui::TableSetupColumn("##lbl",  ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##col",  ImGuiTableColumnFlags_WidthFixed,   55.0f);
    ImGui::TableSetupColumn("##edt",  ImGuiTableColumnFlags_WidthFixed,   55.0f);
    ImGui::TableSetupColumn("##rm",   ImGuiTableColumnFlags_WidthFixed,   80.0f);

    std::optional<std::pair<std::size_t, std::size_t>> pending_reorder;

    for (std::size_t i = 0; i < state.experiments.size(); ++i) {
        auto& exp = state.experiments[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();

        const std::string label = fmt::format(
            "[ID: {}] {} [{}] (mu={:.2f})",
            i,
            fs::path(exp.gate_filepath).filename().string(),
            exp.algorithm,
            exp.mu_val);

        // --- Grip: drag source, drop target, and click source ---
        ImGui::TableSetColumnIndex(0);
        const bool row_clicked = ImGui::Selectable(
            "::", false,
            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);

        bool row_dragging = false;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            row_dragging = true;
            ImGui::SetDragDropPayload("EXP_REORDER", &i, sizeof(i));
            ImGui::TextUnformatted(fmt::format("Move: {}", label).c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EXP_REORDER")) {
                const std::size_t from = *static_cast<const std::size_t*>(payload->Data);
                if (from != i && from < state.experiments.size()) {
                    pending_reorder = std::make_pair(from, i);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Click (release without drag) fills the Add form with this row's presets.
        if (row_clicked && !row_dragging) {
            FillAddFormFromExperiment(tab, exp, state.included_gates);
        }

        // --- Checkbox ---
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("##enabled", &exp.enabled);

        // --- Label ---
        ImGui::TableSetColumnIndex(2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());

        // --- Color ---
        ImGui::TableSetColumnIndex(3);
        ImGui::ColorEdit3("##Color", reinterpret_cast<float*>(&exp.color),
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

        // --- Edit ---
        ImGui::TableSetColumnIndex(4);
        if (ImGui::Button("Edit")) {
            tab.edit_index       = static_cast<int>(i);
            tab.edit_should_open = true;
        }

        // --- Remove ---
        ImGui::TableSetColumnIndex(5);
        if (ImGui::Button("Remove")) {
            RemoveExperiment(state, i);
            ImGui::PopID();
            ImGui::EndTable();
            return;
        }

        ImGui::PopID();
    }

    ImGui::EndTable();

    // Apply reorder *after* drawing the full table so we never see a
    // half-drawn intermediate frame.
    if (pending_reorder) {
        const std::size_t from = pending_reorder->first;
        const std::size_t to   = pending_reorder->second;
        auto& items = state.experiments;

        std::size_t& sel = state.selected_experiment_index;
        if (sel == from) {
            sel = to;
        } else if (from < to && sel > from && sel <= to) {
            sel -= 1;
        } else if (from > to && sel >= to && sel < from) {
            sel += 1;
        }

        if (from < to) {
            std::rotate(items.begin() + static_cast<std::ptrdiff_t>(from),
                        items.begin() + static_cast<std::ptrdiff_t>(from + 1),
                        items.begin() + static_cast<std::ptrdiff_t>(to + 1));
        } else {
            std::rotate(items.begin() + static_cast<std::ptrdiff_t>(to),
                        items.begin() + static_cast<std::ptrdiff_t>(from),
                        items.begin() + static_cast<std::ptrdiff_t>(from + 1));
        }
    }
}

// ----------------------------------------------------------------------------
// Add Experiment form
// ----------------------------------------------------------------------------

static void DrawAddExperimentForm(AppState& state, ExperimentsTabState& tab) {
    if (!state.included_gates.empty()) {
        if (tab.selected_gate_combo >= static_cast<int>(state.included_gates.size())) {
            tab.selected_gate_combo = 0;
        }
        ImGui::Combo("Gate", &tab.selected_gate_combo,
            [](void* data, int idx) -> const char* {
                auto* vec = static_cast<std::vector<std::string>*>(data);
                return (*vec)[static_cast<std::size_t>(idx)].c_str();
            },
            static_cast<void*>(&state.included_gates),
            static_cast<int>(state.included_gates.size()));
    }

    ImGui::Combo("Algorithm", &tab.alg_combo, kAlgorithms, IM_ARRAYSIZE(kAlgorithms));

    ImGui::InputDouble("mu (eV)", &tab.new_mu, 0.01, 0.1, "%.2f");
    ImGui::InputDouble("Start Epsilon (r)", &tab.new_start_eps, 0.1, 1.0, "%.1f");
    ImGui::InputDouble("Start Lambda (tf)", &tab.new_start_lambda, 0.1, 1.0, "%.1f");

    // Read-only: only the table editor may modify the hex.
    ImGui::InputText("Truth Table", tab.tt_buffer, sizeof(tab.tt_buffer),
                     ImGuiInputTextFlags_ReadOnly);

    ImGui::SameLine();
    if (ImGui::Button("Edit as Table...")) {
        auto st = BuildTTEditorFromBuffer(tab.tt_buffer);
        tab.tt_add_nin  = st.nin;
        tab.tt_add_nout = st.nout;
        tab.tt_add_bits = std::move(st.bits);
        tab.tt_add_should_open = true;
    }

    if (ImGui::Button("Add Experiment") && !state.included_gates.empty()) {
        ExperimentConfig new_exp;
        new_exp.gate_filepath         = state.included_gates[static_cast<std::size_t>(tab.selected_gate_combo)];
        new_exp.algorithm             = kAlgorithms[tab.alg_combo];
        new_exp.mu_val                = tab.new_mu;
        new_exp.start_point.epsilon_r = tab.new_start_eps;
        new_exp.start_point.lambda_tf = tab.new_start_lambda;
        new_exp.color                 = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
        new_exp.enabled               = true;
        new_exp.truth_table_hex       = ParseHexTokens(tab.tt_buffer);

        state.experiments.push_back(new_exp);
        state.selected_experiment_index = state.experiments.size() - 1;
    }
}

// ----------------------------------------------------------------------------
// Add TT Editor popup
// ----------------------------------------------------------------------------

static void DrawAddTTEditorPopup(ExperimentsTabState& tab) {
    if (tab.tt_add_should_open) {
        ImGui::OpenPopup("Add TT Editor");
        tab.tt_add_should_open = false;
    }
    if (ImGui::BeginPopupModal("Add TT Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (RenderTTEditorBody(tab.tt_add_nin, tab.tt_add_nout, tab.tt_add_bits,
                                              tab.tt_buffer, sizeof(tab.tt_buffer))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ----------------------------------------------------------------------------
// Edit Experiment modal (with nested TT editor)
// ----------------------------------------------------------------------------

static void DrawEditExperimentModal(AppState& state, ExperimentsTabState& tab) {
    if (tab.edit_should_open) {
        if (tab.edit_index < 0 ||
            tab.edit_index >= static_cast<int>(state.experiments.size())) {
            tab.edit_should_open = false;
        } else {
            ImGui::OpenPopup("Edit Experiment");
            tab.edit_exp = state.experiments[static_cast<std::size_t>(tab.edit_index)];
            FormatHexTokens(tab.edit_tt_buffer, sizeof(tab.edit_tt_buffer),
                                           tab.edit_exp.truth_table_hex);

            tab.edit_gate_idx = 0;
            for (std::size_t k = 0; k < state.included_gates.size(); ++k) {
                if (state.included_gates[k] == tab.edit_exp.gate_filepath) {
                    tab.edit_gate_idx = static_cast<int>(k);
                    break;
                }
            }
            tab.edit_alg_idx = 0;
            for (int k = 0; k < IM_ARRAYSIZE(kAlgorithms); ++k) {
                if (tab.edit_exp.algorithm == kAlgorithms[k]) { tab.edit_alg_idx = k; break; }
            }
            tab.edit_should_open = false;
        }
    }

    if (!ImGui::BeginPopupModal("Edit Experiment", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    if (!state.included_gates.empty()) {
        if (tab.edit_gate_idx >= static_cast<int>(state.included_gates.size())) {
            tab.edit_gate_idx = 0;
        }
        ImGui::Combo("Gate", &tab.edit_gate_idx,
            [](void* data, int idx) -> const char* {
                auto* vec = static_cast<std::vector<std::string>*>(data);
                return (*vec)[static_cast<std::size_t>(idx)].c_str();
            },
            static_cast<void*>(&state.included_gates),
            static_cast<int>(state.included_gates.size()));
    }

    ImGui::Combo("Algorithm", &tab.edit_alg_idx, kAlgorithms, IM_ARRAYSIZE(kAlgorithms));

    ImGui::InputDouble("mu (eV)", &tab.edit_exp.mu_val, 0.01, 0.1, "%.2f");
    ImGui::InputDouble("Start Epsilon (r)", &tab.edit_exp.start_point.epsilon_r, 0.1, 1.0, "%.1f");
    ImGui::InputDouble("Start Lambda (tf)", &tab.edit_exp.start_point.lambda_tf, 0.1, 1.0, "%.1f");

    // Read-only: only the table editor may modify the hex.
    ImGui::InputText("Truth Table", tab.edit_tt_buffer, sizeof(tab.edit_tt_buffer),
                     ImGuiInputTextFlags_ReadOnly);

    ImGui::SameLine();
    if (ImGui::Button("Edit as Table...")) {
        auto st = BuildTTEditorFromBuffer(tab.edit_tt_buffer);
        tab.tt_edit_nin  = st.nin;
        tab.tt_edit_nout = st.nout;
        tab.tt_edit_bits = std::move(st.bits);
        tab.tt_edit_should_open = true;
    }

    if (tab.tt_edit_should_open) {
        ImGui::OpenPopup("Edit TT Editor");
        tab.tt_edit_should_open = false;
    }
    if (ImGui::BeginPopupModal("Edit TT Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (RenderTTEditorBody(tab.tt_edit_nin, tab.tt_edit_nout, tab.tt_edit_bits,
                                              tab.edit_tt_buffer, sizeof(tab.edit_tt_buffer))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    if (ImGui::Button("Apply")) {
        if (!state.included_gates.empty() &&
            tab.edit_gate_idx >= 0 &&
            tab.edit_gate_idx < static_cast<int>(state.included_gates.size())) {
            tab.edit_exp.gate_filepath = state.included_gates[static_cast<std::size_t>(tab.edit_gate_idx)];
        }
        if (tab.edit_alg_idx >= 0 && tab.edit_alg_idx < IM_ARRAYSIZE(kAlgorithms)) {
            tab.edit_exp.algorithm = kAlgorithms[tab.edit_alg_idx];
        }
        tab.edit_exp.truth_table_hex = ParseHexTokens(tab.edit_tt_buffer);

        if (tab.edit_index >= 0 &&
            tab.edit_index < static_cast<int>(state.experiments.size())) {
            state.experiments[static_cast<std::size_t>(tab.edit_index)] = tab.edit_exp;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ----------------------------------------------------------------------------
// Experiments tab entry point
// ----------------------------------------------------------------------------

void DrawExperimentsTab(AppState& state) {
    static ExperimentsTabState tab;

    ImGui::Text("Configured Experiments (Check to Plot):");
    ImGui::Separator();

    if (state.experiments.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No experiments created yet.");
    } else {
        if (ImGui::BeginChild("ExperimentsList", ImVec2(0, 220), true)) {
            DrawExperimentList(state, tab);
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::Text("Add / Edit Experiment:");

    DrawAddExperimentForm(state, tab);
    DrawAddTTEditorPopup(tab);
    DrawEditExperimentModal(state, tab);
}