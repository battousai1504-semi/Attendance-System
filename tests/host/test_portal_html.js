const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const sourcePath = path.join(__dirname, '..', '..', 'firmware', 'main',
                             'enroll_server.c');
const source = fs.readFileSync(sourcePath, 'utf8');
const start = source.indexOf('static const char *CONFIG_HTML =');
const end = source.indexOf('/* ─── GET /', start);
assert(start >= 0 && end > start, 'CONFIG_HTML declaration not found');

const declaration = source.slice(start, end);
const literals = declaration.match(/"(?:\\.|[^"\\])*"/g) || [];
const html = literals.map((literal) => JSON.parse(literal)).join('');
assert(html.startsWith('<!DOCTYPE html>'));
assert(html.includes('/config/test-and-save'));
assert(html.includes('/config/job'));
assert(html.includes('/config/restart'));
assert(html.includes('/config/rollback'));
assert(html.includes('/config/reset'));
assert(html.includes('/audio/status'));
assert(html.includes('/audio/config'));
assert(html.includes('/audio/test'));
assert(html.includes('audioVolume'));
assert(html.includes('Quét Wi-Fi 2,4 GHz'));
assert(!html.includes('localStorage'));
assert(!html.includes('innerHTML'));
assert(!/fetch\([^)]*(password|secret)/i.test(html));

const scriptMatch = html.match(/<script>([\s\S]*?)<\/script>/);
assert(scriptMatch, 'inline portal script not found');
new vm.Script(scriptMatch[1], {filename: 'maintenance-portal-inline.js'});

assert(source.includes('device_config_save(&s_config_job_candidate'));
assert(source.includes('test_candidate_config(&s_config_job_candidate)'));
assert(source.indexOf('test_candidate_config(&s_config_job_candidate)') <
       source.indexOf('device_config_save(&s_config_job_candidate'));
console.log('Maintenance portal HTML tests: PASS');
