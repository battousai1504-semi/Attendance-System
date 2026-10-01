/*
 * =====================================================================
 *  app_main.c — Robot Điểm Danh Tự Động
 *  ESP32-S3 N16R8 + OV2640 (PIN_CONFIG = 2)
 *
 *  State machine:
 *    BOOT → WIFI_CONNECTING → NTP_SYNC → RECOGNIZE_LOOP
 *                                          ↑ (URL trigger)
 *                                          └─ ENROLL_MODE
 * =====================================================================
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_psram.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "attendance.h"
#include "audio_feedback.h"
#include "app_version.h"
#include "board_pins.h"
#include "device_config.h"
#include "enroll_server.h"
#include "face_engine.h"
#include "http_upload.h"
#include "maintenance_mode.h"
#include "manual_clock.h"
#include "ota_manager.h"
#include "student_db.h"
#include "wifi_time.h"

_Static_assert(AUDIO_FEEDBACK_STUDENT_MAX_ID == STUDENT_MAX_COUNT,
               "Student voice track range must cover every enrolled ID");

/* ─── Cấu hình ───────────────────────────────────────────────── */

/* Đường dẫn SPIFFS để lưu face database */
#define FACE_DB_PATH    "/spiffs"

/* Thời gian chờ WiFi kết nối và NTP đặt đồng hồ hệ thống (ms). */
#define NETWORK_TIME_READY_TIMEOUT_MS  30000
#define CAMERA_RECOVERY_THRESHOLD       3
#define CAMERA_RECOVERY_DELAY_MS        250
static const char *TAG = "app_main";
static bool s_upload_enabled;
static device_config_values_t s_device_config;
static device_config_status_t s_device_config_status;

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external-pin";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt-watchdog";
        case ESP_RST_TASK_WDT: return "task-watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

/* ─── LED trạng thái ─────────────────────────────────────────── */
static void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(STATUS_LED_GPIO, 0);
}

static void led_set(bool on)
{
    gpio_set_level(STATUS_LED_GPIO, on ? 1 : 0);
}

static void led_blink(int times, int delay_ms)
{
    for (int i = 0; i < times; i++) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void fatal_error_loop(const char *reason)
{
    ota_manager_reject_pending_image(reason);
    ESP_LOGE(TAG, "FATAL: %s; device stopped in safe mode", reason);
    while (true) {
        led_blink(5, 150);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ─── Khởi tạo Camera ────────────────────────────────────────── */
static esp_err_t init_camera(void)
{
    camera_config_t config = {
        .pin_pwdn      = CAM_PIN_PWDN,
        .pin_reset     = CAM_PIN_RESET,
        .pin_xclk      = CAM_PIN_XCLK,
        .pin_sccb_sda  = CAM_PIN_SIOD,
        .pin_sccb_scl  = CAM_PIN_SIOC,
        .pin_d7        = CAM_PIN_D7,
        .pin_d6        = CAM_PIN_D6,
        .pin_d5        = CAM_PIN_D5,
        .pin_d4        = CAM_PIN_D4,
        .pin_d3        = CAM_PIN_D3,
        .pin_d2        = CAM_PIN_D2,
        .pin_d1        = CAM_PIN_D1,
        .pin_d0        = CAM_PIN_D0,
        .pin_vsync     = CAM_PIN_VSYNC,
        .pin_href      = CAM_PIN_HREF,
        .pin_pclk      = CAM_PIN_PCLK,
        .xclk_freq_hz  = 20000000,
        .ledc_timer    = LEDC_TIMER_0,
        .ledc_channel  = LEDC_CHANNEL_0,
        .pixel_format  = PIXFORMAT_JPEG,
        .frame_size    = FRAMESIZE_VGA,   /* 640x480, AI model cần >=320x240 */
        .jpeg_quality  = 12,
        .fb_count      = 2,
        .fb_location   = CAMERA_FB_IN_PSRAM,
        .grab_mode     = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "Camera OK (VGA, PSRAM)");
    return ESP_OK;
}

/* ─── Khởi tạo SPIFFS ────────────────────────────────────────── */
static bool spiffs_partition_is_erased(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "faces");
    if (partition == NULL) return false;

    uint8_t buffer[256];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
        size_t length = partition->size - offset;
        if (length > sizeof(buffer)) length = sizeof(buffer);
        if (esp_partition_read(partition, offset, buffer, length) != ESP_OK) {
            return false;
        }
        for (size_t index = 0; index < length; index++) {
            if (buffer[index] != 0xff) return false;
        }
    }
    return true;
}

static esp_err_t init_spiffs(void)
{
    const bool first_boot = spiffs_partition_is_erased();
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path              = FACE_DB_PATH,
        .partition_label        = "faces",  /* tên trong partitions.csv */
        .max_files              = 10,
        /* A blank factory partition may be formatted once. A non-blank
         * filesystem that fails to mount is never erased automatically. */
        .format_if_mount_failed = first_boot,
    };
    if (first_boot) ESP_LOGI(TAG, "Blank SPIFFS partition detected; first-boot format allowed");
    esp_err_t err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    } else {
        size_t total = 0;
        size_t used = 0;
        esp_err_t info_err = esp_spiffs_info("faces", &total, &used);
        if (info_err == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS mounted at %s: %u/%u bytes used",
                     FACE_DB_PATH, (unsigned)used, (unsigned)total);
            if (total > used && total - used < 128 * 1024) {
                ESP_LOGW(TAG, "SPIFFS free space is below 128 KiB");
            }
        } else {
            ESP_LOGW(TAG, "SPIFFS mounted but usage query failed: %s",
                     esp_err_to_name(info_err));
        }
    }
    return err;
}

