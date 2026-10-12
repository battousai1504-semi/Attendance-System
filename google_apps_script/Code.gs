const SHEET_NAME = 'Attendance';
const API_VERSION = 2;
const LEGACY_API_VERSION = 'phase1-v2';
const SCHEMA_VERSION = '2';
const ATTENDANCE_STATUSES = ['PRESENT', 'LATE', 'ABSENT', 'EXCUSED'];
const DEVICE_OFFLINE_AFTER_SECONDS = 180;
const REPORT_BATCH_SIZE = 1;
const HEADERS = [
  'Received At',
  'Event ID',
  'Device ID',
  'Student ID',
  'Name',
  'Class',
  'Device Timestamp',
  'Confidence (milli)',
  'Method',
];
const V2_SCHEMAS = {
  Attendance: HEADERS.concat([
    'Session ID', 'Firmware Version', 'API Version',
  ]),
  Students: [
    'Student ID', 'Name', 'Class', 'Active', 'Created At', 'Updated At',
  ],
  Classes: [
    'Class ID', 'Display Name', 'Active', 'Created At', 'Updated At',
  ],
  Sessions: [
    'Session ID', 'Class ID', 'Date', 'Period', 'Starts At', 'Late After',
    'Ends At', 'Status', 'Device ID', 'Created At', 'Report Email',
    'Report Status', 'Report Sent At', 'Report Error', 'Report Attempts',
    'Report Next Retry At',
  ],
  Devices: [
    'Device ID', 'Enabled', 'Secret Hash', 'Last Seen', 'Firmware Version',
    'RSSI', 'Free Heap', 'Free PSRAM', 'Pending Events', 'Uptime (s)',
    'Last Error', 'Updated At', 'Secret Rotated At', 'Status',
    'Sync Latency (ms)', 'Consecutive Failures',
  ],
  AuditLog: [
    'Timestamp', 'Actor', 'Action', 'Entity Type', 'Entity ID', 'Before JSON',
    'After JSON', 'Reason',
  ],
};

class ApiError extends Error {
  constructor(code, retryable) {
    super(code);
    this.code = code;
    this.retryable = Boolean(retryable);
  }
}

function jsonResponse(value) {
  return ContentService
    .createTextOutput(JSON.stringify(value))
    .setMimeType(ContentService.MimeType.JSON);
}

function successResponse(value) {
  return jsonResponse(Object.assign({
    ok: true,
    retryable: false,
    api_version: API_VERSION,
  }, value || {}));
}

function errorResponse(code, retryable) {
  return jsonResponse({
    ok: false,
    error: code,
    retryable: Boolean(retryable),
    api_version: API_VERSION,
  });
}

function getConfiguredSpreadsheet() {
  const properties = PropertiesService.getScriptProperties();
  const spreadsheetId = properties.getProperty('SPREADSHEET_ID');
  const spreadsheet = spreadsheetId
    ? SpreadsheetApp.openById(spreadsheetId)
    : SpreadsheetApp.getActiveSpreadsheet();
  if (!spreadsheet) throw new Error('Set SPREADSHEET_ID in Script Properties');
  return spreadsheet;
}

function getAttendanceSheet() {
  const spreadsheet = getConfiguredSpreadsheet();
  let sheet = spreadsheet.getSheetByName(SHEET_NAME);
  if (!sheet) sheet = spreadsheet.insertSheet(SHEET_NAME);
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);
    sheet.setFrozenRows(1);
    sheet.getRange(1, 1, 1, HEADERS.length).setFontWeight('bold');
  } else {
    const actual = sheet.getRange(1, 1, 1, HEADERS.length).getValues()[0];
    if (HEADERS.some((header, index) => actual[index] !== header)) {
      throw new Error('Attendance sheet headers do not match the expected schema');
    }
  }
  return sheet;
}

function planV2Sheet(spreadsheet, sheetName, expectedHeaders) {
  const sheet = spreadsheet.getSheetByName(sheetName);
  if (!sheet) return {sheet_name: sheetName, action: 'create', prefix: 0};
  if (sheet.getLastRow() === 0) {
    return {sheet_name: sheetName, action: 'initialize', prefix: 0};
  }

  const lastColumn = sheet.getLastColumn();
  const actual = sheet.getRange(1, 1, 1, lastColumn).getValues()[0];
  const allowedPrefixLengths = sheetName === SHEET_NAME ? [HEADERS.length] :
    (sheetName === 'Devices' ? [12, 13] :
      (sheetName === 'Sessions' ? [10] : []));
  const allowedPrefixLength = allowedPrefixLengths.find((length) => {
    return lastColumn === length && expectedHeaders.slice(0, length)
      .every((header, index) => actual[index] === header);
  });
  if (allowedPrefixLength !== undefined) {
    return {
      sheet_name: sheetName,
      action: 'append_headers',
      prefix: allowedPrefixLength,
    };
  }
  if (lastColumn !== expectedHeaders.length ||
      expectedHeaders.some((header, index) => actual[index] !== header)) {
    throw new Error('Schema mismatch in sheet ' + sheetName);
  }
  return {sheet_name: sheetName, action: 'ready', prefix: lastColumn};
}

function applyV2SheetPlan(spreadsheet, plan, expectedHeaders) {
  let sheet = spreadsheet.getSheetByName(plan.sheet_name);
  if (plan.action === 'create') sheet = spreadsheet.insertSheet(plan.sheet_name);
  if (plan.action === 'create' || plan.action === 'initialize') {
    sheet.appendRow(expectedHeaders);
    sheet.setFrozenRows(1);
    sheet.getRange(1, 1, 1, expectedHeaders.length).setFontWeight('bold');
  } else if (plan.action === 'append_headers') {
    const appended = expectedHeaders.slice(plan.prefix);
    sheet.getRange(1, plan.prefix + 1, 1, appended.length)
      .setValues([appended])
      .setFontWeight('bold');
  }
}

function migrateToV2() {
  const lock = LockService.getScriptLock();
  if (!lock.tryLock(30000)) throw new Error('Could not acquire migration lock');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    const plans = Object.keys(V2_SCHEMAS).map((sheetName) => {
      return planV2Sheet(spreadsheet, sheetName, V2_SCHEMAS[sheetName]);
    });
    plans.forEach((plan) => {
      applyV2SheetPlan(spreadsheet, plan, V2_SCHEMAS[plan.sheet_name]);
    });
    const repairedSessionIds = normalizeSessionIdColumn(
      spreadsheet.getSheetByName('Sessions'),
    );
    const repairedAttendanceSessionIds = normalizeAttendanceSessionIds(
      spreadsheet.getSheetByName('Attendance'),
    );
    const properties = PropertiesService.getScriptProperties();
    properties.setProperty('SCHEMA_VERSION', SCHEMA_VERSION);
    if (!properties.getProperty('ALLOW_LEGACY_TOKEN')) {
      properties.setProperty('ALLOW_LEGACY_TOKEN', 'true');
    }
    SpreadsheetApp.flush();
    const result = {
      ok: true,
      schema_version: SCHEMA_VERSION,
      sheets: Object.keys(V2_SCHEMAS),
      repaired_session_ids: repairedSessionIds,
      repaired_attendance_session_ids: repairedAttendanceSessionIds,
    };
    console.log(JSON.stringify(result));
    return result;
  } finally {
    lock.releaseLock();
  }
}

