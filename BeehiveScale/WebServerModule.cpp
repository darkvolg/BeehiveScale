#include "WebServerModule.h"
#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
using WebServerCompat = ESP8266WebServer;
#else
#include <WebServer.h>
#include <WiFi.h>
using WebServerCompat = WebServer;
#endif
#include <ArduinoJson.h>   // ArduinoJson v6 — установить через Library Manager
#include "Memory.h"
#include "Connectivity.h"  // для ntp_sync_time()
#include "Logger.h"
#ifdef USE_SD_CARD
#include <SPI.h>
#include <SD.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#define LOG_FS LittleFS
#elif defined(ESP32)
#include <SPIFFS.h>
#define LOG_FS SPIFFS
#endif

static WebServerCompat _srv(WEB_SERVER_PORT);
static WebData    _wd;
static WebActions _wa;

// ─── Basic Auth проверка ──────────────────────────────────────────────────
static bool _auth() {
  if (!_srv.authenticate(WEB_ADMIN_USER, WEB_ADMIN_PASS)) {
    _srv.requestAuthentication();
    return false;
  }
  return true;
}

// ─── Главная HTML страница (хранится во Flash) ────────────────────────────
static const char PAGE_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="ru"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<!-- ИСПРАВЛЕНО: убран meta refresh — данные обновляются через AJAX fetchData() -->
<title>🐝 BeehiveScale</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0d0f0b;--panel:#141710;--border:#2e3829;
  --amber:#f5a623;--amber2:#ffd166;--green:#6fcf97;
  --red:#eb5757;--blue:#56ccf2;--text:#c8d4b8;--text2:#8a9e78;--text3:#506040;
  --mono:'Courier New',monospace;
}
body{background:var(--bg);color:var(--text);font-family:var(--mono);font-size:14px;min-height:100vh}
a{color:var(--amber);text-decoration:none}

/* header */
.hdr{background:rgba(20,23,16,.97);border-bottom:1px solid var(--border);
  padding:14px 20px;display:flex;align-items:center;justify-content:space-between;
  position:sticky;top:0;z-index:99}
.hdr-logo{font-size:18px;font-weight:700;letter-spacing:3px;color:var(--amber)}
.hdr-sub{font-size:10px;color:var(--text3);letter-spacing:2px;margin-top:2px}
.hdr-ip{font-size:11px;color:var(--text3)}
.live{display:inline-block;width:8px;height:8px;border-radius:50%;
  background:var(--green);box-shadow:0 0 6px var(--green);
  animation:pulse 2s infinite;margin-right:6px}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}

/* grid */
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;padding:16px;max-width:900px;margin:0 auto}
@media(max-width:600px){.grid{grid-template-columns:1fr}}

/* card */
.card{background:var(--panel);border:1px solid var(--border);padding:16px;position:relative;overflow:hidden}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;
  background:linear-gradient(90deg,var(--amber),transparent)}
.card-title{font-size:10px;letter-spacing:2px;color:var(--text3);text-transform:uppercase;margin-bottom:10px}
.card.full{grid-column:1/-1}
.card.green::before{background:linear-gradient(90deg,var(--green),transparent)}
.card.red::before{background:linear-gradient(90deg,var(--red),transparent)}
.card.blue::before{background:linear-gradient(90deg,var(--blue),transparent)}

/* big value */
.val-big{font-size:42px;font-weight:700;color:var(--amber);line-height:1;letter-spacing:-1px}
.val-unit{font-size:16px;color:var(--text2);margin-left:4px}
.val-sub{font-size:11px;color:var(--text3);margin-top:6px}

/* gauge */
.gauge-wrap{display:flex;align-items:center;gap:10px;margin-top:8px}
.gauge{flex:1;height:6px;background:var(--border);position:relative}
.gauge-fill{height:100%;background:var(--amber);transition:width .5s}
.gauge-lbl{font-size:11px;color:var(--text3);width:44px;text-align:right}

/* status row */
.status-row{display:flex;align-items:center;gap:8px;padding:8px 0;
  border-bottom:1px solid var(--border);font-size:12px}
.status-row:last-child{border:none}
.dot{width:8px;height:8px;border-radius:50%;flex-shrink:0}
.dot.ok{background:var(--green);box-shadow:0 0 5px var(--green)}
.dot.err{background:var(--red);box-shadow:0 0 5px var(--red)}
.dot.warn{background:var(--amber);box-shadow:0 0 5px var(--amber)}
.status-lbl{flex:1;color:var(--text2)}
.status-val{color:var(--text);font-size:12px}

