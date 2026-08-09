idf_component_register(
    SRCS
        "main.c"
        "protocol.c"
        "pins.c"
        "cmd_gpio.c"
        "cmd_pwm.c"
        "cmd_adc.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio esp_driver_ledc esp_driver_usb_serial_jtag esp_adc
)