function requiredText(payload, key, maxLength) {
  const value = String(payload[key] == null ? '' : payload[key]).trim();
  if (!value || value.length > maxLength) throw new ApiError('invalid_schema', false);
  return value;
}

function optionalText(payload, key, maxLength) {
  const value = String(payload[key] == null ? '' : payload[key]).trim();
  if (value.length > maxLength) throw new ApiError('invalid_schema', false);
  return value;
}

function integerField(payload, key, minimum, maximum) {
  const value = Number(payload[key]);
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new ApiError('invalid_schema', false);
  }
  return value;
}

function optionalIntegerField(payload, key, minimum, maximum) {
  if (payload[key] === undefined || payload[key] === null || payload[key] === '') {
    return undefined;
  }
  return integerField(payload, key, minimum, maximum);
}

function safeSheetText(value) {
  const text = String(value == null ? '' : value);
  return /^[=+\-@]/.test(text) ? "'" + text : text;
}

function findExactRow(sheet, column, value) {
  if (!sheet || sheet.getLastRow() < 2) return 0;
  const match = sheet.getRange(2, column, sheet.getLastRow() - 1, 1)
    .createTextFinder(String(value))
    .matchEntireCell(true)
    .findNext();
  return match ? match.getRow() : 0;
}

function canonicalSessionId(value) {
  if (value instanceof Date && !isNaN(value.getTime())) {
    return Utilities.formatDate(value, 'Asia/Ho_Chi_Minh', 'ddMMyyyy');
  }
  return String(value == null ? '' : value).replace(/^'/, '').trim();
}

function findSessionRow(sheet, sessionId) {
  if (!sheet || sheet.getLastRow() < 2) return 0;
  const expected = canonicalSessionId(sessionId);
  const values = sheet.getRange(2, 1, sheet.getLastRow() - 1, 1).getValues();
  for (let index = 0; index < values.length; index++) {
    if (canonicalSessionId(values[index][0]) === expected) return index + 2;
  }
  return 0;
}

function normalizeSessionIdColumn(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return 0;
  const range = sheet.getRange(2, 1, sheet.getLastRow() - 1, 1);
  const values = range.getValues();
  const seen = {};
  let repaired = 0;
  values.forEach((row) => {
    const original = row[0];
    const sessionId = canonicalSessionId(original);
    if (!sessionId || !/^[A-Za-z0-9._:-]+$/.test(sessionId)) {
      throw new Error('Invalid Session ID in Sessions sheet: ' + String(original));
    }
    if (seen[sessionId]) throw new Error('Duplicate Session ID: ' + sessionId);
    seen[sessionId] = true;
    if (original instanceof Date || String(original) !== sessionId) repaired++;
    row[0] = sessionId;
  });
  range.setNumberFormat('@').setValues(values);
  return repaired;
}

function normalizeAttendanceSessionIds(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return 0;
  const range = sheet.getRange(2, 10, sheet.getLastRow() - 1, 1);
  const values = range.getValues();
  let repaired = 0;
  values.forEach((row) => {
    if (row[0] === '') return;
    const original = row[0];
    const sessionId = canonicalSessionId(original);
    if (original instanceof Date || String(original) !== sessionId) repaired++;
    row[0] = sessionId;
  });
  range.setNumberFormat('@').setValues(values);
  return repaired;
}

function appendSessionRow(sheet, values) {
  const rowNumber = sheet.getLastRow() + 1;
  const sessionId = canonicalSessionId(values[0]);
  sheet.getRange(rowNumber, 1).setNumberFormat('@').setValue(sessionId);
  sheet.getRange(rowNumber, 2, 1, values.length - 1)
    .setValues([values.slice(1)]);
  return rowNumber;
}

function appendAuditRow(sheet, values) {
  const rowNumber = sheet.getLastRow() + 1;
  sheet.getRange(rowNumber, 5).setNumberFormat('@');
  sheet.getRange(rowNumber, 1, 1, values.length).setValues([values]);
  return rowNumber;
}

function hashSecret(secret) {
  const bytes = Utilities.computeDigest(
    Utilities.DigestAlgorithm.SHA_256,
    String(secret),
    Utilities.Charset.UTF_8,
  );
  return bytes.map((value) => {
    return ((value + 256) % 256).toString(16).padStart(2, '0');
  }).join('');
}

function constantTimeEqual(left, right) {
  const a = String(left || '');
  const b = String(right || '');
  let different = a.length ^ b.length;
  const length = Math.max(a.length, b.length);
  for (let index = 0; index < length; index++) {
    different |= (a.charCodeAt(index % Math.max(a.length, 1)) || 0) ^
      (b.charCodeAt(index % Math.max(b.length, 1)) || 0);
  }
  return different === 0;
}

function enabledValue(value) {
  return value === true || value === 1 || String(value).toUpperCase() === 'TRUE';
}

function requireV2Schema() {
  const version = PropertiesService.getScriptProperties().getProperty('SCHEMA_VERSION');
  if (version !== SCHEMA_VERSION) throw new ApiError('server_not_configured', false);
}

function authenticateDevice(payload, spreadsheet) {
  const deviceId = requiredText(payload, 'device_id', 40);
  if (!/^[A-Za-z0-9._:-]+$/.test(deviceId)) {
    throw new ApiError('invalid_schema', false);
  }
  const token = String(payload.token || '');
  if (token.length < 32 || token.length > 127) {
    throw new ApiError('unauthorized', false);
  }

  const devices = spreadsheet.getSheetByName('Devices');
  const rowNumber = findExactRow(devices, 1, deviceId);
  if (rowNumber) {
    const row = devices.getRange(rowNumber, 1, 1, V2_SCHEMAS.Devices.length)
      .getValues()[0];
    if (!enabledValue(row[1])) throw new ApiError('device_disabled', false);
    if (!constantTimeEqual(hashSecret(token), row[2])) {
      throw new ApiError('unauthorized', false);
    }
    return {device_id: deviceId, token: token, row: rowNumber};
  }

  const properties = PropertiesService.getScriptProperties();
  const legacyAllowed = properties.getProperty('ALLOW_LEGACY_TOKEN') !== 'false';
  const legacyToken = properties.getProperty('API_TOKEN') || '';
  if (!legacyAllowed || legacyToken.length < 32 ||
      !constantTimeEqual(token, legacyToken)) {
    throw new ApiError('unauthorized', false);
  }
  return {device_id: deviceId, token: token, row: 0};
}

function updateDevice(spreadsheet, auth, values) {
  const sheet = spreadsheet.getSheetByName('Devices');
  const now = new Date();
  let row;
  if (auth.row) {
    row = sheet.getRange(auth.row, 1, 1, V2_SCHEMAS.Devices.length)
      .getValues()[0];
  } else {
    row = [auth.device_id, true, hashSecret(auth.token), '', '', '', '', '', '',
      '', '', '', now, 'ONLINE', '', ''];
    sheet.appendRow(row);
    auth.row = sheet.getLastRow();
  }
  row[3] = now;
  if (values.firmware_version !== undefined) row[4] = safeSheetText(values.firmware_version);
  if (values.rssi !== undefined) row[5] = values.rssi;
  if (values.free_heap !== undefined) row[6] = values.free_heap;
  if (values.free_psram !== undefined) row[7] = values.free_psram;
  if (values.pending_events !== undefined) row[8] = values.pending_events;
  if (values.uptime_seconds !== undefined) row[9] = values.uptime_seconds;
  if (values.last_error !== undefined) row[10] = safeSheetText(values.last_error);
  row[11] = now;
  row[13] = enabledValue(row[1]) ? 'ONLINE' : 'DISABLED';
  if (values.sync_latency_ms !== undefined) row[14] = values.sync_latency_ms;
  if (values.consecutive_failures !== undefined) {
    row[15] = values.consecutive_failures;
  }
  sheet.getRange(auth.row, 1, 1, V2_SCHEMAS.Devices.length).setValues([row]);
}

function normalizedApiVersion(payload) {
  const value = payload.api_version;
  if (value == null || value === '' || value === LEGACY_API_VERSION || value === 1) {
    return 1;
  }
  if (String(value) === String(API_VERSION)) return API_VERSION;
  throw new ApiError('unsupported_api_version', false);
}

function parseDeviceTimestamp(text) {
  if (!/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(text)) {
    throw new ApiError('invalid_schema', false);
  }
  const value = new Date(text.replace(' ', 'T') + '+07:00');
  if (isNaN(value.getTime())) throw new ApiError('invalid_schema', false);
  return value;
}

function normalizedDeviceTimestamp(value) {
  if (value instanceof Date) {
    return Utilities.formatDate(value, 'Asia/Ho_Chi_Minh', 'yyyy-MM-dd HH:mm:ss');
  }
  return String(value == null ? '' : value).trim();
}

function attendanceCellDate(value) {
  if (value instanceof Date) return value;
  return parseDeviceTimestamp(normalizedDeviceTimestamp(value));
}

function asDate(value) {
  if (value instanceof Date) return value;
  const date = new Date(value);
  return isNaN(date.getTime()) ? null : date;
}

function matchingSessions(spreadsheet, className, deviceId, eventTime, activeOnly) {
  const sheet = spreadsheet.getSheetByName('Sessions');
  if (!sheet || sheet.getLastRow() < 2) return [];
  return sheet.getRange(2, 1, sheet.getLastRow() - 1, V2_SCHEMAS.Sessions.length)
    .getValues()
    .filter((row) => {
      const status = String(row[7] || '').toUpperCase();
      if (activeOnly ? status !== 'ACTIVE' : !['ACTIVE', 'CLOSED'].includes(status)) {
        return false;
      }
      if (className && String(row[1]) !== className) return false;
      if (row[8] && String(row[8]) !== deviceId) return false;
      if (!eventTime) return true;
      const start = asDate(row[4]);
      const end = asDate(row[6]);
      return start && end && eventTime >= start && eventTime <= end;
    });
}

function resolveAttendanceSession(spreadsheet, requestedId, className, deviceId, eventTime) {
  const sheet = spreadsheet.getSheetByName('Sessions');
  if (requestedId) {
    const rowNumber = findSessionRow(sheet, requestedId);
    if (!rowNumber) throw new ApiError('invalid_session', false);
    const row = sheet.getRange(rowNumber, 1, 1, V2_SCHEMAS.Sessions.length)
      .getValues()[0];
    if (String(row[1]) !== className || (row[8] && String(row[8]) !== deviceId)) {
      throw new ApiError('invalid_session', false);
    }
    const start = asDate(row[4]);
    const end = asDate(row[6]);
    if (!start || !end || eventTime < start || eventTime > end ||
        !['ACTIVE', 'CLOSED'].includes(String(row[7] || '').toUpperCase())) {
      throw new ApiError('invalid_session', false);
    }
    return canonicalSessionId(row[0]);
  }
  if (!className) return '';
  const matches = matchingSessions(spreadsheet, className, deviceId, eventTime, false);
  if (matches.length > 1) throw new ApiError('session_conflict', false);
  return matches.length === 1 ? canonicalSessionId(matches[0][0]) : '';
}

function handleAttendance(payload, auth, spreadsheet, requestVersion) {
  const eventId = requiredText(payload, 'event_id', 64);
  const name = requiredText(payload, 'name', 64);
  const className = optionalText(payload, 'class', 32);
  const timestamp = requiredText(payload, 'timestamp', 32);
  const method = requiredText(payload, 'method', 16);
  const studentId = integerField(payload, 'student_id', 1, 65535);
  const confidence = integerField(payload, 'confidence_milli', 0, 1000);
  const firmwareVersion = optionalText(payload, 'firmware_version', 32);
  const deviceValues = firmwareVersion ? {firmware_version: firmwareVersion} : {};
  const requestedSession = optionalText(payload, 'session_id', 64);
  if (!/^[A-Za-z0-9._:-]+$/.test(eventId) || method !== 'FACE') {
    throw new ApiError('invalid_schema', false);
  }
  const eventTime = parseDeviceTimestamp(timestamp);
  const sheet = spreadsheet.getSheetByName(SHEET_NAME);
  const existingRow = findExactRow(sheet, 2, eventId);
  if (existingRow) {
    const existing = sheet.getRange(existingRow, 1, 1, V2_SCHEMAS.Attendance.length)
      .getValues()[0];
    const existingName = String(existing[4] || '').replace(/^'/, '');
    const existingClass = String(existing[5] || '').replace(/^'/, '');
    if (String(existing[2]) !== auth.device_id || Number(existing[3]) !== studentId ||
        existingName !== name || existingClass !== className ||
        normalizedDeviceTimestamp(existing[6]) !== timestamp ||
        String(existing[8]) !== method) {
      throw new ApiError('event_conflict', false);
    }
    updateDevice(spreadsheet, auth, deviceValues);
    return successResponse({
      duplicate: true,
      event_id: eventId,
      session_id: canonicalSessionId(existing[9]),
    });
  }

  const sessionId = resolveAttendanceSession(
    spreadsheet, requestedSession, className, auth.device_id, eventTime,
  );

  const attendanceRow = [
    new Date(), eventId, auth.device_id, studentId, safeSheetText(name),
    safeSheetText(className), timestamp, confidence, method,
    safeSheetText(sessionId), safeSheetText(firmwareVersion), requestVersion,
  ];
  const attendanceRowNumber = sheet.getLastRow() + 1;
  sheet.getRange(attendanceRowNumber, 10).setNumberFormat('@');
  sheet.getRange(attendanceRowNumber, 1, 1, attendanceRow.length)
    .setValues([attendanceRow]);
  updateDevice(spreadsheet, auth, deviceValues);
  SpreadsheetApp.flush();
  return successResponse({
    duplicate: false,
    event_id: eventId,
    session_id: sessionId,
  });
}

function handleHeartbeat(payload, auth, spreadsheet) {
  const firmwareVersion = requiredText(payload, 'firmware_version', 32);
  const values = {
    firmware_version: firmwareVersion,
    uptime_seconds: integerField(payload, 'uptime_seconds', 0, 2147483647),
    rssi: integerField(payload, 'rssi', -127, 0),
    free_heap: integerField(payload, 'free_heap', 0, 2147483647),
    free_psram: integerField(payload, 'free_psram', 0, 2147483647),
    pending_events: integerField(payload, 'pending_events', 0, 1000000),
    sync_latency_ms: optionalIntegerField(
      payload, 'sync_latency_ms', 0, 3600000,
    ),
    consecutive_failures: optionalIntegerField(
      payload, 'consecutive_failures', 0, 2147483647,
    ),
    last_error: optionalText(payload, 'last_error', 160),
  };
  updateDevice(spreadsheet, auth, values);
  SpreadsheetApp.flush();
  return successResponse({
    device_id: auth.device_id,
    server_time: new Date().toISOString(),
  });
}

function handleOtaManifest(payload, auth, spreadsheet) {
  const firmwareVersion = requiredText(payload, 'firmware_version', 32);
  updateDevice(spreadsheet, auth, {firmware_version: firmwareVersion});
  SpreadsheetApp.flush();

  const properties = PropertiesService.getScriptProperties();
  const enabled = String(properties.getProperty('OTA_ENABLED') || '')
    .toLowerCase() === 'true';
  if (!enabled) {
    return successResponse({
      available: false,
      current_version: firmwareVersion,
    });
  }

  const version = String(properties.getProperty('OTA_VERSION') || '').trim();
  const url = String(properties.getProperty('OTA_URL') || '').trim();
  const sizeText = String(properties.getProperty('OTA_SIZE') || '').trim();
  const sha256 = String(properties.getProperty('OTA_SHA256') || '')
    .trim().toLowerCase();
  const size = Number(sizeText);
  if (!/^[A-Za-z0-9._+-]{1,31}$/.test(version) ||
      !/^https:\/\/[^\s"\\]+$/.test(url) || url.length > 511 ||
      !/^\d+$/.test(sizeText) || !Number.isSafeInteger(size) ||
      size < 1 || size > 0x600000 || !/^[0-9a-f]{64}$/.test(sha256)) {
    throw new ApiError('ota_manifest_invalid', false);
  }
  if (version === firmwareVersion) {
    return successResponse({
      available: false,
      current_version: firmwareVersion,
      target_version: version,
    });
  }
  return successResponse({
    available: true,
    version: version,
    url: url,
    size: size,
    sha256: sha256,
  });
}

function handleSessionSync(payload, auth, spreadsheet) {
  const className = optionalText(payload, 'class', 32);
  const matches = matchingSessions(spreadsheet, className, auth.device_id, new Date(), true);
  if (matches.length > 1) throw new ApiError('session_conflict', false);
  updateDevice(spreadsheet, auth, {});
  SpreadsheetApp.flush();
  if (matches.length === 0) return successResponse({active: false});
  const row = matches[0];
  return successResponse({
    active: true,
    session: {
      session_id: canonicalSessionId(row[0]),
      class: String(row[1]),
      starts_at: asDate(row[4]).toISOString(),
      late_after: asDate(row[5]).toISOString(),
      ends_at: asDate(row[6]).toISOString(),
    },
  });
}

const DASHBOARD_SESSION_SECONDS = 21600;
const DASHBOARD_LOGIN_WINDOW_SECONDS = 900;

function dashboardPasswordHash(salt, password) {
  // Iteration slows offline guessing if Script Properties are ever disclosed.
  let digest = String(salt) + ':' + String(password);
  for (let index = 0; index < 512; index++) digest = hashSecret(digest);
  return digest;
}

function loginDashboard(username, password) {
  const properties = PropertiesService.getScriptProperties();
  const name = String(username || '').trim();
  const secret = String(password || '');
  const cache = CacheService.getScriptCache();
  const limitKey = 'dashboard-login:' + hashSecret(name.toLowerCase()).slice(0, 32);
  const failures = Number(cache.get(limitKey) || 0);
  if (failures >= 5) throw new Error('dashboard_login_limited');
  const configuredName = properties.getProperty('DASHBOARD_USERNAME') || '';
  const salt = properties.getProperty('DASHBOARD_PASSWORD_SALT') || '';
  const expected = properties.getProperty('DASHBOARD_PASSWORD_HASH') || '';
  const valid = name.length <= 80 && name === configuredName && salt && expected &&
    secret.length >= 8 && secret.length <= 127 &&
    constantTimeEqual(dashboardPasswordHash(salt, secret), expected);
  if (!valid) {
    cache.put(limitKey, String(failures + 1), DASHBOARD_LOGIN_WINDOW_SECONDS);
    throw new Error('dashboard_login_failed');
  }
  cache.remove(limitKey);
  const token = Utilities.getUuid() + Utilities.getUuid();
  cache.put('dashboard-session:' + hashSecret(token),
    JSON.stringify({name: name,
      credential_version: properties.getProperty('DASHBOARD_CREDENTIAL_VERSION')}),
    DASHBOARD_SESSION_SECONDS);
  return {token: token, teacher_name: name, expires_in: DASHBOARD_SESSION_SECONDS};
}

function logoutDashboard(token) {
  const value = String(token || '');
  if (value.length === 72) {
    CacheService.getScriptCache().remove('dashboard-session:' + hashSecret(value));
  }
  return {ok: true};
}

function requireDashboardAccess(token) {
  const value = String(token || '');
  const session = value.length === 72
    ? CacheService.getScriptCache().get('dashboard-session:' + hashSecret(value))
    : null;
  let account = null;
  try { account = JSON.parse(session || 'null'); } catch (error) {}
  const properties = PropertiesService.getScriptProperties();
  if (!account || !account.name ||
      account.name !== properties.getProperty('DASHBOARD_USERNAME') ||
      !account.credential_version ||
      account.credential_version !==
        properties.getProperty('DASHBOARD_CREDENTIAL_VERSION')) {
    throw new Error('dashboard_unauthorized');
  }
  return account.name;
}

function cellDateIso(value) {
  const date = asDate(value);
  return date ? date.toISOString() : '';
}

function serializeSession(row) {
  const startsAt = asDate(row[4]);
  const dateValue = asDate(row[2]);
  return {
    session_id: canonicalSessionId(row[0]),
    class_id: String(row[1] || ''),
    date: dateValue ? Utilities.formatDate(
      dateValue, 'Asia/Ho_Chi_Minh', 'yyyy-MM-dd',
    ) : (startsAt ? startsAt.toISOString().slice(0, 10) : String(row[2] || '')),
    period: String(row[3] || ''),
    starts_at: cellDateIso(row[4]),
    late_after: cellDateIso(row[5]),
    ends_at: cellDateIso(row[6]),
    status: String(row[7] || ''),
    device_id: String(row[8] || ''),
    report_email: String(row[10] || ''),
    report_status: String(row[11] || ''),
    report_sent_at: cellDateIso(row[12]),
  };
}

function validReportEmail(value) {
  const email = String(value == null ? '' : value).trim();
  return email.length <= 254 &&
    /^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?)+$/.test(email);
}

const REPORT_LABELS = {
  PRESENT: 'Có mặt', LATE: 'Đi muộn', ABSENT: 'Vắng', EXCUSED: 'Có phép',
};

function reportCsvCell(value) {
  const text = String(value == null ? '' : value);
  const safe = /^\s*[=+@-]/.test(text) ? "'" + text : text;
  return '"' + safe.replace(/"/g, '""') + '"';
}

function sessionReportCsv(data) {
  const sessionId = data.selected_session.session_id;
  const rows = [['Session ID', 'Student ID', 'Name', 'Class', 'Status', 'Reason',
    'Timestamp', 'Event ID', 'Confidence']];
  data.students.forEach((student) => rows.push([
    sessionId, student.student_id, student.name, student.class_id,
    REPORT_LABELS[student.status],
    student.override ? student.override.reason || '' : '',
    student.timestamp, student.event_id,
    student.confidence_milli == null ? '' : student.confidence_milli,
  ]));
  return '\ufeff' + rows.map((row) => row.map(reportCsvCell).join(','))
    .join('\r\n');
}

/** Sends at most one report per run. SENDING is deliberately not auto-retried:
 * a crash after MailApp accepts a message cannot prove whether it was sent. */
function sendClosedSessionReports() {
  requireV2Schema();
  const lock = LockService.getScriptLock();
  if (!lock.tryLock(30000)) throw new Error('busy');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    const sheet = spreadsheet.getSheetByName('Sessions');
    const rows = allDataRows(sheet, V2_SCHEMAS.Sessions.length);
    const now = new Date();
    let processed = 0;
    rows.forEach((row, index) => {
      if (processed >= REPORT_BATCH_SIZE || String(row[7]) !== 'CLOSED' ||
          !validReportEmail(row[10])) return;
      const status = String(row[11] || '');
      const nextRetry = asDate(row[15]);
      if (!['PENDING', 'FAILED'].includes(status) ||
          (status === 'FAILED' && nextRetry && nextRetry > now)) return;
      processed++;
      const rowNumber = index + 2;
      const sessionId = canonicalSessionId(row[0]);
      const attempts = Number(row[14] || 0) + 1;
      sheet.getRange(rowNumber, 12, 1, 5)
        .setValues([['SENDING', row[12] || '', '', attempts, '']]);
      SpreadsheetApp.flush();
      let mailAccepted = false;
      try {
        if (MailApp.getRemainingDailyQuota() < 1) {
          throw new Error('Email quota exhausted');
        }
        const data = buildDashboardData(spreadsheet, {session_id: sessionId});
        if (!data.selected_session || data.selected_session.session_id !== sessionId) {
          throw new Error('Session report not found');
        }
        const filename = sessionId + '.csv';
        const blob = Utilities.newBlob(sessionReportCsv(data),
          'text/csv; charset=utf-8', filename);
        const classId = String(row[1] || '').replace(/[\r\n]/g, ' ');
        const period = String(row[3] || '').replace(/[\r\n]/g, ' ');
        const schoolDate = Utilities.formatDate(asDate(row[4]),
          'Asia/Ho_Chi_Minh', 'yyyy-MM-dd');
        MailApp.sendEmail({
          to: String(row[10]),
          subject: 'Điểm danh ' + classId + ' - ' + period + ' - ' +
            schoolDate,
          body: 'Buổi học ' + sessionId + ' đã kết thúc. ' +
            'File CSV điểm danh được đính kèm.\n\n' +
            'Nếu có điều chỉnh sau khi email này được gửi, hãy xuất lại CSV ' +
            'trên dashboard để lấy bản mới nhất.',
          attachments: [blob],
          name: 'Hệ thống điểm danh',
        });
        mailAccepted = true;
        sheet.getRange(rowNumber, 12, 1, 5)
          .setValues([['SENT', new Date(), '', attempts, '']]);
        appendAuditRow(spreadsheet.getSheetByName('AuditLog'), [
          new Date(), 'SYSTEM', 'SESSION_REPORT_SENT', 'SESSION',
          safeSheetText(sessionId), '{}', JSON.stringify({to: row[10]}),
          'CSV emailed after session close',
        ]);
      } catch (error) {
        if (mailAccepted) {
          console.error('Email accepted but report status is uncertain for ' + sessionId);
          throw error;
        }
        const delayMinutes = Math.min(360, 5 * Math.pow(2, Math.min(attempts - 1, 7)));
        const retryAt = new Date(Date.now() + delayMinutes * 60000);
        sheet.getRange(rowNumber, 12, 1, 5).setValues([[
          'FAILED', row[12] || '', String(error.message || error).slice(0, 200),
          attempts, retryAt,
        ]]);
        console.error('Report email failed for ' + sessionId + ': ' + error);
      }
      SpreadsheetApp.flush();
    });
    return {ok: true, processed_count: processed};
  } finally {
    lock.releaseLock();
  }
}

function allDataRows(sheet, width) {
  if (!sheet || sheet.getLastRow() < 2) return [];
  return sheet.getRange(2, 1, sheet.getLastRow() - 1, width).getValues();
}

function calculatedDeviceStatus(row, now) {
  if (!enabledValue(row[1])) return 'DISABLED';
  const lastSeen = asDate(row[3]);
  if (!lastSeen) return 'OFFLINE';
  const ageSeconds = Math.max(0, (now.getTime() - lastSeen.getTime()) / 1000);
  return ageSeconds <= DEVICE_OFFLINE_AFTER_SECONDS ? 'ONLINE' : 'OFFLINE';
}

function refreshDeviceStatusesInSpreadsheet(spreadsheet, now, existingRows) {
  const sheet = spreadsheet.getSheetByName('Devices');
  const rows = existingRows || allDataRows(sheet, V2_SCHEMAS.Devices.length);
  let changed = 0;
  rows.forEach((row, index) => {
    const status = calculatedDeviceStatus(row, now);
    if (String(row[13] || '') === status) return;
    sheet.getRange(index + 2, 14).setValue(status);
    row[13] = status;
    changed++;
  });
  return changed;
}

function serializeDevices(spreadsheet, now) {
  const rows = allDataRows(
    spreadsheet.getSheetByName('Devices'), V2_SCHEMAS.Devices.length,
  );
  refreshDeviceStatusesInSpreadsheet(spreadsheet, now, rows);
  return rows.map((row) => {
    return {
      device_id: String(row[0] || ''),
      enabled: enabledValue(row[1]),
      last_seen: cellDateIso(row[3]),
      firmware_version: String(row[4] || ''),
      rssi: row[5] === '' ? null : Number(row[5]),
      free_heap: row[6] === '' ? null : Number(row[6]),
      free_psram: row[7] === '' ? null : Number(row[7]),
      pending_events: row[8] === '' ? null : Number(row[8]),
      uptime_seconds: row[9] === '' ? null : Number(row[9]),
      last_error: String(row[10] || ''),
      status: calculatedDeviceStatus(row, now),
      sync_latency_ms: row[14] === '' ? null : Number(row[14]),
      consecutive_failures: row[15] === '' ? 0 : Number(row[15]),
    };
  }).sort((left, right) => left.device_id.localeCompare(right.device_id));
}

function closeExpiredSessionsInSpreadsheet(spreadsheet, now, actor) {
  const sheet = spreadsheet.getSheetByName('Sessions');
  const rows = allDataRows(sheet, V2_SCHEMAS.Sessions.length);
  const auditSheet = spreadsheet.getSheetByName('AuditLog');
  const closed = [];
  rows.forEach((row, index) => {
    if (String(row[7] || '').toUpperCase() !== 'ACTIVE') return;
    const endsAt = asDate(row[6]);
    if (!endsAt || endsAt.getTime() > now.getTime()) return;
    const sessionId = canonicalSessionId(row[0]);
    sheet.getRange(index + 2, 8).setValue('CLOSED');
    if (validReportEmail(row[10]) && !row[11]) {
      sheet.getRange(index + 2, 12).setValue('PENDING');
    }
    appendAuditRow(auditSheet, [
      now, safeSheetText(actor || 'SYSTEM'), 'SESSION_STATUS_CHANGE', 'SESSION',
      safeSheetText(sessionId), JSON.stringify({status: 'ACTIVE'}),
      JSON.stringify({status: 'CLOSED'}), 'Automatically closed at Ends At',
    ]);
    closed.push(sessionId);
  });
  return closed;
}

/** Time-driven trigger target. It is also safe to run manually. */
function closeExpiredSessions() {
  requireV2Schema();
  const lock = LockService.getScriptLock();
  if (!lock.tryLock(30000)) throw new Error('busy');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    const closed = closeExpiredSessionsInSpreadsheet(
      spreadsheet, new Date(), 'SYSTEM',
    );
    SpreadsheetApp.flush();
    return {ok: true, closed_count: closed.length, session_ids: closed};
  } finally {
    lock.releaseLock();
  }
}

/** Time-driven trigger target for device online/offline materialization. */
function refreshDeviceStatuses() {
  requireV2Schema();
  const lock = LockService.getScriptLock();
  if (!lock.tryLock(30000)) throw new Error('busy');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    const changed = refreshDeviceStatusesInSpreadsheet(
      spreadsheet, new Date(),
    );
    SpreadsheetApp.flush();
    return {ok: true, changed_count: changed};
  } finally {
    lock.releaseLock();
  }
}

function ensureTimeTrigger(handler, minutes) {
  const matching = ScriptApp.getProjectTriggers().filter((trigger) => {
    return trigger.getHandlerFunction() === handler;
  });
  matching.slice(1).forEach((trigger) => ScriptApp.deleteTrigger(trigger));
  if (matching.length === 0) {
    ScriptApp.newTrigger(handler).timeBased().everyMinutes(minutes).create();
  }
}

/** Installs session-close, device-health and queued email-report triggers. */
function installPhase2Automation() {
  requireV2Schema();
  // Force mail authorization while an administrator is present, not in a trigger.
  MailApp.getRemainingDailyQuota();
  ensureTimeTrigger('closeExpiredSessions', 5);
  ensureTimeTrigger('refreshDeviceStatuses', 1);
  ensureTimeTrigger('sendClosedSessionReports', 1);
  return {
    ok: true,
    triggers: [
      {handler: 'closeExpiredSessions', interval_minutes: 5},
      {handler: 'refreshDeviceStatuses', interval_minutes: 1},
      {handler: 'sendClosedSessionReports', interval_minutes: 1},
    ],
  };
}

function latestOverrides(spreadsheet, sessionId) {
  const overrides = {};
  const prefix = sessionId + ':';
  allDataRows(spreadsheet.getSheetByName('AuditLog'), V2_SCHEMAS.AuditLog.length)
    .forEach((row) => {
      if (String(row[2]) !== 'ATTENDANCE_OVERRIDE' ||
          String(row[3]) !== 'SESSION_STUDENT' ||
          !String(row[4]).startsWith(prefix)) return;
      try {
        const after = JSON.parse(String(row[6] || '{}'));
        if (ATTENDANCE_STATUSES.includes(after.status)) {
          overrides[String(row[4]).slice(prefix.length)] = {
            status: after.status,
            actor: String(row[1] || ''),
            reason: String(row[7] || ''),
            changed_at: cellDateIso(row[0]),
          };
        }
      } catch (error) {
        console.error('Invalid AuditLog JSON at entity ' + row[4]);
      }
    });
  return overrides;
}

function availableRosterClasses(spreadsheet) {
  const disabled = new Set(allDataRows(
    spreadsheet.getSheetByName('Classes'), V2_SCHEMAS.Classes.length,
  ).filter((row) => !enabledValue(row[2]))
    .map((row) => String(row[0] || '').trim()));
  return [...new Set(allDataRows(
    spreadsheet.getSheetByName('Students'), V2_SCHEMAS.Students.length,
  ).filter((row) => enabledValue(row[3]))
    .map((row) => String(row[2] || '').trim())
    .filter((classId) => classId && !disabled.has(classId)))].sort();
}

function buildDashboardData(spreadsheet, filters) {
  const devices = serializeDevices(spreadsheet, new Date());
  const classes = availableRosterClasses(spreadsheet);
  const sessionRows = allDataRows(
    spreadsheet.getSheetByName('Sessions'), V2_SCHEMAS.Sessions.length,
  );
  const sessions = sessionRows.map(serializeSession).sort((left, right) => {
    return String(right.starts_at).localeCompare(String(left.starts_at));
  });
  const attendanceRows = allDataRows(
    spreadsheet.getSheetByName('Attendance'), V2_SCHEMAS.Attendance.length,
  );
  const today = Utilities.formatDate(new Date(), 'Asia/Ho_Chi_Minh', 'yyyy-MM-dd');
  const unassignedToday = attendanceRows.filter((row) => {
    if (canonicalSessionId(row[9])) return false;
    const receivedAt = asDate(row[0]);
    return receivedAt && !isNaN(receivedAt.getTime()) &&
      Utilities.formatDate(receivedAt, 'Asia/Ho_Chi_Minh', 'yyyy-MM-dd') === today;
  }).length;
  const requestedSessionId = String((filters && filters.session_id) || '');
  let selected = requestedSessionId ?
    sessions.find((session) => session.session_id === requestedSessionId) :
    sessions.find((session) => session.status === 'ACTIVE');
  if (!selected && sessions.length > 0) selected = sessions[0];
  if (!selected) {
    return {
      sessions: sessions,
      selected_session: null,
      summary: {total: 0, present: 0, late: 0, absent: 0, excused: 0},
      students: [],
      devices: devices,
      classes: classes,
      unassigned_today: unassignedToday,
      generated_at: new Date().toISOString(),
    };
  }

  const students = allDataRows(
    spreadsheet.getSheetByName('Students'), V2_SCHEMAS.Students.length,
  ).filter((row) => {
    return String(row[2]) === selected.class_id && enabledValue(row[3]);
  });
  const attendanceByStudent = {};
  attendanceRows.forEach((row) => {
    if (canonicalSessionId(row[9]) !== selected.session_id) return;
    const studentId = String(row[3]);
    const eventTime = attendanceCellDate(row[6]);
    const current = attendanceByStudent[studentId];
    if (!current || eventTime < current.event_time) {
      attendanceByStudent[studentId] = {
        event_id: String(row[1]),
        timestamp: String(row[6]),
        event_time: eventTime,
        confidence_milli: Number(row[7]),
        method: String(row[8]),
      };
    }
  });

  const lateAfter = asDate(selected.late_after);
  const overrides = latestOverrides(spreadsheet, selected.session_id);
  const resultRows = students.map((student) => {
    const studentId = String(student[0]);
    const event = attendanceByStudent[studentId] || null;
    let status = event ?
      (lateAfter && event.event_time > lateAfter ? 'LATE' : 'PRESENT') : 'ABSENT';
    const override = overrides[studentId] || null;
    if (override) status = override.status;
    return {
      student_id: studentId,
      name: String(student[1] || ''),
      class_id: String(student[2] || ''),
      status: status,
      event_id: event ? event.event_id : '',
      timestamp: event ? event.timestamp : '',
      confidence_milli: event ? event.confidence_milli : null,
      method: event ? event.method : '',
      override: override,
    };
  }).sort((left, right) => left.name.localeCompare(right.name));

  const summary = {total: resultRows.length, present: 0, late: 0, absent: 0, excused: 0};
  resultRows.forEach((student) => {
    summary[student.status.toLowerCase()]++;
  });
  return {
    sessions: sessions,
    selected_session: selected,
    summary: summary,
    students: resultRows,
    devices: devices,
    classes: classes,
    unassigned_today: unassignedToday,
    generated_at: new Date().toISOString(),
  };
}

function getDashboardData(accessKey, filters) {
  requireDashboardAccess(accessKey);
  requireV2Schema();
  const lock = LockService.getScriptLock();
  if (!lock.tryLock(10000)) throw new Error('busy');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    const closed = closeExpiredSessionsInSpreadsheet(
      spreadsheet, new Date(), 'SYSTEM',
    );
    if (closed.length > 0) SpreadsheetApp.flush();
    return buildDashboardData(spreadsheet, filters || {});
  } finally {
    lock.releaseLock();
  }
}

