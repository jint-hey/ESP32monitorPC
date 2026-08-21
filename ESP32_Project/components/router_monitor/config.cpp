#include "config.hpp"

#include "core.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <map>

#include "esp_spiffs.h"

namespace router_monitor {

bool mount_config_filesystem(std::string& error) {
    esp_vfs_spiffs_conf_t mount{};
    mount.base_path = "/config";
    mount.partition_label = "storage";
    mount.max_files = 4;
    mount.format_if_mount_failed = false;
    const esp_err_t result = esp_vfs_spiffs_register(&mount);
    if (result != ESP_OK) {
        error = "挂载 SPIFFS 配置分区失败：" + std::string(esp_err_to_name(result));
        return false;
    }
    return true;
}

static bool parse_positive(const std::string& text, int default_value, int& output) {
    if (text.empty() && default_value > 0) {
        output = default_value;
        return true;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed <= 0 || parsed > 86400) {
        return false;
    }
    output = static_cast<int>(parsed);
    return true;
}

bool load_config(const char* path, Config& config, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = std::string("无法读取配置文件：") + path;
        return false;
    }
    std::map<std::string, std::string> values;
    std::string line;
    bool first_line = true;
    while (std::getline(input, line)) {
        if (first_line && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xefU &&
            static_cast<unsigned char>(line[1]) == 0xbbU &&
            static_cast<unsigned char>(line[2]) == 0xbfU) {
            line.erase(0, 3);
        }
        first_line = false;
        const std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == '!') continue;
        const std::size_t separator = trimmed.find_first_of("=:");
        if (separator == std::string::npos) continue;
        values[trim_copy(trimmed.substr(0, separator))] =
            trim_copy(trimmed.substr(separator + 1));
    }
    const auto required = [&](const char* key, std::string& destination) -> bool {
        const auto item = values.find(key);
        if (item == values.end() || item->second.empty()) {
            error = std::string("配置项 ") + key + " 不能为空";
            return false;
        }
        destination = item->second;
        return true;
    };
    std::string token_text;
    if (!required("WIFI_SSID", config.wifi_ssid) ||
        !required("ROUTER_URL", config.router_url) ||
        !required("ROUTER_PASSWORD", config.router_password) ||
        !required("TARGET_TYPE", config.target_type) ||
        !required("TARGET_VALUE", config.target_value) ||
        !required("PUSHPLUS_TOKEN", token_text)) return false;
    config.wifi_password = values["WIFI_PASSWORD"];
    if (config.wifi_ssid.size() > 32) {
        error = "WIFI_SSID 不能超过 32 字节";
        return false;
    }
    if (config.wifi_password.size() > 64) {
        error = "WIFI_PASSWORD 不能超过 64 字节";
        return false;
    }
    while (config.router_url.size() > 1 && config.router_url.back() == '/') {
        config.router_url.pop_back();
    }
    if (config.router_url.rfind("http://", 0) != 0 &&
        config.router_url.rfind("https://", 0) != 0) {
        error = "ROUTER_URL 必须以 http:// 或 https:// 开头";
        return false;
    }
    config.target_type = ascii_lower_copy(config.target_type);
    if (config.target_type != "mac" && config.target_type != "name") {
        error = "TARGET_TYPE 只能是 mac 或 name";
        return false;
    }
    if (config.target_type == "mac") {
        std::string normalized;
        if (!normalize_mac(config.target_value, normalized)) {
            error = "TARGET_VALUE 不是有效的 MAC 地址";
            return false;
        }
        config.target_value = normalized;
    }
    if (!parse_positive(values["QUERY_INTERVAL_SECONDS"], 60,
                        config.query_interval_seconds) ||
        config.query_interval_seconds < 10) {
        error = "QUERY_INTERVAL_SECONDS 必须是不小于 10 的整数";
        return false;
    }
    if (!parse_positive(values["REQUEST_TIMEOUT_SECONDS"], 10,
                        config.request_timeout_seconds)) {
        error = "REQUEST_TIMEOUT_SECONDS 必须是正整数";
        return false;
    }
    config.pushplus_tokens = split_tokens(token_text);
    if (config.pushplus_tokens.empty()) {
        error = "PUSHPLUS_TOKEN 不能为空";
        return false;
    }
    const auto timezone = values.find("TIMEZONE");
    if (timezone != values.end() && !timezone->second.empty()) config.timezone = timezone->second;
    return true;
}

}  // namespace router_monitor
