const assert = require('assert');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..', '..');
const partitions = fs.readFileSync(path.join(root, 'firmware', 'partitions.csv'), 'utf8');
const appMain = fs.readFileSync(path.join(root, 'firmware', 'main', 'app_main.c'), 'utf8');
const ota = fs.readFileSync(path.join(root, 'firmware', 'main', 'ota_manager.c'), 'utf8');
const sdkconfig = fs.readFileSync(path.join(root, 'firmware', 'sdkconfig.defaults'), 'utf8');

assert.match(partitions, /ota_0,\s+app,\s+ota_0,\s+0x10000,\s+0x600000/);
assert.match(partitions, /faces,\s+data,\s+spiffs,\s+0x610000,\s+0x200000/);
assert.match(partitions, /otadata,\s+data,\s+ota,\s+0x810000,\s+0x2000/);
assert.match(partitions, /ota_1,\s+app,\s+ota_1,\s+0x820000,\s+0x600000/);
assert.match(sdkconfig, /CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y/);
assert.match(appMain, /esp_psram_is_initialized\(\)/);
assert.match(appMain, /ota_manager_confirm_running_image\(\)/);
assert.match(ota, /esp_ota_mark_app_invalid_rollback_and_reboot\(\)/);
assert.match(ota, /memcmp\(actual_sha256, manifest->sha256/);
assert.match(ota, /esp_https_ota_finish\(handle\)/);
assert.ok(ota.indexOf('memcmp(actual_sha256, manifest->sha256') <
          ota.indexOf('esp_https_ota_finish(handle)'),
          'SHA-256 must be checked before selecting the boot partition');
console.log('P2.5 OTA source tests: PASS');
