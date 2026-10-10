#include <list>
#include <vector>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "img_converters.h"

#include "face_engine.h"

static const char *TAG = "face_engine";
static HumanFaceDetect *s_detector;
static HumanFaceRecognizer *s_recognizer;
static uint8_t *s_rgb565_buffer;
static size_t s_rgb565_capacity;
#define CAMERA_DECODE_ATTEMPTS 3
#define CAMERA_RETRY_DELAY_MS  30
/* esp32-camera's JPEG decoder emits RGB565 little-endian. This matches the
 * official ESP-WHO decode pipeline and avoids running four detector passes on
 * every idle frame while guessing the byte/color order. */
static const dl::image::pix_type_t s_camera_pix_type =
    dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

static face_engine_result_t make_result(face_engine_state_t state)
{
    face_engine_result_t result;
    result.state = state;
    result.id = -1;
    result.similarity_milli = 0;
    result.face_count = 0;
    return result;
}

static bool jpeg_has_markers(const camera_fb_t *frame)
{
    if (frame == nullptr || frame->buf == nullptr || frame->len < 4 ||
        frame->buf[0] != 0xff || frame->buf[1] != 0xd8) {
        return false;
    }
    /* Some sensors append a few padding bytes, so search the tail instead of
     * requiring EOI to be the final two bytes. */
    const size_t first = frame->len > 32 ? frame->len - 32 : 2;
    for (size_t index = frame->len - 1; index > first; index--) {
        if (frame->buf[index - 1] == 0xff && frame->buf[index] == 0xd9) {
            return true;
        }
    }
    return false;
}

