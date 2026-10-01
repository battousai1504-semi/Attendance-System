#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

#include "audio_feedback.h"
#include "board_pins.h"
#include "dfplayer_protocol.h"

#define AUDIO_CONFIG_MAGIC          0x41554433UL
#define AUDIO_CONFIG_SCHEMA_VERSION 1U
#define AUDIO_CONFIG_NAMESPACE      "audio_cfg"
#define AUDIO_CONFIG_KEY            "settings"

#define AUDIO_QUEUE_LENGTH       8U
#define AUDIO_TASK_STACK         4096U
#define AUDIO_TASK_PRIORITY      3U
#define AUDIO_UART_RX_BUFFER     256
#define AUDIO_UART_TIMEOUT_MS    120
#define AUDIO_MODULE_BOOT_MS     1500
#define AUDIO_MODULE_RESET_MS    1000
#define AUDIO_TRACK_GUARD_MS     1600

#define DFPLAYER_CMD_SET_VOLUME  0x06U
#define DFPLAYER_CMD_SELECT_SD   0x09U
#define DFPLAYER_CMD_RESET       0x0cU
#define DFPLAYER_CMD_PAUSE       0x0eU
#define DFPLAYER_CMD_PLAY_MP3    0x12U
#define DFPLAYER_DEVICE_SD       0x0002U

typedef struct {
    uint32_t magic;
    uint16_t schema_version;
    uint8_t enabled;
    uint8_t volume;
    uint32_t checksum;
} audio_config_record_t;

typedef enum {
    AUDIO_JOB_PLAY,
    AUDIO_JOB_APPLY_CONFIG,
} audio_job_type_t;

typedef struct {
    audio_job_type_t type;
    uint16_t track;
} audio_job_t;

static const char *TAG = "audio_feedback";
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static audio_feedback_status_t s_status = {
    .enabled = true,
    .volume = AUDIO_FEEDBACK_DEFAULT_VOLUME,
};
static TickType_t s_last_event[AUDIO_EVENT_COUNT];
static uint8_t s_rx_stream[64];
static size_t s_rx_stream_size;

static uint32_t crc32_bytes(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xffffffffU;
    for (size_t index = 0; index < length; index++) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; bit++) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

static void default_config(audio_config_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->magic = AUDIO_CONFIG_MAGIC;
    record->schema_version = AUDIO_CONFIG_SCHEMA_VERSION;
    record->enabled = 1U;
    record->volume = AUDIO_FEEDBACK_DEFAULT_VOLUME;
    record->checksum =
        crc32_bytes(record, offsetof(audio_config_record_t, checksum));
}

static bool config_record_valid(const audio_config_record_t *record)
{
    return record != NULL &&
           record->magic == AUDIO_CONFIG_MAGIC &&
           record->schema_version == AUDIO_CONFIG_SCHEMA_VERSION &&
           record->enabled <= 1U &&
           record->volume <= AUDIO_FEEDBACK_MAX_VOLUME &&
           record->checksum ==
               crc32_bytes(record, offsetof(audio_config_record_t, checksum));
}

static void load_config(audio_config_record_t *record)
{
    default_config(record);
    nvs_handle_t handle;
    const esp_err_t open_error =
        nvs_open(AUDIO_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (open_error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No audio settings found; using enabled=true volume=%u",
                 AUDIO_FEEDBACK_DEFAULT_VOLUME);
        return;
    }
    if (open_error != ESP_OK) {
        ESP_LOGW(TAG, "Cannot open audio settings: %s; using defaults",
                 esp_err_to_name(open_error));
        return;
    }
    audio_config_record_t stored = {0};
    size_t size = sizeof(stored);
    const esp_err_t read_error =
        nvs_get_blob(handle, AUDIO_CONFIG_KEY, &stored, &size);
    nvs_close(handle);
    if (read_error == ESP_OK && size == sizeof(stored) &&
        config_record_valid(&stored)) {
        *record = stored;
        return;
    }
    if (read_error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Ignoring invalid audio settings; using defaults");
    }
}