function recordManualCorrection(accessKey, correction) {
  const actor = requireDashboardAccess(accessKey);
  requireV2Schema();
  const sessionId = requiredText(correction || {}, 'session_id', 64);
  const studentId = requiredText(correction || {}, 'student_id', 32);
  const status = requiredText(correction || {}, 'status', 16).toUpperCase();
  const reason = requiredText(correction || {}, 'reason', 200);
  if (!ATTENDANCE_STATUSES.includes(status)) throw new Error('invalid_status');

  const lock = LockService.getScriptLock();
  if (!lock.tryLock(10000)) throw new Error('busy');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    if (!findSessionRow(spreadsheet.getSheetByName('Sessions'), sessionId) ||
        !findExactRow(spreadsheet.getSheetByName('Students'), 1, studentId)) {
      throw new Error('unknown_session_or_student');
    }
    const current = buildDashboardData(spreadsheet, {session_id: sessionId});
    const student = current.students.find((row) => row.student_id === studentId);
    if (!student) throw new Error('student_not_in_session_class');
    const before = {
      status: student.status,
      event_id: student.event_id,
      override: student.override,
    };
    const after = {status: status};
    appendAuditRow(spreadsheet.getSheetByName('AuditLog'), [
      new Date(), safeSheetText(actor), 'ATTENDANCE_OVERRIDE', 'SESSION_STUDENT',
      safeSheetText(sessionId + ':' + studentId), JSON.stringify(before),
      JSON.stringify(after), safeSheetText(reason),
    ]);
    SpreadsheetApp.flush();
    return buildDashboardData(spreadsheet, {session_id: sessionId});
  } finally {
    lock.releaseLock();
  }
}

