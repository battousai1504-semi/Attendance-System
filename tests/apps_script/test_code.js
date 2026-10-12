const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

let flushCount = 0;
let lockAvailable = true;
let mailFailure = false;
const sentMail = [];
const scriptProperties = {};
const cached = new Map();

class FakeSheet {
  constructor(name) {
    this.name = name;
    this.rows = [];
    this.readCount = 0;
  }

  getLastRow() {
    return this.rows.length;
  }

  getLastColumn() {
    return this.rows.reduce((maximum, row) => Math.max(maximum, row.length), 0);
  }

  appendRow(row) {
    this.rows.push(Array.from(row));
  }

  setFrozenRows() {}

  getRange(row, column, rowCount, columnCount) {
    const sheet = this;
    const range = {
      setFontWeight() {
        return range;
      },
      setNumberFormat() {
        return range;
      },
      setValue(value) {
        const rowIndex = row - 1;
        while (sheet.rows.length <= rowIndex) sheet.rows.push([]);
        sheet.rows[rowIndex][column - 1] = value;
        return range;
      },
      setValues(values) {
        for (let rowOffset = 0; rowOffset < rowCount; rowOffset++) {
          const rowIndex = row - 1 + rowOffset;
          while (sheet.rows.length <= rowIndex) sheet.rows.push([]);
          for (let columnOffset = 0; columnOffset < columnCount; columnOffset++) {
            sheet.rows[rowIndex][column - 1 + columnOffset] =
              values[rowOffset][columnOffset];
          }
        }
        return range;
      },
      getValues() {
        sheet.readCount++;
        const values = [];
        for (let rowIndex = row - 1; rowIndex < row - 1 + rowCount; rowIndex++) {
          const source = sheet.rows[rowIndex] || [];
          const value = [];
          for (let columnIndex = column - 1;
               columnIndex < column - 1 + columnCount; columnIndex++) {
            value.push(source[columnIndex] === undefined ? '' : source[columnIndex]);
          }
          values.push(value);
        }
        return values;
      },
      createTextFinder(value) {
        return {
          matchEntireCell() {
            return this;
          },
          findNext() {
            for (let index = row - 1; index < row - 1 + rowCount; index++) {
              if (sheet.rows[index] &&
                  String(sheet.rows[index][column - 1]) === String(value)) {
                return {getRow: () => index + 1};
              }
            }
            return null;
          },
        };
      },
    };
    return range;
  }
}

const sheets = {Attendance: new FakeSheet('Attendance')};
const spreadsheet = {
  getSheetByName(name) {
    return sheets[name] || null;
  },
  insertSheet(name) {
    if (sheets[name]) throw new Error('Sheet already exists: ' + name);
    sheets[name] = new FakeSheet(name);
    return sheets[name];
  },
};

const propertiesApi = {
  getProperty(key) {
    return scriptProperties[key] || '';
  },
  setProperty(key, value) {
    scriptProperties[key] = String(value);
    return propertiesApi;
  },
  deleteProperty(key) {
    delete scriptProperties[key];
    return propertiesApi;
  },
};

const projectTriggers = [];
const ScriptApp = {
  getProjectTriggers() {
    return projectTriggers.slice();
  },
  deleteTrigger(trigger) {
    const index = projectTriggers.indexOf(trigger);
    if (index >= 0) projectTriggers.splice(index, 1);
  },
  newTrigger(handler) {
    return {
      timeBased() { return this; },
      everyMinutes(minutes) { this.minutes = minutes; return this; },
      create() {
        const name = handler;
        const trigger = {getHandlerFunction: () => name, minutes: this.minutes};
        projectTriggers.push(trigger);
        return trigger;
      },
    };
  },
};

