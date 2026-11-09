#include "WebPortal.h"
#include "Config.h"
#include "Storage.h"
#include "ZoneControl.h"
#include "Hardware.h"
#include "BlynkManager.h"
#include "AppLog.h"
#include <WiFi.h>
#include <WebServer.h>

static WebServer server(80);

static String escapeHtml(const String& in) {
  String out;
  out.reserve(in.length() + 32);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else out += c;
  }
  return out;
}

static bool checkAuth() {
  // 1. Direct query parameter fallback (e.g. /?key=admin) to bypass browser captive portal popup issues
  if (server.hasArg("key")) {
    String k = server.arg("key");
    if (k == adminPass || (mustChangeDefaultPass && (k == "admin" || k == "admin1234"))) {
      return true;
    }
  }

  // 2. Standard HTTP Basic Auth
  if (server.authenticate(adminUser, adminPass)) {
    return true;
  }
  // Fallback if password has not been changed yet: allow both "admin" and "admin1234"
  if (mustChangeDefaultPass) {
    if (server.authenticate("admin", "admin") || server.authenticate("admin", "admin1234")) {
      return true;
    }
  }
  server.requestAuthentication(BASIC_AUTH, "SmartFarm Console", "Authentication Required. You can also append ?key=admin to the URL.");
  return false;
}

