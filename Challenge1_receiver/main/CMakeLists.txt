idf_component_register(
    SRCS "Challenge1_receiver.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event nvs_flash esp_driver_ledc
)