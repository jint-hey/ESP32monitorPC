#include "notifier.hpp"

#include "core.hpp"

#include "cJSON.h"
#include "esp_err.h"

namespace router_monitor {

static constexpr char pushplus_url[] = "https://www.pushplus.plus/send";

static const char* status_title(Status status) {
    switch (status) {
        case Status::online: return "设备恢复在线";
        case Status::offline: return "设备离线告警";
        case Status::error: return "设备检测异常";
    }
    return "设备检测异常";
}

Notifier::Notifier(const Config& config)
    : config_(config), http_(config.request_timeout_seconds) {}

bool Notifier::send_one(const std::string& token, const std::string& title,
                        const std::string& content, std::string& error) const {
    cJSON* request = cJSON_CreateObject();
    if (request == nullptr ||
        !cJSON_AddStringToObject(request, "token", token.c_str()) ||
        !cJSON_AddStringToObject(request, "title", title.c_str()) ||
        !cJSON_AddStringToObject(request, "content", content.c_str()) ||
        !cJSON_AddStringToObject(request, "template", "txt")) {
        cJSON_Delete(request);
        error = "创建 PushPlus 请求失败：内存不足";
        return false;
    }
    char* raw_payload = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (raw_payload == nullptr) {
        error = "创建 PushPlus 请求失败：内存不足";
        return false;
    }
    const std::string payload(raw_payload);
    cJSON_free(raw_payload);
    const HttpResponse response = http_.post_json(pushplus_url, payload);
    if (!response.http_ok()) {
        error = response.transport_ok()
                    ? "PushPlus HTTP 状态码 " + std::to_string(response.status_code)
                    : "PushPlus 连接失败：" +
                          std::string(esp_err_to_name(response.transport_error));
        return false;
    }
    cJSON* root = cJSON_Parse(response.body.c_str());
    if (root == nullptr) {
        error = "PushPlus 返回了无效 JSON";
        return false;
    }
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON* message = cJSON_GetObjectItemCaseSensitive(root, "msg");
    const int result_code = cJSON_IsNumber(code) ? code->valueint : -1;
    if (result_code != 200) {
        error = "PushPlus 推送失败（" + std::to_string(result_code) + ": " +
                (cJSON_IsString(message) && message->valuestring != nullptr
                     ? std::string(message->valuestring) : "未知错误") + "）";
        cJSON_Delete(root);
        return false;
    }
    cJSON_Delete(root);
    return true;
}

bool Notifier::send_to_all(const std::string& title, const std::string& content,
                           std::string& error) const {
    std::vector<std::string> errors;
    for (std::size_t index = 0; index < config_.pushplus_tokens.size(); ++index) {
        std::string current;
        if (!send_one(config_.pushplus_tokens[index], title, content, current)) {
            errors.push_back("第 " + std::to_string(index + 1) + " 个 token: " + current);
        }
    }
    if (errors.empty()) return true;
    error.clear();
    for (const auto& item : errors) {
        if (!error.empty()) error += "；";
        error += item;
    }
    return false;
}

bool Notifier::send_status(Status status, const std::string& detail,
                           std::time_t checked_at, std::string& error) const {
    const std::string content =
        "状态：" + std::string(status_name(status)) +
        "\n目标：" + config_.target_value +
        "\n时间：" + format_timestamp(checked_at) +
        "\n详情：" + detail;
    return send_to_all(status_title(status), content, error);
}

bool Notifier::send_lifecycle(const std::string& message, const CheckResult* result,
                              std::time_t occurred_at, std::string& error) const {
    std::string content = message + "\n时间：" + format_timestamp(occurred_at);
    if (result != nullptr) {
        content += "\n设备状态：" + std::string(status_name(result->status));
        content += "\n目标：" + config_.target_value;
        content += "\n检测时间：" + format_timestamp(result->checked_at);
        content += "\n详情：" + result->detail;
    }
    return send_to_all(message, content, error);
}

}  // namespace router_monitor