/* ─── Xử lý kết quả nhận diện thành công ────────────────────── */
static void on_recognized(const face_engine_result_t *result)
{
    student_info_t student;
    if (student_db_get((uint16_t)result->id, &student) != ESP_OK) {
        ESP_LOGE(TAG, "Recognized face ID=%d has no student metadata; event rejected",
                 result->id);
        led_blink(4, 200);
        return;
    }
    const char *name = student.name;
    const char *class_name = student.class_name;

    /* Prepare first, then persist to the outbox before accepting the event. */
    attendance_entry_t entry = {0};
    attendance_result_t att = attendance_prepare((uint16_t)result->id, "FACE", &entry);

    if (att == ATTENDANCE_RECORDED) {
        bool upload_staged = false;
        if (s_upload_enabled) {
            upload_record_t rec = {0};
            rec.student_id       = (uint16_t)result->id;
            rec.confidence_milli = result->similarity_milli;
            snprintf(rec.event_id, sizeof(rec.event_id), "%s", entry.event_id);
            snprintf(rec.device_id, sizeof(rec.device_id), "%s",
                     s_device_config.device_id);
            snprintf(rec.name, sizeof(rec.name), "%s", name);
            snprintf(rec.class_name, sizeof(rec.class_name), "%s", class_name);
            snprintf(rec.timestamp, sizeof(rec.timestamp), "%s", entry.timestamp);
            esp_err_t upload_err = http_upload_stage(&rec);
            if (upload_err != ESP_OK) {
                ESP_LOGE(TAG, "Attendance rejected: cannot persist upload outbox: %s",
                         esp_err_to_name(upload_err));
                led_blink(4, 200);
                return;
            }
            upload_staged = true;
        }

        esp_err_t commit_err = attendance_commit(&entry, upload_staged);
        if (commit_err != ESP_OK) {
            ESP_LOGE(TAG, "Attendance rejected: local persistence failed: %s",
                     esp_err_to_name(commit_err));
            led_blink(4, 200);
            return;
        }
        if (upload_staged) http_upload_notify();
        /* The SD card maps this stable face/student ID to one complete spoken
         * sentence: “Xin chào <tên>, bạn đã điểm danh thành công”. */
        audio_feedback_notify_attendance((uint16_t)result->id);

        led_blink(2, 150);   /* 2 nháy nhanh = thành công */
        ESP_LOGI(TAG, "✅ ĐIỂM DANH: %s (%s) conf=%d‰",
                 name, class_name, result->similarity_milli);

    } else if (att == ATTENDANCE_DUPLICATE) {
        audio_feedback_notify(AUDIO_EVENT_ATTENDANCE_DUPLICATE);
        led_blink(1, 100);   /* 1 nháy = duplicate */
        ESP_LOGI(TAG, "⏭ Duplicate: %s (cooldown)", name);
    } else if (att == ATTENDANCE_TIME_UNAVAILABLE) {
        audio_feedback_notify(AUDIO_EVENT_TIME_UNAVAILABLE);
        led_blink(3, 200);   /* 3 nháy chậm = chờ NTP */
        ESP_LOGW(TAG, "⏳ Chờ đồng bộ giờ NTP...");
    }
}

