#pragma once

#include <vector>
#include <string>
#include <kitty/dynamic_truth_table.hpp>

std::vector<kitty::dynamic_truth_table> ParseTruthTablesFromHex(const char* buffer);
std::vector<std::string> ParseHexTokens(const char* buffer);
void FormatHexTokens(char* buffer, std::size_t buffer_size,
                     const std::vector<std::string>& tokens);