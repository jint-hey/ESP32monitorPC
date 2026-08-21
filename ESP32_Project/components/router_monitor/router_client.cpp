#include "router_client.hpp"

#include "core.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "cJSON.h"
#include "esp_err.h"

namespace router_monitor {

static std::string print_json(cJSON* value) {
    char* text = cJSON_PrintUnformatted(value);
    if (text == nullptr) return {};
    std::string result(text);
    cJSON_free(text);
    return result;
}

static bool read_error_code(const cJSON* root, bool& present, int& code) {
    static constexpr const char* names[] = {"error_code", "err_code", "errorCode"};
    present = false;
    for (const char* name : names) {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
        if (item == nullptr) continue;
        present = true;
        if (cJSON_IsNumber(item)) {
            code = item->valueint;
            return true;
        }
        if (cJSON_IsString(item) && item->valuestring != nullptr) {
            char* end = nullptr;
            errno = 0;
            const long parsed = std::strtol(item->valuestring, &end, 10);
            if (errno == 0 && end != item->valuestring && *end == '\0') {
                code = static_cast<int>(parsed);
                return true;
            }
        }
        return false;
    }
    return true;
}

static std::string transport_message(const HttpResponse& response) {
    if (!response.transport_ok()) {
        return "HTTP 传输失败：" + std::string(esp_err_to_name(response.transport_error));
    }
    return "HTTP 请求失败（状态码 " + std::to_string(response.status_code) + "）";
}

RouterClient::RouterClient(const Config& config)
    : router_url_(config.router_url),
      encoded_password_(encode_router_password(config.router_password)),
      http_(config.request_timeout_seconds) {}

bool RouterClient::login(std::string& error) {
    cJSON* request = cJSON_CreateObject();
    cJSON* login = cJSON_CreateObject();
    if (request == nullptr || login == nullptr ||
        !cJSON_AddStringToObject(request, "method", "do") ||
        !cJSON_AddStringToObject(login, "password", encoded_password_.c_str())) {
        cJSON_Delete(request);
        cJSON_Delete(login);
        error = "创建登录请求失败：内存不足";
        return false;
    }
    cJSON_AddItemToObject(request, "login", login);
    const std::string payload = print_json(request);
    cJSON_Delete(request);
    if (payload.empty()) {
        error = "创建登录请求失败：内存不足";
        return false;
    }
    const HttpResponse response = http_.post_json(router_url_ + "/", payload);
    cJSON* root = cJSON_Parse(response.body.c_str());
    bool code_present = false;
    int code = 0;
    const bool valid_code = root != nullptr && read_error_code(root, code_present, code);
    if (!response.http_ok()) {
        stok_.clear();
        if (response.status_code == 401 && valid_code && code_present) {
            int remaining = 0;
            const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
            const cJSON* time = cJSON_IsObject(data)
                                    ? cJSON_GetObjectItemCaseSensitive(data, "time") : nullptr;
            if (cJSON_IsNumber(time)) remaining = time->valueint;
            error = "路由器认证失败（错误码 " + std::to_string(code) + "，密码错误";
            if (remaining > 0) {
                error += "；锁定前剩余尝试次数 " + std::to_string(remaining);
            }
            error += "）";
        } else {
            error = transport_message(response);
        }
        cJSON_Delete(root);
        return false;
    }
    if (root == nullptr || !valid_code) {
        cJSON_Delete(root);
        error = "路由器登录响应不是有效 JSON";
        return false;
    }
    if (code_present && code != 0) {
        cJSON_Delete(root);
        stok_.clear();
        error = "路由器认证失败（错误码 " + std::to_string(code) + "）";
        return false;
    }
    const cJSON* stok = cJSON_GetObjectItemCaseSensitive(root, "stok");
    if (!cJSON_IsString(stok) || stok->valuestring == nullptr || stok->valuestring[0] == '\0') {
        cJSON_Delete(root);
        error = "路由器登录响应缺少 stok";
        return false;
    }
    stok_ = percent_decode(stok->valuestring);
    cJSON_Delete(root);
    return true;
}

bool RouterClient::query_online_hosts(std::string& response_body, std::string& error) {
    static constexpr char payload[] =
        "{\"hosts_info\":{\"table\":\"online_host\"},\"method\":\"get\"}";
    if (stok_.empty() && !login(error)) return false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string url = router_url_ + "/stok=" + percent_encode_path(stok_) + "/ds";
        const HttpResponse response = http_.post_json(url, payload);
        if (!response.http_ok()) {
            if (response.status_code == 401 && attempt == 0) {
                stok_.clear();
                if (login(error)) continue;
            }
            error = transport_message(response);
            return false;
        }
        cJSON* root = cJSON_Parse(response.body.c_str());
        bool code_present = false;
        int code = 0;
        if (root == nullptr || !read_error_code(root, code_present, code)) {
            cJSON_Delete(root);
            error = "路由器设备响应不是有效 JSON";
            return false;
        }
        cJSON_Delete(root);
        if (code_present && code == 401 && attempt == 0) {
            stok_.clear();
            if (login(error)) continue;
            return false;
        }
        if (code_present && code != 0) {
            error = "查询在线设备失败（错误码 " + std::to_string(code) + "）";
            return false;
        }
        response_body = response.body;
        return true;
    }
    error = "路由器会话重新认证失败";
    return false;
}

