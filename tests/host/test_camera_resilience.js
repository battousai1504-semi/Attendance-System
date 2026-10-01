const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..', 'firmware', 'main');
const face = fs.readFileSync(path.join(root, 'face_engine.cpp'), 'utf8');
const app = fs.readFileSync(path.join(root, 'app_main.c'), 'utf8');
const upload = fs.readFileSync(path.join(root, 'http_upload.c'), 'utf8');

assert.match(face, /CAMERA_DECODE_ATTEMPTS\s+3/);
assert.match(face, /jpeg_has_markers/);
assert.match(face, /Dropping invalid JPEG frame/);
assert.match(face, /free_internal=%u free_psram=%u/);

assert.match(app, /CAMERA_RECOVERY_THRESHOLD\s+3/);
assert.match(app, /esp_camera_deinit\(\)/);
assert.match(app, /Camera recovery succeeded/);

assert.match(upload, /UPLOAD_TASK_CORE\s+1/);
assert.match(upload, /xTaskCreatePinnedToCore/);

console.log('Camera resilience source tests: PASS');
