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
assert.match(html, /id="today-summary"/);
assert.match(html, /id="repeat-session"/);
assert.match(html, /id="device-readiness"/);
assert.match(html, /buildQuickSessionInput/);
assert.match(html, /Tùy chọn nâng cao: đổi giờ, mốc đi muộn hoặc mã buổi/);
assert.match(html, /Buổi sáng · 07:00–10:30/);
assert.match(html, /Buổi chiều · 14:00–16:30/);
assert.doesNotMatch(html, /id="new-period"|id="duration-minutes"/);
assert.match(html, /\.roster-table tr \{ display:grid/);
assert.match(html, /Trạng thái thiết bị và thông tin kỹ thuật/);
assert.match(html, /Trạng thái thiết bị/);
assert.match(html, /device-rows/);
assert.match(html, /renderDevices/);
assert.match(html, /180 giây/);
assert.match(html, /sessionStorage/);
assert.doesNotMatch(html, /localStorage/);
assert.doesNotMatch(html, /innerHTML/);
assert.doesNotMatch(html, /[?&](?:key|token)=/i);
const htmlIds = [...html.matchAll(/\bid="([^"]+)"/g)].map((match) => match[1]);
assert.strictEqual(new Set(htmlIds).size, htmlIds.length, 'duplicate HTML id');

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
    reset() {},
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
const fixedNow = new Date('2026-10-08T00:15:00Z');
class FakeDate extends Date {
  constructor(...args) { super(...(args.length ? args : [fixedNow.toISOString()])); }
  static now() { return fixedNow.getTime(); }
}
const calls = [];
const rendered = [];
const browserSession = new Map();
const runner = {
  withSuccessHandler(callback) { this.success = callback; return this; },
  withFailureHandler(callback) { this.failure = callback; return this; },
  getDashboardData(key, filters) {
    calls.push({key, filters, success: this.success, failure: this.failure});
  },
  recordManualCorrection(key, correction) {
    calls.push({key, correction, success: this.success, failure: this.failure});
  },
  createClassSession(key, values) {
    calls.push({key, values, success: this.success, failure: this.failure});
  },
  loginDashboard(username, password) {
    calls.push({username, password, success: this.success, failure: this.failure});
  },
  logoutDashboard(token) { calls.push({logout:token}); },
};
const browser = vm.createContext({
  console,
  Date: FakeDate,
  document: {
    hidden: false,
    getElementById: element,
    createElement() { return node(); },
  },
  sessionStorage: {
    getItem(key) { return browserSession.get(key) || null; },
    setItem(key, value) { browserSession.set(key, value); },
    removeItem(key) { browserSession.delete(key); },
  },
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
  "status:'ACTIVE',device_id:'robot-01',starts_at:'2026-10-08T00:00:00Z'," +
  "ends_at:'2026-10-08T03:30:00Z'}]," +
  "selected_session:{session_id:'S1',class_id:'12A1',period:'P1'," +
  "status:'ACTIVE',device_id:'robot-01',starts_at:'2026-10-08T00:00:00Z'," +
  "ends_at:'2026-10-08T03:30:00Z'}," +
  "summary:{total:1,present:1,late:0,absent:0,excused:0}," +
  "classes:['12A1']," +
  "students:[{student_id:'1',name:'Học sinh A',status:'PRESENT'," +
  "timestamp:'2026-10-01 14:00:00',confidence_milli:800}]," +
  "devices:[{device_id:'robot-01',status:'ONLINE'}]});",
  browser,
);
assert.strictEqual(element('session-status').textContent, 'Đang điểm danh');
assert.strictEqual(element('new-device').value, 'robot-01');
assert.strictEqual(element('student-rows').children.length, 1);
assert.strictEqual(element('session').children[0].textContent.includes('12A1 · P1'), true);
assert.strictEqual(element('today-title').textContent, 'Buổi học đang diễn ra');
assert.strictEqual(element('repeat-session').hidden, true);
assert.match(element('device-readiness').textContent, /robot-01: Online/);
assert.strictEqual(element('new-class').value, '12A1');
assert.strictEqual(element('new-shift').value, 'Sáng');
assert.match(element('schedule-hint').textContent, /07:00–10:30/);
assert.strictEqual(element('start-session').disabled, false);

browser.futureDashboard = {
  sessions:[{session_id:'FUTURE',class_id:'12A1',period:'Chiều',status:'ACTIVE',
    device_id:'robot-01',starts_at:'2026-10-08T07:00:00Z',
    ends_at:'2026-10-08T09:30:00Z'}],
  selected_session:{session_id:'FUTURE',class_id:'12A1',period:'Chiều',status:'ACTIVE',
    device_id:'robot-01',starts_at:'2026-10-08T07:00:00Z',
    ends_at:'2026-10-08T09:30:00Z'},
  classes:['12A1'], devices:[{device_id:'robot-01',status:'ONLINE'}],
  summary:{total:0,present:0,late:0,absent:0,excused:0}, students:[],
};
vm.runInContext('render(futureDashboard);', browser);
assert.strictEqual(element('session-status').textContent, 'Sắp bắt đầu');
assert.match(element('today-summary').textContent, /bắt đầu 14:00/);
browser.expiredDashboard = {
  ...browser.futureDashboard,
  sessions:[{...browser.futureDashboard.sessions[0],
    starts_at:'2026-10-07T00:00:00Z',ends_at:'2026-10-07T03:30:00Z'}],
  selected_session:{...browser.futureDashboard.selected_session,
    starts_at:'2026-10-07T00:00:00Z',ends_at:'2026-10-07T03:30:00Z'},
};
vm.runInContext('render(expiredDashboard);', browser);
assert.strictEqual(element('session-status').textContent, 'Đã hết giờ');
assert.match(element('today-title').textContent, /đang chờ đóng/);

// With no active session, an explicit recent-session suggestion starts the
// same class/morning/device in one click, using today's primary-school hours.
const yesterday = new Date(FakeDate.now() - 86400000);
const end = new Date(yesterday.getTime() + 210 * 60000);
const closed = {
  session_id: 'RECENT', class_id: '12A1', period: 'Sáng', status: 'CLOSED',
  device_id: 'robot-01', starts_at: yesterday.toISOString(),
  ends_at: end.toISOString(),
};
browser.closedDashboard = {
  sessions: [closed], selected_session: closed,
  classes: ['12A1'],
  summary: {total: 0, present: 0, late: 0, absent: 0, excused: 0},
  students: [], devices: [{device_id: 'robot-01', status: 'ONLINE'}],
  unassigned_today: 2,
};
vm.runInContext('render(closedDashboard);', browser);
assert.strictEqual(element('repeat-session').hidden, false);
assert.match(element('repeat-session').textContent, /sáng · lớp 12A1/);
assert.match(element('unassigned-warning').textContent, /2 lượt/);
browser.closedDashboard.devices[0].status = 'OFFLINE';
vm.runInContext('render(closedDashboard);', browser);
assert.strictEqual(element('repeat-session').hidden, true);
assert.strictEqual(element('start-session').disabled, true);
browser.closedDashboard.devices[0].status = 'ONLINE';
vm.runInContext('render(closedDashboard);', browser);
assert.strictEqual(element('start-session').disabled, false);
browser.closedDashboard.classes = [];
vm.runInContext('render(closedDashboard);', browser);
assert.strictEqual(element('new-class').value, '');
assert.strictEqual(element('start-session').disabled, true);
assert.match(element('schedule-hint').textContent, /Chưa có lớp/);
browser.closedDashboard.classes = ['12A1'];
vm.runInContext('render(closedDashboard);', browser);
browser.closedDashboard.sessions.unshift({
  session_id:'TODAY-MORNING', class_id:'12A1', period:'Sáng', status:'CLOSED',
  device_id:'robot-01', starts_at:'2026-10-08T00:00:00Z',
  ends_at:'2026-10-08T03:30:00Z',
});
vm.runInContext('render(closedDashboard);', browser);
assert.strictEqual(element('repeat-session').hidden, true);
assert.match(element('schedule-hint').textContent, /đã có buổi sáng hôm nay/);
browser.closedDashboard.sessions.shift();
vm.runInContext('render(closedDashboard);', browser);
vm.runInContext("state.actor='Teacher A';", browser);
element('repeat-session').onclick();
assert.strictEqual(calls.length, 1);
assert.strictEqual(calls[0].values.class_id, '12A1');
assert.strictEqual(calls[0].values.period, 'Sáng');
assert.strictEqual(calls[0].values.device_id, 'robot-01');
assert.strictEqual(calls[0].values.actor, 'Teacher A');
assert.strictEqual(Date.parse(calls[0].values.ends_at) -
  Date.parse(calls[0].values.starts_at), 210 * 60000);
assert.strictEqual(calls[0].values.starts_at, '2026-10-08T00:00:00.000Z');
assert.strictEqual(calls[0].values.ends_at, '2026-10-08T03:30:00.000Z');
vm.runInContext("state.mutating=false;state.actor='';", browser);
calls.length = 0;
browser.closedDashboard.sessions.unshift({
  session_id: 'CURRENT', class_id: '12A1', period: 'Chiều', status: 'ACTIVE',
  device_id: 'robot-01', starts_at: new Date(FakeDate.now()).toISOString(),
  ends_at: new Date(FakeDate.now() + 45 * 60000).toISOString(),
});
vm.runInContext('render(closedDashboard);', browser);
assert.strictEqual(element('today-title').textContent, 'Buổi học đang diễn ra');
assert.strictEqual(element('repeat-session').hidden, true);
assert.strictEqual(element('view-active').hidden, false);
element('view-active').onclick();
assert.strictEqual(calls[0].filters.session_id, 'CURRENT');
vm.runInContext('state.pending=false;', browser);
calls.length = 0;
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

// Morning/afternoon schedules use Vietnam time, independent of browser TZ.
// Backend still receives the legacy period field and exact timestamps.
const quickInput = JSON.parse(vm.runInContext(
  "JSON.stringify(buildQuickSessionInput({class_id:'12A1',period:'Sáng'," +
  "device_id:'robot-01',late_minutes:'10'," +
  "starts_at:'2026-10-01T07:00',ends_at:'',session_id:''}," +
  "new Date('2026-10-01T00:05:00Z'))) ",
  browser,
));
assert.match(quickInput.session_id, /^20261001-12A1-Sang-0700/);
assert.strictEqual(quickInput.actor, '');
assert.strictEqual(
  Date.parse(quickInput.ends_at) - Date.parse(quickInput.starts_at), 210 * 60000);
assert.strictEqual(quickInput.starts_at, '2026-10-01T00:00:00.000Z');
assert.strictEqual(quickInput.ends_at, '2026-10-01T03:30:00.000Z');
assert.strictEqual(
  Date.parse(quickInput.late_after) - Date.parse(quickInput.starts_at), 10 * 60000);
const afternoonInput = JSON.parse(vm.runInContext(
  "JSON.stringify(buildQuickSessionInput({class_id:'12A1',period:'Chiều'," +
  "device_id:'robot-01',late_minutes:'10'}," +
  "new Date('2026-10-01T07:10:00Z'))) ", browser,
));
assert.strictEqual(afternoonInput.starts_at, '2026-10-01T07:00:00.000Z');
assert.strictEqual(afternoonInput.ends_at, '2026-10-01T09:30:00.000Z');
assert.match(afternoonInput.session_id, /^20261001-12A1-Chieu-1400/);
assert.strictEqual(vm.runInContext(
  "recommendedShift(new Date('2026-10-01T03:29:00Z'))", browser), 'Sáng');
assert.strictEqual(vm.runInContext(
  "recommendedShift(new Date('2026-10-01T03:30:00Z'))", browser), 'Chiều');
assert.throws(() => vm.runInContext(
  "buildQuickSessionInput({class_id:'12A1',period:'Sáng',device_id:''," +
  "late_minutes:'10'})", browser,
), /Chưa có máy điểm danh/);
assert.throws(() => vm.runInContext(
  "buildQuickSessionInput({class_id:'12A1',period:'Sáng'," +
  "device_id:'robot-01',late_minutes:'10'},new Date('2026-10-01T03:31:00Z'))", browser,
), /đã qua giờ kết thúc/);
vm.runInContext("state.data={sessions:[{class_id:'12A1',period:'Sáng'," +
  "starts_at:'2026-10-01T00:00:00Z'}]};", browser);
assert.throws(() => vm.runInContext(
  "buildQuickSessionInput({class_id:'12A1',period:'Sáng'," +
  "device_id:'robot-01',late_minutes:'10'},new Date('2026-10-01T00:05:00Z'))", browser,
), /đã có buổi sáng ngày này/);
vm.runInContext("state.data={sessions:[],classes:['12A1']," +
  "devices:[{device_id:'robot-01',status:'OFFLINE'}]};", browser);
assert.throws(() => vm.runInContext(
  "buildQuickSessionInput({class_id:'12A1',period:'Sáng'," +
  "device_id:'robot-01',late_minutes:'10'},new Date('2026-10-01T00:05:00Z'))", browser,
), /chưa trực tuyến/);

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
  "state.key='test-session-token';" +
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

// A password is exchanged once for a server-issued token, never stored.
element('teacher-name').value = 'Giáo viên mẫu';
element('teacher-password').value = 'private-test-password';
element('login-form').onsubmit({preventDefault(){}});
const loginCall = calls.at(-1);
assert.strictEqual(loginCall.username, 'Giáo viên mẫu');
loginCall.success({token:'session-token',teacher_name:'Giáo viên mẫu'});
assert.strictEqual(browserSession.get('attendanceDashboardToken'), 'session-token');
assert.strictEqual(browserSession.has('attendanceDashboardKey'), false);
assert.strictEqual(element('teacher-password').value, '');
element('logout').onclick();
assert.strictEqual(browserSession.has('attendanceDashboardToken'), false);

console.log('Apps Script dashboard HTML tests: PASS');