static bool has_device_field(const cJSON* row) {
    static constexpr const char* fields[] = {
        "mac", "macaddr", "mac_addr", "name", "hostname", "host_name",
        "ip", "ipaddr", "ip_addr"
    };
    for (const char* field : fields) {
        if (cJSON_GetObjectItemCaseSensitive(row, field) != nullptr) return true;
    }
    return false;
}

static const cJSON* unwrap_row(const cJSON* row) {
    if (!cJSON_IsObject(row) || has_device_field(row)) return row;
    if (row->child != nullptr && row->child->next == nullptr && cJSON_IsObject(row->child)) {
        return row->child;
    }
    return row;
}

static std::string string_field(const cJSON* row,
                                std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(row, name);
        if (cJSON_IsString(value) && value->valuestring != nullptr) {
            return trim_copy(percent_decode(value->valuestring));
        }
        if (cJSON_IsNumber(value)) return std::to_string(value->valuedouble);
    }
    return {};
}

static bool append_device(const cJSON* raw_row, std::vector<Device>& devices,
                          std::string& error) {
    const cJSON* row = unwrap_row(raw_row);
    if (!cJSON_IsObject(row)) {
        error = "在线设备列表包含无效记录";
        return false;
    }
    Device device;
    device.mac = string_field(row, {"mac", "macaddr", "mac_addr"});
    device.name = string_field(row, {"name"});
    device.hostname = string_field(row, {"hostname", "host_name"});
    device.ip = string_field(row, {"ip", "ipaddr", "ip_addr"});
    devices.push_back(std::move(device));
    return true;
}

bool RouterClient::get_online_devices(std::vector<Device>& devices, std::string& error) {
    devices.clear();
    std::string body;
    if (!query_online_hosts(body, error)) return false;
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        error = "路由器设备响应不是有效 JSON";
        return false;
    }
    const cJSON* hosts = cJSON_GetObjectItemCaseSensitive(root, "hosts_info");
    const cJSON* online = cJSON_IsObject(hosts)
                              ? cJSON_GetObjectItemCaseSensitive(hosts, "online_host") : nullptr;
    if (online == nullptr) {
        cJSON_Delete(root);
        error = "路由器响应缺少 hosts_info.online_host";
        return false;
    }
    bool ok = true;
    if (cJSON_IsNull(online)) {
        ok = true;
    } else if (cJSON_IsArray(online)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, online) {
            if (!append_device(item, devices, error)) { ok = false; break; }
        }
    } else if (cJSON_IsObject(online)) {
        if (has_device_field(online)) {
            ok = append_device(online, devices, error);
        } else {
            const cJSON* item = nullptr;
            cJSON_ArrayForEach(item, online) {
                if (!append_device(item, devices, error)) { ok = false; break; }
            }
        }
    } else {
        error = "在线设备列表格式无法识别";
        ok = false;
    }
    cJSON_Delete(root);
    return ok;
}

void RouterClient::logout() {
    if (stok_.empty()) return;
    const std::string url = router_url_ + "/stok=" + percent_encode_path(stok_) + "/ds";
    static constexpr char payload[] =
        "{\"system\":{\"logout\":\"null\"},\"method\":\"do\"}";
    (void)http_.post_json(url, payload);
    stok_.clear();
}

}  // namespace router_monitor