function createClassSession(accessKey, input) {
  const actor = requireDashboardAccess(accessKey);
  requireV2Schema();
  const sessionId = requiredText(input || {}, 'session_id', 64);
  const classId = requiredText(input || {}, 'class_id', 32);
  const period = requiredText(input || {}, 'period', 24);
  const deviceId = requiredText(input || {}, 'device_id', 40);
  const reportEmail = requiredText(input || {}, 'report_email', 254);
  if (!validReportEmail(reportEmail)) throw new Error('invalid_report_email');
  if (!/^[A-Za-z0-9._:-]+$/.test(sessionId) ||
      !/^[A-Za-z0-9._:-]+$/.test(deviceId)) throw new Error('invalid_session_fields');
  const startsAt = asDate(input.starts_at);
  const lateAfter = asDate(input.late_after);
  const endsAt = asDate(input.ends_at);
  if (!startsAt || !lateAfter || !endsAt || startsAt > lateAfter || lateAfter > endsAt) {
    throw new Error('invalid_session_time');
  }

  const lock = LockService.getScriptLock();
  if (!lock.tryLock(10000)) throw new Error('busy');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    if (!availableRosterClasses(spreadsheet).includes(classId)) {
      throw new Error('class_without_students');
    }
    const sessionsSheet = spreadsheet.getSheetByName('Sessions');
    closeExpiredSessionsInSpreadsheet(spreadsheet, new Date(), 'SYSTEM');
    if (findSessionRow(sessionsSheet, sessionId)) throw new Error('session_exists');
    const activeConflict = allDataRows(sessionsSheet, V2_SCHEMAS.Sessions.length)
      .some((row) => String(row[7]).toUpperCase() === 'ACTIVE' &&
        String(row[8]) === deviceId);
    if (activeConflict) throw new Error('device_has_active_session');
    const now = new Date();
    appendSessionRow(sessionsSheet, [
      safeSheetText(sessionId), safeSheetText(classId),
      Utilities.formatDate(startsAt, 'Asia/Ho_Chi_Minh', 'yyyy-MM-dd'),
      safeSheetText(period), startsAt,
      lateAfter, endsAt, 'ACTIVE', safeSheetText(deviceId), now,
      reportEmail, '', '', '', 0, '',
    ]);
    appendAuditRow(spreadsheet.getSheetByName('AuditLog'), [
      now, safeSheetText(actor), 'SESSION_CREATE', 'SESSION',
      safeSheetText(sessionId), '{}', JSON.stringify({
        class_id: classId,
        device_id: deviceId,
        starts_at: startsAt.toISOString(),
        late_after: lateAfter.toISOString(),
        ends_at: endsAt.toISOString(),
        report_email: reportEmail,
      }), 'Created from teacher dashboard',
    ]);
    SpreadsheetApp.flush();
    return buildDashboardData(spreadsheet, {session_id: sessionId});
  } finally {
    lock.releaseLock();
  }
}

