#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "enroll_server.h"
#include "face_engine.h"
#include "student_db.h"

#define TAG "enroll_server"
#define ENROLLMENT_CAPACITY STUDENT_MAX_COUNT

static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static volatile bool s_done_requested;
static bool s_wifi_initialized;
static bool s_wifi_started;

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool url_decode(const char *src, size_t src_len, char *dst, size_t dst_size)
{
    if (dst_size == 0) return false;
    size_t out = 0;
    for (size_t index = 0; index < src_len; index++) {
        unsigned char decoded;
        if (src[index] == '+') {
            decoded = ' ';
        } else if (src[index] == '%') {
            if (index + 2 >= src_len) return false;
            int high = hex_value(src[index + 1]);
            int low = hex_value(src[index + 2]);
            if (high < 0 || low < 0) return false;
            decoded = (unsigned char)((high << 4) | low);
            index += 2;
        } else {
            decoded = (unsigned char)src[index];
        }
        if (decoded == '\0' || out + 1 >= dst_size) return false;
        dst[out++] = (char)decoded;
    }
    dst[out] = '\0';
    return true;
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_size)
{
    const size_t key_len = strlen(key);
    const char *field = body;
    while (*field != '\0') {
        const char *end = strchr(field, '&');
        if (end == NULL) end = field + strlen(field);
        const char *equals = memchr(field, '=', (size_t)(end - field));
        if (equals != NULL && (size_t)(equals - field) == key_len &&
            strncmp(field, key, key_len) == 0) {
            return url_decode(equals + 1, (size_t)(end - equals - 1), out, out_size);
        }
        field = *end == '&' ? end + 1 : end;
    }
    return false;
}

static bool valid_student_field(const char *value, bool required)
{
    if (value == NULL || (required && value[0] == '\0')) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
        if (*p < 0x20 || *p == '|') return false;
    }
    return true;
}

static esp_err_t send_json_string(httpd_req_t *req, const char *value)
{
    esp_err_t err = httpd_resp_send_chunk(req, "\"", 1);
    if (err != ESP_OK) return err;
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
        const char *escaped = NULL;
        if (*p == '\"') escaped = "\\\"";
        else if (*p == '\\') escaped = "\\\\";
        if (escaped != NULL) {
            err = httpd_resp_send_chunk(req, escaped, HTTPD_RESP_USE_STRLEN);
        } else {
            char one_char[2] = {(char)*p, '\0'};
            err = httpd_resp_send_chunk(req, one_char, 1);
        }
        if (err != ESP_OK) return err;
    }
    return httpd_resp_send_chunk(req, "\"", 1);
}