const context = {
  console,
  Date,
  JSON,
  Number,
  String,
  ContentService: {
    MimeType: {JSON: 'application/json'},
    createTextOutput(text) {
      return {
        text,
        setMimeType() {
          return this;
        },
      };
    },
  },
  ScriptApp,
  MailApp: {
    getRemainingDailyQuota() { return 100; },
    sendEmail(message) {
      if (mailFailure) throw new Error('Temporary mail failure');
      sentMail.push(message);
    },
  },
  HtmlService: {
    createHtmlOutputFromFile(name) {
      return {
        fileName: name,
        title: '',
        meta: {},
        setTitle(value) {
          this.title = value;
          return this;
        },
        addMetaTag(nameValue, content) {
          this.meta[nameValue] = content;
          return this;
        },
      };
    },
  },
  PropertiesService: {
    getScriptProperties() {
      return propertiesApi;
    },
  },
  CacheService: {
    getScriptCache() {
      return {
        get(key) { return cached.get(key) || null; },
        put(key, value) { cached.set(key, String(value)); },
        remove(key) { cached.delete(key); },
      };
    },
  },
  SpreadsheetApp: {
    getActiveSpreadsheet() {
      return spreadsheet;
    },
    openById() {
      return spreadsheet;
    },
    flush() {
      flushCount++;
    },
  },
  LockService: {
    getScriptLock() {
      return {
        tryLock() {
          return lockAvailable;
        },
        releaseLock() {},
      };
    },
  },
  Utilities: {
    newBlob(body, contentType, name) { return {body, contentType, name}; },
    getUuid() { return crypto.randomUUID(); },
    DigestAlgorithm: {SHA_256: 'sha256'},
    Charset: {UTF_8: 'utf8'},
    computeDigest(algorithm, value) {
      return Array.from(crypto.createHash(algorithm).update(String(value)).digest())
        .map((byte) => byte > 127 ? byte - 256 : byte);
    },
    formatDate(value, timeZone, format) {
      const parts = Object.fromEntries(
        new Intl.DateTimeFormat('en-CA', {
          timeZone,
          year: 'numeric', month: '2-digit', day: '2-digit',
          hour: '2-digit', minute: '2-digit', second: '2-digit',
          hourCycle: 'h23',
        }).formatToParts(value).map((part) => [part.type, part.value]),
      );
      const date = `${parts.year}-${parts.month}-${parts.day}`;
      if (format === 'yyyy-MM-dd') return date;
      if (format === 'ddMMyyyy') return `${parts.day}${parts.month}${parts.year}`;
      return `${date} ${parts.hour}:${parts.minute}:${parts.second}`;
    },
  },
};

vm.createContext(context);
const sourcePath = path.join(__dirname, '..', '..', 'google_apps_script', 'Code.gs');
vm.runInContext(fs.readFileSync(sourcePath, 'utf8'), context, {filename: sourcePath});

function post(payload) {
  const response = context.doPost({postData: {contents: JSON.stringify(payload)}});
  return JSON.parse(response.text);
}

function postRaw(contents) {
  return JSON.parse(context.doPost({postData: {contents}}).text);
}

const legacyToken = '0123456789abcdef0123456789abcdef';
const rotatedToken = 'abcdef0123456789abcdef0123456789';
const validPayload = {
  event_id: '1760000000-1-12345678',
  device_id: 'robot-01',
  token: legacyToken,
  student_id: 1,
  name: '=Nguyen Van A',
  class: '10A1',
  timestamp: '2026-09-15 08:30:00',
  confidence_milli: 720,
  method: 'FACE',
};

assert.deepStrictEqual(post(validPayload), {
  ok: false,
  error: 'server_not_configured',
  retryable: false,
  api_version: 2,
});
assert.strictEqual(postRaw('{broken-json').error, 'invalid_json');
assert.strictEqual(post(null).error, 'invalid_body');
assert.strictEqual(JSON.parse(context.doGet().text).ok, false);
assert.throws(() => context.setup(), /Set API_TOKEN/);

scriptProperties.API_TOKEN = legacyToken;
assert.strictEqual(context.setup().ok, true);
const migrated = context.migrateToV2();
assert.strictEqual(migrated.ok, true);
assert.strictEqual(migrated.schema_version, '2');
assert.strictEqual(scriptProperties.SCHEMA_VERSION, '2');
assert.strictEqual(scriptProperties.ALLOW_LEGACY_TOKEN, 'true');
assert.deepStrictEqual(Object.keys(sheets), [
  'Attendance', 'Students', 'Classes', 'Sessions', 'Devices', 'AuditLog',
]);
assert.strictEqual(sheets.Attendance.rows[0].length, 12);
assert.strictEqual(sheets.Devices.rows[0].length, 16);
assert.strictEqual(sheets.Sessions.rows[0].length, 16);
assert.strictEqual(JSON.parse(context.doGet().text).configured, true);

