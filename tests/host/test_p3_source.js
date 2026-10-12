const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const pins = fs.readFileSync(path.join(root, 'firmware', 'main', 'board_pins.h'), 'utf8');
const app = fs.readFileSync(path.join(root, 'firmware', 'main', 'app_main.c'), 'utf8');
const audio = fs.readFileSync(path.join(root, 'firmware', 'main', 'audio_feedback.c'), 'utf8');
const portal = fs.readFileSync(path.join(root, 'firmware', 'main', 'enroll_server.c'), 'utf8');
const enrollHtml = fs.readFileSync(path.join(root, 'firmware', 'main', 'portal_enroll.html'), 'utf8');
const configHtml = fs.readFileSync(path.join(root, 'firmware', 'main', 'portal_config.html'), 'utf8');
const cmake = fs.readFileSync(path.join(root, 'firmware', 'CMakeLists.txt'), 'utf8');

const macro = (name) => {
  const match = pins.match(new RegExp('#define\\s+' + name + '\\s+(-?\\d+)'));
  assert(match, 'missing pin macro ' + name);
  return Number(match[1]);
};
const cameraPins = [
  'CAM_PIN_XCLK', 'CAM_PIN_SIOD', 'CAM_PIN_SIOC', 'CAM_PIN_D7',
  'CAM_PIN_D6', 'CAM_PIN_D5', 'CAM_PIN_D4', 'CAM_PIN_D3',
  'CAM_PIN_D2', 'CAM_PIN_D1', 'CAM_PIN_D0', 'CAM_PIN_VSYNC',
  'CAM_PIN_HREF', 'CAM_PIN_PCLK',
].map(macro);
const tx = macro('DFPLAYER_TX_GPIO');
const rx = macro('DFPLAYER_RX_GPIO');
assert.strictEqual(tx, 41);
assert.strictEqual(rx, 42);
assert.notStrictEqual(tx, rx);
assert(!cameraPins.includes(tx));
assert(!cameraPins.includes(rx));
assert(![0, 2, 19, 20].includes(tx));
assert(![0, 2, 19, 20].includes(rx));

assert.match(cmake, /3\.1\.0-phase3-ux-dev/);
assert.match(audio, /uart_driver_install/);
assert.match(audio, /\.baud_rate = 9600/);
assert.match(audio, /xQueueSend\(s_queue, &job, 0\)/);
assert.match(audio, /AUDIO_CONFIG_MAGIC/);
assert.match(audio, /crc32_bytes/);
assert.match(app, /audio_feedback_notify_attendance\(\(uint16_t\)result->id\)/);
assert.match(audio, /AUDIO_FEEDBACK_STUDENT_TRACK_BASE \+ student_id/);
assert.match(audio, /AUDIO_GREETING_GUARD_MS/);
assert.match(audio, /Student greeting %u missing; using generic success track/);
assert.match(app, /audio_feedback_notify\(AUDIO_EVENT_ATTENDANCE_DUPLICATE\)/);
assert.match(app, /audio_feedback_notify\(AUDIO_EVENT_SYSTEM_READY\)/);
assert.match(app, /DFPlayer audio disabled for this boot/);
assert.doesNotMatch(app, /ESP_ERROR_CHECK\(audio_feedback_init/);
assert.match(portal, /"\/audio\/status"/);
assert.match(portal, /"\/audio\/config"/);
assert.match(portal, /"\/audio\/test"/);
assert.match(enrollHtml, /downloadVoiceRoster/);
assert.match(configHtml, /testStudentAudio/);
assert.match(portal, /AUDIO_EVENT_ENROLL_SUCCESS/);

console.log('Phase 3 audio source tests: PASS');
