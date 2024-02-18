/**
 *  @brief  Helper structures and functions for C++ tests.
 */
#pragma once
#include <fstream>     // `std::ifstream`
#include <iostream>    // `std::cout`, `std::endl`
#include <random>      // `std::random_device`
#include <string>      // `std::string`
#include <type_traits> // `std::conditional`, `std::is_signed`
#include <vector>      // `std::vector`

namespace ashvardanian {
namespace stringzilla {
namespace scripts {

inline std::string read_file(std::string path) {
    std::ifstream stream(path);
    if (!stream.is_open()) { throw std::runtime_error("Failed to open file: " + path); }
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

inline void write_file(std::string path, std::string content) {
    std::ofstream stream(path);
    if (!stream.is_open()) { throw std::runtime_error("Failed to open file: " + path); }
    stream << content;
    stream.close();
}

inline std::mt19937 &global_random_generator() {
    static std::random_device seed_source; // Too expensive to construct every time
    static std::mt19937 generator(seed_source());
    return generator;
}

inline void randomize_string(char *string, std::size_t length, char const *alphabet, std::size_t cardinality) {
    using max_alphabet_size_t = std::uint8_t;
#if defined(_MSC_VER) && !defined(__clang__)
    // MSVC does not support std::uniform_int_distribution for char types
    using compatible_type = std::conditional<
        sizeof(max_alphabet_size_t) == 1,
        std::conditional<std::is_signed<max_alphabet_size_t>::value, std::int16_t, std::uint16_t>::type,
        max_alphabet_size_t>::type;
#else
    using compatible_type = max_alphabet_size_t;
#endif
    std::uniform_int_distribution<compatible_type> distribution(1, static_cast<max_alphabet_size_t>(cardinality));
    std::generate(string, string + length, [&]() -> char {
        return alphabet[static_cast<compatible_type>(distribution(global_random_generator()))];
    });
}

inline std::string random_string(std::size_t length, char const *alphabet, std::size_t cardinality) {
    std::string result(length, '\0');
    randomize_string(&result[0], length, alphabet, cardinality);
    return result;
}

/**
 *  @brief  Inefficient baseline Levenshtein distance computation, as implemented in most codebases.
 *          Allocates a new matrix on every call, with rows potentially scattered around memory.
 */
inline std::size_t levenshtein_baseline(char const *s1, std::size_t len1, char const *s2, std::size_t len2) {
    std::vector<std::vector<std::size_t>> dp(len1 + 1, std::vector<std::size_t>(len2 + 1));

    // Initialize the borders of the matrix.
    for (std::size_t i = 0; i <= len1; ++i) dp[i][0] = i;
    for (std::size_t j = 0; j <= len2; ++j) dp[0][j] = j;

    for (std::size_t i = 1; i <= len1; ++i) {
        for (std::size_t j = 1; j <= len2; ++j) {
            std::size_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            // dp[i][j] is the minimum of deletion, insertion, or substitution
            dp[i][j] = std::min({
                dp[i - 1][j] + 1,       // Deletion
                dp[i][j - 1] + 1,       // Insertion
                dp[i - 1][j - 1] + cost // Substitution
            });
        }
    }

    return dp[len1][len2];
}

/**
 *  @brief  Produces a substitution cost matrix for the Needlemann-Wunsch alignment score,
 *          that would yield the same result as the negative Levenshtein distance.
 */
inline std::vector<std::int8_t> unary_substitution_costs() {
    std::vector<std::int8_t> result(256 * 256);
    for (std::size_t i = 0; i != 256; ++i)
        for (std::size_t j = 0; j != 256; ++j) result[i * 256 + j] = (i == j ? 0 : -1);
    return result;
}

} // namespace scripts
} // namespace stringzilla
} // namespace ashvardanian