assert.strictEqual(post({...validPayload, token: 'wrong-token-that-is-long-enough-000'}).error,
                   'unauthorized');

const acceptedLegacy = post(validPayload);
assert.strictEqual(acceptedLegacy.ok, true);
assert.strictEqual(acceptedLegacy.duplicate, false);
assert.strictEqual(acceptedLegacy.api_version, 2);
assert.strictEqual(sheets.Attendance.rows.length, 2);
assert.strictEqual(sheets.Attendance.rows[1].length, 12);
assert.strictEqual(sheets.Attendance.rows[1][4], "'=Nguyen Van A");
assert.strictEqual(sheets.Attendance.rows[1][11], 1);
assert.strictEqual(sheets.Devices.rows.length, 2);
assert.notStrictEqual(sheets.Devices.rows[1][2], legacyToken);

const duplicate = post(validPayload);
assert.strictEqual(duplicate.ok, true);
assert.strictEqual(duplicate.duplicate, true);
assert.strictEqual(sheets.Attendance.rows.length, 2);
const originalTimestamp = sheets.Attendance.rows[1][6];
sheets.Attendance.rows[1][6] = new Date('2026-09-15T08:30:00+07:00');
assert.strictEqual(post(validPayload).duplicate, true);
sheets.Attendance.rows[1][6] = originalTimestamp;
assert.strictEqual(post({...validPayload, student_id: 2}).error, 'event_conflict');

const heartbeat = post({
  api_version: 2,
  type: 'heartbeat',
  device_id: 'robot-01',
  token: legacyToken,
  firmware_version: '2.0.0-phase2-dev',
  uptime_seconds: 120,
  rssi: -55,
  free_heap: 180000,
  free_psram: 6000000,
  pending_events: 1,
  sync_latency_ms: 410,
  consecutive_failures: 2,
  last_error: 'previous_timeout',
});
assert.strictEqual(heartbeat.ok, true);
assert.strictEqual(heartbeat.device_id, 'robot-01');
assert.strictEqual(sheets.Devices.rows.length, 2);
assert.strictEqual(sheets.Devices.rows[1][4], '2.0.0-phase2-dev');
assert.strictEqual(sheets.Devices.rows[1][5], -55);
assert.strictEqual(sheets.Devices.rows[1][13], 'ONLINE');
assert.strictEqual(sheets.Devices.rows[1][14], 410);
assert.strictEqual(sheets.Devices.rows[1][15], 2);
sheets.Devices.rows[1][3] = new Date(Date.now() - 181000);
assert.strictEqual(context.refreshDeviceStatuses().changed_count, 1);
assert.strictEqual(sheets.Devices.rows[1][13], 'OFFLINE');
assert.strictEqual(post(validPayload).duplicate, true);
assert.strictEqual(sheets.Devices.rows[1][13], 'ONLINE');
assert.strictEqual(sheets.Devices.rows[1][4], '2.0.0-phase2-dev');

scriptProperties.BOOTSTRAP_DEVICE_ID = 'robot-01';
scriptProperties.BOOTSTRAP_DEVICE_SECRET = rotatedToken;
assert.strictEqual(context.provisionDeviceFromProperties().ok, true);
assert.strictEqual(scriptProperties.BOOTSTRAP_DEVICE_SECRET, undefined);
assert.strictEqual(post({...validPayload, event_id: 'legacy-after-rotate'}).error,
                   'unauthorized');

const rotatedHeartbeat = post({
  api_version: 2,
  type: 'heartbeat',
  device_id: 'robot-01',
  token: rotatedToken,
  firmware_version: '2.0.0-phase2-dev',
  uptime_seconds: 180,
  rssi: -60,
  free_heap: 175000,
  free_psram: 5900000,
  pending_events: 0,
  last_error: '',
});
assert.strictEqual(rotatedHeartbeat.ok, true);