/* ─── HTML trang chính ──────────────────────────────────────── */
static const char *INDEX_HTML =
    "<!DOCTYPE html><html lang='vi'><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Đăng ký khuôn mặt</title><style>"
    "body{margin:0;font-family:sans-serif;background:#0f172a;color:#e2e8f0;padding:16px}"
    "h1{color:#38bdf8;text-align:center}"
    ".card{background:#1e293b;border-radius:12px;padding:20px;max-width:440px;margin:16px auto}"
    "label{display:block;margin-bottom:4px;color:#94a3b8;font-size:.9em}"
    "input{width:100%;box-sizing:border-box;padding:10px;border:1px solid #334155;"
    "border-radius:8px;background:#0f172a;color:#e2e8f0;font-size:1em;margin-bottom:12px}"
    "button{width:100%;padding:12px;border:0;border-radius:8px;font-size:1em;"
    "cursor:pointer;margin-bottom:8px;color:#fff}button:disabled{opacity:.55}"
    ".enroll{background:#0ea5e9}.undo{background:#b45309}.done{background:#16a34a}"
    ".reset{background:#991b1b;margin-top:18px}"
    "#progress{padding:10px;background:#0f172a;border-radius:8px;margin-bottom:14px}"
    "#students{max-height:190px;overflow:auto;padding-left:28px;color:#cbd5e1}"
    "#students li{padding:3px 0}"
    "#status{text-align:center;padding:10px;border-radius:8px;margin-top:8px;display:none}"
    ".ok{background:#14532d;color:#86efac}"
    ".err{background:#450a0a;color:#fca5a5}"
    "</style></head><body>"
    "<h1>🎓 Đăng ký học sinh</h1><div class='card'>"
    "<div id='progress'>Đã đăng ký: <b id='count'>0</b> / <span id='capacity'>50</span></div>"
    "<div id='dbwarning' class='err' style='display:none;margin-bottom:12px;padding:10px'></div>"
    "<ol id='students'></ol>"
    "<label>Họ tên học sinh</label>"
    "<input id='name' type='text' placeholder='Nguyễn Văn A' autocomplete='off'>"
    "<label>Lớp</label>"
    "<input id='cls' type='text' placeholder='10A1' autocomplete='off'>"
    "<button id='enrollBtn' class='enroll' onclick='doEnroll()'>📸 Chụp & Đăng ký</button>"
    "<button class='undo' onclick='undoLast()'>↩ Hoàn tác em cuối</button>"
    "<button class='done' onclick='doneSession()'>✅ Kết thúc buổi đăng ký</button>"
    "<button class='reset' onclick='resetIdentities()'>🗑 Xóa toàn bộ danh tính</button>"
    "<div id='status'></div></div>"
    "<script>"
    "function showStatus(msg,ok){var el=document.getElementById('status');"
    "el.textContent=msg;el.className=ok?'ok':'err';el.style.display='block'}"
    "async function refreshStudents(){try{var j=await (await fetch('/students')).json();"
    "document.getElementById('count').textContent=j.count;"
    "document.getElementById('capacity').textContent=j.capacity;"
    "var warning=document.getElementById('dbwarning');"
    "if(!j.consistent){warning.textContent='Dữ liệu không đồng bộ: '+j.face_count+' khuôn mặt, '+j.count+' học sinh. Hãy hoàn tác hoặc xóa toàn bộ danh tính.';warning.style.display='block'}"
    "else warning.style.display='none';"
    "var list=document.getElementById('students');list.textContent='';"
    "j.students.forEach(function(s){var li=document.createElement('li');"
    "li.textContent='ID '+s.id+': '+s.name+(s.class?' — '+s.class:'');list.appendChild(li)})}catch(e){}}"
    "async function doEnroll(){"
    "  var nameInput=document.getElementById('name');var name=nameInput.value.trim();"
    "  var cls=document.getElementById('cls').value.trim();"
    "  var button=document.getElementById('enrollBtn');"
    "  if(!name){showStatus('Vui lòng nhập tên học sinh',false);nameInput.focus();return;}"
    "  button.disabled=true;showStatus('⏳ Đang chụp; chỉ để một khuôn mặt trước camera...',true);"
    "  try{"
    "    var r=await fetch('/enroll',{method:'POST',"
    "      headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "      body:'name='+encodeURIComponent(name)+'&class='+encodeURIComponent(cls)});"
    "    var j=await r.json();"
    "    if(j.ok){showStatus('✅ Đã đăng ký ID '+j.face_id+': '+name,true);"
    "      nameInput.value='';nameInput.focus();await refreshStudents();}"
    "    else showStatus('❌ Lỗi: '+j.error,false);"
    "  }catch(e){showStatus('❌ Lỗi kết nối với robot',false);}finally{button.disabled=false;}"
    "}"
    "async function undoLast(){if(!confirm('Xóa học sinh vừa đăng ký cuối cùng?'))return;"
    "  try{var j=await (await fetch('/undo',{method:'POST'})).json();"
    "  showStatus(j.ok?'Đã xóa ID '+j.removed_id:'❌ Lỗi: '+j.error,j.ok);"
    "  await refreshStudents();}catch(e){showStatus('❌ Lỗi kết nối với robot',false);}}"
    "async function doneSession(){if(!confirm('Đã đăng ký xong tất cả học sinh trong lớp?'))return;"
    "  try{var j=await (await fetch('/done',{method:'POST'})).json();"
    "  if(j.ok)showStatus('🚀 Robot đang chuyển sang chế độ điểm danh...',true);"
    "  else showStatus('❌ Lỗi: '+j.error,false);}catch(e){showStatus('❌ Lỗi kết nối với robot',false);}}"
    "async function resetIdentities(){var code=prompt('Thao tác này xóa toàn bộ khuôn mặt và danh sách học sinh. Nhập RESET để xác nhận:');"
    "  if(code!=='RESET')return;try{var j=await (await fetch('/reset-identities',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'confirm=RESET'})).json();"
    "  showStatus(j.ok?'Đã xóa toàn bộ danh tính':'❌ Lỗi: '+j.error,j.ok);await refreshStudents();}catch(e){showStatus('❌ Lỗi kết nối với robot',false);}}"
    "refreshStudents();document.getElementById('name').focus();"
    "</script></body></html>";