static const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Farm Local Console</title>
  <style>
    * { box-sizing: border-box; margin:0; padding:0; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif; }
    body { background:#0f172a; color:#f8fafc; padding:16px; }
    .container { max-width:960px; margin:0 auto; }
    h1 { text-align:center; font-size:24px; color:#38bdf8; margin-bottom:16px; }
    .card { background:#1e293b; border-radius:12px; padding:18px; margin-bottom:16px; border:1px solid #334155; }
    .card-title { font-size:18px; font-weight:bold; color:#94a3b8; margin-bottom:12px; display:flex; justify-content:space-between; align-items:center; }
    .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(280px,1fr)); gap:14px; }
    .box { background:#0f172a; border-radius:8px; padding:12px; border:1px solid #334155; margin-bottom:8px; }
    .lbl { font-size:12px; color:#94a3b8; }
    .val { font-size:22px; font-weight:bold; color:#38bdf8; margin:4px 0; }
    .badge { font-size:11px; padding:3px 8px; border-radius:12px; font-weight:bold; }
    .badge-ok { background:#065f46; color:#34d399; }
    .badge-err { background:#7f1d1d; color:#f87171; }
    .badge-warn { background:#78350f; color:#fcd34d; }
    .btn { padding:8px 14px; border-radius:6px; border:none; font-weight:bold; cursor:pointer; margin:4px 2px; }
    .btn-green { background:#22c55e; color:white; }
    .btn-gray { background:#475569; color:white; }
    .btn-red { background:#ef4444; color:white; }
    .btn-blue { background:#0284c7; color:white; }
    input, select { width:100%; padding:8px; border-radius:6px; border:1px solid #475569; background:#0f172a; color:white; margin:4px 0 10px 0; }
    .tab { overflow:hidden; border-bottom:1px solid #334155; margin-bottom:14px; }
    .tab button { background:inherit; float:left; border:none; outline:none; cursor:pointer; padding:10px 16px; color:#94a3b8; font-weight:bold; }
    .tab button.active { color:#38bdf8; border-bottom:2px solid #38bdf8; }
    .tabcontent { display:none; }
    .guide-box { background:#1e1b4b; border-left:4px solid #818cf8; padding:10px; font-size:12px; border-radius:4px; margin-bottom:12px; color:#c7d2fe; line-height:1.5; }
    .alert-box { background:#7f1d1d; border-left:4px solid #ef4444; padding:12px; font-size:13px; border-radius:6px; margin-bottom:14px; color:#fecaca; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🌱 Smart Farm 2 แปลง (Control Console)</h1>
)rawliteral";

static const char HTML_FOOTER[] PROGMEM = R"rawliteral(
  </div>
  <script>
    function openTab(evt, tabName) {
      var i, tabcontent, tablinks;
      tabcontent = document.getElementsByClassName("tabcontent");
      for (i = 0; i < tabcontent.length; i++) tabcontent[i].style.display = "none";
      tablinks = document.getElementsByClassName("tablinks");
      for (i = 0; i < tablinks.length; i++) tablinks[i].className = tablinks[i].className.replace(" active", "");
      var target = document.getElementById(tabName);
      if (target) target.style.display = "block";
      if (evt && evt.currentTarget) {
        evt.currentTarget.className += " active";
      } else {
        var btn = document.getElementById("tabBtn_" + tabName);
        if (btn) btn.className += " active";
      }
    }
    window.addEventListener('load', function() {
      var params = new URLSearchParams(window.location.search);
      var t = params.get('tab');
      if (t && document.getElementById(t)) {
        openTab(null, t);
      }
    });

    const soilData = {
      0: { low: 30.0, high: 45.0, crit: 15.0 }, // Sandy Loam
      1: { low: 42.0, high: 60.0, crit: 22.0 }, // Loam
      2: { low: 52.0, high: 70.0, crit: 32.0 }, // Clay Loam
      3: { low: 55.0, high: 75.0, crit: 35.0 }  // Cocopeat
    };

    function applyPresets(prefix) {
      const sType = parseInt(document.getElementById(prefix + 'soil').value);
      const cType = parseInt(document.getElementById(prefix + 'crop').value);

      if (sType !== 4 && soilData[sType]) {
        let bLow = soilData[sType].low;
        let bHigh = soilData[sType].high;
        let bCrit = soilData[sType].crit;

        document.getElementById(prefix + 'low').value = bLow.toFixed(1);
        document.getElementById(prefix + 'high').value = bHigh.toFixed(1);
        document.getElementById(prefix + 'crit').value = bCrit.toFixed(1);

        if (cType === 0) { // Leafy
          setStage(prefix, 0, 10, bLow + 5, bHigh, 2, 8, 15);
          setStage(prefix, 1, 25, bLow + 3, bHigh, 2, 8, 20);
          setStage(prefix, 2, 40, bLow, bHigh, 3, 8, 25);
          setStage(prefix, 3, 50, bLow, bHigh, 2, 8, 20);
          document.getElementById(prefix + 'pulse').value = 2;
          document.getElementById(prefix + 'soak').value = 8;
          document.getElementById(prefix + 'maxDaily').value = 20;
          document.getElementById(prefix + 'maxTemp').value = 35.0;
          document.getElementById(prefix + 'manualCut').value = 15;
        } else if (cType === 1) { // Solanaceae
          setStage(prefix, 0, 15, bLow + 5, bHigh, 2, 10, 20);
          setStage(prefix, 1, 45, bLow, bHigh, 3, 10, 30);
          setStage(prefix, 2, 85, bLow, bHigh + 3, 4, 12, 45);
          setStage(prefix, 3, 120, bLow - 3, bHigh - 2, 3, 10, 30);
          document.getElementById(prefix + 'pulse').value = 3;
          document.getElementById(prefix + 'soak').value = 10;
          document.getElementById(prefix + 'maxDaily').value = 30;
          document.getElementById(prefix + 'maxTemp').value = 36.0;
          document.getElementById(prefix + 'manualCut').value = 20;
        } else if (cType === 2) { // Vine / Cucurbits
          setStage(prefix, 0, 15, bLow + 5, bHigh, 2, 10, 20);
          setStage(prefix, 1, 40, bLow, bHigh, 3, 12, 35);
          setStage(prefix, 2, 70, bLow, bHigh + 5, 4, 15, 50);
          setStage(prefix, 3, 85, bLow - 8, bHigh - 10, 2, 15, 25);
          document.getElementById(prefix + 'pulse').value = 3;
          document.getElementById(prefix + 'soak').value = 12;
          document.getElementById(prefix + 'maxDaily').value = 35;
          document.getElementById(prefix + 'maxTemp').value = 36.0;
          document.getElementById(prefix + 'manualCut').value = 20;
        } else if (cType === 3) { // Herbs
          setStage(prefix, 0, 15, bLow + 3, bHigh, 2, 12, 15);
          setStage(prefix, 1, 45, bLow - 3, bHigh - 3, 2, 15, 20);
          setStage(prefix, 2, 80, bLow - 5, bHigh - 5, 2, 15, 25);
          setStage(prefix, 3, 120, bLow - 5, bHigh - 5, 2, 15, 20);
          document.getElementById(prefix + 'pulse').value = 2;
          document.getElementById(prefix + 'soak').value = 15;
          document.getElementById(prefix + 'maxDaily').value = 20;
          document.getElementById(prefix + 'maxTemp').value = 35.0;
          document.getElementById(prefix + 'manualCut').value = 15;
        }
      }
    }

    function setStage(prefix, idx, dap, low, high, pulse, soak, maxDaily) {
      document.getElementById(prefix + 's' + idx + '_dap').value = dap;
      document.getElementById(prefix + 's' + idx + '_low').value = low.toFixed(1);
      document.getElementById(prefix + 's' + idx + '_high').value = high.toFixed(1);
      document.getElementById(prefix + 's' + idx + '_pulse').value = pulse;
      document.getElementById(prefix + 's' + idx + '_soak').value = soak;
      document.getElementById(prefix + 's' + idx + '_maxDaily').value = maxDaily;
    }
  </script>
</body>
</html>
)rawliteral";

static void handleRoot() {
  if (!checkAuth()) return;

  String s;
  s.reserve(32768); // Pre-allocate heap to avoid dozens of reallocations
  s += FPSTR(HTML_HEADER);

  // Security warning if using default password
  if (mustChangeDefaultPass) {
    s += "<div class='alert-box'>⚠️ <b>ความปลอดภัย:</b> กำลังใช้รหัสผ่านเริ่มต้น <code>admin</code> กรุณาเปลี่ยนรหัสผ่านในแท็บ <b>Network & Security</b> ทันที</div>";
  }

  // Navigation Tabs
  s += "<div class='tab'>";
  s += "<button id='tabBtn_Dashboard' class='tablinks active' onclick=\"openTab(event, 'Dashboard')\">📊 Dashboard & Manual</button>";
  s += "<button id='tabBtn_GrowthStages' class='tablinks' onclick=\"openTab(event, 'GrowthStages')\">🌾 Crop & Soil Presets</button>";
  s += "<button id='tabBtn_Network' class='tablinks' onclick=\"openTab(event, 'Network')\">⚙️ Network & Security</button>";
  s += "<button id='tabBtn_Logs' class='tablinks' onclick=\"openTab(event, 'Logs')\">📋 System Logs</button>";
  s += "</div>";

  // --- TAB 1: DASHBOARD ---
  s += "<div id='Dashboard' class='tabcontent' style='display:block;'>";

  // Master Control Card
  s += "<div class='card' style='display:flex; justify-content:space-between; align-items:center;'>";
  s += "<div><b>ระบบหลัก (Master Enable): </b>" + String(masterEnable ? "<span class='badge badge-ok'>เปิดทำงาน</span>" : "<span class='badge badge-err'>ปิดพักระบบ</span>") + "</div>";
  s += "<form method='POST' action='/cmd' style='display:inline;'>";
  s += "<input type='hidden' name='master' value='" + String(masterEnable ? 0 : 1) + "'>";
  s += "<button type='submit' class='btn " + String(masterEnable ? "btn-red" : "btn-green") + "'>" + (masterEnable ? "ปิด Master" : "เปิด Master") + "</button>";
  s += "</form>";
  s += "</div>";

  s += "<div class='grid'>";

  // Zone Left Card
  s += "<div class='card'><div class='card-title'><span>🌿 แปลงซ้าย (Zone Left)</span>";
  s += zoneLeft.sensorOnline ? "<span class='badge badge-ok'>ONLINE</span>" : "<span class='badge badge-err'>OFFLINE</span>";
  s += "</div>";
  s += "<div class='box'><div class='lbl'>ความชื้นดิน</div><div class='val'>" + String(zoneLeft.humidity, 1) + " %</div></div>";
  s += "<div class='box'><div class='lbl'>อุณหภูมิดิน</div><div class='val'>" + String(zoneLeft.temperature, 1) + " °C</div></div>";
  s += "<div class='box'><div class='lbl'>สถานะการรด / DAP</div><div class='val' style='font-size:16px;'>";
  s += (zoneLeft.state == STATE_WATERING ? "💧 WATERING" : (zoneLeft.state == STATE_SOAKING ? "⏳ SOAKING" : (zoneLeft.state == STATE_FAULT ? "🚨 FAULT" : "IDLE")));
  if (configLeft.selectedStage == STAGE_UNPLANTED) {
    s += " | <span style='color:#94a3b8;'>DAP: - (🚫 ไม่ได้ปลูก)</span></div></div>";
  } else {
    s += " | DAP: " + String(zoneLeft.currentDAP) + " วัน (Stage " + String(configLeft.selectedStage + 1) + ")</div></div>";
  }

  s += "<div class='box'><div class='lbl'>เลือกระยะการเติบโต (Active Stage)</div>";
  s += "<form method='POST' action='/cmd' style='display:flex; gap:6px; align-items:center; margin-top:4px;'>";
  s += "<input type='hidden' name='zone' value='0'>";
  s += "<select name='stage' style='margin:0; padding:6px;'>";
  s += "<option value='0' " + String(configLeft.selectedStage == STAGE_UNPLANTED ? "selected" : "") + ">🚫 ไม่ได้ปลูก (พักแปลง / ยังไม่ลงพืช)</option>";
  s += "<option value='1' " + String(configLeft.selectedStage == 0 ? "selected" : "") + ">ระยะ 1: ตั้งตัว/ต้นกล้า</option>";
  s += "<option value='2' " + String(configLeft.selectedStage == 1 ? "selected" : "") + ">ระยะ 2: เจริญเติบโต</option>";
  s += "<option value='3' " + String(configLeft.selectedStage == 2 ? "selected" : "") + ">ระยะ 3: ออกดอก/ผล</option>";
  s += "<option value='4' " + String(configLeft.selectedStage == 3 ? "selected" : "") + ">ระยะ 4: เก็บเกี่ยว/สุกแก่</option>";
  s += "</select>";
  s += "<button type='submit' class='btn btn-blue' style='margin:0; padding:6px 12px; font-size:12px;'>เลือก</button>";
  s += "</form></div>";

  if (zoneLeft.lockoutUntilMs > millis()) {
    unsigned long remainMin = (zoneLeft.lockoutUntilMs - millis()) / 60000UL + 1;
    s += "<div class='box' style='border-color:#ef4444;'><div class='lbl' style='color:#f87171;'>⚠️ Lockout (No-moisture-rise)</div><div style='font-size:13px; color:#fca5a5;'>ระงับรดอัตโนมัติอีก " + String(remainMin) + " นาที</div></div>";
  }

  s += "<div class='box'><div class='lbl'>เวลารดสะสมวันนี้</div><div style='font-size:14px; color:#cbd5e1;'>" + String(zoneLeft.dailyRuntimeSeconds / 60) + " / " + String(configLeft.maxDailyRuntimeMinutes) + " นาที</div></div>";

  s += "<p style='margin:10px 0;'>โหมด: <b>" + String(zoneLeft.mode == MODE_AUTO ? "AUTO" : (zoneLeft.mode == MODE_MANUAL ? "MANUAL" : "OFF")) + "</b></p>";
  s += "<div style='display:flex; gap:4px;'>";
  s += "<form method='POST' action='/cmd' style='display:inline;'><input type='hidden' name='zone' value='0'><input type='hidden' name='mode' value='1'><button type='submit' class='btn btn-blue'>Auto</button></form>";
  s += "<form method='POST' action='/cmd' style='display:inline;'><input type='hidden' name='zone' value='0'><input type='hidden' name='mode' value='2'><button type='submit' class='btn btn-gray'>Manual</button></form>";
  s += "<form method='POST' action='/cmd' style='display:inline;'><input type='hidden' name='zone' value='0'><input type='hidden' name='mode' value='0'><button type='submit' class='btn btn-red'>Off</button></form>";
  s += "</div>";

  if (zoneLeft.mode == MODE_MANUAL) {
    s += "<div style='margin-top:10px; padding-top:8px; border-top:1px solid #334155;'>";
    if (zoneLeft.manualValveOpen) {
      s += "<form method='POST' action='/cmd'><input type='hidden' name='zone' value='0'><input type='hidden' name='valve' value='0'><button type='submit' class='btn btn-red' style='width:100%;'>🔴 ปิดวาล์วซ้าย</button></form>";
    } else {
      s += "<form method='POST' action='/cmd'><input type='hidden' name='zone' value='0'><input type='hidden' name='valve' value='1'><button type='submit' class='btn btn-green' style='width:100%;'>🟢 เปิดวาล์วซ้าย</button></form>";
    }
    s += "</div>";
  }
  s += "</div>";

  // Zone Right Card
  s += "<div class='card'><div class='card-title'><span>🌿 แปลงขวา (Zone Right)</span>";
  s += zoneRight.sensorOnline ? "<span class='badge badge-ok'>ONLINE</span>" : "<span class='badge badge-err'>OFFLINE</span>";
  s += "</div>";
  s += "<div class='box'><div class='lbl'>ความชื้นดิน</div><div class='val'>" + String(zoneRight.humidity, 1) + " %</div></div>";
  s += "<div class='box'><div class='lbl'>อุณหภูมิดิน</div><div class='val'>" + String(zoneRight.temperature, 1) + " °C</div></div>";
  s += "<div class='box'><div class='lbl'>สถานะการรด / DAP</div><div class='val' style='font-size:16px;'>";
  s += (zoneRight.state == STATE_WATERING ? "💧 WATERING" : (zoneRight.state == STATE_SOAKING ? "⏳ SOAKING" : (zoneRight.state == STATE_FAULT ? "🚨 FAULT" : "IDLE")));
  if (configRight.selectedStage == STAGE_UNPLANTED) {
    s += " | <span style='color:#94a3b8;'>DAP: - (🚫 ไม่ได้ปลูก)</span></div></div>";
  } else {
    s += " | DAP: " + String(zoneRight.currentDAP) + " วัน (Stage " + String(configRight.selectedStage + 1) + ")</div></div>";
  }

  s += "<div class='box'><div class='lbl'>เลือกระยะการเติบโต (Active Stage)</div>";
  s += "<form method='POST' action='/cmd' style='display:flex; gap:6px; align-items:center; margin-top:4px;'>";
  s += "<input type='hidden' name='zone' value='1'>";
  s += "<select name='stage' style='margin:0; padding:6px;'>";
  s += "<option value='0' " + String(configRight.selectedStage == STAGE_UNPLANTED ? "selected" : "") + ">🚫 ไม่ได้ปลูก (พักแปลง / ยังไม่ลงพืช)</option>";
  s += "<option value='1' " + String(configRight.selectedStage == 0 ? "selected" : "") + ">ระยะ 1: ตั้งตัว/ต้นกล้า</option>";
  s += "<option value='2' " + String(configRight.selectedStage == 1 ? "selected" : "") + ">ระยะ 2: เจริญเติบโต</option>";
  s += "<option value='3' " + String(configRight.selectedStage == 2 ? "selected" : "") + ">ระยะ 3: ออกดอก/ผล</option>";
  s += "<option value='4' " + String(configRight.selectedStage == 3 ? "selected" : "") + ">ระยะ 4: เก็บเกี่ยว/สุกแก่</option>";
  s += "</select>";
  s += "<button type='submit' class='btn btn-blue' style='margin:0; padding:6px 12px; font-size:12px;'>เลือก</button>";
  s += "</form></div>";

  if (zoneRight.lockoutUntilMs > millis()) {
    unsigned long remainMin = (zoneRight.lockoutUntilMs - millis()) / 60000UL + 1;
    s += "<div class='box' style='border-color:#ef4444;'><div class='lbl' style='color:#f87171;'>⚠️ Lockout (No-moisture-rise)</div><div style='font-size:13px; color:#fca5a5;'>ระงับรดอัตโนมัติอีก " + String(remainMin) + " นาที</div></div>";
  }

  s += "<div class='box'><div class='lbl'>เวลารดสะสมวันนี้</div><div style='font-size:14px; color:#cbd5e1;'>" + String(zoneRight.dailyRuntimeSeconds / 60) + " / " + String(configRight.maxDailyRuntimeMinutes) + " นาที</div></div>";

  s += "<p style='margin:10px 0;'>โหมด: <b>" + String(zoneRight.mode == MODE_AUTO ? "AUTO" : (zoneRight.mode == MODE_MANUAL ? "MANUAL" : "OFF")) + "</b></p>";
  s += "<div style='display:flex; gap:4px;'>";
  s += "<form method='POST' action='/cmd' style='display:inline;'><input type='hidden' name='zone' value='1'><input type='hidden' name='mode' value='1'><button type='submit' class='btn btn-blue'>Auto</button></form>";
  s += "<form method='POST' action='/cmd' style='display:inline;'><input type='hidden' name='zone' value='1'><input type='hidden' name='mode' value='2'><button type='submit' class='btn btn-gray'>Manual</button></form>";
  s += "<form method='POST' action='/cmd' style='display:inline;'><input type='hidden' name='zone' value='1'><input type='hidden' name='mode' value='0'><button type='submit' class='btn btn-red'>Off</button></form>";
  s += "</div>";

  if (zoneRight.mode == MODE_MANUAL) {
    s += "<div style='margin-top:10px; padding-top:8px; border-top:1px solid #334155;'>";
    if (zoneRight.manualValveOpen) {
      s += "<form method='POST' action='/cmd'><input type='hidden' name='zone' value='1'><input type='hidden' name='valve' value='0'><button type='submit' class='btn btn-red' style='width:100%;'>🔴 ปิดวาล์วขวา</button></form>";
    } else {
      s += "<form method='POST' action='/cmd'><input type='hidden' name='zone' value='1'><input type='hidden' name='valve' value='1'><button type='submit' class='btn btn-green' style='width:100%;'>🟢 เปิดวาล์วขวา</button></form>";
    }
    s += "</div>";
  }
  s += "</div>";
  s += "</div>"; // end grid

  // Relay Controls (CH3-CH8)
  s += "<div class='card' style='margin-top:14px;'><div class='card-title'>⚡ ควบคุมรีเลย์ทั่วไป (CH3 - CH8) <span style='font-size:12px; font-weight:normal; color:#64748b;'>*CH1 และ CH2 สงวนเฉพาะวาล์ว</span></div><div>";
  for (int i = 3; i <= 8; i++) {
    bool st = Relay_GetChannel(i);
    s += "<form method='POST' action='/cmd' style='display:inline;'>";
    s += "<input type='hidden' name='relay' value='" + String(i) + "'>";
    s += "<input type='hidden' name='state' value='" + String(st ? 0 : 1) + "'>";
    s += "<button type='submit' class='btn " + String(st ? "btn-green" : "btn-gray") + "'>CH " + String(i) + ": " + (st ? "ON" : "OFF") + "</button>";
    s += "</form>";
  }
  s += "</div></div>";
  s += "</div>"; // end Tab 1

  // --- TAB 2: GROWTH STAGES & PRESETS ---
  s += "<div id='GrowthStages' class='tabcontent'>";
  s += "<form method='POST' action='/saveStages'>";
  s += "<div class='card'><div class='card-title'>🌾 โปรไฟล์ดินและกลุ่มพืช (Soil & Crop Presets)</div>";
  s += "<div class='guide-box'>💡 <b>คำแนะนำ:</b> เลือกชนิดดินและกลุ่มพืช ระบบจะคำนวณเกณฑ์ Low, High, Critical และจังหวะรดน้ำให้อัตโนมัติ หรือเลือก <b>กำหนดเอง (Custom)</b> เพื่อระบุตัวเลขอิสระ</div>";

  for (int z = 0; z < 2; z++) {
    ZoneConfig& cfg = (z == 0) ? configLeft : configRight;
    String pfx = (z == 0) ? "L_" : "R_";
    s += "<h3 style='color:#38bdf8; margin:16px 0 8px 0; border-bottom:1px solid #334155; padding-bottom:4px;'>" + String(z == 0 ? "🌿 แปลงซ้าย (Zone Left)" : "🌿 แปลงขวา (Zone Right)") + "</h3>";

    s += "<div style='display:grid; grid-template-columns:1fr 1fr; gap:10px;'>";
    s += "<div><label class='lbl'>1. ชนิดดินในแปลง (Soil Preset)</label><select name='" + pfx + "soil' id='" + pfx + "soil' onchange=\"applyPresets('" + pfx + "')\">";
    s += "<option value='0' " + String(cfg.soilType == SOIL_SANDY_LOAM ? "selected" : "") + ">ดินร่วนปนทราย (Sandy Loam)</option>";
    s += "<option value='1' " + String(cfg.soilType == SOIL_LOAM ? "selected" : "") + ">ดินร่วนมาตรฐาน (Loam)</option>";
    s += "<option value='2' " + String(cfg.soilType == SOIL_CLAY_LOAM ? "selected" : "") + ">ดินเหนียว / ร่วนเหนียว (Clay Loam)</option>";
    s += "<option value='3' " + String(cfg.soilType == SOIL_COCOPEAT ? "selected" : "") + ">วัสดุปลูก / ขุยมะพร้าว (Cocopeat)</option>";
    s += "<option value='4' " + String(cfg.soilType == SOIL_CUSTOM ? "selected" : "") + ">กำหนดเองอิสระ (Custom)</option>";
    s += "</select></div>";

    s += "<div><label class='lbl'>2. กลุ่มพืช (Crop Group)</label><select name='" + pfx + "crop' id='" + pfx + "crop' onchange=\"applyPresets('" + pfx + "')\">";
    s += "<option value='0' " + String(cfg.cropGroup == CROP_LEAFY ? "selected" : "") + ">ผักกินใบและผักสวนครัวทั่วไป</option>";
    s += "<option value='1' " + String(cfg.cropGroup == CROP_SOLANACEAE ? "selected" : "") + ">พืชตระกูลมะเขือและพริก</option>";
    s += "<option value='2' " + String(cfg.cropGroup == CROP_VINE_FRUIT ? "selected" : "") + ">พืชเถาเลื้อยและพืชผล (เมล่อน/แตง)</option>";
    s += "<option value='3' " + String(cfg.cropGroup == CROP_HERB_AROMA ? "selected" : "") + ">สมุนไพรและผักกลิ่นแรง</option>";
    s += "<option value='4' " + String(cfg.cropGroup == CROP_CUSTOM ? "selected" : "") + ">กำหนดเองอิสระ (Custom)</option>";
    s += "</select></div>";
    s += "</div>";

    s += "<label class='lbl'>ชื่อพืช</label><input type='text' name='" + pfx + "name' value='" + String(cfg.cropName) + "'>";
    s += "<div style='display:grid; grid-template-columns:2fr 1fr; gap:10px;'>";
    s += "<div><label class='lbl'>วันที่เริ่มปลูก (YYYY-MM-DD)</label><input type='text' name='" + pfx + "date' value='" + String(cfg.startYear) + "-" + String(cfg.startMonth) + "-" + String(cfg.startDay) + "'></div>";
    s += "<div><label class='lbl'>DAP Offset (วัน)</label><input type='number' name='" + pfx + "offset' value='" + String(cfg.dapOffset) + "'></div>";
    s += "</div>";
    s += "<div style='text-align:right; margin:-4px 0 10px 0;'>";
    s += "<button type='button' class='btn btn-blue' style='font-size:12px; padding:6px 12px;' onclick=\"if(confirm('ยืนยันเริ่มรอบปลูกใหม่สำหรับแปลงนี้วันนี้หรือไม่? (ระบบจะตั้งวันที่เริ่มเป็นวันนี้ DAP=0 และเลือก Stage 1)')) { var f=document.createElement('form'); f.method='POST'; f.action='/resetDap'; var i=document.createElement('input'); i.type='hidden'; i.name='zone'; i.value='" + String(z) + "'; f.appendChild(i); document.body.appendChild(f); f.submit(); }\">🌱 เริ่มรอบปลูกใหม่วันนี้ (Reset DAP = 0)</button>";
    s += "</div>";

    // Base Thresholds & Overrides
    s += "<div style='background:#0f172a; padding:10px; border-radius:6px; margin:8px 0; border:1px solid #334155;'>";
    s += "<div class='lbl' style='font-weight:bold; color:#38bdf8; margin-bottom:6px;'>เกณฑ์หลัก / Override ทั่วไป</div>";
    s += "<div style='display:grid; grid-template-columns:repeat(auto-fit, minmax(130px, 1fr)); gap:6px;'>";
    s += "<div><label class='lbl'>เริ่มรด (Low %)</label><input type='number' step='0.1' name='" + pfx + "low' id='" + pfx + "low' value='" + String(cfg.moistureLow, 1) + "'></div>";
    s += "<div><label class='lbl'>หยุดรด (High %)</label><input type='number' step='0.1' name='" + pfx + "high' id='" + pfx + "high' value='" + String(cfg.moistureHigh, 1) + "'></div>";
    s += "<div><label class='lbl'>วิกฤต (Crit %)</label><input type='number' step='0.1' name='" + pfx + "crit' id='" + pfx + "crit' value='" + String(cfg.moistureCritical, 1) + "'></div>";
    s += "<div><label class='lbl'>Pulse (นาที)</label><input type='number' name='" + pfx + "pulse' id='" + pfx + "pulse' value='" + String(cfg.pulseOnMinutes) + "'></div>";
    s += "<div><label class='lbl'>Soak (นาที)</label><input type='number' name='" + pfx + "soak' id='" + pfx + "soak' value='" + String(cfg.soakMinutes) + "'></div>";
    s += "<div><label class='lbl'>Max Daily (นาที)</label><input type='number' name='" + pfx + "maxDaily' id='" + pfx + "maxDaily' value='" + String(cfg.maxDailyRuntimeMinutes) + "'></div>";
    s += "<div><label class='lbl'>เตือน Temp (°C)</label><input type='number' step='0.1' name='" + pfx + "maxTemp' id='" + pfx + "maxTemp' value='" + String(cfg.maxSoilTemp, 1) + "'></div>";
    s += "<div><label class='lbl'>ตัด Manual (นาที)</label><input type='number' name='" + pfx + "manualCut' id='" + pfx + "manualCut' value='" + String(cfg.manualDurationMinutes) + "'></div>";
    s += "</div></div>";

    s += "<label class='lbl'>การใช้ระบบ 4 ช่วงอายุ (Growth Stages)</label>";
    s += "<select name='" + pfx + "useStages'><option value='0' " + String(!cfg.useGrowthStages ? "selected" : "") + ">ปิด (ใช้ค่าคงที่ด้านบนตลอดรอบปลูก)</option>";
    s += "<option value='1' " + String(cfg.useGrowthStages ? "selected" : "") + ">เปิดใช้งาน 4 Stages อัตโนมัติตามอายุ DAP</option></select>";

    for (int i = 0; i < 4; i++) {
      s += "<div class='box' style='margin-bottom:8px;'><div class='lbl' style='font-weight:bold; color:#38bdf8;'>ระยะที่ " + String(i+1) + "</div>";
      s += "<div style='display:grid; grid-template-columns:repeat(auto-fit, minmax(110px, 1fr)); gap:6px;'>";
      s += "<div><label class='lbl'>ถึง DAP</label><input type='number' name='" + pfx + "s" + String(i) + "_dap' id='" + pfx + "s" + String(i) + "_dap' value='" + String(cfg.stages[i].endDAP) + "'></div>";
      s += "<div><label class='lbl'>Low %</label><input type='number' step='0.1' name='" + pfx + "s" + String(i) + "_low' id='" + pfx + "s" + String(i) + "_low' value='" + String(cfg.stages[i].moistureLow, 1) + "'></div>";
      s += "<div><label class='lbl'>High %</label><input type='number' step='0.1' name='" + pfx + "s" + String(i) + "_high' id='" + pfx + "s" + String(i) + "_high' value='" + String(cfg.stages[i].moistureHigh, 1) + "'></div>";
      s += "<div><label class='lbl'>Pulse (น.)</label><input type='number' name='" + pfx + "s" + String(i) + "_pulse' id='" + pfx + "s" + String(i) + "_pulse' value='" + String(cfg.stages[i].pulseOnMinutes) + "'></div>";
      s += "<div><label class='lbl'>Soak (น.)</label><input type='number' name='" + pfx + "s" + String(i) + "_soak' id='" + pfx + "s" + String(i) + "_soak' value='" + String(cfg.stages[i].soakMinutes) + "'></div>";
      s += "<div><label class='lbl'>Max Daily (น.)</label><input type='number' name='" + pfx + "s" + String(i) + "_maxDaily' id='" + pfx + "s" + String(i) + "_maxDaily' value='" + String(cfg.stages[i].maxDailyRuntimeMinutes) + "'></div>";
      s += "</div></div>";
    }
  }

  s += "<button type='submit' class='btn btn-green' style='width:100%; margin-top:10px;'>💾 บันทึกโปรไฟล์พืชและ Stages</button>";
  s += "</div></form></div>";

  // --- TAB 3: NETWORK & SECURITY ---
  s += "<div id='Network' class='tabcontent'>";
  s += "<form method='POST' action='/saveNetwork'>";
  s += "<div class='card'><div class='card-title'>⚙️ ตั้งค่าเครือข่าย & Blynk Auth Token</div>";
  s += "<label class='lbl'>Wi-Fi SSID</label><input type='text' name='ssid' value='" + String(wifiSSID) + "'>";
  s += "<label class='lbl'>Wi-Fi Password</label><input type='password' name='pass' placeholder='(ไม่เปลี่ยน)'>";
  s += "<label class='lbl'>AP Wi-Fi Password (อย่างน้อย 8 ตัวอักษร)</label><input type='password' name='ap_pass' placeholder='(ไม่เปลี่ยน, ปัจจุบันตั้งใน NVS)'>";
  s += "<label class='lbl'>Blynk Device Auth Token</label><input type='password' name='auth' placeholder='(ไม่เปลี่ยน)'>";

  s += "<div class='card-title' style='margin-top:16px;'>🔒 ความปลอดภัยของ Web Portal</div>";
  s += "<label class='lbl'>Username (HTTP Basic Auth)</label><input type='text' name='admin_user' value='" + String(adminUser) + "' readonly style='color:#64748b;'>";
  s += "<label class='lbl'>รหัสผ่านใหม่สำหรับ Admin</label><input type='password' name='admin_pass' placeholder='(ไม่เปลี่ยน)'>";
  s += "<label class='lbl'>ยืนยันรหัสผ่านใหม่</label><input type='password' name='admin_pass_confirm' placeholder='(ไม่เปลี่ยน)'>";

  s += "<button type='submit' class='btn btn-green' style='width:100%; margin-top:10px;'>💾 บันทึกการตั้งค่าและรีสตาร์ท</button>";
  s += "</div></form>";

  // Factory Reset
  s += "<div class='card' style='border-color:#7f1d1d;'><div class='card-title' style='color:#f87171;'>⚠️ Factory Reset</div>";
  s += "<p style='font-size:13px; color:#94a3b8; margin-bottom:10px;'>ล้างการตั้งค่าทั้งหมดกลับเป็นค่าเริ่มต้นโรงงาน</p>";
  s += "<form method='POST' action='/factoryReset' onsubmit=\"return confirm('ยืนยันล้างการตั้งค่าทั้งหมดกลับสู่ค่าเริ่มต้นโรงงานหรือไม่?')\">";
  s += "<button type='submit' class='btn btn-red'>Factory Reset</button></form>";
  s += "</div></div>";

  // --- TAB 4: SYSTEM LOGS ---
  s += "<div id='Logs' class='tabcontent'>";
  s += "<div class='card'><div class='card-title'><span>📋 บันทึกประวัติการทำงาน (System Debug Logs)</span></div>";
  s += "<div class='guide-box'>💡 <b>บันทึกประวัติการทำงาน 2 วัน:</b> บันทึกลง Flash อัตโนมัติทุก 1 ชั่วโมง และบันทึกทันทีเมื่อเปิด/ปิดวาล์วหรือเกิดเหตุขัดข้อง สามารถดาวน์โหลดเป็นไฟล์ .txt มาเก็บไว้ดูในคอมพิวเตอร์ได้</div>";

  size_t totalBytes = AppLog_GetTotalSize();
  float totalKB = totalBytes / 1024.0f;

  s += "<div style='display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px; margin-bottom:14px; background:#0f172a; padding:12px; border-radius:8px; border:1px solid #334155;'>";
  s += "<div><span class='lbl'>ขนาด Log ใน Flash: </span><b style='color:#38bdf8; font-size:16px;'>" + String(totalKB, 1) + " KB</b> <span style='font-size:12px; color:#64748b;'>(หมุนเวียนอัตโนมัติสูงสุด 150 KB)</span></div>";
  s += "<div>";
  s += "<a href='/downloadLog' class='btn btn-green' style='text-decoration:none; display:inline-block; font-size:13px;'>📥 ดาวน์โหลด Log (.txt)</a>";
  s += "<a href='/?tab=Logs' class='btn btn-blue' style='text-decoration:none; display:inline-block; font-size:13px;'>🔄 รีเฟรชหน้าจอ</a>";
  s += "<form method='POST' action='/clearLog' onsubmit=\"return confirm('ยืนยันล้างประวัติ Log ทั้งหมดในเครื่องหรือไม่?')\" style='display:inline;'>";
  s += "<button type='submit' class='btn btn-red' style='font-size:13px;'>🗑️ ล้างไฟล์ Log</button>";
  s += "</form>";
  s += "</div></div>";

  s += "<div class='lbl' style='margin-bottom:6px;'>ตัวอย่างบันทึกล่าสุด (Recent Log View):</div>";
  s += "<pre style='background:#020617; border:1px solid #334155; border-radius:8px; padding:12px; color:#38bdf8; font-family:monospace; font-size:12px; white-space:pre-wrap; word-break:break-all; max-height:360px; overflow-y:auto; line-height:1.4;'>";
  s += escapeHtml(AppLog_GetRecent(3000));
  s += "</pre>";
  s += "</div></div>";

  s += FPSTR(HTML_FOOTER);
  server.send(200, "text/html", s);
}

static void handleCmd() {
  if (!checkAuth()) return;

  if (server.hasArg("relay")) {
    int r = server.arg("relay").toInt();
    if (r == 1 || r == 2) {
      server.send(403, "text/plain", "Forbidden: Relay CH1 and CH2 are dedicated to Zone Valves! Control via Zone commands.");
      return;
    }
    if (r < 3 || r > 8) {
      server.send(400, "text/plain", "Bad Request: Relay channel must be 3..8");
      return;
    }
    if (server.hasArg("state")) {
      int st = server.arg("state").toInt();
      Relay_SetChannel(r, st == 1);
      BlynkManager_SendControlState();
    }
  }

  if (server.hasArg("master")) {
    int m = server.arg("master").toInt();
    Storage_SaveMasterEnable(m == 1);
    BlynkManager_SendControlState();
  }

  if (server.hasArg("zone")) {
    int z = server.arg("zone").toInt();
    if (z != 0 && z != 1) {
      server.send(400, "text/plain", "Bad Request: Zone must be 0 or 1");
      return;
    }
    if (server.hasArg("mode")) {
      int m = server.arg("mode").toInt();
      if (m < 0 || m > 2) {
        server.send(400, "text/plain", "Bad Request: Mode must be 0, 1, or 2");
        return;
      }
      ZoneControl_SetMode(z, (ZoneMode)m);
      BlynkManager_SendControlState();
    }
    if (server.hasArg("valve")) {
      int v = server.arg("valve").toInt();
      if (v != 0 && v != 1) {
        server.send(400, "text/plain", "Bad Request: Valve state must be 0 or 1");
        return;
      }
      ZoneControl_SetManualSwitch(z, v == 1);
      BlynkManager_SendControlState();
    }
    if (server.hasArg("stage")) {
      int st = server.arg("stage").toInt();
      if (st == 0) {
        if (z == 0) {
          configLeft.selectedStage = STAGE_UNPLANTED;
          zoneLeft.currentStageIndex = STAGE_UNPLANTED;
          zoneLeft.currentDAP = 0;
          Storage_SaveZoneConfig(0, configLeft);
        } else {
          configRight.selectedStage = STAGE_UNPLANTED;
          zoneRight.currentStageIndex = STAGE_UNPLANTED;
          zoneRight.currentDAP = 0;
          Storage_SaveZoneConfig(1, configRight);
        }
        AppLog_Log("STAGE", "Zone %s set to UNPLANTED (ไม่ได้ปลูก / พักแปลง). DAP stopped.", (z == 0) ? "Left" : "Right");
        BlynkManager_SendStatus();
      } else if (st >= 1 && st <= 4) {
        if (z == 0) {
          configLeft.selectedStage = st - 1;
          zoneLeft.currentStageIndex = st - 1;
          Storage_SaveZoneConfig(0, configLeft);
        } else {
          configRight.selectedStage = st - 1;
          zoneRight.currentStageIndex = st - 1;
          Storage_SaveZoneConfig(1, configRight);
        }
        AppLog_Log("STAGE", "Zone %s set to Stage %d", (z == 0) ? "Left" : "Right", st);
        BlynkManager_SendStatus();
      }
    }
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleSaveNetwork() {
  if (!checkAuth()) return;

  String s = server.arg("ssid");
  String p = server.arg("pass");
  String a = server.arg("auth");

  const char* newSsid = (s.length() > 0) ? s.c_str() : wifiSSID;
  const char* newPass = (p.length() > 0) ? p.c_str() : wifiPass;
  const char* newAuth = (a.length() > 0) ? a.c_str() : blynkAuth;

  Storage_SaveNetwork(newSsid, newPass, newAuth);

  String ap_p = server.arg("ap_pass");
  if (ap_p.length() >= 8) {
    Storage_SaveApPass(ap_p.c_str());
  }

  String adm_p = server.arg("admin_pass");
  String adm_c = server.arg("admin_pass_confirm");
  if (adm_p.length() > 0) {
    if (adm_p != adm_c) {
      server.send(400, "text/html", "<h3>Error: Admin passwords do not match!</h3><br><a href='/'>Go Back</a>");
      return;
    }
    if (adm_p.length() < 4) {
      server.send(400, "text/html", "<h3>Error: Admin password must be at least 4 characters!</h3><br><a href='/'>Go Back</a>");
      return;
    }
    Storage_SaveAdminAuth("admin", adm_p.c_str());
  }

  AppLog_Log("CONFIG", "Network & admin security settings saved by user. Rebooting...");
  server.send(200, "text/html", "<h2>Settings Saved! Rebooting in 3 seconds...</h2><script>setTimeout(function(){location.href='/';},3000);</script>");
  delay(1000);
  ESP.restart();
}

static void handleSaveStages() {
  if (!checkAuth()) return;

  ZoneConfig tempL = configLeft;
  ZoneConfig tempR = configRight;

  // Parse Left
  tempL.soilType = (SoilType)server.arg("L_soil").toInt();
  tempL.cropGroup = (CropGroup)server.arg("L_crop").toInt();
  strncpy(tempL.cropName, server.arg("L_name").c_str(), sizeof(tempL.cropName) - 1);
  sscanf(server.arg("L_date").c_str(), "%d-%d-%d", &tempL.startYear, &tempL.startMonth, &tempL.startDay);
  tempL.dapOffset = server.arg("L_offset").toInt();
  tempL.moistureLow = server.arg("L_low").toFloat();
  tempL.moistureHigh = server.arg("L_high").toFloat();
  tempL.moistureCritical = server.arg("L_crit").toFloat();
  tempL.pulseOnMinutes = server.arg("L_pulse").toInt();
  tempL.soakMinutes = server.arg("L_soak").toInt();
  tempL.maxDailyRuntimeMinutes = server.arg("L_maxDaily").toInt();
  tempL.maxSoilTemp = server.arg("L_maxTemp").toFloat();
  tempL.manualDurationMinutes = server.arg("L_manualCut").toInt();
  tempL.useGrowthStages = (server.arg("L_useStages").toInt() == 1);
  for (int i = 0; i < 4; i++) {
    tempL.stages[i].endDAP = server.arg("L_s" + String(i) + "_dap").toInt();
    tempL.stages[i].moistureLow = server.arg("L_s" + String(i) + "_low").toFloat();
    tempL.stages[i].moistureHigh = server.arg("L_s" + String(i) + "_high").toFloat();
    tempL.stages[i].pulseOnMinutes = server.arg("L_s" + String(i) + "_pulse").toInt();
    tempL.stages[i].soakMinutes = server.arg("L_s" + String(i) + "_soak").toInt();
    tempL.stages[i].maxDailyRuntimeMinutes = server.arg("L_s" + String(i) + "_maxDaily").toInt();
  }

  // Parse Right
  tempR.soilType = (SoilType)server.arg("R_soil").toInt();
  tempR.cropGroup = (CropGroup)server.arg("R_crop").toInt();
  strncpy(tempR.cropName, server.arg("R_name").c_str(), sizeof(tempR.cropName) - 1);
  sscanf(server.arg("R_date").c_str(), "%d-%d-%d", &tempR.startYear, &tempR.startMonth, &tempR.startDay);
  tempR.dapOffset = server.arg("R_offset").toInt();
  tempR.moistureLow = server.arg("R_low").toFloat();
  tempR.moistureHigh = server.arg("R_high").toFloat();
  tempR.moistureCritical = server.arg("R_crit").toFloat();
  tempR.pulseOnMinutes = server.arg("R_pulse").toInt();
  tempR.soakMinutes = server.arg("R_soak").toInt();
  tempR.maxDailyRuntimeMinutes = server.arg("R_maxDaily").toInt();
  tempR.maxSoilTemp = server.arg("R_maxTemp").toFloat();
  tempR.manualDurationMinutes = server.arg("R_manualCut").toInt();
  tempR.useGrowthStages = (server.arg("R_useStages").toInt() == 1);
  for (int i = 0; i < 4; i++) {
    tempR.stages[i].endDAP = server.arg("R_s" + String(i) + "_dap").toInt();
    tempR.stages[i].moistureLow = server.arg("R_s" + String(i) + "_low").toFloat();
    tempR.stages[i].moistureHigh = server.arg("R_s" + String(i) + "_high").toFloat();
    tempR.stages[i].pulseOnMinutes = server.arg("R_s" + String(i) + "_pulse").toInt();
    tempR.stages[i].soakMinutes = server.arg("R_s" + String(i) + "_soak").toInt();
    tempR.stages[i].maxDailyRuntimeMinutes = server.arg("R_s" + String(i) + "_maxDaily").toInt();
  }

  // Validate Left
  String errL;
  if (!Config_Validate(tempL, errL)) {
    server.send(400, "text/html", "<h3>Validation Error in Zone Left Config:</h3><p style='color:#ef4444; font-weight:bold;'>" + errL + "</p><br><a href='/'>Go Back</a>");
    return;
  }

  // Validate Right
  String errR;
  if (!Config_Validate(tempR, errR)) {
    server.send(400, "text/html", "<h3>Validation Error in Zone Right Config:</h3><p style='color:#ef4444; font-weight:bold;'>" + errR + "</p><br><a href='/'>Go Back</a>");
    return;
  }

  // Both valid, save!
  configLeft = tempL;
  zoneLeft.currentStageIndex = configLeft.selectedStage;
  Storage_SaveZoneConfig(0, configLeft);
  configRight = tempR;
  zoneRight.currentStageIndex = configRight.selectedStage;
  Storage_SaveZoneConfig(1, configRight);

  // Sync with Blynk
  BlynkManager_SendZoneConfig(0);
  BlynkManager_SendZoneConfig(1);
  BlynkManager_SendStatus();

  AppLog_Log("CROP", "Crop profiles & growth stages updated by user.");

  server.sendHeader("Location", "/?tab=GrowthStages");
  server.send(303);
}

static void handleDownloadLog() {
  if (!checkAuth()) return;
  AppLog_StreamToClient(server);
}

static void handleClearLog() {
  if (!checkAuth()) return;
  AppLog_Clear();
  server.sendHeader("Location", "/?tab=Logs");
  server.send(303);
}

static void handleResetDap() {
  if (!checkAuth()) return;
  int z = server.arg("zone").toInt();
  if (z != 0 && z != 1) {
    server.send(400, "text/plain", "Bad Request: Zone must be 0 or 1");
    return;
  }
  RTCDateTime now;
  if (!RTC_GetTime(now) || now.year < 2024) {
    server.send(400, "text/html", "<h3>Error: RTC time not valid! Cannot set planting date to today.</h3><br><a href='/?tab=GrowthStages'>Go Back</a>");
    return;
  }
  ZoneConfig& cfg = (z == 0) ? configLeft : configRight;
  cfg.startYear = now.year;
  cfg.startMonth = now.month;
  cfg.startDay = now.day;
  cfg.dapOffset = 0;
  cfg.selectedStage = 0;
  Storage_SaveZoneConfig(z, cfg);
  if (z == 0) {
    zoneLeft.currentDAP = 0;
    zoneLeft.currentStageIndex = 0;
  } else {
    zoneRight.currentDAP = 0;
    zoneRight.currentStageIndex = 0;
  }
  AppLog_Log("CROP", "Zone %s crop cycle reset. Start date: %04d-%02d-%02d, DAP: 0, Stage: 1",
             (z == 0) ? "Left" : "Right", now.year, now.month, now.day);
  BlynkManager_SendZoneConfig(z);
  BlynkManager_SendStatus();
  server.sendHeader("Location", "/?tab=GrowthStages");
  server.send(303);
}

static void handleFactoryReset() {
  if (!checkAuth()) return;
  AppLog_Log("SYSTEM", "Factory reset triggered by user. Rebooting...");
  Storage_FactoryReset();
  server.send(200, "text/html", "<h2>Factory Reset Complete! Rebooting in 3 seconds...</h2><script>setTimeout(function(){location.href='/';},3000);</script>");
  delay(1000);
  ESP.restart();
}

void WebPortal_Init() {
  const char* authHeaders[] = {"Authorization"};
  server.collectHeaders(authHeaders, 1);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/cmd", HTTP_POST, handleCmd);
  server.on("/saveNetwork", HTTP_POST, handleSaveNetwork);
  server.on("/saveStages", HTTP_POST, handleSaveStages);
  server.on("/factoryReset", HTTP_POST, handleFactoryReset);
  server.on("/downloadLog", HTTP_GET, handleDownloadLog);
  server.on("/clearLog", HTTP_POST, handleClearLog);
  server.on("/resetDap", HTTP_POST, handleResetDap);
  server.begin();
  Serial.println("[WebPortal] HTTP Server started with Basic Auth (Authorization header collected).");
}

void WebPortal_Update() {
  server.handleClient();
}