const otaRequest = {
  api_version: 2,
  type: 'ota_manifest',
  device_id: 'robot-01',
  token: rotatedToken,
  firmware_version: '2.5.0-phase2-dev',
};
const otaDisabled = post(otaRequest);
assert.strictEqual(otaDisabled.ok, true);
assert.strictEqual(otaDisabled.available, false);
scriptProperties.OTA_ENABLED = 'true';
scriptProperties.OTA_VERSION = '2.5.1-ota-test';
scriptProperties.OTA_URL = 'https://example.com/robot_attendance.bin';
scriptProperties.OTA_SIZE = '4718592';
scriptProperties.OTA_SHA256 =
  '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef';
const otaAvailable = post(otaRequest);
assert.strictEqual(otaAvailable.ok, true);
assert.strictEqual(otaAvailable.available, true);
assert.strictEqual(otaAvailable.version, '2.5.1-ota-test');
assert.strictEqual(otaAvailable.size, 4718592);
assert.strictEqual(otaAvailable.sha256.length, 64);
assert.strictEqual(post({...otaRequest,
  firmware_version: '2.5.1-ota-test'}).available, false);
scriptProperties.OTA_URL = 'http://unsafe.example/firmware.bin';
assert.strictEqual(post(otaRequest).error, 'ota_manifest_invalid');
scriptProperties.OTA_URL = 'https://example.com/robot_attendance.bin';
scriptProperties.OTA_ENABLED = 'false';

sheets.Devices.rows[1][1] = false;
assert.strictEqual(context.refreshDeviceStatuses().changed_count, 1);
assert.strictEqual(sheets.Devices.rows[1][13], 'DISABLED');
assert.strictEqual(post({
  api_version: 2,
  type: 'session_sync',
  device_id: 'robot-01',
  token: rotatedToken,
  class: '10A1',
}).error, 'device_disabled');
sheets.Devices.rows[1][1] = true;
assert.strictEqual(context.refreshDeviceStatuses().changed_count, 1);
assert.strictEqual(sheets.Devices.rows[1][13], 'ONLINE');

const historicalStart = new Date('2026-09-15T08:00:00+07:00');
const historicalLate = new Date('2026-09-15T08:10:00+07:00');
const historicalEnd = new Date('2026-09-15T09:00:00+07:00');
sheets.Sessions.appendRow([
  '10A1-20260915-P1', '10A1', '2026-09-15', 'P1', historicalStart,
  historicalLate, historicalEnd, 'CLOSED', 'robot-01', new Date(),
]);
const now = Date.now();
sheets.Sessions.appendRow([
  '10A1-ACTIVE', '10A1', '2026-09-21', 'P2', new Date(now - 3600000),
  new Date(now - 3000000), new Date(now + 3600000), 'ACTIVE', 'robot-01',
  new Date(),
]);

const v2Attendance = post({
  ...validPayload,
  api_version: 2,
  type: 'attendance',
  event_id: '1760000000-1-v2abcdef',
  token: rotatedToken,
  name: 'Nguyen Van A',
  firmware_version: '2.0.0-phase2-dev',
  session_id: '',
});
assert.strictEqual(v2Attendance.ok, true);
assert.strictEqual(v2Attendance.session_id, '10A1-20260915-P1');
assert.strictEqual(sheets.Attendance.rows[2][9], '10A1-20260915-P1');
assert.strictEqual(sheets.Attendance.rows[2][11], 2);

const sessionSync = post({
  api_version: 2,
  type: 'session_sync',
  device_id: 'robot-01',
  token: rotatedToken,
  class: '10A1',
});
assert.strictEqual(sessionSync.ok, true);
assert.strictEqual(sessionSync.active, true);
assert.strictEqual(sessionSync.session.session_id, '10A1-ACTIVE');

sheets.Sessions.appendRow([
  '10A1-ACTIVE-2', '10A1', '2026-09-21', 'P3', new Date(now - 1800000),
  new Date(now - 1200000), new Date(now + 1800000), 'ACTIVE', 'robot-01',
  new Date(),
]);
assert.strictEqual(post({
  api_version: 2,
  type: 'session_sync',
  device_id: 'robot-01',
  token: rotatedToken,
  class: '10A1',
}).error, 'session_conflict');
sheets.Sessions.rows.pop();

assert.strictEqual(context.doGet({parameter: {view: 'dashboard'}}).fileName,
                   'Dashboard');
assert.throws(() => context.getDashboardData('wrong-dashboard-key', {}),
              /dashboard_unauthorized/);