/* btn */
.btn{display:inline-block;font-family:var(--mono);font-size:12px;letter-spacing:1px;
  padding:10px 20px;border:1px solid;cursor:pointer;background:transparent;
  transition:all .2s;text-transform:uppercase}
.btn-amber{border-color:var(--amber);color:var(--amber)}
.btn-amber:hover{background:var(--amber);color:#000}
.btn-red{border-color:var(--red);color:var(--red)}
.btn-red:hover{background:var(--red);color:#fff}
.btn-green{border-color:var(--green);color:var(--green)}
.btn-green:hover{background:var(--green);color:#000}
.btn-blue{border-color:var(--blue);color:var(--blue)}
.btn-blue:hover{background:var(--blue);color:#000}
.btn:disabled{opacity:.4;cursor:not-allowed}

/* settings form */
.form-row{display:flex;flex-direction:column;gap:6px;margin-bottom:14px}
.form-row label{font-size:10px;letter-spacing:1.5px;color:var(--text2);text-transform:uppercase}
input,select,textarea{
  background:#1c2018;border:1px solid var(--border);color:var(--text);
  font-family:var(--mono);font-size:13px;padding:9px 12px;outline:none;width:100%;
  -webkit-text-fill-color:var(--text)}
input:-webkit-autofill,input:-webkit-autofill:focus{
  -webkit-box-shadow:0 0 0 50px #1c2018 inset;
  -webkit-text-fill-color:var(--text);
  border:1px solid var(--border);caret-color:var(--text)}
input:focus,select:focus{border-color:var(--amber)}
.form-actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}

/* toast */
.toast{position:fixed;bottom:20px;right:20px;z-index:200;
  font-family:var(--mono);font-size:12px;padding:12px 20px;
  border:1px solid var(--green);background:rgba(13,15,11,.97);color:var(--green);
  letter-spacing:1px;transform:translateX(200%);transition:transform .3s}
.toast.show{transform:none}
.toast.err{border-color:var(--red);color:var(--red)}

/* history chart area */
.chart-area{margin-top:12px;position:relative;height:80px;overflow:hidden}
.chart-svg{width:100%;height:100%}

/* refresh bar */
.refresh-bar{height:2px;background:var(--border);position:fixed;top:0;left:0;z-index:100}
.refresh-fill{height:100%;background:var(--amber);transition:width linear}
</style>
</head>
<body>

<div class="refresh-bar"><div class="refresh-fill" id="rbar" style="width:100%"></div></div>

<div class="hdr">
  <div>
    <div class="hdr-logo">🐝 BeehiveScale</div>
    <div class="hdr-sub">LIVE MONITOR · ESP32</div>
  </div>
  <div class="hdr-ip">
    <span class="live"></span>ONLINE &nbsp;|&nbsp;
    <span id="cur-time">--:--:--</span>
  </div>
</div>

<div class="grid">

  <!-- WEIGHT CARD -->
  <div class="card" id="weight-card">
    <div class="card-title">⚖️ Текущий вес</div>
    <div class="val-big" id="w-val">__WGT__<span class="val-unit">кг</span></div>
    <div class="val-sub">Пред: <b id="w-ref">__PRV__</b> кг &nbsp;|&nbsp; Привес: <b id="w-delta" style="color:var(--amber2)">__DLT__</b> кг</div>
    <div class="gauge-wrap">
      <div class="gauge"><div class="gauge-fill" id="w-gauge" style="width:__GPCT__%"></div></div>
      <div class="gauge-lbl" id="w-gpct">__GPCT__%</div>
    </div>
  </div>

  <!-- TEMPERATURE CARD -->
  <div class="card blue">
    <div class="card-title">🌡 Температура</div>
    <div class="val-big" id="t-val">__TMP__<span class="val-unit">°C</span></div>
    <div class="val-sub">
      Влажность: <b id="h-val">__HUM__</b> % &nbsp;|&nbsp;
      RTC: <b id="rtc-val">__RTC__</b> °C
    </div>
    <div class="gauge-wrap">
      <div class="gauge"><div class="gauge-fill" id="t-gauge" style="width:__TPCT__;background:var(--blue)"></div></div>
      <div class="gauge-lbl" id="t-gpct">__TMP__°C</div>
    </div>
  </div>

  <!-- STATUS CARD -->
  <div class="card">
    <div class="card-title">📡 Статус системы</div>
    <div class="status-row">
      <div class="dot __SRDOT__"></div>
      <div class="status-lbl">Датчик HX711</div>
      <div class="status-val">__SRST__</div>
    </div>
    <div class="status-row">
      <div class="dot __WFDOT__"></div>
      <div class="status-lbl">Wi-Fi</div>
      <div class="status-val">__WFST__</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Веб-сервер</div>
      <div class="status-val">Активен :80</div>
    </div>
    <div class="status-row">
      <div class="dot warn"></div>
      <div class="status-lbl">Пробуждений</div>
      <div class="status-val">__WKC__</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Cal. Factor</div>
      <div class="status-val">__CF__</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Offset</div>
      <div class="status-val">__OFS__</div>
    </div>
    <div class="status-row">
      <div class="dot ok" id="heap-dot"></div>
      <div class="status-lbl">Free Heap</div>
      <div class="status-val" id="heap-val">__HEAP__ b</div>
    </div>
    <div class="status-row">
      <div class="dot __BATDOT__" id="bat-dot"></div>
      <div class="status-lbl">Батарея</div>
      <div class="status-val" id="bat-val">__BATV__V (__BATP__%)</div>
    </div>
    <div class="status-row">
      <div class="dot __SDDOT__" id="sd-dot"></div>
      <div class="status-lbl">SD-карта</div>
      <div class="status-val" id="sd-val">__SDLOG__ KB лог / __SDFREE__ KB своб.</div>
    </div>
  </div>

  <!-- CHART CARD -->
  <div class="card full" id="chart-card">
    <div class="card-title">📈 График веса (последние 24 ч)</div>
    <div class="chart-area" style="height:120px" id="chart-wrap">
      <svg id="chart-svg" class="chart-svg" viewBox="0 0 400 100" preserveAspectRatio="none">
        <text x="200" y="55" text-anchor="middle" fill="#506040" font-size="10">Загрузка...</text>
      </svg>
    </div>
    <div style="display:flex;justify-content:space-between;font-size:10px;color:var(--text3);margin-top:4px">
      <span id="chart-min-t"></span><span id="chart-mid-t"></span><span id="chart-max-t"></span>
    </div>
    <div style="font-size:10px;color:var(--text3);margin-top:4px">
      Мин: <b id="chart-wmin">--</b> кг &nbsp;|&nbsp; Макс: <b id="chart-wmax">--</b> кг &nbsp;|&nbsp; Точек: <b id="chart-pts">0</b>
    </div>
  </div>

  <!-- DATETIME CARD -->
  <div class="card green">
    <div class="card-title">🕐 Дата и время (RTC)</div>
    <div class="val-big" style="font-size:28px;color:var(--green)" id="dt-val">__DT__</div>
    <div class="val-sub" style="margin-top:10px">Uptime ESP32: <b id="upt-val">__UPT__</b></div>
  </div>

  <!-- ACTIONS CARD -->
  <div class="card">
    <div class="card-title">🔧 Действия</div>
    <div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:4px">
      <button class="btn btn-amber" onclick="doAction('/api/tare')">⚖ Тарировка</button>
      <button class="btn btn-green" onclick="doAction('/api/save')">💾 Сохранить эталон</button>
      __NTP_BTN__
      <button class="btn btn-blue"  onclick="doDownload('/api/log')">📥 Скачать лог</button>
      <button class="btn btn-red"   onclick="if(confirm('Очистить лог SD-карты?'))doAction('/api/log/clear')">🗑 Очистить лог</button>
      <button class="btn btn-red"   onclick="if(confirm('Перезагрузить ESP32?'))doAction('/api/reboot')">↺ Перезагрузить</button>
    </div>
    <div style="font-size:10px;color:var(--text3);margin-top:12px;line-height:1.7">
      Тарировка: обнуляет показания (убрать груз перед нажатием)<br>
      Сохранить эталон: запоминает текущий вес как базовый<br>
      NTP Время: синхронизация времени через интернет<br>
      Скачать лог: скачивает CSV файл с SD-карты<br>
      Очистить лог: удаляет все записи лога
    </div>
  </div>

  <!-- SETTINGS CARD -->
  <div class="card">
    <div class="card-title">⚙️ Быстрые настройки</div>
    <div class="form-row">
      <label>Порог тревоги Telegram (кг)</label>
      <input type="number" id="cfg-alert" value="__ALRT__" step="0.1" min="0.1" max="10">
    </div>
    <div class="form-row">
      <label>Эталонный груз калибровки (г)</label>
      <input type="number" id="cfg-calib" value="__CWGT__" step="100" min="100" max="5000">
    </div>
    <div class="form-row">
      <label>EMA сглаживание (0.05 – 0.9)</label>
      <input type="number" id="cfg-ema" value="__EMA__" step="0.05" min="0.05" max="0.9">
    </div>
    <div class="form-row">
      <label>Интервал сна deep sleep (сек, 30–86400)</label>
      <input type="number" id="cfg-sleep" value="__SLPSEC__" step="60" min="30" max="86400">
    </div>
    <div class="form-row">
      <label>Таймаут подсветки LCD (сек, 0=всегда)</label>
      <input type="number" id="cfg-bl" value="__BLSEC__" step="10" min="0" max="3600">
    </div>
    <div class="form-row">
      <label>Пароль Wi-Fi AP (8–23 символа)</label>
      <input type="password" id="cfg-appass" value="__APPASS__" minlength="8" maxlength="23" autocomplete="new-password">
    </div>
    <div class="form-actions">
      <button class="btn btn-green" onclick="saveSettings()">💾 Сохранить</button>
    </div>
  </div>

  <!-- API INFO CARD -->
  <div class="card full">
    <div class="card-title">🔌 REST API</div>
    <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:8px;margin-top:8px">
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--green)">GET /api/data</div>
        <div style="color:var(--text3);margin-top:3px">Все показания в JSON</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--amber)">POST /api/tare</div>
        <div style="color:var(--text3);margin-top:3px">Тарировка весов</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--amber)">POST /api/save</div>
        <div style="color:var(--text3);margin-top:3px">Сохранить текущий как эталон</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--blue)">POST /api/ntp</div>
        <div style="color:var(--text3);margin-top:3px">Синхронизация времени NTP</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--amber)">POST /api/settings</div>
        <div style="color:var(--text3);margin-top:3px">Изменить настройки (JSON)</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--green)">GET /api/log</div>
        <div style="color:var(--text3);margin-top:3px">Скачать лог CSV</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--red)">POST /api/log/clear</div>
        <div style="color:var(--text3);margin-top:3px">Очистить лог</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--red)">POST /api/reboot</div>
        <div style="color:var(--text3);margin-top:3px">Перезагрузить ESP32</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--green)">GET /api/log/json</div>
        <div style="color:var(--text3);margin-top:3px">Лог в JSON (для HA/Grafana)</div>
      </div>
    </div>
  </div>