static esp_err_t save_config(bool enabled, uint8_t volume)
{
    audio_config_record_t record = {0};
    record.magic = AUDIO_CONFIG_MAGIC;
    record.schema_version = AUDIO_CONFIG_SCHEMA_VERSION;
    record.enabled = enabled ? 1U : 0U;
    record.volume = volume;
    record.checksum =
        crc32_bytes(&record, offsetof(audio_config_record_t, checksum));

    nvs_handle_t handle;
    esp_err_t error = nvs_open(AUDIO_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_blob(handle, AUDIO_CONFIG_KEY, &record, sizeof(record));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

static esp_err_t send_command(uint8_t command, uint16_t parameter,
                              bool request_feedback)
{
    uint8_t frame[DFPLAYER_FRAME_SIZE];
    dfplayer_build_frame(command, parameter, request_feedback, frame);
    const int written = uart_write_bytes((uart_port_t)DFPLAYER_UART_NUM,
                                         frame, sizeof(frame));
    if (written != (int)sizeof(frame)) return ESP_FAIL;
    return uart_wait_tx_done((uart_port_t)DFPLAYER_UART_NUM,
                             pdMS_TO_TICKS(AUDIO_UART_TIMEOUT_MS));
}

static void note_valid_response(const dfplayer_frame_t *frame)
{
    bool first_response = false;
    taskENTER_CRITICAL(&s_lock);
    if (!s_status.module_responding) first_response = true;
    s_status.module_responding = true;
    taskEXIT_CRITICAL(&s_lock);
    if (first_response) {
        ESP_LOGI(TAG, "DFPlayer responded on UART%d", DFPLAYER_UART_NUM);
    }
    ESP_LOGD(TAG, "DFPlayer response command=0x%02x parameter=0x%04x",
             frame->command, frame->parameter);
}

static void consume_rx_stream(void)
{
    while (s_rx_stream_size > 0) {
        size_t start = 0;
        while (start < s_rx_stream_size && s_rx_stream[start] != 0x7eU) start++;
        if (start > 0) {
            memmove(s_rx_stream, s_rx_stream + start, s_rx_stream_size - start);
            s_rx_stream_size -= start;
        }
        if (s_rx_stream_size < DFPLAYER_FRAME_SIZE) return;

        dfplayer_frame_t frame;
        if (dfplayer_parse_frame(s_rx_stream, DFPLAYER_FRAME_SIZE, &frame)) {
            note_valid_response(&frame);
            memmove(s_rx_stream, s_rx_stream + DFPLAYER_FRAME_SIZE,
                    s_rx_stream_size - DFPLAYER_FRAME_SIZE);
            s_rx_stream_size -= DFPLAYER_FRAME_SIZE;
        } else {
            memmove(s_rx_stream, s_rx_stream + 1, s_rx_stream_size - 1);
            s_rx_stream_size--;
        }
    }
}

static void receive_responses(TickType_t timeout)
{
    uint8_t data[32];
    const int count = uart_read_bytes((uart_port_t)DFPLAYER_UART_NUM, data,
                                      sizeof(data), timeout);
    if (count <= 0) return;
    if ((size_t)count > sizeof(s_rx_stream) - s_rx_stream_size) {
        ESP_LOGW(TAG, "DFPlayer RX overflow; resetting parser");
        s_rx_stream_size = 0;
    }
    size_t copy_size = (size_t)count;
    if (copy_size > sizeof(s_rx_stream)) copy_size = sizeof(s_rx_stream);
    memcpy(s_rx_stream + s_rx_stream_size, data, copy_size);
    s_rx_stream_size += copy_size;
    consume_rx_stream();
}

static uint16_t track_for_event(audio_feedback_event_t event)
{
    static const uint16_t tracks[AUDIO_EVENT_COUNT] = {
        [AUDIO_EVENT_SYSTEM_READY] = 1,
        [AUDIO_EVENT_ATTENDANCE_SUCCESS] = 2,
        [AUDIO_EVENT_ATTENDANCE_DUPLICATE] = 3,
        [AUDIO_EVENT_TIME_UNAVAILABLE] = 4,
        [AUDIO_EVENT_ENROLL_SUCCESS] = 5,
        [AUDIO_EVENT_SYSTEM_ERROR] = 6,
    };
    return (unsigned)event < AUDIO_EVENT_COUNT ? tracks[event] : 0;
}

static TickType_t cooldown_for_event(audio_feedback_event_t event)
{
    switch (event) {
        case AUDIO_EVENT_ATTENDANCE_DUPLICATE:
            return pdMS_TO_TICKS(5000);
        case AUDIO_EVENT_TIME_UNAVAILABLE:
        case AUDIO_EVENT_SYSTEM_ERROR:
            return pdMS_TO_TICKS(10000);
        default:
            return pdMS_TO_TICKS(500);
    }
}

static void audio_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(AUDIO_MODULE_BOOT_MS));
    uart_flush_input((uart_port_t)DFPLAYER_UART_NUM);
    s_rx_stream_size = 0;
    if (send_command(DFPLAYER_CMD_RESET, 0, false) != ESP_OK) {
        ESP_LOGW(TAG, "DFPlayer reset command failed");
    }
    vTaskDelay(pdMS_TO_TICKS(AUDIO_MODULE_RESET_MS));
    send_command(DFPLAYER_CMD_SELECT_SD, DFPLAYER_DEVICE_SD, true);
    receive_responses(pdMS_TO_TICKS(300));

    audio_feedback_status_t status;
    audio_feedback_get_status(&status);
    if (status.enabled) {
        send_command(DFPLAYER_CMD_SET_VOLUME, status.volume, true);
        receive_responses(pdMS_TO_TICKS(300));
    } else {
        send_command(DFPLAYER_CMD_PAUSE, 0, false);
    }

    taskENTER_CRITICAL(&s_lock);
    s_status.initialized = true;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "P3 audio ready: UART%d TX=GPIO%d RX=GPIO%d enabled=%s volume=%u",
             DFPLAYER_UART_NUM, DFPLAYER_TX_GPIO, DFPLAYER_RX_GPIO,
             status.enabled ? "yes" : "no", status.volume);

    while (true) {
        audio_job_t job;
        if (xQueueReceive(s_queue, &job, pdMS_TO_TICKS(200)) == pdTRUE) {
            audio_feedback_get_status(&status);
            if (job.type == AUDIO_JOB_APPLY_CONFIG) {
                if (status.enabled) {
                    send_command(DFPLAYER_CMD_SET_VOLUME, status.volume, true);
                } else {
                    send_command(DFPLAYER_CMD_PAUSE, 0, true);
                }
            } else if (status.enabled && job.track >= 1U &&
                       job.track <= AUDIO_FEEDBACK_TRACK_COUNT) {
                const esp_err_t error =
                    send_command(DFPLAYER_CMD_PLAY_MP3, job.track, true);
                if (error != ESP_OK) {
                    ESP_LOGW(TAG, "Cannot queue audio track %u: %s",
                             job.track, esp_err_to_name(error));
                } else {
                    ESP_LOGI(TAG, "Playing /mp3/%04u.mp3", job.track);
                    /* Only this low-priority audio task waits. The guard keeps
                     * a following command from cutting off a short prompt. */
                    vTaskDelay(pdMS_TO_TICKS(AUDIO_TRACK_GUARD_MS));
                }
            }
        }
        receive_responses(0);
    }
}