scriptProperties.BOOTSTRAP_TEACHER_USERNAME = 'Giáo viên mẫu';
scriptProperties.BOOTSTRAP_TEACHER_PASSWORD = 'test-only-password';
assert.strictEqual(context.provisionDashboardAccountFromProperties().ok, true);
assert.strictEqual(scriptProperties.BOOTSTRAP_TEACHER_PASSWORD, undefined);
assert.strictEqual(scriptProperties.DASHBOARD_PASSWORD_HASH.length, 64);
assert.throws(() => context.loginDashboard('Giáo viên mẫu', 'wrong-password'),
              /dashboard_login_failed/);

sheets.Students.appendRow([1, 'Nguyen Van A', '10A1', true, new Date(), new Date()]);
sheets.Students.appendRow([2, 'Tran Thi B', '10A1', true, new Date(), new Date()]);
sheets.Students.appendRow([3, 'Other Class', '11A1', true, new Date(), new Date()]);
const dashboardKey = context.loginDashboard('Giáo viên mẫu', 'test-only-password').token;
assert.deepStrictEqual(Array.from(context.getDashboardData(dashboardKey, {}).classes),
  ['10A1', '11A1']);
assert.ok(context.getDashboardData(dashboardKey, {}).unassigned_today >= 1);
sheets.Classes.appendRow(['10A1', 'Lớp 10A1', true, new Date(), new Date()]);
sheets.Classes.appendRow(['11A1', 'Lớp 11A1', false, new Date(), new Date()]);
assert.deepStrictEqual(Array.from(context.getDashboardData(dashboardKey, {}).classes),
  ['10A1']);

// Google Sheets may coerce an all-numeric Session ID into a Date. Dashboard
// reads, corrections and manual close must still address the canonical ID.
const coercedSessionId = new Date('2026-09-23T00:00:00+07:00');
sheets.Sessions.appendRow([
  coercedSessionId, '11A1', '2026-09-23', 'P1', new Date(now - 600000),
  new Date(now - 300000), new Date(now + 3600000), 'ACTIVE', 'robot-date',
  new Date(),
]);
const coercedDashboard = context.getDashboardData(dashboardKey, {
  session_id: '23092026',
});
assert.strictEqual(coercedDashboard.selected_session.session_id, '23092026');
assert.strictEqual(context.recordManualCorrection(dashboardKey, {
  session_id: '23092026', student_id: '3', status: 'EXCUSED',
  actor: 'Teacher A', reason: 'Approved numeric-session correction',
}).students[0].status, 'EXCUSED');
assert.strictEqual(context.updateClassSessionStatus(dashboardKey, {
  session_id: '23092026', status: 'CLOSED', actor: 'Teacher A',
  reason: 'Manual close of numeric session',
}).selected_session.status, 'CLOSED');

// Dashboard access closes an expired ACTIVE session immediately; the trigger
// provides the same sweep even when nobody has the dashboard open.
sheets.Sessions.appendRow([
  'AUTO-CLOSE', '11A1', '2026-09-23', 'P0', new Date(now - 7200000),
  new Date(now - 6900000), new Date(now - 3600000), 'ACTIVE', 'robot-auto',
  new Date(),
]);
const flushBeforeAutoClose = flushCount;
const autoClosedDashboard = context.getDashboardData(dashboardKey, {
  session_id: 'AUTO-CLOSE',
});
assert.strictEqual(autoClosedDashboard.selected_session.status, 'CLOSED');
assert.ok(flushCount > flushBeforeAutoClose);
assert.strictEqual(sheets.AuditLog.rows.at(-1)[7], 'Automatically closed at Ends At');
assert.strictEqual(context.installPhase2Automation().ok, true);
assert.strictEqual(context.installPhase2Automation().ok, true);
assert.strictEqual(projectTriggers.length, 3);
assert.deepStrictEqual(
  projectTriggers.map((trigger) => [
    trigger.getHandlerFunction(), trigger.minutes,
  ]).sort(),
  [['closeExpiredSessions', 5], ['refreshDeviceStatuses', 1],
    ['sendClosedSessionReports', 1]],
);

