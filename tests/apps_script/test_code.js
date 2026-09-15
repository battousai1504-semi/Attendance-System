const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

let apiToken = '';
let flushCount = 0;

class FakeSheet {
  constructor() {
    this.rows = [];
  }

  getLastRow() {
    return this.rows.length;
  }

  appendRow(row) {
    this.rows.push(row);
  }

  setFrozenRows() {}

  getRange(row, column, rowCount, columnCount) {
    const sheet = this;
    return {
      setFontWeight() {},
      getValues() {
        const values = [];
        for (let rowIndex = row - 1; rowIndex < row - 1 + rowCount; rowIndex++) {
          const source = sheet.rows[rowIndex] || [];
          values.push(source.slice(column - 1, column - 1 + columnCount));
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
              if (sheet.rows[index] && sheet.rows[index][column - 1] === value) {
                return {row: index + 1};
              }
            }
            return null;
          },
        };
      },
    };
  }
}

const attendanceSheet = new FakeSheet();
const spreadsheet = {
  getSheetByName() {
    return attendanceSheet;
  },
  insertSheet() {
    return attendanceSheet;
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
  PropertiesService: {
    getScriptProperties() {
      return {
        getProperty(key) {
          if (key === 'API_TOKEN') return apiToken;
          if (key === 'SPREADSHEET_ID') return '';
          return '';
        },
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
          return true;
        },
        releaseLock() {},
      };
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

const validPayload = {
  event_id: '1760000000-1-12345678',
  device_id: 'robot-01',
  token: '0123456789abcdef0123456789abcdef',
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
  api_version: 'phase1-v2',
});
assert.strictEqual(postRaw('{broken-json').error, 'invalid_json');
assert.strictEqual(post(null).error, 'invalid_body');

assert.strictEqual(JSON.parse(context.doGet().text).ok, false);
assert.throws(() => context.setup(), /Set API_TOKEN/);

apiToken = validPayload.token;
assert.strictEqual(context.setup().ok, true);
assert.strictEqual(JSON.parse(context.doGet().text).configured, true);
assert.deepStrictEqual(post({...validPayload, token: 'wrong-token'}),
                       {
                         ok: false,
                         error: 'unauthorized',
                         retryable: false,
                         api_version: 'phase1-v2',
                       });

const accepted = post(validPayload);
assert.strictEqual(accepted.ok, true);
assert.strictEqual(accepted.duplicate, false);
assert.strictEqual(accepted.event_id, validPayload.event_id);
assert.strictEqual(accepted.api_version, 'phase1-v2');
assert.strictEqual(attendanceSheet.rows.length, 2); // header + one event
assert.strictEqual(attendanceSheet.rows[1][4], "'=Nguyen Van A");
assert.strictEqual(flushCount, 2); // setup + first accepted event

const duplicate = post(validPayload);
assert.strictEqual(duplicate.ok, true);
assert.strictEqual(duplicate.duplicate, true);
assert.strictEqual(attendanceSheet.rows.length, 2);

assert.strictEqual(post({...validPayload, event_id: '=formula'}).error,
                   'invalid_text_fields');
assert.strictEqual(post({...validPayload, method: 'FINGERPRINT'}).error,
                   'invalid_text_fields');
assert.strictEqual(post({...validPayload, confidence_milli: 1001}).error,
                   'invalid_numeric_fields');

console.log('Apps Script tests: PASS');