function updateClassSessionStatus(accessKey, input) {
  const actor = requireDashboardAccess(accessKey);
  requireV2Schema();
  const sessionId = requiredText(input || {}, 'session_id', 64);
  const status = requiredText(input || {}, 'status', 16).toUpperCase();
  if (!['CLOSED', 'CANCELLED'].includes(status)) throw new Error('invalid_session_status');

  const lock = LockService.getScriptLock();
  if (!lock.tryLock(10000)) throw new Error('busy');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    const sheet = spreadsheet.getSheetByName('Sessions');
    const rowNumber = findSessionRow(sheet, sessionId);
    if (!rowNumber) throw new Error('unknown_session');
    const row = sheet.getRange(rowNumber, 1, 1, V2_SCHEMAS.Sessions.length)
      .getValues()[0];
    const previous = String(row[7] || '').toUpperCase();
    if (previous !== 'ACTIVE' && previous !== status) {
      throw new Error('session_not_active');
    }
    if (previous !== status) sheet.getRange(rowNumber, 8).setValue(status);
    if (previous !== status && status === 'CLOSED' &&
        validReportEmail(row[10]) && !row[11]) {
      sheet.getRange(rowNumber, 12).setValue('PENDING');
    }
    if (previous !== status) {
      appendAuditRow(spreadsheet.getSheetByName('AuditLog'), [
        new Date(), safeSheetText(actor), 'SESSION_STATUS_CHANGE', 'SESSION',
        safeSheetText(sessionId), JSON.stringify({status: previous}),
        JSON.stringify({status: status}), safeSheetText(input.reason || ''),
      ]);
    }
    SpreadsheetApp.flush();
    return buildDashboardData(spreadsheet, {session_id: sessionId});
  } finally {
    lock.releaseLock();
  }
}

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return errorResponse('missing_body', false);
    }
    let payload;
    try {
      payload = JSON.parse(e.postData.contents);
    } catch (error) {
      return errorResponse('invalid_json', false);
    }
    if (!payload || Array.isArray(payload) || typeof payload !== 'object') {
      return errorResponse('invalid_body', false);
    }
    requireV2Schema();
    const requestVersion = normalizedApiVersion(payload);
    const type = String(payload.type || 'attendance');
    if (!['attendance', 'heartbeat', 'session_sync', 'ota_manifest'].includes(type)) {
      throw new ApiError('unknown_request_type', false);
    }

    const lock = LockService.getScriptLock();
    if (!lock.tryLock(10000)) return errorResponse('busy', true);
    try {
      const spreadsheet = getConfiguredSpreadsheet();
      const auth = authenticateDevice(payload, spreadsheet);
      closeExpiredSessionsInSpreadsheet(spreadsheet, new Date(), 'SYSTEM');
      if (type === 'attendance') {
        return handleAttendance(payload, auth, spreadsheet, requestVersion);
      }
      if (type === 'heartbeat') return handleHeartbeat(payload, auth, spreadsheet);
      if (type === 'ota_manifest') {
        return handleOtaManifest(payload, auth, spreadsheet);
      }
      return handleSessionSync(payload, auth, spreadsheet);
    } finally {
      lock.releaseLock();
    }
  } catch (error) {
    if (error instanceof ApiError) {
      return errorResponse(error.code, error.retryable);
    }
    console.error(error);
    return errorResponse('internal_error', true);
  }
}

