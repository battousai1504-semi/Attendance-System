#pragma once

#include <stdint.h>

typedef enum {
    FACE_ENGINE_ERROR = -1,
    FACE_ENGINE_NO_FACE = 0,
    FACE_ENGINE_UNKNOWN = 1,
    FACE_ENGINE_MATCH = 2,
    FACE_ENGINE_ENROLLED = 3,
    FACE_ENGINE_MULTIPLE_FACES = 4,
} face_engine_state_t;

typedef struct {
    face_engine_state_t state;
    int id;
    int similarity_milli;
    int face_count;
} face_engine_result_t;


#ifdef __cplusplus
extern "C" {
#endif

int face_engine_init(const char *database_path);
face_engine_result_t face_engine_recognize(void);
face_engine_result_t face_engine_enroll(void);
int face_engine_get_enrolled_count(void);
int face_engine_clear_all(void);
int face_engine_delete_last(void);

#ifdef __cplusplus
}
#endif
