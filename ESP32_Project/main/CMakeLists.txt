idf_component_register(
    SRCS
        "app_main.cpp"
        "codex_quota_state.cpp"
        "hardware_state.cpp"
        "oled_display.cpp"
        "oled_ui.cpp"
        "pc_connection_state.cpp"
        "pc_protocol.cpp"
        "uart_receiver.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_uart
        esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
)
