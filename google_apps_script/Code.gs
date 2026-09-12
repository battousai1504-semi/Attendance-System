const SHEET_NAME = 'Attendance';
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

function safeSheetText(value) {
  const text = String(value == null ? '' : value);
  return /^[=+\-@]/.test(text) ? "'" + text : text;
}

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return jsonResponse({ok: false, error: 'missing_body'});
    }
    const payload = JSON.parse(e.postData.contents);
    const configuredToken =
      PropertiesService.getScriptProperties().getProperty('API_TOKEN') || '';
    if (configuredToken && String(payload.token || '') !== configuredToken) {
      return jsonResponse({ok: false, error: 'unauthorized'});
    }

    const eventId = requiredText(payload, 'event_id', 64);
    const deviceId = requiredText(payload, 'device_id', 40);
    const studentId = Number(payload.student_id);
    const confidence = Number(payload.confidence_milli);
    if (!Number.isInteger(studentId) || studentId <= 0 ||
        !Number.isInteger(confidence) || confidence < 0 || confidence > 1000) {
      return jsonResponse({ok: false, error: 'invalid_numeric_fields'});
    }

    const lock = LockService.getScriptLock();
    if (!lock.tryLock(10000)) {
      return jsonResponse({ok: false, error: 'busy'});
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
          return jsonResponse({ok: true, duplicate: true, event_id: eventId});
        }
      }

      sheet.appendRow([
        new Date(),
        eventId,
        deviceId,
        studentId,
        safeSheetText(payload.name),
        safeSheetText(payload.class),
        requiredText(payload, 'timestamp', 32),
        confidence,
        String(payload.method || 'FACE'),
      ]);
      return jsonResponse({ok: true, duplicate: false, event_id: eventId});
    } finally {
      lock.releaseLock();
    }
  } catch (error) {
    console.error(error);
    return jsonResponse({ok: false, error: String(error.message || error)});
  }
}

function doGet() {
  return jsonResponse({ok: true, service: 'robot-attendance'});
}

function setup() {
  getAttendanceSheet();
}
