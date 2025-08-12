#include <iostream>
#include <vector>
#include <string>
#include <cctype>

// Check if string is all the same digit '9'
bool is_all_nines(const std::string &num) {
    for (char c : num) {
        if (c != '9') return false;
    }
    return true;
}

// Check if string is a power of 10 (like 1, 10, 100, 1000...)
bool is_power_of_10(const std::string &num) {
    if (num.empty()) return false;
    if (num[0] != '1') return false;
    for (size_t i = 1; i < num.size(); ++i) {
        if (num[i] != '0') return false;
    }
    return true;
}

// Convert if it's power of 10 or all 9s
std::string convert_if_special(const std::string &num) {
    // Remove leading zeros
    size_t start = num.find_first_not_of('0');
    if (start == std::string::npos) return "0"; // all zeros

    std::string trimmed = num.substr(start);

    if (is_power_of_10(trimmed)) {
        // Special case: "10" should be 1E1
        return "1E" + std::to_string(trimmed.size() - 1);
    }
    if (is_all_nines(trimmed)) {
        return "1E" + std::to_string(trimmed.size());
    }

    // Not special → echo original
    return num;
}

// Recursively format tower
std::string format_tower(const std::vector<std::string> &exps) {
    if (exps.size() == 1) {
        return convert_if_special(exps[0]);
    }
    return convert_if_special(exps[0]) + "^(" +
           format_tower(std::vector<std::string>(exps.begin() + 1, exps.end())) + ")";
}

// Parse string like "999^9999^999" into vector<string>
std::vector<std::string> parse_tower(const std::string &expr) {
    std::vector<std::string> exps;
    size_t pos = 0, prev = 0;
    while ((pos = expr.find('^', prev)) != std::string::npos) {
        exps.push_back(expr.substr(prev, pos - prev));
        prev = pos + 1;
    }
    exps.push_back(expr.substr(prev));
    return exps;
}

int main() {
    std::string expr;
    std::getline(std::cin, expr);

    auto exps = parse_tower(expr);
    std::cout << format_tower(exps) << "\n";

    return 0;
}
