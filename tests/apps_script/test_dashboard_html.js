const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const dashboardPath = path.join(
  __dirname, '..', '..', 'google_apps_script', 'Dashboard.html',
);
const html = fs.readFileSync(dashboardPath, 'utf8');
const scripts = Array.from(html.matchAll(/<script>([\s\S]*?)<\/script>/g));

assert.strictEqual(scripts.length, 1);
assert.doesNotThrow(() => new vm.Script(scripts[0][1], {filename: dashboardPath}));
assert.match(html, /getDashboardData/);
assert.match(html, /recordManualCorrection/);
assert.match(html, /createClassSession/);
assert.match(html, /updateClassSessionStatus/);
assert.match(html, /Đang ghi điều chỉnh/);
assert.match(html, /Đã đổi/);
assert.match(html, /setInterval/);
assert.match(html, /friendlyError/);
assert.match(html, /Xuất CSV/);
assert.match(html, /Trạng thái thiết bị/);
assert.match(html, /device-rows/);
assert.match(html, /renderDevices/);
assert.match(html, /180 giây/);
assert.match(html, /sessionStorage/);
assert.doesNotMatch(html, /localStorage/);
assert.doesNotMatch(html, /innerHTML/);
assert.doesNotMatch(html, /[?&](?:key|token)=/i);

class FakeBlob {
  constructor(parts) { this.body = parts.join(''); }
}
const elements = {};
function element(id) {
  if (!elements[id]) {
    elements[id] = {
      hidden: false, disabled: false, value: '', textContent: '', className: '',
      setAttribute() {}, querySelectorAll() { return []; },
    };
  }
  return elements[id];
}
let exportedBlob = null;
let exportedName = '';
let downloads = 0;
const calls = [];
const rendered = [];
const runner = {
  withSuccessHandler(callback) { this.success = callback; return this; },
  withFailureHandler(callback) { this.failure = callback; return this; },
  getDashboardData(key, filters) {
    calls.push({key, filters, success: this.success, failure: this.failure});
  },
};
const browser = vm.createContext({
  console,
  document: {
    hidden: false,
    getElementById: element,
    createElement() {
      return {
        click() { downloads++; },
        set download(value) { exportedName = value; },
      };
    },
  },
  sessionStorage: {getItem() { return null; }, setItem() {}, removeItem() {}},
  setInterval() {},
  Option: class { constructor(label, value) { this.textContent=label; this.value=value; } },
  Blob: FakeBlob,
  URL: {
    createObjectURL(blob) { exportedBlob = blob; return 'blob:test'; },
    revokeObjectURL() {},
  },
  google: {script: {run: runner}},
  rendered,
});
vm.runInContext(scripts[0][1], browser);
vm.runInContext(
  'render = function(data) { state.data=data; rendered.push(data.selected_session.session_id); };',
  browser,
);

// A slow response for A must not overwrite the more recent selection B.
vm.runInContext("load('A'); load('B');", browser);
calls[0].success({selected_session: {session_id: 'A', status: 'CLOSED'}});
assert.deepStrictEqual(rendered, []);
calls[1].success({selected_session: {session_id: 'B', status: 'CLOSED'}});
assert.deepStrictEqual(rendered, ['B']);
assert.strictEqual(vm.runInContext('state.pending', browser), false);

// A previously loaded closed session appears immediately, but writes/export
// remain disabled until the revalidation request completes.
vm.runInContext(
  "state.cache.A={selected_session:{session_id:'A',status:'CLOSED'},students:[]}; load('A',true);",
  browser,
);
assert.deepStrictEqual(rendered, ['B', 'A']);
assert.strictEqual(element('export').disabled, true);
calls[2].success({selected_session: {session_id: 'A', status: 'CLOSED'}});
assert.strictEqual(element('export').disabled, false);

vm.runInContext("load('A',true);", browser);
calls[3].failure(new Error('busy'));
assert.strictEqual(vm.runInContext('state.stale', browser), true);
assert.strictEqual(element('export').disabled, true);
assert.strictEqual(element('refresh').disabled, false);
vm.runInContext('exportCsv();', browser);
assert.strictEqual(downloads, 0);

// CSV includes stable session identity and the latest correction reason.
// Formula-looking cells must not execute when opened in a spreadsheet.
vm.runInContext(
  "state.data={selected_session:{session_id:'E2E-A',status:'CLOSED'},students:[" +
  "{student_id:'2',name:'Học sinh A',class_id:'12A1',status:'EXCUSED'," +
  "override:{reason:'Nghỉ ốm'},timestamp:'',event_id:'',confidence_milli:null}," +
  "{student_id:'3',name:'=2+2',class_id:'12A1',status:'ABSENT'," +
  "override:null,timestamp:'',event_id:'',confidence_milli:null}]};" +
  "state.pending=false;state.stale=false;exportCsv();",
  browser,
);
assert.strictEqual(downloads, 1);
assert.strictEqual(exportedName, 'E2E-A.csv');
assert.ok(exportedBlob.body.startsWith('\ufeff'));
assert.match(exportedBlob.body, /"Session ID","Student ID","Name","Class","Status","Reason"/);
assert.match(exportedBlob.body, /"E2E-A","2","Học sinh A","12A1","Có phép","Nghỉ ốm"/);
assert.match(exportedBlob.body, /"'=2\+2"/);

console.log('Apps Script dashboard HTML tests: PASS');