</div><!-- /grid -->

<div class="toast" id="toast"></div>

<script>
// ── Auto-refresh bar ──────────────────────────────────────────────────
const REFRESH = WEB_REFRESH_SEC_JS * 1000;
let   _start  = Date.now();
const bar     = document.getElementById('rbar');

function tickBar() {
  const elapsed = Date.now() - _start;
  const pct     = Math.max(0, 100 - (elapsed / REFRESH * 100));
  bar.style.width = pct + '%';
  bar.style.transitionDuration = '0.5s';
  if (elapsed < REFRESH) requestAnimationFrame(tickBar);
}
tickBar();

// ── Live clock ────────────────────────────────────────────────────────
function tickClock() {
  const t = new Date();
  const p = n => String(n).padStart(2,'0');
  document.getElementById('cur-time').textContent =
    p(t.getHours())+':'+p(t.getMinutes())+':'+p(t.getSeconds());
  setTimeout(tickClock, 1000);
}
tickClock();

// ── Toast ─────────────────────────────────────────────────────────────
function showToast(msg, isErr) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className   = 'toast' + (isErr ? ' err' : '') + ' show';
  setTimeout(() => el.classList.remove('show'), 3000);
}

// ── API actions ───────────────────────────────────────────────────────
function doAction(url) {
  console.log('[JS] Requesting:', url);
  fetch(url, { method:'POST' })
    .then(r => {
      console.log('[JS] Response status:', r.status);
      return r.json();
    })
    .then(d => {
      console.log('[JS] Response data:', d);
      showToast(d.ok ? '✓ ' + d.msg : '✗ ' + d.msg, !d.ok);
    })
    .catch(e => {
      console.error('[JS] Error:', e);
      showToast('✗ Ошибка связи', true);
    });
}

