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
assert.match(html, /id="correction-dialog"/);
assert.match(html, /id="correction-reason"/);
assert.doesNotMatch(html, /prompt\(/);
assert.match(html, /createClassSession/);
assert.match(html, /updateClassSessionStatus/);
assert.match(html, /Đang ghi điều chỉnh/);
assert.match(html, /Đã đổi/);
assert.match(html, /setInterval/);
assert.match(html, /friendlyError/);
assert.match(html, /Xuất CSV/);
assert.match(html, /Bắt đầu buổi học mới/);
assert.match(html, /buildQuickSessionInput/);
assert.match(html, /Tùy chọn nâng cao: giờ, đi muộn, thiết bị, mã buổi/);
assert.match(html, /Trạng thái thiết bị và thông tin kỹ thuật/);
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
function node() {
  return {
    hidden:false, disabled:false, value:'', textContent:'', className:'',
    open:false, children:[],
    setAttribute() {}, querySelectorAll() { return []; },
    replaceChildren(...children) { this.children=children; this.value=''; },
    appendChild(child) {
      this.children.push(child);
      if (child.selected || (this.children.length === 1 && !this.value)) {
        this.value=child.value || '';
      }
    },
    append(...children) { children.forEach((child) => this.appendChild(child)); },
    showModal() { this.open=true; }, close() { this.open=false; }, focus() {},
    click() { downloads++; },
    set download(value) { exportedName = value; },
  };
}
function element(id) {
  if (!elements[id]) elements[id] = node();
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
  recordManualCorrection(key, correction) {
    calls.push({key, correction, success: this.success, failure: this.failure});
  },
};
const browser = vm.createContext({
  console,
  document: {
    hidden: false,
    getElementById: element,
    createElement() { return node(); },
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
  "render({sessions:[{session_id:'S1',class_id:'12A1',period:'P1'," +
  "status:'ACTIVE',device_id:'robot-01',starts_at:'2026-10-01T07:00:00Z'," +
  "ends_at:'2026-10-01T07:45:00Z'}]," +
  "selected_session:{session_id:'S1',class_id:'12A1',period:'P1'," +
  "status:'ACTIVE',device_id:'robot-01',starts_at:'2026-10-01T07:00:00Z'," +
  "ends_at:'2026-10-01T07:45:00Z'}," +
  "summary:{total:1,present:1,late:0,absent:0,excused:0}," +
  "students:[{student_id:'1',name:'Học sinh A',status:'PRESENT'," +
  "timestamp:'2026-10-01 14:00:00',confidence_milli:800}]," +
  "devices:[{device_id:'robot-01',status:'ONLINE'}]});",
  browser,
);
assert.strictEqual(element('session-status').textContent, 'Đang điểm danh');
assert.strictEqual(element('new-device').value, 'robot-01');
assert.strictEqual(element('student-rows').children.length, 1);
assert.strictEqual(element('session').children[0].textContent.includes('12A1 · P1'), true);
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

// The teacher enters class, period and duration; backend-required timestamps
// and a stable unique session ID are derived locally before the authenticated
// createClassSession call. Advanced settings remain optional.
const quickInput = JSON.parse(vm.runInContext(
  "JSON.stringify(buildQuickSessionInput({class_id:'12A1',period:'P1'," +
  "device_id:'robot-01',duration_minutes:'45',late_minutes:'10'," +
  "starts_at:'2026-10-01T07:00:00',ends_at:'',session_id:''}))",
  browser,
));
assert.match(quickInput.session_id, /^20261001-12A1-P1-0700/);
assert.strictEqual(quickInput.actor, '');
assert.strictEqual(
  Date.parse(quickInput.ends_at) - Date.parse(quickInput.starts_at), 45 * 60000);
assert.strictEqual(
  Date.parse(quickInput.late_after) - Date.parse(quickInput.starts_at), 10 * 60000);
assert.throws(() => vm.runInContext(
  "buildQuickSessionInput({class_id:'12A1',period:'P1',device_id:''," +
  "duration_minutes:'45',late_minutes:'10'})", browser,
), /Chưa có thiết bị/);

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

// One quiet row action opens a focused correction dialog. A reason is required
// and the authenticated backend receives the original session/student ID.
vm.runInContext(
  "state.data={selected_session:{session_id:'E2E-A',status:'ACTIVE'}," +
  "students:[{student_id:'2',name:'Học sinh A',status:'ABSENT'}]};" +
  "state.pending=false;state.stale=false;state.mutating=false;" +
  "openCorrection(state.data.students[0]);",
  browser,
);
assert.strictEqual(element('correction-dialog').open, true);
element('correction-status').value = 'EXCUSED';
element('correction-reason').value = 'Có giấy xin phép';
vm.runInContext('saveCorrection({preventDefault(){}});', browser);
const correctionCall = calls.at(-1);
assert.strictEqual(correctionCall.correction.session_id, 'E2E-A');
assert.strictEqual(correctionCall.correction.student_id, '2');
assert.strictEqual(correctionCall.correction.status, 'EXCUSED');
assert.strictEqual(correctionCall.correction.reason, 'Có giấy xin phép');
correctionCall.success({selected_session:{session_id:'E2E-A',status:'ACTIVE'},students:[]});
assert.strictEqual(element('correction-dialog').open, false);
assert.strictEqual(vm.runInContext('state.mutating', browser), false);

console.log('Apps Script dashboard HTML tests: PASS');
