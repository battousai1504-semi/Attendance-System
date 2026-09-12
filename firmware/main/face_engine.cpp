#include <list>
#include <vector>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "img_converters.h"

#include "face_engine.h"

static const char *TAG = "face_engine";
static HumanFaceDetect *s_detector;
static HumanFaceRecognizer *s_recognizer;
static uint8_t *s_rgb565_buffer;
static dl::image::pix_type_t s_camera_pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;
static bool s_camera_pix_type_verified;

static face_engine_result_t make_result(face_engine_state_t state)
{
    face_engine_result_t result;
    result.state = state;
    result.id = -1;
    result.similarity_milli = 0;
    result.face_count = 0;
    return result;
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

    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr || frame->format != PIXFORMAT_JPEG) {
        ESP_LOGE(TAG, "Camera frame unavailable or not JPEG");
        if (frame != nullptr) {
            esp_camera_fb_return(frame);
        }
        return result;
    }

    const size_t rgb565_bytes = frame->width * frame->height * 2;
    if (s_rgb565_buffer == nullptr) {
        s_rgb565_buffer = (uint8_t *)heap_caps_malloc(rgb565_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_rgb565_buffer == nullptr || !jpg2rgb565(frame->buf, frame->len, s_rgb565_buffer, JPEG_IMAGE_SCALE_0)) {
        ESP_LOGE(TAG, "JPEG to RGB565 conversion failed");
        esp_camera_fb_return(frame);
        return result;
    }

    dl::image::img_t image = {
        .data = s_rgb565_buffer,
        .width = (uint16_t)frame->width,
        .height = (uint16_t)frame->height,
        .pix_type = s_camera_pix_type,
    };
    std::list<dl::detect::result_t> best_faces;
    if (!s_camera_pix_type_verified) {
        const dl::image::pix_type_t formats[] = {
            dl::image::DL_IMAGE_PIX_TYPE_RGB565LE,
            dl::image::DL_IMAGE_PIX_TYPE_RGB565BE,
            dl::image::DL_IMAGE_PIX_TYPE_BGR565LE,
            dl::image::DL_IMAGE_PIX_TYPE_BGR565BE,
        };
        for (const auto format : formats) {
            image.pix_type = format;
            std::list<dl::detect::result_t> &trial_faces = s_detector->run(image);
            ESP_LOGI(TAG, "Face-format trial %s: %u face(s)",
                     dl::image::pix_type2str(format).c_str(), (unsigned)trial_faces.size());
            if (trial_faces.size() > best_faces.size()) {
                best_faces = trial_faces;
                s_camera_pix_type = format;
            }
        }
        if (!best_faces.empty()) {
            s_camera_pix_type_verified = true;
            ESP_LOGI(TAG, "Selected camera input format: %s", dl::image::pix_type2str(s_camera_pix_type).c_str());
        }
    } else {
        image.pix_type = s_camera_pix_type;
        best_faces = s_detector->run(image);
    }
    image.pix_type = s_camera_pix_type;
    std::list<dl::detect::result_t> &faces = best_faces;
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
