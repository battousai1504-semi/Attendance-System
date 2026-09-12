#pragma once

/*
 * =========================================================
 *  board_pins.h — Robot Điểm Danh
 *  PIN_CONFIG = 2: Generic ESP32-S3 N16R8 CAM
 * =========================================================
 */

/* ---- Camera OV2640 (đã xác nhận hoạt động) ---- */
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD     4
#define CAM_PIN_SIOC     5
#define CAM_PIN_D7      16   /* Y9 */
#define CAM_PIN_D6      17   /* Y8 */
#define CAM_PIN_D5      18   /* Y7 */
#define CAM_PIN_D4      12   /* Y6 */
#define CAM_PIN_D3      10   /* Y5 */
#define CAM_PIN_D2       8   /* Y4 */
#define CAM_PIN_D1       9   /* Y3 */
#define CAM_PIN_D0      11   /* Y2 */
#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK    13

/* ---- LED trạng thái (GPIO built-in thường là 48 trên N16R8) ---- */
#define STATUS_LED_GPIO  48

/* ---- Không có phần cứng thêm trong giai đoạn này ---- */
/* Loa, LCD, PIR, vân tay sẽ thêm sau (Phase 3) */
