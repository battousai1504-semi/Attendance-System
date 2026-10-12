const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const sourcePath = path.join(__dirname, '..', '..', 'firmware', 'main',
                             'enroll_server.c');
const source = fs.readFileSync(sourcePath, 'utf8');
const html = fs.readFileSync(path.join(__dirname, '..', '..', 'firmware',
  'main', 'portal_config.html'), 'utf8');
const enrollHtml = fs.readFileSync(path.join(__dirname, '..', '..', 'firmware',
  'main', 'portal_enroll.html'), 'utf8');
assert(html.startsWith('<!DOCTYPE html>'));
assert(enrollHtml.startsWith('<!DOCTYPE html>'));
assert(enrollHtml.includes('/enroll'));
assert(enrollHtml.includes('student-voice-roster.json'));
assert(html.includes('/config/test-and-save'));
assert(html.includes('/config/job'));
assert(html.includes('/config/restart'));
assert(html.includes('/config/rollback'));
assert(html.includes('/config/reset'));
assert(html.includes('/audio/status'));
assert(html.includes('/audio/config'));
assert(html.includes('/audio/test'));
assert(!html.includes('audioVolume'));
assert(html.includes('Âm lượng mặc định 30/30'));
assert(html.includes('Cài đặt kỹ thuật'));
assert(html.includes('Quét Wi-Fi 2,4 GHz'));
assert(!html.includes('localStorage'));
assert(!html.includes('innerHTML'));
assert(!/fetch\([^)]*(password|secret)/i.test(html));

const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
assert(scriptMatch, 'inline portal script not found');
new vm.Script(scriptMatch[1], {filename: 'maintenance-portal-inline.js'});
const enrollScript = enrollHtml.match(/<script>([\s\S]*?)<\/script>/);
assert(enrollScript, 'enrollment portal script not found');
new vm.Script(enrollScript[1], {filename: 'enrollment-portal-inline.js'});
assert(source.includes('portal_config_html_start'));
assert(source.includes('portal_enroll_html_start'));

assert(source.includes('device_config_save(&s_config_job_candidate'));
assert(source.includes('test_candidate_config(&s_config_job_candidate)'));
assert(source.indexOf('test_candidate_config(&s_config_job_candidate)') <
       source.indexOf('device_config_save(&s_config_job_candidate'));
async function testConfigRequestRecovery() {
  const elements = new Map();
  const element = (id) => {
    if (!elements.has(id)) {
      elements.set(id, {
        textContent: '', className: '', value: '', disabled: false,
        checked: true, style: {}, open: false,
      });
    }
    return elements.get(id);
  };
  const timers = [];
  let jobState = 'idle';
  let postCount = 0;
  const browser = vm.createContext({
    TypeError,
    URLSearchParams,
    FormData: class { *[Symbol.iterator]() { yield ['ssid', 'HP coffee']; } },
    document: {
      getElementById: element,
      querySelector: () => element('password'),
      createElement: () => ({}),
    },
    setTimeout: (callback) => timers.push(callback),
    fetch: async (url) => {
      if (url === '/config/test-and-save') {
        postCount++;
        throw new TypeError('Failed to fetch');
      }
      const value = url === '/config/status'
        ? {ok: true, configured: true, ssid: 'old-network', device_id: 'robot-01'}
        : url === '/config/job'
          ? {ok: true, state: jobState, error: ''}
          : {ok: true, enabled: true, initialized: true,
             module_responding: true, dropped_events: 0};
      return {ok: true, json: async () => value};
    },
  });
  vm.runInContext(scriptMatch[1], browser);
  await new Promise(setImmediate);
  await element('configForm').onsubmit({preventDefault() {}, target: {}});
  assert.strictEqual(element('saveBtn').disabled, true);
  assert.match(element('status').textContent, /Đang kiểm tra robot đã nhận/);
  assert.strictEqual(postCount, 1);
  jobState = 'saved';
  timers.shift()();
  await new Promise(setImmediate);
  assert.strictEqual(element('saveBtn').disabled, false);
  assert.match(element('status').textContent, /Đã kiểm tra và lưu/);
  assert.strictEqual(element('restartBtn').style.display, 'block');
  assert.strictEqual(postCount, 1, 'lost response must not resubmit config');

  jobState = 'idle';
  await element('configForm').onsubmit({preventDefault() {}, target: {}});
  assert.strictEqual(element('restartBtn').style.display, 'none');
  for (let check = 0; check < 4; check++) {
    timers.shift()();
    await new Promise(setImmediate);
  }
  assert.strictEqual(element('saveBtn').disabled, false);
  assert.match(element('status').textContent, /Robot chưa nhận yêu cầu/);
  assert.strictEqual(postCount, 2, 'status polling must not resubmit config');
}

testConfigRequestRecovery()
  .then(() => console.log('Maintenance portal HTML tests: PASS'))
  .catch((error) => { console.error(error); process.exitCode = 1; });