function doDownload(url) {
  window.open(url, '_blank');
}

// ── SVG Chart ─────────────────────────────────────────────────────────
function renderChart(data) {
  const svg = document.getElementById('chart-svg');
  if (!data || data.length === 0) {
    svg.innerHTML = '<text x="200" y="55" text-anchor="middle" fill="#506040" font-size="10">Нет данных</text>';
    return;
  }
  // Фильтруем последние 24ч
  const now = Date.now();
  const pts = data.filter(d => {
    // dt формат: "DD.MM.YYYY HH:MM:SS" или ISO
    return true; // берём всё что есть
  });
  if (pts.length === 0) return;

  const weights = pts.map(d => parseFloat(d.w));
  let wMin = Math.min(...weights);
  let wMax = Math.max(...weights);
  if (wMax === wMin) { wMin -= 0.1; wMax += 0.1; }

  document.getElementById('chart-wmin').textContent = wMin.toFixed(2);
  document.getElementById('chart-wmax').textContent = wMax.toFixed(2);
  document.getElementById('chart-pts').textContent  = pts.length;

  const W = 400, H = 100, PAD = 8;
  const xScale = (i) => PAD + (i / (pts.length - 1 || 1)) * (W - PAD*2);
  const yScale = (w) => H - PAD - ((w - wMin) / (wMax - wMin)) * (H - PAD*2);

  // Сетка
  let svgHtml = '<line x1="'+PAD+'" y1="'+(H/2)+'" x2="'+(W-PAD)+'" y2="'+(H/2)+'" stroke="#2e3829" stroke-width="1"/>';
  // Заливка
  let area = 'M '+xScale(0)+' '+H;
  let line = 'M '+xScale(0)+' '+yScale(weights[0]);
  for (let i = 0; i < pts.length; i++) {
    const x = xScale(i), y = yScale(weights[i]);
    area += ' L '+x+' '+y;
    if (i > 0) line += ' L '+x+' '+y;
  }
  area += ' L '+xScale(pts.length-1)+' '+H+' Z';
  svgHtml += '<path d="'+area+'" fill="rgba(245,166,35,0.15)" stroke="none"/>';
  svgHtml += '<path d="'+line+'" fill="none" stroke="#f5a623" stroke-width="1.5"/>';
  // Текущая точка
  const lx = xScale(pts.length-1), ly = yScale(weights[weights.length-1]);
  svgHtml += '<circle cx="'+lx+'" cy="'+ly+'" r="3" fill="#f5a623"/>';

  svg.innerHTML = svgHtml;

  // Временные метки (первая, средняя, последняя)
  if (pts.length > 0) {
    document.getElementById('chart-min-t').textContent = pts[0].dt ? pts[0].dt.substring(0,16) : '';
    const mid = Math.floor(pts.length/2);
    document.getElementById('chart-mid-t').textContent = pts[mid] ? pts[mid].dt.substring(0,16) : '';
    document.getElementById('chart-max-t').textContent = pts[pts.length-1].dt ? pts[pts.length-1].dt.substring(0,16) : '';
  }
}

