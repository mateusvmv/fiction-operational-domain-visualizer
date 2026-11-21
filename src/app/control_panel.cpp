#include "control_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>

#include "../app.hpp"
#include "imgui.h"

static bool IsHexChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static bool IsPowerOfTwo(std::size_t n) { return n > 0 && (n & (n - 1)) == 0; }

TTEditorState BuildTTEditorFromBuffer(const char* buffer) {
    TTEditorState st;
    auto tokens = ParseHexTokens(buffer);

    st.nin  = 2;
    st.nout = 1;
    if (!tokens.empty()) {
        for (const auto& tok : tokens) {
            bool valid = !tok.empty();
            for (char c : tok) { if (!IsHexChar(c)) { valid = false; break; } }
            if (valid && IsPowerOfTwo(tok.size())) {
                st.nin = 2 + static_cast<int>(std::log2(static_cast<double>(tok.size())));
                break;
            }
        }
        st.nout = static_cast<int>(tokens.size());
    }
    st.nin  = std::clamp(st.nin,  2, 8);
    st.nout = std::clamp(st.nout, 1, 8);

    st.bits.assign(static_cast<std::size_t>(st.nout),
                   std::vector<bool>(1u << st.nin, false));

    const std::size_t expected_digits = 1u << (st.nin - 2);
    const std::size_t fill_count =
        std::min(static_cast<std::size_t>(st.nout), tokens.size());
    for (std::size_t o = 0; o < fill_count; ++o) {
        const std::string& tok = tokens[o];
        if (tok.size() != expected_digits) continue;
        for (std::size_t d = 0; d < tok.size(); ++d) {
            int val = HexVal(tok[tok.size() - 1 - d]);
            for (int k = 0; k < 4; ++k) {
                std::size_t pattern = d * 4 + static_cast<std::size_t>(k);
                if (pattern < st.bits[o].size()) {
                    st.bits[o][pattern] = ((val >> k) & 1) != 0;
                }
            }
        }
    }
    return st;
}

std::string BitsToHex(const std::vector<std::vector<bool>>& bits, int nin, int nout) {
    std::string result;
    result.reserve(static_cast<std::size_t>(nout) *
                   static_cast<std::size_t>((1 << nin) / 4) + 1);
    const int ndigits = (1 << nin) / 4;
    for (int o = 0; o < nout; ++o) {
        if (o > 0) result.push_back(',');
        for (int d = ndigits - 1; d >= 0; --d) {
            int val = 0;
            for (int k = 0; k < 4; ++k) {
                const int pattern = d * 4 + k;
                if (pattern < static_cast<int>(bits[static_cast<std::size_t>(o)].size()) &&
                    bits[static_cast<std::size_t>(o)][static_cast<std::size_t>(pattern)]) {
                    val |= (1 << k);
                }
            }
            result.push_back("0123456789ABCDEF"[val]);
        }
    }
    return result;
}

bool RenderTTEditorBody(int& nin, int& nout, std::vector<std::vector<bool>>& bits,
                        char* out_buffer, std::size_t out_buffer_size) {
    if (ImGui::InputInt("Inputs", &nin)) {
        nin = std::clamp(nin, 2, 8);
        for (auto& out : bits) out.resize(1u << nin, false);
    }
    if (ImGui::InputInt("Outputs", &nout)) {
        nout = std::clamp(nout, 1, 8);
        bits.resize(static_cast<std::size_t>(nout),
                    std::vector<bool>(1u << nin, false));
        for (auto& out : bits) out.resize(1u << nin, false);
    }

    ImGui::Separator();

    const int nrows = 1 << nin;
    const int ncols = 1 + nout;

    std::vector<std::string> col_names;
    col_names.reserve(static_cast<std::size_t>(ncols));
    col_names.push_back("In (MSB..LSB)");
    for (int o = 0; o < nout; ++o) col_names.push_back(fmt::format("Out{}", o));

    if (ImGui::BeginChild("TTEditorScroll", ImVec2(0, 350), true)) {
        const ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("TTEditor", ncols, tflags)) {
            for (const auto& name : col_names) ImGui::TableSetupColumn(name.c_str());
            ImGui::TableHeadersRow();

            for (int row = 0; row < nrows; ++row) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                std::string inbits;
                inbits.reserve(static_cast<std::size_t>(nin));
                for (int i = nin - 1; i >= 0; --i) {
                    inbits.push_back(static_cast<char>('0' + ((row >> i) & 1)));
                }
                ImGui::TextUnformatted(inbits.c_str());

                for (int o = 0; o < nout; ++o) {
                    ImGui::TableSetColumnIndex(1 + o);
                    bool b = bits[static_cast<std::size_t>(o)][static_cast<std::size_t>(row)];
                    std::string cid = fmt::format("##cell_o{}_r{}", o, row);
                    if (ImGui::Checkbox(cid.c_str(), &b)) {
                        bits[static_cast<std::size_t>(o)][static_cast<std::size_t>(row)] = b;
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    if (ImGui::Button("Clear All")) {
        for (auto& out : bits) std::fill(out.begin(), out.end(), false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Set All")) {
        for (auto& out : bits) std::fill(out.begin(), out.end(), true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Invert All")) {
        for (auto& out : bits) for (auto&& b : out) b = !b;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(nout < 2);
    if (ImGui::Button("Copy Out0 -> All")) {
        for (int o = 1; o < nout; ++o) {
            bits[static_cast<std::size_t>(o)] = bits.front();
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    bool should_close = false;
    if (ImGui::Button("Apply")) {
        std::string result = BitsToHex(bits, nin, nout);
        if (result.size() >= out_buffer_size) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Error: %zu-char hex doesn't fit the %zu-byte buffer. "
                "Reduce inputs/outputs.", result.size(), out_buffer_size);
        } else {
            std::snprintf(out_buffer, out_buffer_size, "%s", result.c_str());
            should_close = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        should_close = true;
    }
    return should_close;
}

void DrawControlPanel(AppState& state) {
    ImGui::Begin("SiDB Plot Control Panel");

    if (ImGui::BeginTabBar("ControlPanelTabs")) {
        if (ImGui::BeginTabItem("Gates")) {
            DrawGatesTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Generate Gates")) {
            DrawGateGeneratorTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Experiments")) {
            DrawExperimentsTab(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}