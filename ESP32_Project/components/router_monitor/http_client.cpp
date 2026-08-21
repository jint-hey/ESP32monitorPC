#include "http_client.hpp"

#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

namespace router_monitor {

static esp_err_t handle_http_event(esp_http_client_event_t* event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr && event->data_len > 0) {
        auto* body = static_cast<std::string*>(event->user_data);
        body->append(static_cast<const char*>(event->data),
                     static_cast<std::size_t>(event->data_len));
    }
    return ESP_OK;
}

HttpResponse HttpClient::post_json(const std::string& url, const std::string& json) const {
    HttpResponse response;
    esp_http_client_config_t settings{};
    settings.url = url.c_str();
    settings.event_handler = handle_http_event;
    settings.user_data = &response.body;
    settings.timeout_ms = timeout_seconds_ * 1000;
    settings.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&settings);
    if (client == nullptr) {
        response.transport_error = ESP_ERR_NO_MEM;
        return response;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json; charset=UTF-8");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "User-Agent", "router-device-monitor/1.0-esp32s3");
    esp_http_client_set_post_field(client, json.data(), static_cast<int>(json.size()));
    response.transport_error = esp_http_client_perform(client);
    if (response.transport_error == ESP_OK) {
        response.status_code = esp_http_client_get_status_code(client);
    } else {
        response.socket_errno = esp_http_client_get_errno(client);
    }
    esp_http_client_cleanup(client);
    return response;
}

}  // namespace router_monitor