/* ─── Main recognize loop ────────────────────────────────────── */
static void recognize_loop(void)
{
    ESP_LOGI(TAG, "=== Bắt đầu vòng lặp điểm danh ===");
    led_set(true);   /* LED sáng liên tục = đang hoạt động */

    int consecutive_camera_errors = 0;
    while (true) {
        if (ota_manager_busy()) {
            led_set(false);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        led_set(true);
        face_engine_result_t result = face_engine_recognize();

        if (result.state != FACE_ENGINE_ERROR) {
            consecutive_camera_errors = 0;
        }
        switch (result.state) {
        case FACE_ENGINE_MATCH:
            on_recognized(&result);
            break;

        case FACE_ENGINE_UNKNOWN:
            /* Khuôn mặt lạ — không làm gì, không log spam */
            break;

        case FACE_ENGINE_NO_FACE:
            /* Không có mặt — idle */
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;

        case FACE_ENGINE_ENROLLED:
            /* Không xảy ra trong recognize loop, chỉ trong enroll mode */
            break;

        case FACE_ENGINE_MULTIPLE_FACES:
            /* Recognize mode tự chọn khuôn mặt lớn nhất; trạng thái này chỉ dùng khi enroll. */
            break;

        case FACE_ENGINE_ERROR: {
            consecutive_camera_errors++;
            ESP_LOGW(TAG, "Camera frame pipeline failed (%d/%d)",
                     consecutive_camera_errors, CAMERA_RECOVERY_THRESHOLD);
            if (consecutive_camera_errors >= CAMERA_RECOVERY_THRESHOLD) {
                ESP_LOGW(TAG, "Reinitializing camera after repeated frame failures");
                const esp_err_t deinit_error = esp_camera_deinit();
                if (deinit_error != ESP_OK) {
                    ESP_LOGW(TAG, "Camera deinit returned: %s",
                             esp_err_to_name(deinit_error));
                }
                vTaskDelay(pdMS_TO_TICKS(CAMERA_RECOVERY_DELAY_MS));
                const esp_err_t init_error = init_camera();
                if (init_error == ESP_OK) {
                    ESP_LOGI(TAG, "Camera recovery succeeded");
                } else {
                    ESP_LOGE(TAG, "Camera recovery failed: %s",
                             esp_err_to_name(init_error));
                    audio_feedback_notify(AUDIO_EVENT_SYSTEM_ERROR);
                }
                consecutive_camera_errors = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }
        }

        vTaskDelay(pdMS_TO_TICKS(100));  /* ~10 fps max */
    }
}

/* ─── Entry point ────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Robot Điểm Danh %s ===", CDP_FIRMWARE_VERSION);
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %s (%d)", reset_reason_name(reset_reason),
             (int)reset_reason);
    manual_clock_init();
    led_init();
    led_blink(3, 100);   /* 3 nháy nhanh khi boot */

    /* NVS (cần cho WiFi và student_db) */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS requires recovery (%s); refusing automatic erase to protect identities",
                 esp_err_to_name(nvs_ret));
        fatal_error_loop("NVS recovery required; use an explicit factory reset");
    }
    if (nvs_ret != ESP_OK) {
        fatal_error_loop("NVS initialization failed");
    }
    const esp_err_t config_load_error =
        device_config_load(&s_device_config, &s_device_config_status);
    bool config_ready = config_load_error == ESP_OK;
    if (!config_ready) {
        ESP_LOGW(TAG, "No valid runtime network/cloud configuration; maintenance portal required");
    }
    const bool maintenance_requested =
        maintenance_mode_enrollment_requested();

    /* Khởi tạo network interface và event loop — chỉ gọi 1 lần, dùng chung cho cả AP và STA */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* SPIFFS — face database và attendance CSV */
    ESP_ERROR_CHECK(init_spiffs());

    /* Student DB */
    ESP_ERROR_CHECK(student_db_init());

    if (!esp_psram_is_initialized() ||
        heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        fatal_error_loop("PSRAM health check failed");
    }

    /* Camera */
    if (init_camera() != ESP_OK) {
        fatal_error_loop("camera initialization failed");
    }

    /* Face engine (model AI) */
    if (face_engine_init(FACE_DB_PATH) != 0) {
        fatal_error_loop("face engine initialization failed");
    }
    int student_count = student_db_count();
    int enrolled_faces = face_engine_get_enrolled_count();
    const bool identity_db_consistent = enrolled_faces == student_count;
    ESP_LOGI(TAG, "Đã đăng ký %d học sinh | %d khuôn mặt",
             student_count, enrolled_faces);
    if (!identity_db_consistent) {
        ESP_LOGE(TAG, "Identity database mismatch: students=%d faces=%d",
                 student_count, enrolled_faces);
    }

    /* A dedicated non-release image lets the hardware test prove rollback
     * without corrupting a real build. It intentionally fails before VALID. */
