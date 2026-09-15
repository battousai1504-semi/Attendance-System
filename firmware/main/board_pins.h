#pragma once

/*
 * =========================================================
 *  board_pins.h — Robot Điểm Danh
 *  Board: ESP32-S3-WROOM N16R8 camera board (Freenove-style clone)
 *  The camera is connected internally through the board FPC connector.
 * =========================================================
 */

/* ---- Internal camera bus (ESP32S3_EYE/Freenove-compatible routing) ---- */
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

/* GPIO2 drives the plain onboard indicator marked IO2. GPIO48 drives the
 * onboard WS2812 and must not be toggled as a conventional GPIO LED. */
#define STATUS_LED_GPIO  2

/* Hold the board BOOT button while the firmware starts to reopen enrollment. */
#define ENROLL_BUTTON_GPIO         0
#define ENROLL_BUTTON_ACTIVE_LEVEL 0
#define ENROLL_BUTTON_HOLD_MS      2500

/* ---- Không có phần cứng thêm trong giai đoạn này ---- */
/* Loa, LCD, PIR, vân tay sẽ thêm sau (Phase 3) */
