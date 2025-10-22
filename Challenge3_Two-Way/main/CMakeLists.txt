idf_component_register(
    SRCS "Challenge3_Two-Way.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash
    PRIV_REQUIRES esp_driver_gpio esp_timer
)                   