/* ─── GET / ──────────────────────────────────────────────────── */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handler_students(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    esp_err_t err = httpd_resp_send_chunk(req, "{\"ok\":true,\"students\":[", HTTPD_RESP_USE_STRLEN);
    bool first = true;
    int count = 0;
    for (uint16_t id = 1; err == ESP_OK && id <= ENROLLMENT_CAPACITY; id++) {
        student_info_t student;
        if (student_db_get(id, &student) != ESP_OK) continue;
        if (!first) err = httpd_resp_send_chunk(req, ",", 1);
        if (err != ESP_OK) break;
        char prefix[48];
        snprintf(prefix, sizeof(prefix), "{\"id\":%u,\"name\":", (unsigned)id);
        err = httpd_resp_send_chunk(req, prefix, HTTPD_RESP_USE_STRLEN);
        if (err == ESP_OK) err = send_json_string(req, student.name);
        if (err == ESP_OK) err = httpd_resp_send_chunk(req, ",\"class\":", HTTPD_RESP_USE_STRLEN);
        if (err == ESP_OK) err = send_json_string(req, student.class_name);
        if (err == ESP_OK) err = httpd_resp_send_chunk(req, "}", 1);
        first = false;
        count++;
    }
    if (err == ESP_OK) {
        char suffix[128];
        const int face_count = face_engine_get_enrolled_count();
        snprintf(suffix, sizeof(suffix),
                 "],\"count\":%d,\"face_count\":%d,\"consistent\":%s,\"capacity\":%d}",
                 count, face_count, count == face_count ? "true" : "false",
                 ENROLLMENT_CAPACITY);
        err = httpd_resp_send_chunk(req, suffix, HTTPD_RESP_USE_STRLEN);
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

/* ─── POST /enroll ───────────────────────────────────────────── */
static esp_err_t handler_enroll(httpd_req_t *req)
{
    char body[256] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }
    size_t total = 0;
    while (total < req->content_len) {
        int received = httpd_req_recv(req, body + total, req->content_len - total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete body");
            return ESP_FAIL;
        }
        total += (size_t)received;
    }
    if (total == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[total] = '\0';

    /* Parse form-encoded: name=...&class=... */
    char name[48] = {0};
    char class_name[16] = {0};

    bool name_ok = form_get_value(body, "name", name, sizeof(name));
    bool class_ok = form_get_value(body, "class", class_name, sizeof(class_name));

    if (!name_ok || !class_ok || !valid_student_field(name, true) ||
        !valid_student_field(class_name, false)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Dữ liệu tên/lớp không hợp lệ hoặc quá dài\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (face_engine_get_enrolled_count() >= STUDENT_MAX_COUNT) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Đã đạt giới hạn học sinh\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    /* Thực hiện enroll khuôn mặt */
    face_engine_result_t result = face_engine_enroll();
    char resp[128];
    if (result.state == FACE_ENGINE_ENROLLED) {
        uint16_t face_id = (uint16_t)result.id;
        esp_err_t db_err = student_db_set(face_id, name, class_name);
        if (db_err == ESP_OK) {
            esp_err_t marker_err = student_db_set_enrollment_complete(false);
            if (marker_err != ESP_OK) {
                ESP_LOGW(TAG, "Could not mark enrollment session active: %s",
                         esp_err_to_name(marker_err));
            }
            snprintf(resp, sizeof(resp), "{\"ok\":true,\"face_id\":%u}", face_id);
            ESP_LOGI(TAG, "Enrolled: id=%u name='%s' class='%s'", face_id, name, class_name);
        } else {
            ESP_LOGE(TAG, "Student metadata save failed: %s; rolling back face",
                     esp_err_to_name(db_err));
            if (face_engine_delete_last() != 0) {
                ESP_LOGE(TAG, "Face rollback failed for id=%u", face_id);
            }
            snprintf(resp, sizeof(resp),
                     "{\"ok\":false,\"error\":\"Không thể lưu thông tin học sinh\"}");
        }
    } else if (result.state == FACE_ENGINE_NO_FACE) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"Không phát hiện khuôn mặt\"}");
    } else if (result.state == FACE_ENGINE_MULTIPLE_FACES) {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"Chỉ để một khuôn mặt trong khung hình\"}");
    } else {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"Lỗi nhận diện (code %d)\"}", result.state);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ─── POST /undo ─────────────────────────────────────────────── */
static esp_err_t handler_undo(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    int face_count = face_engine_get_enrolled_count();
    if (face_count <= 0) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Chưa có học sinh để hoàn tác\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    uint16_t removed_id = (uint16_t)face_count;
    if (face_engine_delete_last() != 0) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Không thể xóa khuôn mặt cuối\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    esp_err_t db_err = student_db_delete(removed_id);
    if (db_err != ESP_OK) {
        ESP_LOGE(TAG, "Deleted face id=%u but metadata delete failed: %s",
                 removed_id, esp_err_to_name(db_err));
        httpd_resp_send(req,
                        "{\"ok\":false,\"error\":\"Đã xóa khuôn mặt nhưng lỗi xóa thông tin\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    (void)student_db_set_enrollment_complete(false);
    char response[80];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"removed_id\":%u,\"count\":%d}",
             removed_id, face_engine_get_enrolled_count());
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Enrollment undo: removed id=%u", removed_id);
    return ESP_OK;
}

/* ─── POST /done ─────────────────────────────────────────────── */
static esp_err_t handler_done(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    int face_count = face_engine_get_enrolled_count();
    if (face_count <= 0) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Chưa đăng ký học sinh nào\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    const int student_count = student_db_count();
    if (face_count != student_count) {
        httpd_resp_send(req,
                        "{\"ok\":false,\"error\":\"Số khuôn mặt và học sinh không khớp; hãy sửa hoặc reset danh tính\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    esp_err_t marker_err = student_db_set_enrollment_complete(true);
    if (marker_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not finish enrollment session: %s", esp_err_to_name(marker_err));
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Không thể lưu trạng thái hoàn tất\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    char response[64];
    snprintf(response, sizeof(response), "{\"ok\":true,\"count\":%d}", face_count);
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    s_done_requested = true;
    ESP_LOGI(TAG, "Teacher completed enrollment session with %d faces", face_count);
    return ESP_OK;
}

/* ─── POST /reset-identities ────────────────────────────────── */
static esp_err_t handler_reset_identities(httpd_req_t *req)
{
    char body[32] = {0};
    if (req->content_len <= 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid confirmation");
        return ESP_FAIL;
    }
    size_t total = 0;
    while (total < req->content_len) {
        const int received = httpd_req_recv(req, body + total,
                                            req->content_len - total);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Incomplete confirmation");
            return ESP_FAIL;
        }
        total += (size_t)received;
    }
    body[total] = '\0';
    char confirmation[8] = {0};
    if (!form_get_value(body, "confirm", confirmation, sizeof(confirmation)) ||
        strcmp(confirmation, "RESET") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Confirmation required");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json; charset=UTF-8");
    if (face_engine_clear_all() != 0 || student_db_clear_all() != ESP_OK) {
        ESP_LOGE(TAG, "Identity reset failed; database may require recovery");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Không thể xóa toàn bộ danh tính\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "All face embeddings and student metadata were reset");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* ─── Public API ─────────────────────────────────────────────── */
esp_err_t enroll_server_start(const char *ap_ssid, const char *ap_pass)
{
    if (s_server != NULL || s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ap_ssid == NULL || ap_ssid[0] == '\0' || strlen(ap_ssid) > 32 ||
        ap_pass == NULL || strlen(ap_pass) < 8 || strlen(ap_pass) > 63) {
        ESP_LOGE(TAG, "Enrollment AP requires a valid SSID and WPA2 password");
        return ESP_ERR_INVALID_ARG;
    }
    s_done_requested = false;
    esp_err_t err;

    /* ── Cấu hình AP (netif và event loop đã init trong app_main) ── */
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) goto fail;
    s_wifi_initialized = true;

    /* ── Cấu hình AP ── */
    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.channel = 6;
    strncpy((char *)ap_cfg.ap.password, ap_pass, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;

    /* Khởi động HTTP server */
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) goto fail;

    static const httpd_uri_t uri_root     = { .uri = "/",         .method = HTTP_GET,  .handler = handler_root     };
    static const httpd_uri_t uri_students = { .uri = "/students", .method = HTTP_GET,  .handler = handler_students };
    static const httpd_uri_t uri_enroll   = { .uri = "/enroll",   .method = HTTP_POST, .handler = handler_enroll   };
    static const httpd_uri_t uri_undo     = { .uri = "/undo",     .method = HTTP_POST, .handler = handler_undo     };
    static const httpd_uri_t uri_done     = { .uri = "/done",     .method = HTTP_POST, .handler = handler_done     };
    static const httpd_uri_t uri_reset = {
        .uri = "/reset-identities", .method = HTTP_POST,
        .handler = handler_reset_identities
    };
    err = httpd_register_uri_handler(s_server, &uri_root);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_students);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_enroll);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_undo);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_done);
    if (err != ESP_OK) goto fail;
    err = httpd_register_uri_handler(s_server, &uri_reset);
    if (err != ESP_OK) goto fail;

    ESP_LOGI(TAG, "Enroll server UP — SSID: %s  IP: 192.168.4.1", ap_ssid);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Enroll server start failed: %s", esp_err_to_name(err));
    enroll_server_stop();
    return err;
}

esp_err_t enroll_server_stop(void)
{
    esp_err_t first_error = ESP_OK;
    if (s_server) {
        esp_err_t err = httpd_stop(s_server);
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        s_server = NULL;
    }
    if (s_wifi_started) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        s_wifi_started = false;
    }
    if (s_wifi_initialized) {
        esp_err_t err = esp_wifi_deinit();
        if (err != ESP_OK && first_error == ESP_OK) first_error = err;
        s_wifi_initialized = false;
    }
    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    ESP_LOGI(TAG, "Enroll server stopped");
    return first_error;
}

bool enroll_server_done(void)
{
    return s_done_requested;
}