function v2SheetsReady() {
  if (PropertiesService.getScriptProperties().getProperty('SCHEMA_VERSION') !==
      SCHEMA_VERSION) return false;
  const spreadsheet = getConfiguredSpreadsheet();
  return Object.keys(V2_SCHEMAS).every((sheetName) => {
    try {
      return planV2Sheet(spreadsheet, sheetName, V2_SCHEMAS[sheetName]).action === 'ready';
    } catch (error) {
      return false;
    }
  });
}

function doGet(e) {
  if (e && e.parameter && e.parameter.view === 'dashboard') {
    return HtmlService.createHtmlOutputFromFile('Dashboard')
      .setTitle('Robot Attendance')
      .addMetaTag('viewport', 'width=device-width, initial-scale=1');
  }
  const token = PropertiesService.getScriptProperties().getProperty('API_TOKEN') || '';
  let sheetReady = false;
  try {
    sheetReady = v2SheetsReady();
  } catch (error) {
    console.error(error);
  }
  const configured = token.length >= 32 && sheetReady;
  return jsonResponse({
    ok: configured,
    service: 'robot-attendance',
    api_version: API_VERSION,
    schema_version: SCHEMA_VERSION,
    configured: configured,
    sheet_ready: sheetReady,
  });
}

function setup() {
  const token = PropertiesService.getScriptProperties().getProperty('API_TOKEN') || '';
  if (token.length < 32) {
    throw new Error('Set API_TOKEN in Script Properties (at least 32 characters)');
  }
  getAttendanceSheet();
  SpreadsheetApp.flush();
  const result = {ok: true, api_version: API_VERSION, sheet: SHEET_NAME};
  console.log(JSON.stringify(result));
  return result;
}