const flushBeforeReadOnlyDashboard = flushCount;
const devicesReadBeforeDashboard = sheets.Devices.readCount;
const historicalDashboard = context.getDashboardData(dashboardKey, {
  session_id: '10A1-20260915-P1',
});
assert.strictEqual(flushCount, flushBeforeReadOnlyDashboard);
assert.strictEqual(sheets.Devices.readCount - devicesReadBeforeDashboard, 1);
assert.strictEqual(historicalDashboard.summary.total, 2);
assert.strictEqual(historicalDashboard.summary.late, 1);
assert.strictEqual(historicalDashboard.summary.absent, 1);
assert.strictEqual(historicalDashboard.students[0].status, 'LATE');
assert.strictEqual(historicalDashboard.students[1].status, 'ABSENT');
assert.strictEqual(historicalDashboard.devices.length, 1);
assert.strictEqual(historicalDashboard.devices[0].device_id, 'robot-01');
assert.strictEqual(historicalDashboard.devices[0].status, 'ONLINE');
assert.strictEqual(historicalDashboard.devices[0].sync_latency_ms, 410);
assert.strictEqual(historicalDashboard.devices[0].secret_hash, undefined);

const attendanceRowsBeforeCorrection = sheets.Attendance.rows.length;
const auditRowsBeforeCorrection = sheets.AuditLog.rows.length;
const correctedDashboard = context.recordManualCorrection(dashboardKey, {
  session_id: '10A1-20260915-P1',
  student_id: '2',
  status: 'EXCUSED',
  actor: 'Teacher A',
  reason: 'Approved absence',
});
assert.strictEqual(sheets.Attendance.rows.length, attendanceRowsBeforeCorrection);
assert.strictEqual(sheets.AuditLog.rows.length, auditRowsBeforeCorrection + 1);
assert.strictEqual(correctedDashboard.summary.absent, 0);
assert.strictEqual(correctedDashboard.summary.excused, 1);
assert.strictEqual(sheets.AuditLog.rows.at(-1)[2], 'ATTENDANCE_OVERRIDE');

// Text-entered classes work when active students exist, even without a row
// in Classes. A disabled or unknown class must not create an empty session.
sheets.Students.appendRow([4, 'Student 3A', '3A', true, new Date(), new Date()]);
assert.deepStrictEqual(Array.from(context.getDashboardData(dashboardKey, {}).classes),
  ['10A1', '3A']);
const manualClassInput = {
  session_id: '3A-MANUAL', class_id: '3A', period: 'Sáng',
  device_id: 'robot-03', report_email: 'teacher@example.com',
  starts_at: new Date(now + 7200000).toISOString(),
  late_after: new Date(now + 7500000).toISOString(),
  ends_at: new Date(now + 10800000).toISOString(),
};
assert.strictEqual(context.createClassSession(dashboardKey, manualClassInput)
  .selected_session.class_id, '3A');
assert.strictEqual(context.updateClassSessionStatus(dashboardKey, {
  session_id: '3A-MANUAL', status: 'CANCELLED',
}).selected_session.status, 'CANCELLED');
sheets.Students.rows.pop();
assert.throws(() => context.createClassSession(dashboardKey, {
  ...manualClassInput, session_id: 'UNKNOWN-CLASS', class_id: '99Z',
}), /class_without_students/);
assert.throws(() => context.createClassSession(dashboardKey, {
  ...manualClassInput, session_id: 'DISABLED-CLASS', class_id: '11A1',
}), /class_without_students/);

assert.throws(() => context.createClassSession(dashboardKey, {
  session_id: '10A1-NEW-CONFLICT', class_id: '10A1', period: 'P3',
  device_id: 'robot-01', actor: 'Teacher A',
  report_email: 'teacher@example.com',
  starts_at: new Date(now + 7200000).toISOString(),
  late_after: new Date(now + 7500000).toISOString(),
  ends_at: new Date(now + 10800000).toISOString(),
}), /device_has_active_session/);

