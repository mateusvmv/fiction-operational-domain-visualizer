#pragma once

#include "../app.hpp"
#include <cstddef>
#include <string>
#include <vector>

struct TTEditorState {
    int nin = 2;
    int nout = 1;
    std::vector<std::vector<bool>> bits;
};

TTEditorState BuildTTEditorFromBuffer(const char* buffer);
std::string BitsToHex(const std::vector<std::vector<bool>>& bits, int nin, int nout);
bool RenderTTEditorBody(int& nin, int& nout, std::vector<std::vector<bool>>& bits,
                        char* out_buffer, std::size_t out_buffer_size);

void DrawGatesTab(AppState& state);
void DrawGateGeneratorTab(AppState& state);
void DrawExperimentsTab(AppState& state);

struct PerPatternSnapshot {
    std::uint64_t            input_pattern = 0;              // 0 .. 2^n-1
    std::vector<std::int8_t> charge_states;                  // -1 / 0 / +1 / 127 (NONE) per dot
    std::vector<std::uint8_t> expected_output;               // one bit per truth-table output
    bool                     operational = false;
};