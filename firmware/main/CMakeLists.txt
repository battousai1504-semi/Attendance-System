set(CDP_COMBINED_CA_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/cdp_ca_bundle.pem")
set(CDP_GOOGLE_CA_SOURCE "${CMAKE_CURRENT_LIST_DIR}/google_tls_intermediates.pem")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CDP_GOOGLE_CA_SOURCE}")

# Google currently serves a cross-signed GTS chain whose final issuer is the
# legacy GlobalSign Root CA. Embed only the three required CA certificates:
# parsing the complete 140+ certificate Mozilla bundle exhausts heap while
# the camera and face-recognition models are active.
file(READ "${CDP_GOOGLE_CA_SOURCE}" CDP_GOOGLE_CA_CONTENT)
file(WRITE "${CDP_COMBINED_CA_BUNDLE}" "${CDP_GOOGLE_CA_CONTENT}\n")

idf_component_register(
    SRCS
        "app_main.c"
        "attendance.c"
        "face_engine.cpp"
        "manual_clock.c"
        "student_db.c"
        "wifi_time.c"
        "http_upload.c"
        "enroll_server.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "${CDP_COMBINED_CA_BUNDLE}"
    REQUIRES
        esp32-camera
        esp_http_client
        esp_http_server
        esp_wifi
        esp_netif
        esp_event
        mbedtls
        esp_driver_gpio
        spiffs
        nvs_flash
        human_face_recognition
        human_face_detect
        esp-dl
)