const closedDashboard = context.updateClassSessionStatus(dashboardKey, {
  session_id: '10A1-ACTIVE', status: 'CLOSED', actor: 'Teacher A',
  reason: 'Class finished',
});
assert.strictEqual(closedDashboard.selected_session.status, 'CLOSED');
assert.strictEqual(sentMail.length, 0, 'legacy session must not send mail');
assert.throws(() => context.createClassSession(dashboardKey, {
  session_id: 'INVALID-MAIL', class_id: '10A1', period: 'Sáng',
  device_id: 'robot-01', report_email: 'one@example.com, two@example.com',
  starts_at: new Date(now + 7200000).toISOString(),
  late_after: new Date(now + 7500000).toISOString(),
  ends_at: new Date(now + 10800000).toISOString(),
}), /invalid_report_email/);
const createdDashboard = context.createClassSession(dashboardKey, {
  session_id: '10A1-NEW', class_id: '10A1', period: 'Sáng',
  device_id: 'robot-01', actor: 'Teacher A',
  report_email: 'teacher@example.com',
  starts_at: new Date(now + 7200000).toISOString(),
  late_after: new Date(now + 7500000).toISOString(),
  ends_at: new Date(now + 10800000).toISOString(),
});
assert.strictEqual(createdDashboard.selected_session.session_id, '10A1-NEW');
assert.strictEqual(createdDashboard.selected_session.period, 'Sáng');
assert.strictEqual(createdDashboard.selected_session.status, 'ACTIVE');
assert.strictEqual(createdDashboard.selected_session.report_email, 'teacher@example.com');
assert.match(createdDashboard.selected_session.date, /^\d{4}-\d{2}-\d{2}$/);
assert.strictEqual(sheets.AuditLog.rows.at(-1)[2], 'SESSION_CREATE');
assert.strictEqual(sheets.AuditLog.rows.at(-1)[1], 'Giáo viên mẫu');
context.recordManualCorrection(dashboardKey, {
  session_id: '10A1-NEW', student_id: '2', status: 'EXCUSED',
  reason: 'Có giấy xin phép',
});
const queued = context.updateClassSessionStatus(dashboardKey, {
  session_id: '10A1-NEW', status: 'CLOSED', reason: 'Class finished',
});
assert.strictEqual(queued.selected_session.report_status, 'PENDING');
assert.strictEqual(context.sendClosedSessionReports().processed_count, 1);
assert.strictEqual(sentMail.length, 1);
assert.strictEqual(sentMail[0].to, 'teacher@example.com');
assert.strictEqual(sentMail[0].attachments[0].name, '10A1-NEW.csv');
assert.match(sentMail[0].attachments[0].body, /"Reason","Timestamp"/);
assert.match(sentMail[0].attachments[0].body, /"Có phép","Có giấy xin phép"/);
assert.ok(sentMail[0].attachments[0].body.startsWith('\ufeff'));
assert.strictEqual(context.getDashboardData(dashboardKey, {
  session_id: '10A1-NEW',
}).selected_session.report_status, 'SENT');
assert.strictEqual(context.sendClosedSessionReports().processed_count, 0);
assert.strictEqual(sentMail.length, 1, 'sent report must not be repeated');
assert.strictEqual(sheets.AuditLog.rows.at(-1)[2], 'SESSION_REPORT_SENT');
const csvInjection = context.sessionReportCsv({
  selected_session: {session_id: 'TEST-CSV'},
  students: [{student_id: '4', name: '=2+2', class_id: '10A1',
    status: 'ABSENT', override: {reason: '+unsafe'}, timestamp: '',
    event_id: '', confidence_milli: null}],
});
assert.match(csvInjection, /"'=2\+2"/);
assert.match(csvInjection, /"'\+unsafe"/);
sheets.Sessions.appendRow([
  'CANCELLED-MAIL', '10A1', '2026-09-21', 'Chiều', new Date(now - 3600000),
  new Date(now - 3000000), new Date(now + 3600000), 'CANCELLED',
  'robot-cancelled', new Date(), 'teacher@example.com', 'PENDING', '', '', 0, '',
]);
assert.strictEqual(context.sendClosedSessionReports().processed_count, 0);
sheets.Sessions.rows.pop();
sheets.Sessions.appendRow([
  'UNCERTAIN-MAIL', '10A1', '2026-09-21', 'Chiều', new Date(now - 3600000),
  new Date(now - 3000000), new Date(now - 1000), 'CLOSED',
  'robot-uncertain', new Date(), 'teacher@example.com', 'SENDING', '', '', 1, '',
]);
assert.strictEqual(context.sendClosedSessionReports().processed_count, 0);
sheets.Sessions.rows.pop();

