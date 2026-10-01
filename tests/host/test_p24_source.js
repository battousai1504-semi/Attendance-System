const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..', 'firmware', 'main');
const upload = fs.readFileSync(path.join(root, 'http_upload.c'), 'utf8');
const codec = fs.readFileSync(path.join(root, 'outbox_codec.c'), 'utf8');

assert.match(upload, /HEARTBEAT_INTERVAL_MS\s+60000/);
assert.match(upload, /UPLOAD_BATCH_MAX\s+5/);
assert.match(upload, /do_heartbeat/);
assert.match(upload, /upload_policy_backoff_ms/);
assert.match(upload, /OUTBOX_RECORD_VERSION_V2/);
assert.match(upload, /outbox_repair/);
assert.match(upload, /xTaskCreatePinnedToCore/);
assert.match(codec, /OUTBOX_RECORD_VERSION_V1/);
assert.match(codec, /stored_checksum != outbox_checksum/);

console.log('P2.4 firmware source tests: PASS');
