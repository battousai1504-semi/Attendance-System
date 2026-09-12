#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_random.h"

#include "attendance.h"
#include "manual_clock.h"

#define ATTENDANCE_FILE "/spiffs/attendance.csv"
#define ATTENDANCE_COOLDOWN_SECONDS 60
#define ATTENDANCE_TRACKED_IDS 256

static const char *TAG = "attendance";
static time_t s_last_record_time[ATTENDANCE_TRACKED_IDS];

static bool parse_timestamp(const char *text, time_t *epoch_out)
{
    int year, month, day, hour, minute, second;
    if (sscanf(text, "%d-%d-%d %d:%d:%d",
               &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }
    struct tm value = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second,
        .tm_isdst = -1,
    };
    const time_t epoch = mktime(&value);
    if (epoch == (time_t)-1) {
        return false;
    }
    *epoch_out = epoch;
    return true;
}

static bool parse_record_line(const char *line, attendance_entry_t *entry)
{
    unsigned int id = 0;
    char first[ATTENDANCE_EVENT_ID_MAX] = {0};
    char second[ATTENDANCE_TIMESTAMP_MAX] = {0};
    char method[ATTENDANCE_METHOD_MAX] = {0};

    /* Current format: event_id,timestamp,person_id,method. */
    if (sscanf(line, "%47[^,],%23[^,],%u,%15[^\r\n]",
               first, second, &id, method) == 4) {
        snprintf(entry->event_id, sizeof(entry->event_id), "%s", first);
        snprintf(entry->timestamp, sizeof(entry->timestamp), "%s", second);
    /* Legacy format: timestamp,person_id,method. */
    } else if (sscanf(line, "%23[^,],%u,%15[^\r\n]", second, &id, method) == 3) {
        entry->event_id[0] = '\0';
        snprintf(entry->timestamp, sizeof(entry->timestamp), "%s", second);
    } else {
        return false;
    }

    if (id >= ATTENDANCE_TRACKED_IDS) {
        return false;
    }
    entry->person_id = (uint16_t)id;
    snprintf(entry->method, sizeof(entry->method), "%s", method);
    return parse_timestamp(entry->timestamp, &entry->epoch);
}

esp_err_t attendance_init(void)
{
    memset(s_last_record_time, 0, sizeof(s_last_record_time));
    FILE *file = fopen(ATTENDANCE_FILE, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "Cannot create %s", ATTENDANCE_FILE);
        return ESP_FAIL;
    }
    fclose(file);

    file = fopen(ATTENDANCE_FILE, "r");
    if (file != NULL) {
        char line[160];
        attendance_entry_t entry;
        while (fgets(line, sizeof(line), file) != NULL) {
            memset(&entry, 0, sizeof(entry));
            if (parse_record_line(line, &entry) &&
                entry.epoch > s_last_record_time[entry.person_id]) {
                s_last_record_time[entry.person_id] = entry.epoch;
            }
        }
        fclose(file);
    }
    ESP_LOGI(TAG, "Attendance storage ready: %s", ATTENDANCE_FILE);
    return ESP_OK;
}