/**
 * Creates or rotates one device secret without storing the plaintext in a
 * sheet. Set BOOTSTRAP_DEVICE_ID and BOOTSTRAP_DEVICE_SECRET in Script
 * Properties, run this function once, then the plaintext property is deleted.
 */
function provisionDeviceFromProperties() {
  requireV2Schema();
  const properties = PropertiesService.getScriptProperties();
  const deviceId = String(properties.getProperty('BOOTSTRAP_DEVICE_ID') || '').trim();
  const secret = String(properties.getProperty('BOOTSTRAP_DEVICE_SECRET') || '');
  if (!/^[A-Za-z0-9._:-]{1,40}$/.test(deviceId) ||
      secret.length < 32 || secret.length > 127) {
    throw new Error('Set valid BOOTSTRAP_DEVICE_ID and 32-127 char BOOTSTRAP_DEVICE_SECRET');
  }

  const lock = LockService.getScriptLock();
  if (!lock.tryLock(30000)) throw new Error('Could not acquire device lock');
  try {
    const spreadsheet = getConfiguredSpreadsheet();
    const sheet = spreadsheet.getSheetByName('Devices');
    let rowNumber = findExactRow(sheet, 1, deviceId);
    const now = new Date();
    let row;
    if (rowNumber) {
      row = sheet.getRange(rowNumber, 1, 1, V2_SCHEMAS.Devices.length)
        .getValues()[0];
    } else {
      row = [deviceId, true, '', '', '', '', '', '', '', '', '', now, now,
        'OFFLINE', '', ''];
      sheet.appendRow(row);
      rowNumber = sheet.getLastRow();
    }
    row[1] = true;
    row[2] = hashSecret(secret);
    row[11] = now;
    row[12] = now;
    sheet.getRange(rowNumber, 1, 1, V2_SCHEMAS.Devices.length).setValues([row]);
    properties.deleteProperty('BOOTSTRAP_DEVICE_SECRET');
    SpreadsheetApp.flush();
    return {ok: true, device_id: deviceId, secret_rotated: true};
  } finally {
    lock.releaseLock();
  }
}