// A transient send failure remains visible and is retried after backoff.
sheets.Sessions.appendRow([
  'MAIL-RETRY', '10A1', '2026-09-21', 'Chiều', new Date(now - 3600000),
  new Date(now - 3000000), new Date(now - 1000), 'ACTIVE', 'robot-retry',
  new Date(), 'retry@example.com', '', '', '', 0, '',
]);
assert.ok(context.closeExpiredSessions().session_ids.includes('MAIL-RETRY'));
mailFailure = true;
assert.strictEqual(context.sendClosedSessionReports().processed_count, 1);
mailFailure = false;
const retryRow = sheets.Sessions.rows.at(-1);
assert.strictEqual(retryRow[11], 'FAILED');
assert.match(retryRow[13], /Temporary mail failure/);
assert.strictEqual(context.sendClosedSessionReports().processed_count, 0);
retryRow[15] = new Date(now - 1000);
assert.strictEqual(context.sendClosedSessionReports().processed_count, 1);
assert.strictEqual(retryRow[11], 'SENT');
assert.strictEqual(sentMail.length, 2);

assert.strictEqual(post({...validPayload, api_version: 99}).error,
                   'unsupported_api_version');
assert.strictEqual(post({...validPayload, api_version: 2, type: 'unknown'}).error,
                   'unknown_request_type');
assert.strictEqual(post({
  ...validPayload,
  token: rotatedToken,
  event_id: '=formula',
}).error, 'invalid_schema');

lockAvailable = false;
assert.strictEqual(post({...validPayload, token: rotatedToken, event_id: 'busy-event'}).error,
                   'busy');
lockAvailable = true;

// A staging sheet migrated by an earlier draft had 12 Device columns.
// P2.4 appends secret rotation and health columns without rewriting old data.
sheets.Devices.rows.forEach((row) => row.splice(12));
// A pre-email deployment has ten Session columns; migration appends fields.
sheets.Sessions.rows.forEach((row) => row.splice(10));
assert.strictEqual(context.migrateToV2().ok, true);
assert.strictEqual(sheets.Sessions.rows[0].length, 16);
assert.strictEqual(sheets.Sessions.rows[0][10], 'Report Email');
assert.strictEqual(sheets.Devices.rows[0].length, 16);
assert.strictEqual(sheets.Devices.rows[0][12], 'Secret Rotated At');
assert.strictEqual(sheets.Devices.rows[0][13], 'Status');
assert.strictEqual(sheets.Devices.rows[0][14], 'Sync Latency (ms)');
assert.strictEqual(sheets.Devices.rows[0][15], 'Consecutive Failures');

const rowCounts = Object.fromEntries(
  Object.entries(sheets).map(([name, sheet]) => [name, sheet.rows.length]),
);
assert.strictEqual(context.migrateToV2().ok, true);
assert.deepStrictEqual(
  Object.fromEntries(
    Object.entries(sheets).map(([name, sheet]) => [name, sheet.rows.length]),
  ),
  rowCounts,
);

const originalStudentsHeader = sheets.Students.rows[0][0];
const attendanceTails = sheets.Attendance.rows.map((row) => row.splice(9));
sheets.Students.rows[0][0] = 'Unexpected Header';
assert.throws(() => context.migrateToV2(), /Schema mismatch in sheet Students/);
assert.strictEqual(sheets.Attendance.rows[0].length, 9);
sheets.Students.rows[0][0] = originalStudentsHeader;
sheets.Attendance.rows.forEach((row, index) => row.push(...attendanceTails[index]));

assert.ok(flushCount > 0);
scriptProperties.BOOTSTRAP_TEACHER_USERNAME = 'Giáo viên mẫu';
scriptProperties.BOOTSTRAP_TEACHER_PASSWORD = 'replacement-test-password';
context.provisionDashboardAccountFromProperties();
assert.throws(() => context.getDashboardData(dashboardKey, {}),
              /dashboard_unauthorized/);
assert.strictEqual(context.loginDashboard('Giáo viên mẫu',
  'replacement-test-password').teacher_name, 'Giáo viên mẫu');
assert.strictEqual(context.logoutDashboard(dashboardKey).ok, true);
assert.throws(() => context.getDashboardData(dashboardKey, {}),
              /dashboard_unauthorized/);
console.log('Apps Script API v2 tests: PASS');
