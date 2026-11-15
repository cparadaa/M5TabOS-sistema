idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "." "../../compartilhado"
                       REQUIRES esp_hosted
                       PRIV_REQUIRES nvs_flash esp_wifi esp_coex esp_ringbuf app_update esp_timer espressif__esp-now ieee802154 espcoredump esp_pm esp_driver_gpio)

# Conta o que o P4 manda para a rede (o __wrap em app_main.c, "O QUE O P4 MANDA").
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_wifi_internal_tx")