#ifdef CDP_OTA_ROLLBACK_TEST
    fatal_error_loop("intentional P2.5 rollback test image");
#endif

    /* NVS, SPIFFS, PSRAM, camera and face engine are all usable. Only now may
     * a first-boot OTA image cancel automatic rollback. */
    ota_manager_confirm_running_image();

    /* Audio is deliberately outside the OTA health gate: a missing DFPlayer,
     * speaker or SD card must never roll back otherwise healthy firmware. */
    const esp_err_t audio_error = audio_feedback_init();
    if (audio_error != ESP_OK) {
        ESP_LOGW(TAG, "DFPlayer audio disabled for this boot: %s",
                 esp_err_to_name(audio_error));
    }

    /* Một buổi đăng ký chỉ kết thúc khi giáo viên bấm nút hoàn tất. Nếu mất
     * điện giữa buổi, robot mở lại AP và tiếp tục từ danh sách đã lưu. */
    bool enrollment_complete = student_db_is_enrollment_complete();
    if (!config_ready || enrolled_faces == 0 || !enrollment_complete ||
        maintenance_requested || !identity_db_consistent) {
        char setup_ap_password[20] = {0};
        if (config_ready) {
            snprintf(setup_ap_password, sizeof(setup_ap_password), "Cfg-%.12s",
                     s_device_config.device_secret);
        } else {
            snprintf(setup_ap_password, sizeof(setup_ap_password), "%s",
                     DEVICE_SETUP_AP_PASSWORD);
        }
        ESP_LOGI(TAG, "Maintenance session active: faces=%d enrollment_complete=%s",
                 enrolled_faces, enrollment_complete ? "yes" : "no");
        ESP_LOGI(TAG, "Maintenance portal active at http://192.168.4.1 on SSID '%s'",
                 DEVICE_SETUP_AP_SSID);
        esp_err_t enroll_err = enroll_server_start(DEVICE_SETUP_AP_SSID,
                                                    setup_ap_password);
        memset(setup_ap_password, 0, sizeof(setup_ap_password));
        if (enroll_err == ESP_OK) {
            while (!enroll_server_done()) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            const bool restart_requested = enroll_server_restart_requested();
            const esp_err_t clear_error =
                maintenance_mode_clear_enrollment_request();
            if (clear_error != ESP_OK) {
                ESP_LOGE(TAG, "Không thể xóa cờ maintenance: %s",
                         esp_err_to_name(clear_error));
            }
            enroll_err = enroll_server_stop();
            if (enroll_err != ESP_OK) {
                ESP_LOGE(TAG, "Dừng maintenance portal lỗi: %s", esp_err_to_name(enroll_err));
            }
            if (restart_requested) {
                ESP_LOGI(TAG, "Restarting to activate the selected runtime configuration");
                vTaskDelay(pdMS_TO_TICKS(300));
                esp_restart();
            }
            ESP_LOGI(TAG, "Đã đăng ký %d khuôn mặt. Chuyển sang chế độ điểm danh.",
                     face_engine_get_enrolled_count());
        } else {
            ESP_LOGE(TAG, "Không thể mở maintenance portal: %s", esp_err_to_name(enroll_err));
            fatal_error_loop("maintenance server failed to start");
        }
    }

    if (device_config_load(&s_device_config, &s_device_config_status) != ESP_OK) {
        fatal_error_loop("runtime configuration is unavailable");
    }

    /* WiFi Station + NTP (chạy nền) */
    esp_err_t attendance_err = attendance_init();
    if (attendance_err != ESP_OK) {
        ESP_LOGE(TAG, "Attendance storage unavailable: %s", esp_err_to_name(attendance_err));
    }
    esp_err_t wifi_err = wifi_time_start(s_device_config.wifi_ssid,
                                         s_device_config.wifi_password);

    /* Chờ WiFi kết nối và NTP đồng bộ. Uploader vẫn tự chờ ở nền nếu mạng
     * chậm hơn khoảng thời gian khởi động này. */
    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "Đang kết nối WiFi và chờ NTP...");
        TickType_t t0 = xTaskGetTickCount();
        while (wifi_time_get_state() < WIFI_TIME_SYNCED) {
            if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(NETWORK_TIME_READY_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "WiFi/NTP chưa sẵn sàng sau %d giây; tiếp tục chờ ở nền",
                         NETWORK_TIME_READY_TIMEOUT_MS / 1000);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } else {
        ESP_LOGE(TAG, "WiFi không khởi động được: %s", esp_err_to_name(wifi_err));
    }

    /* HTTP Upload đến Google Apps Script */
    if (device_config_values_valid(&s_device_config)) {
        esp_err_t upload_err = http_upload_init(s_device_config.gas_url,
                                                s_device_config.device_secret,
                                                s_device_config.device_id);
        s_upload_enabled = upload_err == ESP_OK;
        if (!s_upload_enabled) {
            ESP_LOGE(TAG, "Không thể khởi tạo uploader: %s", esp_err_to_name(upload_err));
        } else {
            const esp_err_t ota_error = ota_manager_start();
            if (ota_error != ESP_OK) {
                ESP_LOGE(TAG, "Không thể khởi tạo OTA manager: %s",
                         esp_err_to_name(ota_error));
            }
        }
    } else {
        ESP_LOGW(TAG, "Cloud URL/token/device ID chưa hợp lệ — upload bị tắt");
    }

    /* GPIO0 is monitored only after normal boot. Holding it here writes a
     * latched NVS flag and restarts into enrollment, avoiding ROM download
     * mode caused by holding BOOT during RESET. */
    esp_err_t maintenance_err = maintenance_mode_start();
    if (maintenance_err != ESP_OK) {
        ESP_LOGE(TAG, "Không thể bật nút maintenance: %s",
                 esp_err_to_name(maintenance_err));
    }

    /* Vòng lặp điểm danh vô tận */
    audio_feedback_notify(AUDIO_EVENT_SYSTEM_READY);
    recognize_loop();
}