function loadChart() {
  fetch('/api/log/json')
    .then(r => r.json())
    .then(d => renderChart(d))
    .catch(() => {});
}
// Загружаем график при старте и каждые 5 минут
loadChart();
setInterval(loadChart, 300000);

function saveSettings() {
  const apPass = document.getElementById('cfg-appass').value;
  const body = {
    alertDelta: parseFloat(document.getElementById('cfg-alert').value),
    calibWeight: parseFloat(document.getElementById('cfg-calib').value),
    emaAlpha: parseFloat(document.getElementById('cfg-ema').value),
    sleepSec: parseInt(document.getElementById('cfg-sleep').value),
    lcdBlSec: parseInt(document.getElementById('cfg-bl').value)
  };
  if (apPass.length >= 8) body.apPass = apPass;
  fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify(body)
  })
  .then(r => r.json())
  .then(d => showToast(d.ok ? '✓ Сохранено' : '✗ ' + d.msg, !d.ok))
  .catch(() => showToast('✗ Ошибка', true));
}

// ── Live data fetch (без перезагрузки страницы) ───────────────────────
function fetchData() {
  fetch('/api/data')
    .then(r => r.json())
    .then(d => {
      document.getElementById('w-val').childNodes[0].textContent = d.weight.toFixed(2);
      document.getElementById('w-ref').textContent = d.prev.toFixed(2);
      const dlt = (d.weight - d.prev).toFixed(2);
      const dltEl = document.getElementById('w-delta');
      dltEl.textContent = (dlt > 0 ? '+' : '') + dlt;
      dltEl.style.color = dlt >= 0 ? 'var(--green)' : 'var(--red)';
      const pct = Math.min(100, Math.max(0, d.weight / 60 * 100)).toFixed(0);
      document.getElementById('w-gauge').style.width = pct + '%';
      document.getElementById('w-gpct').textContent = pct + '%';

      document.getElementById('t-val').childNodes[0].textContent = d.temp > -90 ? d.temp.toFixed(1) : '--';
      document.getElementById('h-val').textContent = d.hum > -90 ? d.hum.toFixed(1) : '--';
      document.getElementById('rtc-val').textContent = d.rtcT > -90 ? d.rtcT.toFixed(1) : '--';
      const tpct = Math.min(100, Math.max(0, (d.temp + 20) / 80 * 100)).toFixed(0);
      document.getElementById('t-gauge').style.width = tpct + '%';

      document.getElementById('dt-val').textContent = d.datetime;
      document.getElementById('upt-val').textContent = d.uptime;
      if (d.heap !== undefined) {
        document.getElementById('heap-val').textContent = d.heap + ' b';
        const hd = document.getElementById('heap-dot');
        if (d.heap < 10000) { hd.className='dot err'; } else if (d.heap < 30000) { hd.className='dot warn'; } else { hd.className='dot ok'; }
      }
      if (d.batV !== undefined) {
        document.getElementById('bat-val').textContent = d.batV.toFixed(2) + 'V (' + d.batPct + '%)';
        const bd = document.getElementById('bat-dot');
        if (d.batPct < 10) { bd.className='dot err'; } else if (d.batPct < 30) { bd.className='dot warn'; } else { bd.className='dot ok'; }
      }
      if (d.sdLog !== undefined) {
        document.getElementById('sd-val').textContent = Math.round(d.sdLog/1024) + ' KB лог / ' + Math.round(d.sdFree/1024) + ' KB своб.';
        const sd = document.getElementById('sd-dot');
        sd.className = d.sdFree < 102400 ? 'dot warn' : 'dot ok';
      }
    })
    .catch(() => {});
}
setInterval(fetchData, WEB_REFRESH_SEC_JS * 1000);
fetchData();
</script>
</body></html>
)rawhtml";