attendance_result_t attendance_record(uint16_t person_id, const char *method,
                                      attendance_entry_t *entry_out)
{
    if (person_id == 0 || person_id >= ATTENDANCE_TRACKED_IDS ||
        method == NULL || method[0] == '\0' || entry_out == NULL) {
        return ATTENDANCE_ERROR;
    }
    if (!manual_clock_is_valid()) {
        ESP_LOGW(TAG, "Attendance ignored: waiting for Wi-Fi/NTP clock");
        return ATTENDANCE_TIME_UNAVAILABLE;
    }
    const time_t now = manual_clock_now();
    const double elapsed = difftime(now, s_last_record_time[person_id]);
    if (s_last_record_time[person_id] != 0 && elapsed >= 0 &&
        elapsed < ATTENDANCE_COOLDOWN_SECONDS) {
        ESP_LOGI(TAG, "Duplicate ignored: ID=%u (%s)", person_id, method);
        return ATTENDANCE_DUPLICATE;
    }

    attendance_entry_t entry = {0};
    entry.person_id = person_id;
    entry.epoch = now;
    snprintf(entry.method, sizeof(entry.method), "%s", method);
    manual_clock_format(entry.timestamp, sizeof(entry.timestamp));
    snprintf(entry.event_id, sizeof(entry.event_id), "%lld-%u-%08lx",
             (long long)now, (unsigned)person_id, (unsigned long)esp_random());

    FILE *file = fopen(ATTENDANCE_FILE, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "Cannot append attendance record");
        return ATTENDANCE_ERROR;
    }
    const int written = fprintf(file, "%s,%s,%u,%s\n",
                                entry.event_id, entry.timestamp, person_id, entry.method);
    fclose(file);
    if (written < 0) {
        ESP_LOGE(TAG, "Attendance write failed");
        return ATTENDANCE_ERROR;
    }

    s_last_record_time[person_id] = now;
    *entry_out = entry;
    ESP_LOGI(TAG, "ATTENDANCE SAVED: %s | ID=%u | %s | event=%s",
             entry.timestamp, person_id, entry.method, entry.event_id);
    return ATTENDANCE_RECORDED;
}

esp_err_t attendance_clear(void)
{
    FILE *file = fopen(ATTENDANCE_FILE, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Cannot clear attendance history");
        return ESP_FAIL;
    }
    fclose(file);
    memset(s_last_record_time, 0, sizeof(s_last_record_time));
    ESP_LOGI(TAG, "Attendance history cleared");
    return ESP_OK;
}

size_t attendance_get_count(void)
{
    FILE *file = fopen(ATTENDANCE_FILE, "r");
    if (file == NULL) {
        return 0;
    }

    char line[160] = {0};
    size_t count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] != '\0' && line[0] != '\n') {
            count++;
        }
    }
    fclose(file);
    return count;
}

bool attendance_format_at(size_t index, char *summary, size_t summary_size,
                          char *timestamp, size_t timestamp_size)
{
    if (summary == NULL || summary_size == 0 || timestamp == NULL || timestamp_size == 0) {
        return false;
    }

    FILE *file = fopen(ATTENDANCE_FILE, "r");
    if (file == NULL) {
        return false;
    }

    char line[160] = {0};
    size_t current = 0;
    bool found = false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] == '\0' || line[0] == '\n') {
            continue;
        }
        if (current++ == index) {
            found = true;
            break;
        }
    }
    fclose(file);
    if (!found) {
        return false;
    }

    attendance_entry_t entry = {0};
    if (!parse_record_line(line, &entry)) {
        return false;
    }
    snprintf(summary, summary_size, "ID %u %s", entry.person_id, entry.method);
    snprintf(timestamp, timestamp_size, "%s", entry.timestamp);
    return true;
}

void attendance_print_recent(size_t max_records)
{
    if (max_records == 0) {
        return;
    }
    FILE *file = fopen(ATTENDANCE_FILE, "r");
    if (file == NULL) {
        ESP_LOGW(TAG, "No attendance history yet");
        return;
    }

    char records[10][160] = {{0}};
    if (max_records > 10) {
        max_records = 10;
    }
    size_t count = 0;
    char line[160] = {0};
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] == '\0') {
            continue;
        }
        if (count < max_records) {
            strncpy(records[count++], line, sizeof(records[0]) - 1);
        } else {
            memmove(records, records + 1, (max_records - 1) * sizeof(records[0]));
            strncpy(records[max_records - 1], line, sizeof(records[0]) - 1);
        }
    }
    fclose(file);

    ESP_LOGI(TAG, "----- ATTENDANCE HISTORY (%u latest) -----", (unsigned)count);
    for (size_t index = 0; index < count; index++) {
        ESP_LOGI(TAG, "%s", records[index]);
    }
    if (count == 0) {
        ESP_LOGI(TAG, "(empty)");
    }
}
