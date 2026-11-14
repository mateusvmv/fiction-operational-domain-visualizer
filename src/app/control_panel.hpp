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

std::vector<std::string> ParseHexTokens(const char* buffer);
void FormatHexTokens(char* buffer, std::size_t buffer_size,
                     const std::vector<std::string>& tokens);
TTEditorState BuildTTEditorFromBuffer(const char* buffer);
std::string BitsToHex(const std::vector<std::vector<bool>>& bits, int nin, int nout);
bool RenderTTEditorBody(int& nin, int& nout, std::vector<std::vector<bool>>& bits,
                        char* out_buffer, std::size_t out_buffer_size);

void DrawGatesTab(AppState& state);
void DrawGateGeneratorTab(AppState& state);
void DrawExperimentsTab(AppState& state);