// Настройки читаются/записываются через Memory.h (web_get_*/save_web_settings)

// ─── Uptime в читаемом виде ───────────────────────────────────────────────
static String _uptime() {
  unsigned long s = millis() / 1000UL;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
           s/86400, (s%86400)/3600, (s%3600)/60, s%60);
  return String(buf);
}

// ─── Сборка HTML — заменяем плейсхолдеры реальными данными ───────────────
static String _buildPage() {
  String html = FPSTR(PAGE_HTML);
  float  wgt  = *_wd.weight;
  float  ref  = *_wd.lastSavedWeight;
  float  prv  = *_wd.prevWeight;
  float  dlt  = wgt - prv;
  float  tmp  = *_wd.tempC;
  float  hum  = *_wd.humidity;
  float  rtcT = *_wd.rtcTempC;
  float  pct  = constrain(wgt / 60.0f * 100.0f, 0, 100);
  float  tpct = constrain((tmp + 20.0f) / 80.0f * 100.0f, 0, 100);

  auto fmtF = [](float v, int d=2) -> String {
    return v > -90 ? String(v, d) : String("--");
  };

  html.replace("__WGT__",  fmtF(wgt));
  html.replace("__PRV__",  fmtF(prv));
  html.replace("__DLT__",  (dlt>=0?"+":"")+fmtF(dlt));
  html.replace("__GPCT__", String((int)pct));
  html.replace("__TMP__",  fmtF(tmp,1));
  html.replace("__HUM__",  fmtF(hum,1));
  html.replace("__RTC__",  fmtF(rtcT,1));
  html.replace("__TPCT__", String((int)tpct)+"%");
  html.replace("__SRDOT__",*_wd.sensorReady ? "ok" : "err");
  html.replace("__SRST__", *_wd.sensorReady ? "OK" : "ОШИБКА");
  html.replace("__WFDOT__",*_wd.wifiOk ? "ok" : "err");
  html.replace("__WFST__", *_wd.wifiOk ? "Подключён" : "Нет связи");
  html.replace("__WKC__",  String(*_wd.wakeupCount));
  html.replace("__CF__",   String(*_wd.calibFactor, 2));
  html.replace("__OFS__",  String(*_wd.offset));
  { 
    char _hbuf[12]; 
#if defined(ESP32)
    snprintf(_hbuf, sizeof(_hbuf), "%lu", (unsigned long)ESP.getFreeHeap());
#else
    // ESP8266: getFreeHeap может отсутствовать в старых версиях ядра
    snprintf(_hbuf, sizeof(_hbuf), "%lu", (unsigned long)ESP.getFreeHeap());
#endif
    html.replace("__HEAP__", _hbuf);
  }
  html.replace("__BATV__",  String(*_wd.batVoltage, 2));
  html.replace("__BATP__",  String(*_wd.batPercent));
  html.replace("__BATDOT__", *_wd.batPercent < 10 ? "err" : (*_wd.batPercent < 30 ? "warn" : "ok"));
  {
    size_t  sdSz   = log_size();
    uint32_t sdFr  = log_free_space();
    html.replace("__SDLOG__",  String(sdSz / 1024));
    html.replace("__SDFREE__", String(sdFr / 1024));
    html.replace("__SDDOT__",  sdFr < 102400UL ? "warn" : "ok");
  }
  html.replace("__DT__",   *_wd.datetime);
  html.replace("__UPT__",  _uptime());
  html.replace("__ALRT__", String(web_get_alert_delta(), 1));
  html.replace("__CWGT__", String(web_get_calib_weight(), 0));
  html.replace("__EMA__",  String(web_get_ema_alpha(), 2));
  html.replace("__SLPSEC__", String(get_sleep_sec()));
  html.replace("__BLSEC__",  String(get_lcd_bl_sec()));
  {
    char apbuf[24];
    get_ap_pass(apbuf, sizeof(apbuf));
    html.replace("__APPASS__", String(apbuf));
  }
#if defined(WIFI_MODE_AP)
  html.replace("__NTP_BTN__", "<button class=\"btn btn-blue\" disabled title=\"Недоступно в AP режиме\">🕐 NTP (AP)</button>");
#else
  html.replace("__NTP_BTN__", "<button class=\"btn btn-blue\" onclick=\"doAction('/api/ntp')\">🕐 NTP Время</button>");
#endif
  html.replace("WEB_REFRESH_SEC_JS", String(WEB_REFRESH_SEC));
  return html;
}

