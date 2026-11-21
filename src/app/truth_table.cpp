#include "truth_table.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

#include <kitty/dynamic_truth_table.hpp>
#include <kitty/constructors.hpp>

std::vector<kitty::dynamic_truth_table> ParseTruthTablesFromHex(const char* buffer)
{
    const auto tokens = ParseHexTokens(buffer);
    std::vector<kitty::dynamic_truth_table> tts;
    tts.reserve(tokens.size());

    for (const auto& hex : tokens) {
        if (hex.empty()) {
            throw std::invalid_argument("Truth table contains an empty hexadecimal token.");
        }
        const unsigned num_vars = static_cast<unsigned>(std::log2(static_cast<double>(hex.size()) * 4.0));
        kitty::dynamic_truth_table tt(num_vars);
        kitty::create_from_hex_string(tt, hex);
        tts.push_back(std::move(tt));
    }
    return tts;
}

void FormatHexTokens(char* buffer, std::size_t buffer_size,
                     const std::vector<std::string>& tokens) {
    std::string joined;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) joined += ',';
        joined += tokens[i];
    }
    if (joined.size() >= buffer_size) joined.resize(buffer_size - 1);
    std::snprintf(buffer, buffer_size, "%s", joined.c_str());
}

std::vector<std::string> ParseHexTokens(const char* buffer) {
    std::vector<std::string> tokens;
    std::string s(buffer ? buffer : "");
    std::size_t pos = 0;
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    while (pos <= s.size()) {
        std::size_t comma = s.find(',', pos);
        if (comma == std::string::npos) comma = s.size();
        std::string tok = s.substr(pos, comma - pos);
        tok.erase(tok.begin(), std::find_if(tok.begin(), tok.end(), not_space));
        tok.erase(std::find_if(tok.rbegin(), tok.rend(), not_space).base(), tok.end());
        if (!tok.empty()) tokens.push_back(std::move(tok));
        if (comma == s.size()) break;
        pos = comma + 1;
    }
    return tokens;
}