/** Installs the teacher account from one-time Script Properties. */
function provisionDashboardAccountFromProperties() {
  const properties = PropertiesService.getScriptProperties();
  const name = String(properties.getProperty('BOOTSTRAP_TEACHER_USERNAME') || '').trim();
  const password = String(properties.getProperty('BOOTSTRAP_TEACHER_PASSWORD') || '');
  if (!name || name.length > 80 || password.length < 8 || password.length > 127) {
    throw new Error('Set BOOTSTRAP_TEACHER_USERNAME and BOOTSTRAP_TEACHER_PASSWORD');
  }
  const salt = Utilities.getUuid();
  properties.setProperty('DASHBOARD_USERNAME', name);
  properties.setProperty('DASHBOARD_PASSWORD_SALT', salt);
  properties.setProperty('DASHBOARD_PASSWORD_HASH', dashboardPasswordHash(salt, password));
  properties.setProperty('DASHBOARD_CREDENTIAL_VERSION', Utilities.getUuid());
  properties.deleteProperty('BOOTSTRAP_TEACHER_USERNAME');
  properties.deleteProperty('BOOTSTRAP_TEACHER_PASSWORD');
  properties.deleteProperty('DASHBOARD_ACCESS_HASH');
  properties.deleteProperty('BOOTSTRAP_DASHBOARD_KEY');
  return {ok: true, teacher_name: name};
}