// ─── JSON ответ ───────────────────────────────────────────────────────────
static void _sendJson(bool ok, const String &msg) {
  StaticJsonDocument<128> doc;
  doc["ok"]  = ok;
  doc["msg"] = msg;
  String out; serializeJson(doc, out);
  _srv.send(ok ? 200 : 400, "application/json", out);
}

// ─── Маршруты ─────────────────────────────────────────────────────────────
static inline void _activity() {
  lastActivityTime = millis();
  if (_wa.onActivity) _wa.onActivity();
}

static void _handleRoot() {
  if (!_auth()) return;
  _activity();
  _srv.send(200, "text/html; charset=utf-8", _buildPage());
}

static void _handleData() {
  if (!_auth()) return;
  _activity();
  StaticJsonDocument<384> doc;
  doc["weight"]   = *_wd.weight;
  doc["ref"]      = *_wd.lastSavedWeight;
  doc["prev"]     = *_wd.prevWeight;
  doc["temp"]     = *_wd.tempC;
  doc["hum"]      = *_wd.humidity;
  doc["rtcT"]     = *_wd.rtcTempC;
  doc["sensor"]   = *_wd.sensorReady;
  doc["wifi"]     = *_wd.wifiOk;
  doc["datetime"] = *_wd.datetime;
  doc["uptime"]   = _uptime();
  doc["wakeups"]  = *_wd.wakeupCount;
  doc["cf"]       = *_wd.calibFactor;
  doc["offset"]   = *_wd.offset;
  doc["batV"]     = *_wd.batVoltage;
  doc["batPct"]   = *_wd.batPercent;
  doc["sdLog"]    = (unsigned long)log_size();
  doc["sdFree"]   = (unsigned long)log_free_space();
#if defined(ESP32) || defined(ESP8266)
  doc["heap"]     = ESP.getFreeHeap();
#else
  doc["heap"]     = 0;
#endif
  String out; serializeJson(doc, out);
  _srv.send(200, "application/json", out);
}

static void _handleTare() {
  if (!_auth()) return;
  _activity();
  if (_wa.doTare) { _wa.doTare(); _sendJson(true, "Тарировка выполнена"); }
  else _sendJson(false, "Нет обработчика");
}

static void _handleSave() {
  if (!_auth()) return;
  _activity();
  if (_wa.doSave) { _wa.doSave(); _sendJson(true, "Эталон сохранён"); }
  else _sendJson(false, "Нет обработчика");
}

