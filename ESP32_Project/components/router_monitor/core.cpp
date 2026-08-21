#include "core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace router_monitor {

std::string trim_copy(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

std::string ascii_lower_copy(const std::string& value) {
    std::string result = value;
    for (char& ch : result) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    }
    return result;
}

bool ascii_case_equal(const std::string& left, const std::string& right) {
    return ascii_lower_copy(left) == ascii_lower_copy(right);
}

bool normalize_mac(const std::string& input, std::string& output) {
    std::string compact;
    compact.reserve(12);
    for (unsigned char ch : input) {
        if (std::isxdigit(ch) != 0) compact.push_back(static_cast<char>(std::toupper(ch)));
    }
    if (compact.size() != 12) return false;
    output.clear();
    output.reserve(17);
    for (std::size_t index = 0; index < compact.size(); index += 2) {
        if (!output.empty()) output.push_back('-');
        output.append(compact, index, 2);
    }
    return true;
}

bool device_matches(const Device& device, const Config& config) {
    if (config.target_type == "mac") {
        std::string actual;
        std::string expected;
        return normalize_mac(device.mac, actual) &&
               normalize_mac(config.target_value, expected) && actual == expected;
    }
    if (config.target_type == "name") {
        const std::string expected = trim_copy(config.target_value);
        return !expected.empty() &&
               (ascii_case_equal(trim_copy(device.name), expected) ||
                ascii_case_equal(trim_copy(device.hostname), expected));
    }
    return false;
}

static std::vector<std::uint16_t> utf8_to_utf16(const std::string& text) {
    std::vector<std::uint16_t> result;
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::uint32_t codepoint = first;
        std::size_t length = 1;
        if ((first & 0xe0U) == 0xc0U && index + 1 < text.size()) {
            codepoint = ((first & 0x1fU) << 6U) |
                        (static_cast<unsigned char>(text[index + 1]) & 0x3fU);
            length = 2;
        } else if ((first & 0xf0U) == 0xe0U && index + 2 < text.size()) {
            codepoint = ((first & 0x0fU) << 12U) |
                        ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 6U) |
                        (static_cast<unsigned char>(text[index + 2]) & 0x3fU);
            length = 3;
        } else if ((first & 0xf8U) == 0xf0U && index + 3 < text.size()) {
            codepoint = ((first & 0x07U) << 18U) |
                        ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 12U) |
                        ((static_cast<unsigned char>(text[index + 2]) & 0x3fU) << 6U) |
                        (static_cast<unsigned char>(text[index + 3]) & 0x3fU);
            length = 4;
        }
        if (codepoint <= 0xffffU) {
            result.push_back(static_cast<std::uint16_t>(codepoint));
        } else {
            codepoint -= 0x10000U;
            result.push_back(static_cast<std::uint16_t>(0xd800U | (codepoint >> 10U)));
            result.push_back(static_cast<std::uint16_t>(0xdc00U | (codepoint & 0x3ffU)));
        }
        index += length;
    }
    return result;
}

std::string encode_router_password(const std::string& password) {
    static constexpr char key[] = "RDpbLfCPsJZ7fiv";
    static constexpr char alphabet[] =
        "yLwVl0zKqws7LgKPRQ84Mdt708T1qQ3Ha7xv3H7NyU84p21BriUWBU43odz3iP4r"
        "BL3cD02KZciXTysVXiV8ngg6vL48rPJyAUw0HurW20xqxv9aYb4M9wK1Ae0wlro"
        "510qXeU07kV57fQMc8L6aLgMLwygtc0F10a0Dg70TOoouyFhdysuRMO51yY5ZlO"
        "ZZLEal1h0t9YQW0Ko7oBwmCAHoic4HYbUyVeU3sfQ1xtXcPcf1aT303wAQhv66qzW";
    constexpr std::size_t key_length = sizeof(key) - 1;
    constexpr std::size_t alphabet_length = sizeof(alphabet) - 1;
    const auto units = utf8_to_utf16(password);
    const std::size_t count = std::max(units.size(), key_length);
    std::string encoded;
    encoded.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint32_t left = index < key_length
                                       ? static_cast<unsigned char>(key[index]) : 187U;
        const std::uint32_t right = index < units.size() ? units[index] : 187U;
        encoded.push_back(alphabet[(left ^ right) % alphabet_length]);
    }
    return encoded;
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string percent_decode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hex_value(value[index + 1]);
            const int low = hex_value(value[index + 2]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        result.push_back(value[index]);
    }
    return result;
}

std::string percent_encode_path(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 3);
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('%');
            result.push_back(hex[ch >> 4U]);
            result.push_back(hex[ch & 0x0fU]);
        }
    }
    return result;
}

std::vector<std::string> split_tokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (std::size_t index = 0; index < value.size();) {
        const bool fullwidth = index + 2 < value.size() &&
            static_cast<unsigned char>(value[index]) == 0xefU &&
            static_cast<unsigned char>(value[index + 1]) == 0xbcU &&
            (static_cast<unsigned char>(value[index + 2]) == 0x8cU ||
             static_cast<unsigned char>(value[index + 2]) == 0x9bU);
        if (value[index] == ',' || value[index] == ';' || fullwidth) {
            const std::string token = trim_copy(current);
            if (!token.empty()) tokens.push_back(token);
            current.clear();
            index += fullwidth ? 3 : 1;
        } else {
            current.push_back(value[index++]);
        }
    }
    const std::string token = trim_copy(current);
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

const char* status_name(Status status) {
    switch (status) {
        case Status::online: return "ONLINE";
        case Status::offline: return "OFFLINE";
        case Status::error: return "ERROR";
    }
    return "ERROR";
}

std::string format_timestamp(std::time_t value) {
    char output[48]{};
    std::tm local{};
    localtime_r(&value, &local);
    if (std::strftime(output, sizeof(output), "%Y-%m-%d %H:%M:%S %z", &local) == 0) {
        std::snprintf(output, sizeof(output), "%lld", static_cast<long long>(value));
    }
    return output;
}

std::string Config::target_key() const {
    return target_type + ":" +
           (target_type == "name" ? ascii_lower_copy(target_value) : target_value);
}

}  // namespace router_monitor