esp_err_t audio_feedback_init(void)
{
    if (s_queue != NULL || s_task != NULL) return ESP_ERR_INVALID_STATE;

    audio_config_record_t config;
    load_config(&config);
    taskENTER_CRITICAL(&s_lock);
    s_status.enabled = config.enabled != 0;
    s_status.volume = config.volume;
    s_status.initialized = false;
    s_status.module_responding = false;
    s_status.dropped_events = 0;
    memset(s_last_event, 0, sizeof(s_last_event));
    taskEXIT_CRITICAL(&s_lock);

    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t error =
        uart_driver_install((uart_port_t)DFPLAYER_UART_NUM,
                            AUDIO_UART_RX_BUFFER, 0, 0, NULL, 0);
    if (error != ESP_OK) return error;
    error = uart_param_config((uart_port_t)DFPLAYER_UART_NUM, &uart_config);
    if (error == ESP_OK) {
        error = uart_set_pin((uart_port_t)DFPLAYER_UART_NUM,
                             DFPLAYER_TX_GPIO, DFPLAYER_RX_GPIO,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (error != ESP_OK) {
        uart_driver_delete((uart_port_t)DFPLAYER_UART_NUM);
        return error;
    }

    s_queue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(audio_job_t));
    if (s_queue == NULL) {
        uart_driver_delete((uart_port_t)DFPLAYER_UART_NUM);
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "dfplayer", AUDIO_TASK_STACK, NULL,
                    AUDIO_TASK_PRIORITY, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        uart_driver_delete((uart_port_t)DFPLAYER_UART_NUM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool audio_feedback_notify(audio_feedback_event_t event)
{
    if ((unsigned)event >= AUDIO_EVENT_COUNT || s_queue == NULL) return false;
    const TickType_t now = xTaskGetTickCount();
    bool enabled;
    bool throttled;
    taskENTER_CRITICAL(&s_lock);
    enabled = s_status.enabled;
    throttled = s_last_event[event] != 0 &&
                now - s_last_event[event] < cooldown_for_event(event);
    if (enabled && !throttled) s_last_event[event] = now;
    taskEXIT_CRITICAL(&s_lock);
    if (!enabled || throttled) return false;

    const audio_job_t job = {
        .type = AUDIO_JOB_PLAY,
        .track = track_for_event(event),
    };
    if (xQueueSend(s_queue, &job, 0) == pdTRUE) return true;
    taskENTER_CRITICAL(&s_lock);
    s_status.dropped_events++;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "Audio queue full; event %d dropped", (int)event);
    return false;
}

esp_err_t audio_feedback_set_config(bool enabled, uint8_t volume)
{
    if (volume > AUDIO_FEEDBACK_MAX_VOLUME) return ESP_ERR_INVALID_ARG;
    const esp_err_t error = save_config(enabled, volume);
    if (error != ESP_OK) return error;
    taskENTER_CRITICAL(&s_lock);
    s_status.enabled = enabled;
    s_status.volume = volume;
    taskEXIT_CRITICAL(&s_lock);
    /* Persisted settings remain useful even when the optional UART failed to
     * initialize during this boot; they will be applied on the next boot. */
    if (s_queue == NULL) return ESP_OK;
    const audio_job_t job = {.type = AUDIO_JOB_APPLY_CONFIG};
    return xQueueSendToFront(s_queue, &job, pdMS_TO_TICKS(100)) == pdTRUE
               ? ESP_OK : ESP_ERR_TIMEOUT;
}

void audio_feedback_get_status(audio_feedback_status_t *status)
{
    if (status == NULL) return;
    taskENTER_CRITICAL(&s_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_lock);
}

esp_err_t audio_feedback_test_track(uint16_t track)
{
    if (track < 1U || track > AUDIO_FEEDBACK_TRACK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_feedback_status_t status;
    audio_feedback_get_status(&status);
    if (!status.initialized || !status.enabled || s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const audio_job_t job = {.type = AUDIO_JOB_PLAY, .track = track};
    return xQueueSend(s_queue, &job, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