static bool ensure_rgb565_capacity(size_t required)
{
    if (s_rgb565_buffer != nullptr && s_rgb565_capacity >= required) return true;
    uint8_t *replacement = (uint8_t *)heap_caps_malloc(
        required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (replacement == nullptr) return false;
    if (s_rgb565_buffer != nullptr) heap_caps_free(s_rgb565_buffer);
    s_rgb565_buffer = replacement;
    s_rgb565_capacity = required;
    return true;
}

static face_engine_result_t capture_and_detect(camera_fb_t **frame_out,
                                                std::list<dl::detect::result_t> *faces_out,
                                                dl::image::img_t *image_out)
{
    face_engine_result_t result = make_result(FACE_ENGINE_ERROR);
    if (s_detector == nullptr || s_recognizer == nullptr) {
        ESP_LOGE(TAG, "Face engine is not initialized");
        return result;
    }

    camera_fb_t *frame = nullptr;
    for (int attempt = 1; attempt <= CAMERA_DECODE_ATTEMPTS; attempt++) {
        frame = esp_camera_fb_get();
        if (frame == nullptr) {
            ESP_LOGW(TAG, "Camera returned no frame (attempt %d/%d)",
                     attempt, CAMERA_DECODE_ATTEMPTS);
        } else if (frame->format != PIXFORMAT_JPEG) {
            ESP_LOGW(TAG, "Dropping non-JPEG camera frame (format=%d attempt=%d/%d)",
                     (int)frame->format, attempt, CAMERA_DECODE_ATTEMPTS);
        } else {
            const size_t rgb565_bytes =
                (size_t)frame->width * (size_t)frame->height * 2U;
            if (!ensure_rgb565_capacity(rgb565_bytes)) {
                ESP_LOGE(TAG, "Cannot allocate %u-byte RGB565 buffer",
                         (unsigned)rgb565_bytes);
                esp_camera_fb_return(frame);
                return result;
            }
            const bool markers_valid = jpeg_has_markers(frame);
            if (markers_valid && jpg2rgb565(frame->buf, frame->len,
                                            s_rgb565_buffer,
                                            JPEG_IMAGE_SCALE_0)) {
                break;
            }
            ESP_LOGW(TAG,
                     "Dropping invalid JPEG frame len=%u markers=%s attempt=%d/%d "
                     "free_internal=%u free_psram=%u",
                     (unsigned)frame->len, markers_valid ? "yes" : "no",
                     attempt, CAMERA_DECODE_ATTEMPTS,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
        if (frame != nullptr) esp_camera_fb_return(frame);
        frame = nullptr;
        if (attempt < CAMERA_DECODE_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(CAMERA_RETRY_DELAY_MS));
        }
    }
    if (frame == nullptr) {
        ESP_LOGE(TAG, "Camera JPEG decode failed after %d attempts",
                 CAMERA_DECODE_ATTEMPTS);
        return result;
    }

    dl::image::img_t image = {
        .data = s_rgb565_buffer,
        .width = (uint16_t)frame->width,
        .height = (uint16_t)frame->height,
        .pix_type = s_camera_pix_type,
    };
    std::list<dl::detect::result_t> faces = s_detector->run(image);
    result.face_count = (int)faces.size();
    if (faces.empty()) {
        result.state = FACE_ENGINE_NO_FACE;
    }

    *frame_out = frame;
    *faces_out = faces;
    *image_out = image;
    return result;
}

extern "C" int face_engine_init(const char *database_path)
{
    if (s_detector != nullptr && s_recognizer != nullptr) {
        return 0;
    }

    s_detector = new HumanFaceDetect(HumanFaceDetect::MSRMNP_S8_V1, false);
    s_recognizer = new HumanFaceRecognizer(database_path, HumanFaceFeat::MFN_S8_V1, false);
    if (s_detector == nullptr || s_recognizer == nullptr) {
        ESP_LOGE(TAG, "Unable to allocate face models");
        return -1;
    }
    s_recognizer->set_threshold(0.46f);

    ESP_LOGI(TAG, "Face models ready; %d enrolled identity/identities, threshold=0.46",
             s_recognizer->get_num_feats());
    ESP_LOGI(TAG, "Camera decode format fixed to %s",
             dl::image::pix_type2str(s_camera_pix_type).c_str());
    return 0;
}

extern "C" face_engine_result_t face_engine_recognize(void)
{
    camera_fb_t *frame = nullptr;
    std::list<dl::detect::result_t> faces;
    dl::image::img_t image;
    face_engine_result_t result = capture_and_detect(&frame, &faces, &image);
    if (frame == nullptr) {
        return result;
    }

    if (result.state == FACE_ENGINE_NO_FACE) {
        esp_camera_fb_return(frame);
        return result;
    }

    std::vector<dl::recognition::result_t> matches = s_recognizer->recognize(image, faces);
    if (!matches.empty()) {
        result.state = FACE_ENGINE_MATCH;
        result.id = matches[0].id;
        result.similarity_milli = (int)(matches[0].similarity * 1000.0f);
    } else {
        result.state = FACE_ENGINE_UNKNOWN;
    }
    esp_camera_fb_return(frame);
    return result;
}

extern "C" face_engine_result_t face_engine_enroll(void)
{
    camera_fb_t *frame = nullptr;
    std::list<dl::detect::result_t> faces;
    dl::image::img_t image;
    face_engine_result_t result = capture_and_detect(&frame, &faces, &image);
    if (frame == nullptr) {
        return result;
    }

    if (faces.size() > 1) {
        result.state = FACE_ENGINE_MULTIPLE_FACES;
    } else if (faces.size() == 1 && s_recognizer->enroll(image, faces) == ESP_OK) {
        result.state = FACE_ENGINE_ENROLLED;
        result.id = s_recognizer->get_num_feats();
    }
    esp_camera_fb_return(frame);
    return result;
}

extern "C" int face_engine_get_enrolled_count(void)
{
    return s_recognizer == nullptr ? -1 : s_recognizer->get_num_feats();
}

extern "C" int face_engine_clear_all(void)
{
    if (s_recognizer == nullptr) {
        return -1;
    }
    return s_recognizer->clear_all_feats() == ESP_OK ? 0 : -1;
}

extern "C" int face_engine_delete_last(void)
{
    if (s_recognizer == nullptr) {
        return -1;
    }
    return s_recognizer->delete_last_feat() == ESP_OK ? 0 : -1;
}
