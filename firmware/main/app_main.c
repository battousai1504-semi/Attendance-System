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
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "attendance.h"
#include "board_pins.h"
#include "cloud_config.h"
#include "enroll_server.h"
#include "face_engine.h"
#include "http_upload.h"
#include "manual_clock.h"
#include "student_db.h"
#include "wifi_time.h"

/* ─── Cấu hình ───────────────────────────────────────────────── */

/* WiFi để kết nối Internet/NTP và upload lên GAS.
 * Tạo file wifi_time_config.h từ wifi_time_config.h.example trước khi build. */
#include "wifi_time_config.h"

/* Đường dẫn SPIFFS để lưu face database */
#define FACE_DB_PATH    "/spiffs"

/* Thời gian chờ WiFi kết nối và NTP đặt đồng hồ hệ thống (ms). */
#define NETWORK_TIME_READY_TIMEOUT_MS  30000
#define FIRMWARE_VERSION                "1.2.1-phase1-rc1"

static const char *TAG = "app_main";
static bool s_upload_enabled;

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

static bool enrollment_button_held(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ENROLL_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK ||
        gpio_get_level(ENROLL_BUTTON_GPIO) != ENROLL_BUTTON_ACTIVE_LEVEL) {
        return false;
    }

    ESP_LOGI(TAG, "BOOT button detected; keep holding to enter enrollment");
    for (int elapsed = 0; elapsed < ENROLL_BUTTON_HOLD_MS; elapsed += 50) {
        if (gpio_get_level(ENROLL_BUTTON_GPIO) != ENROLL_BUTTON_ACTIVE_LEVEL) {
            ESP_LOGI(TAG, "BOOT button released before enrollment threshold");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGW(TAG, "Maintenance enrollment requested with BOOT button");
    return true;
}

static void fatal_error_loop(const char *reason)
{
    ESP_LOGE(TAG, "FATAL: %s; device stopped in safe mode", reason);
    while (true) {
        led_blink(5, 150);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static bool cloud_config_valid(void)
{
    if (strncmp(CDP_GAS_URL, "https://script.google.com/macros/s/", 35) != 0 ||
        strstr(CDP_GAS_URL, "YOUR_DEPLOYMENT_ID") != NULL ||
        strstr(CDP_GAS_URL, "/exec") == NULL || strlen(CDP_GAS_TOKEN) < 32 ||
        strstr(CDP_GAS_TOKEN, "replace-with") != NULL ||
        CDP_DEVICE_ID[0] == '\0') {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)CDP_DEVICE_ID;
         *p != '\0'; p++) {
        const bool allowed = (*p >= 'a' && *p <= 'z') ||
                             (*p >= 'A' && *p <= 'Z') ||
                             (*p >= '0' && *p <= '9') || *p == '.' ||
                             *p == '_' || *p == ':' || *p == '-';
        if (!allowed) return false;
    }
    return true;
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
            snprintf(rec.device_id, sizeof(rec.device_id), "%s", CDP_DEVICE_ID);
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

        led_blink(2, 150);   /* 2 nháy nhanh = thành công */
        ESP_LOGI(TAG, "✅ ĐIỂM DANH: %s (%s) conf=%d‰",
                 name, class_name, result->similarity_milli);

    } else if (att == ATTENDANCE_DUPLICATE) {
        led_blink(1, 100);   /* 1 nháy = duplicate */
        ESP_LOGI(TAG, "⏭ Duplicate: %s (cooldown)", name);
    } else if (att == ATTENDANCE_TIME_UNAVAILABLE) {
        led_blink(3, 200);   /* 3 nháy chậm = chờ NTP */
        ESP_LOGW(TAG, "⏳ Chờ đồng bộ giờ NTP...");
    }
}

/* ─── Main recognize loop ────────────────────────────────────── */
static void recognize_loop(void)
{
    ESP_LOGI(TAG, "=== Bắt đầu vòng lặp điểm danh ===");
    led_set(true);   /* LED sáng liên tục = đang hoạt động */

    while (true) {
        face_engine_result_t result = face_engine_recognize();

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

        case FACE_ENGINE_ERROR:
            ESP_LOGE(TAG, "Camera/face engine error");
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));  /* ~10 fps max */
    }
}

/* ─── Entry point ────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Robot Điểm Danh %s ===", FIRMWARE_VERSION);
    manual_clock_init();
    led_init();
    const bool maintenance_enrollment = enrollment_button_held();
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

    /* Khởi tạo network interface và event loop — chỉ gọi 1 lần, dùng chung cho cả AP và STA */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* SPIFFS — face database và attendance CSV */
    ESP_ERROR_CHECK(init_spiffs());

    /* Student DB */
    ESP_ERROR_CHECK(student_db_init());

    /* Camera */
    ESP_ERROR_CHECK(init_camera());

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

    /* Một buổi đăng ký chỉ kết thúc khi giáo viên bấm nút hoàn tất. Nếu mất
     * điện giữa buổi, robot mở lại AP và tiếp tục từ danh sách đã lưu. */
    bool enrollment_complete = student_db_is_enrollment_complete();
    if (enrolled_faces == 0 || !enrollment_complete || maintenance_enrollment ||
        !identity_db_consistent) {
        if (strstr(CDP_ENROLL_AP_PASSWORD, "replace-with") != NULL) {
            fatal_error_loop("change the default enrollment AP password");
        }
        ESP_LOGI(TAG, "Enrollment session active: faces=%d complete=%s",
                 enrolled_faces, enrollment_complete ? "yes" : "no");
        ESP_LOGI(TAG, "Kết nối WiFi enrollment '%s' rồi mở http://192.168.4.1",
                 CDP_ENROLL_AP_SSID);
        esp_err_t enroll_err = enroll_server_start(CDP_ENROLL_AP_SSID,
                                                    CDP_ENROLL_AP_PASSWORD);
        if (enroll_err == ESP_OK) {
            while (!enroll_server_done()) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            enroll_err = enroll_server_stop();
            if (enroll_err != ESP_OK) {
                ESP_LOGE(TAG, "Dừng enrollment mode lỗi: %s", esp_err_to_name(enroll_err));
            }
            ESP_LOGI(TAG, "Đã đăng ký %d khuôn mặt. Chuyển sang chế độ điểm danh.",
                     face_engine_get_enrolled_count());
        } else {
            ESP_LOGE(TAG, "Không thể mở enrollment mode: %s", esp_err_to_name(enroll_err));
            fatal_error_loop("enrollment server failed to start");
        }
    }

    /* WiFi Station + NTP (chạy nền) */
    esp_err_t attendance_err = attendance_init();
    if (attendance_err != ESP_OK) {
        ESP_LOGE(TAG, "Attendance storage unavailable: %s", esp_err_to_name(attendance_err));
    }
    esp_err_t wifi_err = wifi_time_start();

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
    if (cloud_config_valid()) {
        esp_err_t upload_err = http_upload_init(CDP_GAS_URL, CDP_GAS_TOKEN);
        s_upload_enabled = upload_err == ESP_OK;
        if (!s_upload_enabled) {
            ESP_LOGE(TAG, "Không thể khởi tạo uploader: %s", esp_err_to_name(upload_err));
        }
    } else {
        ESP_LOGW(TAG, "Cloud URL/token/device ID chưa hợp lệ — upload bị tắt");
    }

    /* Vòng lặp điểm danh vô tận */
    recognize_loop();
}
