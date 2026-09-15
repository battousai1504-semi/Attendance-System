const SHEET_NAME = 'Attendance';
const API_VERSION = 'phase1-v2';
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

function jsonResponse(value) {
  return ContentService
    .createTextOutput(JSON.stringify(value))
    .setMimeType(ContentService.MimeType.JSON);
}

function errorResponse(code, retryable) {
  return jsonResponse({
    ok: false,
    error: code,
    retryable: Boolean(retryable),
    api_version: API_VERSION,
  });
}

function getAttendanceSheet() {
  const properties = PropertiesService.getScriptProperties();
  const spreadsheetId = properties.getProperty('SPREADSHEET_ID');
  const spreadsheet = spreadsheetId
    ? SpreadsheetApp.openById(spreadsheetId)
    : SpreadsheetApp.getActiveSpreadsheet();
  if (!spreadsheet) {
    throw new Error('Set SPREADSHEET_ID in Script Properties');
  }

  let sheet = spreadsheet.getSheetByName(SHEET_NAME);
  if (!sheet) sheet = spreadsheet.insertSheet(SHEET_NAME);
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);
    sheet.setFrozenRows(1);
    sheet.getRange(1, 1, 1, HEADERS.length).setFontWeight('bold');
  } else {
    const actualHeaders = sheet.getRange(1, 1, 1, HEADERS.length).getValues()[0];
    if (HEADERS.some((header, index) => actualHeaders[index] !== header)) {
      throw new Error('Attendance sheet headers do not match the expected schema');
    }
  }
  return sheet;
}

function requiredText(payload, key, maxLength) {
  const value = String(payload[key] == null ? '' : payload[key]).trim();
  if (!value || value.length > maxLength) {
    throw new Error('Invalid ' + key);
  }
  return value;
}

function optionalText(payload, key, maxLength) {
  const value = String(payload[key] == null ? '' : payload[key]).trim();
  if (value.length > maxLength) throw new Error('Invalid ' + key);
  return value;
}

function safeSheetText(value) {
  const text = String(value == null ? '' : value);
  return /^[=+\-@]/.test(text) ? "'" + text : text;
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
    const configuredToken =
      PropertiesService.getScriptProperties().getProperty('API_TOKEN') || '';
    if (configuredToken.length < 32) {
      return errorResponse('server_not_configured', false);
    }
    if (String(payload.token || '') !== configuredToken) {
      return errorResponse('unauthorized', false);
    }

    const eventId = requiredText(payload, 'event_id', 64);
    const deviceId = requiredText(payload, 'device_id', 40);
    const name = requiredText(payload, 'name', 64);
    const className = optionalText(payload, 'class', 32);
    const timestamp = requiredText(payload, 'timestamp', 32);
    const method = requiredText(payload, 'method', 16);
    const studentId = Number(payload.student_id);
    const confidence = Number(payload.confidence_milli);
    if (!/^[A-Za-z0-9._:-]+$/.test(eventId) ||
        !/^[A-Za-z0-9._:-]+$/.test(deviceId) ||
        !/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(timestamp) ||
        method !== 'FACE') {
      return errorResponse('invalid_text_fields', false);
    }
    if (!Number.isInteger(studentId) || studentId <= 0 ||
        !Number.isInteger(confidence) || confidence < 0 || confidence > 1000) {
      return errorResponse('invalid_numeric_fields', false);
    }

    const lock = LockService.getScriptLock();
    if (!lock.tryLock(10000)) {
      return errorResponse('busy', true);
    }
    try {
      const sheet = getAttendanceSheet();
      if (sheet.getLastRow() > 1) {
        const duplicate = sheet
          .getRange(2, 2, sheet.getLastRow() - 1, 1)
          .createTextFinder(eventId)
          .matchEntireCell(true)
          .findNext();
        if (duplicate) {
          return jsonResponse({
            ok: true,
            duplicate: true,
            event_id: eventId,
            api_version: API_VERSION,
          });
        }
      }

      sheet.appendRow([
        new Date(),
        eventId,
        deviceId,
        studentId,
        safeSheetText(name),
        safeSheetText(className),
        timestamp,
        confidence,
        method,
      ]);
      SpreadsheetApp.flush();
      return jsonResponse({
        ok: true,
        duplicate: false,
        event_id: eventId,
        api_version: API_VERSION,
      });
    } finally {
      lock.releaseLock();
    }
  } catch (error) {
    console.error(error);
    return errorResponse('internal_error', true);
  }
}

function doGet() {
  const token =
    PropertiesService.getScriptProperties().getProperty('API_TOKEN') || '';
  let sheetReady = false;
  try {
    getAttendanceSheet();
    sheetReady = true;
  } catch (error) {
    console.error(error);
  }
  const configured = token.length >= 32 && sheetReady;
  return jsonResponse({
    ok: configured,
    service: 'robot-attendance',
    api_version: API_VERSION,
    configured,
    sheet_ready: sheetReady,
  });
}

function setup() {
  const token =
    PropertiesService.getScriptProperties().getProperty('API_TOKEN') || '';
  if (token.length < 32) {
    throw new Error('Set API_TOKEN in Script Properties (at least 32 characters)');
  }
  getAttendanceSheet();
  SpreadsheetApp.flush();
  const result = {ok: true, api_version: API_VERSION, sheet: SHEET_NAME};
  console.log(JSON.stringify(result));
  return result;
}