static void _handleSettings() {
  if (!_auth()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }

  // Валидация входных данных
  float newAlert  = web_get_alert_delta();
  float newCalib  = web_get_calib_weight();
  float newAlpha  = web_get_ema_alpha();

  if (doc.containsKey("alertDelta")) {
    float val = doc["alertDelta"].as<float>();
    if (val >= 0.1f && val <= 10.0f) {
      newAlert = val;
    } else {
      _sendJson(false, "alertDelta должен быть от 0.1 до 10.0 кг");
      return;
    }
  }

  if (doc.containsKey("calibWeight")) {
    float val = doc["calibWeight"].as<float>();
    if (val >= 100.0f && val <= 5000.0f) {
      newCalib = val;
    } else {
      _sendJson(false, "calibWeight должен быть от 100 до 5000 г");
      return;
    }
  }

  if (doc.containsKey("emaAlpha")) {
    float val = doc["emaAlpha"].as<float>();
    if (val >= 0.05f && val <= 0.9f) {
      newAlpha = val;
    } else {
      _sendJson(false, "emaAlpha должен быть от 0.05 до 0.9");
      return;
    }
  }

  // Сохраняем в EEPROM и обновляем кэш в Memory
  save_web_settings(newAlert, newCalib, newAlpha);

  // Расширенные настройки
  if (doc.containsKey("sleepSec")) {
    uint32_t val = doc["sleepSec"].as<uint32_t>();
    if (val >= 30UL && val <= 86400UL) set_sleep_sec(val);
    else { _sendJson(false, "sleepSec: 30–86400"); return; }
  }
  if (doc.containsKey("lcdBlSec")) {
    uint16_t val = doc["lcdBlSec"].as<uint16_t>();
    if (val <= 3600) set_lcd_bl_sec(val);
    else { _sendJson(false, "lcdBlSec: 0–3600"); return; }
  }
  if (doc.containsKey("apPass")) {
    const char* pass = doc["apPass"].as<const char*>();
    if (pass && strlen(pass) >= 8 && strlen(pass) <= 23) {
      set_ap_pass(pass);
    } else { _sendJson(false, "apPass: 8–23 символа"); return; }
  }

  _sendJson(true, "Сохранено");
}

static void _handleReboot() {
  if (!_auth()) return;
  _sendJson(true, "Перезагрузка...");
  _srv.client().flush();
  delay(200);
  ESP.restart();
}

// Обработчик NTP синхронизации
static void _handleNtp() {
  if (!_auth()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }

  Serial.println(F("[Web] NTP sync requested..."));

  if (ntp_sync_time()) {
    _sendJson(true, "Время синхронизировано");
  } else {
    _sendJson(false, "Ошибка синхронизации");
  }
}

static void _handleNotFound() {
  _srv.send(404, "text/plain", "Not found");
}

// ─── /api/log  GET — скачать CSV-лог ─────────────────────────────────────
static void _handleLog() {
  if (!log_exists()) {
    _srv.send(404, "text/plain", "Log not found");
    return;
  }
#ifdef USE_SD_CARD
  File f = SD.open(LOG_FILE, FILE_READ);
#else
  File f = LOG_FS.open(LOG_FILE, "r");
#endif
  if (!f) {
    _srv.send(500, "text/plain", "Cannot open log");
    return;
  }
  _srv.sendHeader("Content-Disposition", "attachment; filename=\"beehive_log.csv\"");
  _srv.streamFile(f, "text/csv");
  f.close();
}

// ─── /api/log/clear  POST — очистить лог ─────────────────────────────────
static void _handleLogClear() {
  log_clear();
  _srv.send(200, "application/json", "{\"ok\":true}");
}

// ─── /api/log/json  GET — лог в JSON ─────────────────────────────────────
static void _handleLogJson() {
  if (!_auth()) return;
  String json = log_to_json(500);
  _srv.send(200, "application/json", json);
}

// ─── PUBLIC API ───────────────────────────────────────────────────────────
void webserver_init(WebData &data, WebActions &actions) {
  _wd = data;
  _wa = actions;

  _srv.on("/",             HTTP_GET,  _handleRoot);
  _srv.on("/api/data",     HTTP_GET,  _handleData);
  _srv.on("/api/tare",     HTTP_POST, _handleTare);
  _srv.on("/api/save",     HTTP_POST, _handleSave);
  _srv.on("/api/settings",   HTTP_POST, _handleSettings);
  _srv.on("/api/ntp",        HTTP_POST, _handleNtp);
  _srv.on("/api/reboot",     HTTP_POST, _handleReboot);
  _srv.on("/api/log",        HTTP_GET,  _handleLog);
  _srv.on("/api/log/clear",  HTTP_POST, _handleLogClear);
  _srv.on("/api/log/json",   HTTP_GET,  _handleLogJson);
  _srv.onNotFound(_handleNotFound);

  _srv.begin();
  Serial.print(F("[WebServer] Started on port "));
  Serial.print(WEB_SERVER_PORT);
  Serial.print(F("  http://"));
#if defined(WIFI_MODE_AP)
  Serial.println(WiFi.softAPIP());
#else
  Serial.println(WiFi.localIP());
#endif
}

void webserver_handle() {
  _srv.handleClient();
}
