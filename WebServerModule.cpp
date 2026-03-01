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
.btn{display:inline-flex;align-items:center;justify-content:center;font-family:var(--mono);font-size:12px;letter-spacing:1px;
  padding:12px 20px;border:1px solid;cursor:pointer;background:transparent;
  transition:all .2s;text-transform:uppercase;min-height:44px;gap:8px}
.btn-amber{border-color:var(--amber);color:var(--amber)}
.btn-amber:hover{background:var(--amber);color:#000}
.btn-red{border-color:var(--red);color:var(--red)}
.btn-red:hover{background:var(--red);color:#fff}
.btn-green{border-color:var(--green);color:var(--green)}
.btn-green:hover{background:var(--green);color:#000}
.btn-blue{border-color:var(--blue);color:var(--blue)}
.btn-blue:hover{background:var(--blue);color:#000}
.btn:disabled{opacity:.4;cursor:not-allowed}
.btn:active{transform:scale(0.96);opacity:0.8}

/* mobile nav */
.nav-btm{display:none;position:fixed;bottom:0;left:0;right:0;background:rgba(20,23,16,.98);
  border-top:1px solid var(--border);z-index:1000;justify-content:space-around;padding:8px 0}
@media(max-width:600px){
  .nav-btm{display:flex}
  .grid{padding-bottom:80px}
  .hdr{padding:10px 15px}
  .val-big{font-size:36px}
}
.nav-item{color:var(--text3);display:flex;flex-direction:column;align-items:center;gap:4px;font-size:9px;text-transform:uppercase;flex:1}
.nav-item.active{color:var(--amber)}
.nav-item svg{width:20px;height:20px;fill:currentColor}

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
    <div class="hdr-sub">LIVE MONITOR · ESP8266</div>
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
    <div class="val-big" id="w-val">--<span class="val-unit">кг</span></div>
    <div class="val-sub">Пред: <b id="w-ref">--</b> кг &nbsp;|&nbsp; Привес: <b id="w-delta" style="color:var(--amber2)">--</b> кг</div>
    <div class="gauge-wrap">
      <div class="gauge"><div class="gauge-fill" id="w-gauge" style="width:0%"></div></div>
      <div class="gauge-lbl" id="w-gpct">0%</div>
    </div>
  </div>

  <!-- TEMPERATURE CARD -->
  <div class="card blue">
    <div class="card-title">🌡 Температура</div>
    <div class="val-big" id="t-val">--<span class="val-unit">°C</span></div>
    <div class="val-sub">
      Влажность: <b id="h-val">--</b> % &nbsp;|&nbsp;
      RTC: <b id="rtc-val">--</b> °C
    </div>
    <div class="gauge-wrap">
      <div class="gauge"><div class="gauge-fill" id="t-gauge" style="width:0%;background:var(--blue)"></div></div>
      <div class="gauge-lbl" id="t-gpct">--°C</div>
    </div>
  </div>

  <!-- STATUS CARD -->
  <div class="card">
    <div class="card-title">📡 Статус системы</div>
    <div class="status-row">
      <div class="dot warn" id="sr-dot"></div>
      <div class="status-lbl">Датчик HX711</div>
      <div class="status-val" id="sr-val">...</div>
    </div>
    <div class="status-row">
      <div class="dot warn" id="wf-dot"></div>
      <div class="status-lbl">Wi-Fi</div>
      <div class="status-val" id="wf-val">...</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Веб-сервер</div>
      <div class="status-val">Активен :80</div>
    </div>
    <div class="status-row">
      <div class="dot warn"></div>
      <div class="status-lbl">Пробуждений</div>
      <div class="status-val" id="wkc-val">--</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Cal. Factor</div>
      <div class="status-val" id="cf-val">--</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Offset</div>
      <div class="status-val" id="ofs-val">--</div>
    </div>
    <div class="status-row">
      <div class="dot ok" id="heap-dot"></div>
      <div class="status-lbl">Free Heap</div>
      <div class="status-val" id="heap-val">-- b</div>
    </div>
    <div class="status-row">
      <div class="dot ok" id="bat-dot"></div>
      <div class="status-lbl">Батарея</div>
      <div class="status-val" id="bat-val">--V (--%)</div>
    </div>
    <div class="status-row">
      <div class="dot ok" id="sd-dot"></div>
      <div class="status-lbl">Хранилище</div>
      <div class="status-val" id="sd-val">-- KB лог / -- KB своб.</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Дата и время</div>
      <div class="status-val" id="dt-val">--</div>
    </div>
    <div class="status-row">
      <div class="dot ok"></div>
      <div class="status-lbl">Uptime</div>
      <div class="status-val" id="upt-val">--</div>
    </div>
  </div>

  <!-- CHART CARD -->
  <div class="card full" id="chart-card">
    <div class="card-title" style="display:flex;justify-content:space-between;align-items:center">
      <span>📈 График веса (последние 24 ч)</span>
      <a href="/chart" style="font-size:11px;color:var(--text3);text-decoration:none" onmouseover="this.style.color='var(--amber)'" onmouseout="this.style.color='var(--text3)'">Открыть полный →</a>
    </div>
    <div class="chart-area" style="height:160px" id="chart-wrap">
      <svg id="chart-svg" class="chart-svg" viewBox="0 0 460 140" preserveAspectRatio="none">
        <text x="230" y="75" text-anchor="middle" fill="#506040" font-size="10">Загрузка...</text>
      </svg>
    </div>
    <div style="font-size:10px;color:var(--text3);margin-top:4px">
      Мин: <b id="chart-wmin">--</b> кг &nbsp;|&nbsp; Макс: <b id="chart-wmax">--</b> кг &nbsp;|&nbsp; Точек: <b id="chart-pts">0</b>
    </div>
  </div>

  <!-- ACTIONS CARD -->
  <div class="card">
    <div class="card-title">🔧 Действия</div>
    <div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:4px">
      <button class="btn btn-amber" onclick="doAction('/api/tare')">⚖ Тарировка</button>
      <button class="btn btn-green" onclick="doAction('/api/save')">💾 Сохранить эталон</button>
      <button class="btn btn-blue" id="ntp-btn" onclick="doAction('/api/ntp')">🕐 NTP Время</button>
      <button class="btn btn-blue"  onclick="window.location='/chart'">📈 Графики и лог</button>
      <button class="btn btn-red"   onclick="if(confirm('Очистить лог SD-карты?'))doAction('/api/log/clear')">🗑 Очистить лог</button>
      <button class="btn btn-red"   onclick="if(confirm('Перезагрузить ESP32?'))doAction('/api/reboot')">↺ Перезагрузить</button>
      <button class="btn btn-blue"  onclick="doAction('/api/tg/test')">✉ Тест TG</button>
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
      <input type="number" id="cfg-alert" value="" step="0.1" min="0.1" max="10">
    </div>
    <div class="form-row">
      <label>Эталонный груз калибровки (г)</label>
      <input type="number" id="cfg-calib" value="" step="100" min="100" max="5000">
    </div>
    <div class="form-row">
      <label>EMA сглаживание (0.05 – 0.9)</label>
      <input type="number" id="cfg-ema" value="" step="0.05" min="0.05" max="0.9">
    </div>
    <div class="form-row">
      <label>Интервал сна deep sleep (сек, 30–86400)</label>
      <input type="number" id="cfg-sleep" value="" step="60" min="30" max="86400">
    </div>
    <div class="form-row">
      <label>Таймаут подсветки LCD (сек, 0=всегда)</label>
      <input type="number" id="cfg-bl" value="" step="10" min="0" max="3600">
    </div>
    <div class="form-row">
      <label>Пароль Wi-Fi AP (8–23 символа)</label>
      <input type="password" id="cfg-appass" value="" minlength="8" maxlength="23" autocomplete="new-password">
    </div>
    <div class="form-actions">
      <button class="btn btn-green" onclick="saveSettings()">💾 Сохранить</button>
    </div>
  </div>

  <!-- WIFI LINK -->
  <div class="card">
    <div class="card-title">📶 Wi-Fi</div>
    <div style="font-size:13px;color:var(--text2);margin-bottom:10px">Режим: <b style="color:var(--amber)" id="wifi-mode-lbl">--</b></div>
    <a href="/wifi" class="btn btn-blue" style="display:inline-block;text-decoration:none">⚙ Настройки Wi-Fi</a>
  </div>

  <!-- HIVE INFO CARD (фичи 12, 17) -->
  <div class="card full" id="hive-info-card">
    <div class="card-title">🐝 Информация об улье</div>
    <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:10px;margin-top:6px">
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border)">
        <div style="font-size:10px;color:var(--text3);letter-spacing:1px;margin-bottom:4px">СЕЗОН</div>
        <div style="font-size:18px;color:var(--amber)" id="hi-season">--</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border)">
        <div style="font-size:10px;color:var(--text3);letter-spacing:1px;margin-bottom:4px">ВЕС СЕГОДНЯ МИН/МАКС</div>
        <div style="font-size:15px;color:var(--green)" id="hi-wrange">-- / -- кг</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border)">
        <div style="font-size:10px;color:var(--text3);letter-spacing:1px;margin-bottom:4px">ТЕМП СЕГОДНЯ МИН/МАКС</div>
        <div style="font-size:15px;color:var(--blue)" id="hi-trange">-- / -- °C</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border)">
        <div style="font-size:10px;color:var(--text3);letter-spacing:1px;margin-bottom:4px">ИЗМЕНЕНИЕ ЗА ДЕНЬ</div>
        <div style="font-size:15px" id="hi-delta">-- кг</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border)">
        <div style="font-size:10px;color:var(--text3);letter-spacing:1px;margin-bottom:4px">ТОЧЕК СЕГОДНЯ</div>
        <div style="font-size:15px;color:var(--text2)" id="hi-count">--</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border)">
        <div style="font-size:10px;color:var(--text3);letter-spacing:1px;margin-bottom:4px">ДНЕЙ НАБЛЮДЕНИЙ</div>
        <div style="font-size:15px;color:var(--text2)" id="hi-days">--</div>
      </div>
    </div>
  </div>

  <!-- TELEGRAM CARD -->
  <div class="card">
    <div class="card-title">✉ Telegram уведомления</div>
    <div class="form-row">
      <label>Bot Token</label>
      <input type="password" id="tg-token" value="" placeholder="123456789:ABC..." autocomplete="off">
    </div>
    <div class="form-row">
      <label>Chat ID</label>
      <input type="text" id="tg-chatid" value="" placeholder="-100123456789">
    </div>
    <div class="form-actions">
      <button class="btn btn-green" onclick="saveTelegram()">💾 Сохранить</button>
      <button class="btn btn-blue"  onclick="doAction('/api/tg/test')">✉ Тест</button>
    </div>
    <div style="font-size:10px;color:var(--text3);margin-top:10px;line-height:1.6">
      Token: получить у @BotFather<br>
      Chat ID: узнать через @userinfobot
    </div>
  </div>

  <!-- CALIBRATION CARD -->
  <div class="card">
    <div class="card-title">⚖ Калибровка весов</div>
    <div class="form-row">
      <label>Cal. Factor (текущий: <b id="calib-cf-live">--</b>)</label>
      <input type="number" id="calib-cf" step="1" min="100" max="100000" placeholder="например 2280">
    </div>
    <div class="form-row">
      <label>Offset (текущий: <b id="calib-ofs-live">--</b>)</label>
      <input type="number" id="calib-ofs" step="1" placeholder="обычно не меняется">
    </div>
    <div class="form-actions">
      <button class="btn btn-amber" onclick="applyCalib()">✓ Применить CF</button>
      <button class="btn btn-blue"  onclick="doTareAndRefresh()">⊘ Тара + обновить</button>
    </div>
    <div style="font-size:10px;color:var(--text3);margin-top:10px;line-height:1.6">
      Шаг калибровки: поставьте груз → подберите CF так<br>
      чтобы показание равнялось реальной массе груза.<br>
      Текущий вес: <b id="calib-wgt-live">--</b> кг
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
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--amber)">POST /api/tg/settings</div>
        <div style="color:var(--text3);margin-top:3px">Сохранить Telegram token/chatId</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--blue)">POST /api/tg/test</div>
        <div style="color:var(--text3);margin-top:3px">Тестовое Telegram сообщение</div>
      </div>
      <div style="background:#1c2018;padding:10px 12px;border:1px solid var(--border);font-size:11px">
        <div style="color:var(--amber)">POST /api/calib/set</div>
        <div style="color:var(--text3);margin-top:3px">Установить calibFactor / offset</div>
      </div>
    </div>
  </div>

</div><!-- /grid -->

<div class="toast" id="toast"></div>

<script>
// ── Auto-refresh bar ──────────────────────────────────────────────────
const REFRESH = 5000;
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
function showToast(msg, isErr, ms) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className   = 'toast' + (isErr ? ' err' : '') + ' show';
  setTimeout(() => el.classList.remove('show'), ms || 3000);
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
    svg.innerHTML = '<text x="230" y="75" text-anchor="middle" fill="#506040" font-size="10">Нет данных</text>';
    return;
  }
  const pts = data.filter(() => true);
  if (pts.length === 0) return;

  const weights = pts.map(d => parseFloat(d.w));
  let wMin = Math.min(...weights);
  let wMax = Math.max(...weights);
  if (wMax === wMin) { wMin -= 0.5; wMax += 0.5; }
  // округляем границы до красивых значений
  const wRange = wMax - wMin;
  const step = wRange <= 1 ? 0.2 : wRange <= 5 ? 1 : wRange <= 20 ? 5 : 10;
  wMin = Math.floor(wMin / step) * step;
  wMax = Math.ceil(wMax / step) * step;

  document.getElementById('chart-wmin').textContent = wMin.toFixed(2);
  document.getElementById('chart-wmax').textContent = wMax.toFixed(2);
  document.getElementById('chart-pts').textContent  = pts.length;

  // Координатная система: viewBox 0 0 460 140
  // Отступы: слева 42px (ось Y + метки), снизу 22px (ось X + метки), сверху 8px, справа 8px
  const W = 460, H = 140;
  const L = 42, R = 8, T = 8, B = 22;
  const plotW = W - L - R;
  const plotH = H - T - B;

  const xS = (i) => L + (i / (pts.length - 1 || 1)) * plotW;
  const yS = (w) => T + plotH - ((w - wMin) / (wMax - wMin || 1)) * plotH;

  let html = '';

  // Горизонтальные линии сетки и метки оси Y (4 деления)
  const yTicks = 4;
  for (let k = 0; k <= yTicks; k++) {
    const w = wMin + (wMax - wMin) * k / yTicks;
    const y = yS(w);
    // gridline
    html += '<line x1="'+L+'" y1="'+y+'" x2="'+(W-R)+'" y2="'+y+'" stroke="#2a3325" stroke-width="1"/>';
    // метка
    const lbl = w % 1 === 0 ? w.toFixed(0) : w.toFixed(1);
    html += '<text x="'+(L-4)+'" y="'+(y+3.5)+'" text-anchor="end" fill="#7a8c6a" font-size="8">'+lbl+'</text>';
  }

  // Вертикальные линии сетки и метки оси X (3 точки: начало, середина, конец)
  const xTickIdx = [0, Math.floor((pts.length-1)/2), pts.length-1];
  xTickIdx.forEach(i => {
    if (i < 0 || i >= pts.length) return;
    const x = xS(i);
    html += '<line x1="'+x+'" y1="'+T+'" x2="'+x+'" y2="'+(T+plotH)+'" stroke="#2a3325" stroke-width="1"/>';
    const lbl = pts[i].dt ? pts[i].dt.substring(11,16) : '';
    const anchor = i === 0 ? 'start' : i === pts.length-1 ? 'end' : 'middle';
    html += '<text x="'+x+'" y="'+(H-4)+'" text-anchor="'+anchor+'" fill="#7a8c6a" font-size="8">'+lbl+'</text>';
  });

  // Дата первой и последней точки под осью X
  if (pts.length > 0) {
    const d0 = pts[0].dt ? pts[0].dt.substring(0,10) : '';
    const d1 = pts[pts.length-1].dt ? pts[pts.length-1].dt.substring(0,10) : '';
    if (d0) html += '<text x="'+L+'" y="'+(H-4)+'" text-anchor="start" fill="#506040" font-size="7">'+d0+'</text>';
    if (d1 && d1 !== d0) html += '<text x="'+(W-R)+'" y="'+(H-4)+'" text-anchor="end" fill="#506040" font-size="7">'+d1+'</text>';
  }

  // Оси (линии)
  html += '<line x1="'+L+'" y1="'+T+'" x2="'+L+'" y2="'+(T+plotH)+'" stroke="#506040" stroke-width="1"/>';
  html += '<line x1="'+L+'" y1="'+(T+plotH)+'" x2="'+(W-R)+'" y2="'+(T+plotH)+'" stroke="#506040" stroke-width="1"/>';

  // Подпись оси Y
  html += '<text x="6" y="'+(T+plotH/2)+'" text-anchor="middle" fill="#7a8c6a" font-size="8" transform="rotate(-90,6,'+(T+plotH/2)+')">кг</text>';

  // Заливка под графиком
  let area = 'M '+xS(0)+' '+(T+plotH);
  let line = 'M '+xS(0)+' '+yS(weights[0]);
  for (let i = 0; i < pts.length; i++) {
    const x = xS(i), y = yS(weights[i]);
    area += ' L '+x+' '+y;
    if (i > 0) line += ' L '+x+' '+y;
  }
  area += ' L '+xS(pts.length-1)+' '+(T+plotH)+' Z';
  html += '<path d="'+area+'" fill="rgba(245,166,35,0.12)" stroke="none"/>';
  html += '<path d="'+line+'" fill="none" stroke="#f5a623" stroke-width="1.5"/>';

  // Маркер последней точки
  const lx = xS(pts.length-1), ly = yS(weights[weights.length-1]);
  html += '<circle cx="'+lx+'" cy="'+ly+'" r="3" fill="#f5a623"/>';

  svg.innerHTML = html;
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
      document.getElementById('t-gpct').textContent = (d.temp > -90 ? d.temp.toFixed(1) : '--') + '°C';

      const srDot = document.getElementById('sr-dot');
      const srVal = document.getElementById('sr-val');
      if (srDot && srVal) { srDot.className='dot '+(d.sensor?'ok':'err'); srVal.textContent=d.sensor?'OK':'ОШИБКА'; }
      const wfDot = document.getElementById('wf-dot');
      const wfVal = document.getElementById('wf-val');
      if (wfDot && wfVal) { wfDot.className='dot '+(d.wifi?'ok':'err'); wfVal.textContent=d.wifi?'Подключён':'Нет связи'; }
      const wkcEl = document.getElementById('wkc-val');
      if (wkcEl) wkcEl.textContent = d.wakeups !== undefined ? d.wakeups : '--';

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
        const mode = d.sdFallback ? ' [Flash]' : ' [SD]';
        document.getElementById('sd-val').textContent = Math.round(d.sdLog/1024) + ' KB лог / ' + Math.round(d.sdFree/1024) + ' KB своб.' + mode;
        const sd = document.getElementById('sd-dot');
        sd.className = (d.sdLog===0 && d.sdFree===0) ? 'dot err' : (d.sdFree < 102400 ? 'dot warn' : 'dot ok');
      }
      updateCalibLive(d);
    })
    .catch(() => {});
}
setInterval(fetchData, REFRESH);
fetchData();

// ── Загрузка конфига форм при старте ──────────────────────────────────
function loadConfig() {
  fetch('/api/config')
    .then(r => r.json())
    .then(d => {
      if (d.alertDelta !== undefined)  document.getElementById('cfg-alert').value  = d.alertDelta;
      if (d.calibWeight !== undefined) document.getElementById('cfg-calib').value  = d.calibWeight;
      if (d.emaAlpha !== undefined)    document.getElementById('cfg-ema').value    = d.emaAlpha;
      if (d.sleepSec !== undefined)    document.getElementById('cfg-sleep').value  = d.sleepSec;
      if (d.lcdBlSec !== undefined)    document.getElementById('cfg-bl').value     = d.lcdBlSec;
      if (d.tgToken !== undefined)     document.getElementById('tg-token').value   = d.tgToken;
      if (d.tgChatId !== undefined)    document.getElementById('tg-chatid').value  = d.tgChatId;
      const wml = document.getElementById('wifi-mode-lbl');
      if (wml && d.wifiMode !== undefined) wml.textContent = d.wifiMode === 1 ? 'Роутер (STA)' : 'Точка доступа (AP)';
      const ntpBtn = document.getElementById('ntp-btn');
      if (ntpBtn && d.wifiMode === 0) { ntpBtn.disabled = true; ntpBtn.title = 'Недоступно в AP режиме'; ntpBtn.textContent = '🕐 NTP (AP)'; }
    })
    .catch(() => {});
}
loadConfig();

// ── Обновление живых данных калибровки ────────────────────────────────
function updateCalibLive(d) {
  if (d.cf !== undefined) {
    document.getElementById('calib-cf-live').textContent = d.cf.toFixed(2);
    document.getElementById('calib-ofs-live').textContent = d.offset;
    const cfVal = document.getElementById('cf-val');
    const ofsVal = document.getElementById('ofs-val');
    if (cfVal) cfVal.textContent = d.cf.toFixed(2);
    if (ofsVal) ofsVal.textContent = d.offset;
  }
  if (d.weight !== undefined) document.getElementById('calib-wgt-live').textContent = d.weight.toFixed(3);
}

function saveTelegram() {
  const token  = document.getElementById('tg-token').value.trim();
  const chatid = document.getElementById('tg-chatid').value.trim();
  fetch('/api/tg/settings', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({token: token, chatId: chatid})
  })
  .then(r=>r.json())
  .then(d=>showToast(d.ok ? '✓ TG сохранён' : '✗ ' + d.msg, !d.ok))
  .catch(()=>showToast('✗ Ошибка', true));
}

function applyCalib() {
  const cf  = parseFloat(document.getElementById('calib-cf').value);
  const ofs = document.getElementById('calib-ofs').value;
  const body = {};
  if (!isNaN(cf) && cf > 0) body.calibFactor = cf;
  if (ofs !== '') body.offset = parseInt(ofs);
  if (Object.keys(body).length === 0) { showToast('✗ Введите значение', true); return; }
  fetch('/api/calib/set', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify(body)
  })
  .then(r=>r.json())
  .then(d=>showToast(d.ok ? '✓ ' + d.msg : '✗ ' + d.msg, !d.ok))
  .catch(()=>showToast('✗ Ошибка', true));
}

function doTareAndRefresh() {
  doAction('/api/tare');
  setTimeout(fetchData, 1500);
}

// ── Фичи 12+17: загрузка суточной статистики и информации об улье ────────
var _seasonRu = {Vesna:'🌱 Весна', Leto:'☀ Лето', Osen:'🍂 Осень', Zima:'❄ Зима'};
function loadDayStat() {
  fetch('/api/daystat')
    .then(r=>r.json())
    .then(function(d) {
      document.getElementById('hi-season').textContent = _seasonRu[d.season] || d.season;
      if (d.valid) {
        document.getElementById('hi-wrange').textContent = d.wMin.toFixed(2) + ' / ' + d.wMax.toFixed(2) + ' кг';
        var tr = document.getElementById('hi-trange');
        if (d.tMin !== null && d.tMin > -90) {
          tr.textContent = d.tMin.toFixed(1) + ' / ' + d.tMax.toFixed(1) + ' °C';
        } else {
          tr.textContent = 'нет данных';
        }
        document.getElementById('hi-count').textContent = d.count + ' изм.';
      }
      var dEl = document.getElementById('hi-delta');
      var dlt = d.deltaKg !== undefined ? d.deltaKg : 0;
      dEl.textContent = (dlt >= 0 ? '+' : '') + dlt.toFixed(2) + ' кг';
      dEl.style.color = dlt >= 0 ? 'var(--green)' : 'var(--red)';
      document.getElementById('hi-days').textContent = d.daysSinceStart > 0 ? d.daysSinceStart + ' дн.' : '< 1 дн.';
    })
    .catch(function(){});
}
// ── Табы для мобильных ────────────────────────────────────────────────
function showTab(id, el) {
  document.querySelectorAll('.card').forEach(c => {
    if(!c.classList.contains('full') && c.id !== 'weight-card' && c.id !== 'hive-info-card') {
      c.style.display = (id === 'all' || c.innerHTML.toLowerCase().includes(id)) ? 'block' : 'none';
    }
  });
  document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
  if(el) el.classList.add('active');
  window.scrollTo(0,0);
}

// ── Консоль логов ─────────────────────────────────────────────────────
let _logAuto = true;
function addLog(msg, type='info') {
  const c = document.getElementById('debug-console');
  if(!c) return;
  const div = document.createElement('div');
  div.style.color = type==='err'?'var(--red)':type==='warn'?'var(--amber)':'var(--text3)';
  div.textContent = '[' + new Date().toLocaleTimeString() + '] ' + msg;
  c.appendChild(div);
  if(_logAuto) c.scrollTop = c.scrollHeight;
  while(c.childNodes.length > 50) c.removeChild(c.firstChild);
}

// ── Мастер калибровки ─────────────────────────────────────────────────
let _calStep = 0;
function nextCalStep() {
  _calStep++;
  const c = document.getElementById('cal-wizard');
  const b = document.getElementById('cal-btn');
  if(_calStep === 1) {
    c.innerHTML = '<b style="color:var(--amber)">ШАГ 1:</b> Снимите всё с весов и нажмите ОК.';
    b.textContent = 'ОК, ПУСТО';
  } else if(_calStep === 2) {
    doAction('/api/tare');
    addLog('Тарировка выполнена...');
    c.innerHTML = '<b style="color:var(--amber)">ШАГ 2:</b> Положите груз 5кг (или другой эталон) и введите его вес в граммах ниже.';
    b.textContent = 'ГОТОВО, ГРУЗ НА ВЕСАХ';
  } else if(_calStep === 3) {
    const w = parseFloat(document.getElementById('cfg-calib').value);
    addLog('Расчёт калибровочного коэффициента для ' + w + 'г...');
    // Здесь можно добавить запрос на авто-калибровку, если API поддерживает
    c.innerHTML = '<b style="color:var(--green)">ЗАВЕРШЕНО!</b> Проверьте текущий вес. Если не совпадает, подправьте Cal.Factor вручную.';
    b.style.display = 'none';
  }
}

// Перехват fetch для вывода в консоль
const _origFetch = window.fetch;
window.fetch = function() {
  return _origFetch.apply(this, arguments).then(r => {
    var url = typeof arguments[0] === 'string' ? arguments[0] : (arguments[0] && arguments[0].url) || '';
    if(url.includes('/api/')) addLog('API: ' + url + ' [' + r.status + ']');
    return r;
  });
};

loadDayStat();
setInterval(loadDayStat, 60000);
addLog('Система готова. Ожидание данных...');

</script>

<div class="nav-btm">
  <div class="nav-item active" onclick="showTab('all', this)">
    <svg viewBox="0 0 24 24"><path d="M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z"/></svg>Главная
  </div>
  <div class="nav-item" onclick="showTab('статус', this)">
    <svg viewBox="0 0 24 24"><path d="M16 11V3H8v6H2v12h20V11h-6zm-6-6h4v14h-4V5zm-6 6h4v8H4v-8zm16 8h-4v-8h4v8z"/></svg>Статус
  </div>
  <div class="nav-item" onclick="showTab('настройки', this)">
    <svg viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>Настройки
  </div>
</div>

<div class="card full" style="margin-top:20px;border-style:dashed">
  <div class="card-title">💻 Консоль отладки</div>
  <div id="debug-console" style="height:120px;overflow-y:auto;background:#000;padding:8px;font-size:11px;color:var(--text3);line-height:1.4;border:1px solid var(--border)"></div>
  <div style="margin-top:8px;display:flex;gap:10px">
    <button class="btn btn-blue" style="min-height:30px;padding:5px 10px" onclick="document.getElementById('debug-console').innerHTML=''">Очистить</button>
    <label style="font-size:11px;display:flex;align-items:center;gap:5px"><input type="checkbox" checked onchange="_logAuto=this.checked" style="width:auto"> Авто-скролл</label>
  </div>
</div>

<div id="modal-cal" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.9);z-index:2000;padding:20px;align-items:center;justify-content:center">
  <div class="card" style="max-width:400px;width:100%">
    <div class="card-title">🧙 Мастер калибровки</div>
    <div id="cal-wizard" style="margin:20px 0;line-height:1.5">Для начала процесса нажмите кнопку ниже.</div>
    <div style="display:flex;gap:10px">
      <button class="btn btn-amber" id="cal-btn" onclick="nextCalStep()">Начать</button>
      <button class="btn btn-red" onclick="document.getElementById('modal-cal').style.display='none'">Отмена</button>
    </div>
  </div>
</div>

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

// _buildPage() удалён — страница полностью статическая, данные через AJAX

// ─── JSON ответ ───────────────────────────────────────────────────────────
static void _sendJson(bool ok, const String &msg) {
  StaticJsonDocument<128> doc;
  doc["ok"]  = ok;
  doc["msg"] = msg;
  String out; serializeJson(doc, out);
  _srv.send(ok ? 200 : 400, "application/json", out);
}

// ─── Страница графика ─────────────────────────────────────────────────────
static const char CHART_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="ru"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>📈 График — BeehiveScale</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0d0f0b;--panel:#141710;--border:#2e3829;--amber:#f5a623;--text1:#e8e0d0;--text2:#b0a890;--text3:#7a8c6a;--red:#e05555;--green:#6fcf97}
body{background:var(--bg);color:var(--text1);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;font-size:14px;padding:12px;min-height:100vh}
h1{font-size:18px;color:var(--amber);margin-bottom:12px;display:flex;align-items:center;gap:10px}
h1 a{color:var(--text3);font-size:13px;text-decoration:none;font-weight:normal;margin-left:auto}
h1 a:hover{color:var(--amber)}
.toolbar{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:14px;align-items:center}
.btn{padding:6px 14px;border:1px solid var(--border);background:var(--panel);color:var(--text1);border-radius:6px;cursor:pointer;font-size:13px;transition:border-color .2s}
.btn:hover{border-color:var(--amber)}
.btn.active{border-color:var(--amber);color:var(--amber)}
.sep{flex:1}
.card{background:var(--panel);border:1px solid var(--border);border-radius:10px;padding:14px}
.chart-wrap{position:relative;width:100%;height:320px;overflow:hidden;user-select:none}
svg.chart{width:100%;height:100%}
.tooltip{position:absolute;background:#1e2419;border:1px solid var(--amber);border-radius:6px;padding:6px 10px;font-size:12px;pointer-events:none;display:none;white-space:nowrap;z-index:10}
.tooltip b{color:var(--amber)}
.stats{display:flex;flex-wrap:wrap;gap:16px;margin-top:12px;font-size:13px;color:var(--text2)}
.stats span b{color:var(--text1)}
.msg{text-align:center;padding:60px 0;color:var(--text3)}
</style>
</head><body>
<h1>📈 График — BeehiveScale <a href="/">← Главная</a></h1>
<div class="toolbar">
  <button class="btn" onclick="setPeriod(1)">1 ч</button>
  <button class="btn" onclick="setPeriod(6)">6 ч</button>
  <button class="btn active" onclick="setPeriod(24)">24 ч</button>
  <button class="btn" onclick="setPeriod(0)">Всё</button>
  <span style="width:1px;background:var(--border);align-self:stretch;margin:0 4px"></span>
  <button class="btn" id="btnW"  onclick="setSeries('w')" title="Только вес">⚖ Вес</button>
  <button class="btn" id="btnT"  onclick="setSeries('t')" title="Только темп.">🌡 Темп</button>
  <button class="btn active" id="btnWT" onclick="setSeries('wt')" title="Вес + темп.">⚖+🌡</button>
  <span class="sep"></span>
  <button class="btn" onclick="loadData()">🔄 Обновить</button>
  <button class="btn" onclick="window.open('/api/log','_blank')" title="Скачать весь лог">⬇ Весь CSV</button>
  <input type="date" id="export-date" style="padding:5px 8px;font-size:12px;background:var(--panel);border:1px solid var(--border);color:var(--text1);border-radius:4px" title="Выбрать дату для экспорта">
  <button class="btn" onclick="downloadByDate()" title="Скачать CSV за выбранный день">⬇ За день</button>
</div>
<div class="card">
  <div class="chart-wrap" id="chart-wrap">
    <div class="msg" id="chart-msg">Загрузка...</div>
    <svg class="chart" id="chart-svg" style="display:none"></svg>
    <div class="tooltip" id="tooltip"></div>
  </div>
  <div class="chart-wrap" id="chart-wrap-t" style="height:160px;margin-top:8px;display:none">
    <svg class="chart" id="chart-svg-t" viewBox="0 0 800 140" preserveAspectRatio="none"></svg>
    <div class="tooltip" id="tooltip-t"></div>
  </div>
  <div class="stats" id="stats" style="display:none">
    <span>Вес — Мин: <b id="s-min">--</b> Макс: <b id="s-max">--</b> Ср: <b id="s-avg">--</b> кг</span>
    <span id="s-temp-stat" style="color:var(--blue)">Темп — Мин: <b id="s-tmin">--</b> Макс: <b id="s-tmax">--</b> °C</span>
    <span>Точек: <b id="s-pts">0</b></span>
    <span>Период: <b id="s-from">--</b> — <b id="s-to">--</b></span>
  </div>
</div>
<script>
var allData = [];
var period = 24;
var series = 'wt'; // 'w' | 't' | 'wt'

function setPeriod(h) {
  period = h;
  document.querySelectorAll('.toolbar .btn').forEach(function(b){
    var lbl = b.textContent.trim();
    if (lbl==='1 ч'||lbl==='6 ч'||lbl==='24 ч'||lbl==='Всё') b.classList.remove('active');
  });
  var labels = {1:'1 ч',6:'6 ч',24:'24 ч',0:'Всё'};
  document.querySelectorAll('.toolbar .btn').forEach(function(b){
    if (b.textContent.trim() === (labels[h]||'')) b.classList.add('active');
  });
  renderAll();
}

function setSeries(s) {
  series = s;
  ['btnW','btnT','btnWT'].forEach(function(id){ var el=document.getElementById(id); if(el) el.classList.remove('active'); });
  var map = {w:'btnW', t:'btnT', wt:'btnWT'};
  var el = document.getElementById(map[s]);
  if (el) el.classList.add('active');
  renderAll();
}

function renderAll() {
  renderChart();
  var wrapT = document.getElementById('chart-wrap-t');
  var stT   = document.getElementById('s-temp-stat');
  if (series === 't' || series === 'wt') {
    if (wrapT) wrapT.style.display = '';
    if (stT)   stT.style.display   = '';
    renderTempChart();
  } else {
    if (wrapT) wrapT.style.display = 'none';
    if (stT)   stT.style.display   = 'none';
  }
}

function parseDate(s) {
  // формат "DD.MM.YYYY HH:MM:SS"
  if (!s) return null;
  var m = s.match(/(\d{2})\.(\d{2})\.(\d{4})\s+(\d{2}):(\d{2}):(\d{2})/);
  if (m) return new Date(+m[3],+m[2]-1,+m[1],+m[4],+m[5],+m[6]);
  return new Date(s);
}

function loadData() {
  document.getElementById('chart-msg').style.display='';
  document.getElementById('chart-msg').textContent='Загрузка...';
  document.getElementById('chart-svg').style.display='none';
  document.getElementById('stats').style.display='none';
  fetch('/api/log/json')
    .then(r=>r.json())
    .then(function(d){ allData=d; renderAll(); })
    .catch(function(){ document.getElementById('chart-msg').textContent='Ошибка загрузки данных'; });
}

function renderChart() {
  var msg = document.getElementById('chart-msg');
  var svgEl = document.getElementById('chart-svg');
  if (!allData || allData.length === 0) {
    msg.textContent='Нет данных'; msg.style.display=''; svgEl.style.display='none'; return;
  }

  // Фильтр по периоду
  var pts = allData;
  if (period > 0) {
    var cutoff = Date.now() - period * 3600000;
    pts = allData.filter(function(d) {
      var t = parseDate(d.dt);
      return t && t.getTime() >= cutoff;
    });
    // Если нет данных с таймштампами — берём последние N точек
    if (pts.length === 0) {
      var n = period === 1 ? 60 : period === 6 ? 360 : 1440;
      pts = allData.slice(-Math.min(n, allData.length));
    }
  }
  if (pts.length === 0) {
    msg.textContent='Нет данных за выбранный период'; msg.style.display=''; svgEl.style.display='none'; return;
  }
  msg.style.display='none'; svgEl.style.display='';

  var weights = pts.map(function(d){ return parseFloat(d.w); }).filter(function(v){ return !isNaN(v); });
  var wMin = Math.min.apply(null,weights);
  var wMax = Math.max.apply(null,weights);
  if (wMax === wMin) { wMin -= 0.5; wMax += 0.5; }
  var wRange = wMax - wMin;
  var step = wRange <= 1 ? 0.2 : wRange <= 5 ? 1 : wRange <= 20 ? 5 : 10;
  var wMinR = Math.floor(wMin/step)*step;
  var wMaxR = Math.ceil(wMax/step)*step;

  // Статистика
  var avg = weights.reduce(function(a,b){return a+b;},0)/weights.length;
  document.getElementById('s-min').textContent = wMin.toFixed(2);
  document.getElementById('s-max').textContent = wMax.toFixed(2);
  document.getElementById('s-avg').textContent = avg.toFixed(2);
  document.getElementById('s-pts').textContent = pts.length;
  document.getElementById('s-from').textContent = pts[0].dt ? pts[0].dt.substring(0,16) : '--';
  document.getElementById('s-to').textContent   = pts[pts.length-1].dt ? pts[pts.length-1].dt.substring(0,16) : '--';
  document.getElementById('stats').style.display='';

  // Размеры SVG (viewBox)
  var W=800, H=300, L=52, R=12, T=12, B=32;
  var pW=W-L-R, pH=H-T-B;
  svgEl.setAttribute('viewBox','0 0 '+W+' '+H);

  function xS(i){ return L + (i/(pts.length-1||1))*pW; }
  function yS(w){ return T + pH - ((w-wMinR)/(wMaxR-wMinR||1))*pH; }

  var html='';

  // Горизонтальная сетка + метки Y
  var yTicks = Math.round((wMaxR-wMinR)/step);
  if (yTicks < 2) yTicks = 4;
  if (yTicks > 8) yTicks = 8;
  for (var k=0; k<=yTicks; k++) {
    var w = wMinR + (wMaxR-wMinR)*k/yTicks;
    var y = yS(w);
    var isDark = (k%2===0);
    html += '<line x1="'+L+'" y1="'+y.toFixed(1)+'" x2="'+(W-R)+'" y2="'+y.toFixed(1)+'" stroke="'+(isDark?'#252e1f':'#1e261a')+'" stroke-width="1"/>';
    var lbl = (w%1===0) ? w.toFixed(0) : w.toFixed(1);
    html += '<text x="'+(L-6)+'" y="'+(y+4).toFixed(1)+'" text-anchor="end" fill="#7a8c6a" font-size="11">'+lbl+'</text>';
  }

  // Вертикальная сетка + метки X (5 точек)
  var xTicks = Math.min(5, pts.length);
  for (var t=0; t<xTicks; t++) {
    var idx = Math.round(t*(pts.length-1)/(xTicks-1||1));
    var x = xS(idx);
    html += '<line x1="'+x.toFixed(1)+'" y1="'+T+'" x2="'+x.toFixed(1)+'" y2="'+(T+pH)+'" stroke="#1e261a" stroke-width="1"/>';
    var lbl2 = pts[idx].dt ? pts[idx].dt.substring(11,16) : '';
    var anchor = t===0?'start':t===xTicks-1?'end':'middle';
    html += '<text x="'+x.toFixed(1)+'" y="'+(H-6)+'" text-anchor="'+anchor+'" fill="#7a8c6a" font-size="10">'+lbl2+'</text>';
  }

  // Даты по краям оси X
  var d0 = pts[0].dt ? pts[0].dt.substring(0,10) : '';
  var d1 = pts[pts.length-1].dt ? pts[pts.length-1].dt.substring(0,10) : '';
  if (d0) html += '<text x="'+L+'" y="'+(H-6)+'" text-anchor="start" fill="#506040" font-size="9">'+d0+'</text>';
  if (d1 && d1!==d0) html += '<text x="'+(W-R)+'" y="'+(H-6)+'" text-anchor="end" fill="#506040" font-size="9">'+d1+'</text>';

  // Оси
  html += '<line x1="'+L+'" y1="'+T+'" x2="'+L+'" y2="'+(T+pH)+'" stroke="#506040" stroke-width="1.5"/>';
  html += '<line x1="'+L+'" y1="'+(T+pH)+'" x2="'+(W-R)+'" y2="'+(T+pH)+'" stroke="#506040" stroke-width="1.5"/>';

  // Подпись оси Y
  html += '<text x="12" y="'+(T+pH/2)+'" text-anchor="middle" fill="#7a8c6a" font-size="11" transform="rotate(-90,12,'+(T+pH/2)+')">кг</text>';

  // Заливка
  var area = 'M '+xS(0).toFixed(1)+' '+(T+pH);
  var line = 'M '+xS(0).toFixed(1)+' '+yS(weights[0]).toFixed(1);
  for (var i=0; i<pts.length; i++) {
    var xx=xS(i), yy=yS(weights[i]);
    area += ' L '+xx.toFixed(1)+' '+yy.toFixed(1);
    if (i>0) line += ' L '+xx.toFixed(1)+' '+yy.toFixed(1);
  }
  area += ' L '+xS(pts.length-1).toFixed(1)+' '+(T+pH)+' Z';
  html += '<path d="'+area+'" fill="rgba(245,166,35,0.10)" stroke="none"/>';
  html += '<path d="'+line+'" fill="none" stroke="#f5a623" stroke-width="2"/>';

  // Маркер последней точки
  var lx=xS(pts.length-1), ly=yS(weights[weights.length-1]);
  html += '<circle cx="'+lx.toFixed(1)+'" cy="'+ly.toFixed(1)+'" r="4" fill="#f5a623"/>';

  // Невидимые точки для tooltip (поверх всего)
  for (var i=0; i<pts.length; i++) {
    html += '<circle class="dot" data-i="'+i+'" cx="'+xS(i).toFixed(1)+'" cy="'+yS(weights[i]).toFixed(1)+'" r="5" fill="transparent" stroke="none"/>';
  }

  svgEl.innerHTML = html;

  // Tooltip через mousemove по SVG
  var wrap = document.getElementById('chart-wrap');
  var tooltip = document.getElementById('tooltip');
  svgEl.addEventListener('mousemove', function(e) {
    var rect = svgEl.getBoundingClientRect();
    var mx = e.clientX - rect.left;
    // найти ближайшую точку по X
    var svgX = mx / rect.width * W;
    var best = -1, bestDist = 9999;
    for (var i=0; i<pts.length; i++) {
      var d = Math.abs(xS(i) - svgX);
      if (d < bestDist) { bestDist=d; best=i; }
    }
    if (best < 0 || bestDist > W/pts.length*2) { tooltip.style.display='none'; return; }
    var p = pts[best];
    tooltip.innerHTML = '<b>'+parseFloat(p.w).toFixed(3)+' кг</b><br>'+( p.dt||'');
    if (p.t !== undefined) tooltip.innerHTML += '<br>🌡 '+parseFloat(p.t).toFixed(1)+' °C';
    tooltip.style.display = '';
    var tx = e.clientX - rect.left + 12;
    var ty = e.clientY - rect.top - 40;
    if (tx + 130 > rect.width) tx = e.clientX - rect.left - 140;
    tooltip.style.left = tx + 'px';
    tooltip.style.top  = ty + 'px';
  });
  svgEl.addEventListener('mouseleave', function() { tooltip.style.display='none'; });

  // Скрываем вес если показываем только температуру
  svgEl.style.display = (series === 't') ? 'none' : '';
  document.getElementById('chart-wrap').style.display = (series === 't') ? 'none' : '';
}

// ── Фича 15: График температуры ───────────────────────────────────────────
function renderTempChart() {
  var svgEl = document.getElementById('chart-svg-t');
  var wrap  = document.getElementById('chart-wrap-t');
  if (!svgEl) return;

  // Фильтр по периоду (аналогично renderChart)
  var pts = allData;
  if (period > 0) {
    var cutoff = Date.now() - period * 3600000;
    pts = allData.filter(function(d){ var t=parseDate(d.dt); return t && t.getTime()>=cutoff; });
    if (pts.length === 0) { var n = period===1?60:period===6?360:1440; pts=allData.slice(-Math.min(n,allData.length)); }
  }

  var temps = pts.map(function(d){ return parseFloat(d.t); }).filter(function(v){ return !isNaN(v) && v > -90; });
  if (temps.length === 0) { svgEl.innerHTML='<text x="400" y="70" text-anchor="middle" fill="#506040" font-size="10">Нет данных температуры</text>'; return; }

  var tMin = Math.min.apply(null, temps);
  var tMax = Math.max.apply(null, temps);
  if (tMax === tMin) { tMin -= 1; tMax += 1; }

  // Статистика
  document.getElementById('s-tmin').textContent = tMin.toFixed(1);
  document.getElementById('s-tmax').textContent = tMax.toFixed(1);

  var W=800, H=120, L=44, R=10, T=8, B=22;
  var pW=W-L-R, pH=H-T-B;
  svgEl.setAttribute('viewBox','0 0 '+W+' '+H);

  var xS = function(i){ return L + i/(pts.length-1||1)*pW; };
  var yS = function(v){ return T + pH - (v-tMin)/(tMax-tMin||1)*pH; };

  var html = '';

  // Сетка
  for (var k=0; k<=3; k++) {
    var tv = tMin + (tMax-tMin)*k/3;
    var ty = yS(tv);
    html += '<line x1="'+L+'" y1="'+ty.toFixed(1)+'" x2="'+(W-R)+'" y2="'+ty.toFixed(1)+'" stroke="#1e2e1e" stroke-width="1"/>';
    html += '<text x="'+(L-4)+'" y="'+(ty+3.5).toFixed(1)+'" text-anchor="end" fill="#6a8c7a" font-size="8">'+tv.toFixed(1)+'</text>';
  }

  // Метки оси X
  var xTicks = [0, Math.floor((pts.length-1)/2), pts.length-1];
  xTicks.forEach(function(i){
    if (i < 0 || i >= pts.length) return;
    var x = xS(i), lbl = pts[i].dt ? pts[i].dt.substring(11,16) : '';
    html += '<line x1="'+x.toFixed(1)+'" y1="'+T+'" x2="'+x.toFixed(1)+'" y2="'+(T+pH)+'" stroke="#1e2e1e" stroke-width="1"/>';
    var anchor = i===0?'start':i===pts.length-1?'end':'middle';
    html += '<text x="'+x.toFixed(1)+'" y="'+(H-4)+'" text-anchor="'+anchor+'" fill="#6a8c7a" font-size="8">'+lbl+'</text>';
  });

  // Оси
  html += '<line x1="'+L+'" y1="'+T+'" x2="'+L+'" y2="'+(T+pH)+'" stroke="#506040" stroke-width="1.5"/>';
  html += '<line x1="'+L+'" y1="'+(T+pH)+'" x2="'+(W-R)+'" y2="'+(T+pH)+'" stroke="#506040" stroke-width="1.5"/>';
  html += '<text x="10" y="'+(T+pH/2)+'" text-anchor="middle" fill="#6a8c7a" font-size="9" transform="rotate(-90,10,'+(T+pH/2)+')">°C</text>';

  // Линия температуры
  var tPts = pts.filter(function(d){ return !isNaN(parseFloat(d.t)) && parseFloat(d.t) > -90; });
  if (tPts.length > 1) {
    var area = '', line = '';
    var firstI = true;
    for (var i=0; i<pts.length; i++) {
      var tv2 = parseFloat(pts[i].t);
      if (isNaN(tv2) || tv2 <= -90) continue;
      var xx = xS(i), yy = yS(tv2);
      if (firstI) { area = 'M '+xx.toFixed(1)+' '+(T+pH); line = 'M '+xx.toFixed(1)+' '+yy.toFixed(1); firstI=false; }
      area += ' L '+xx.toFixed(1)+' '+yy.toFixed(1);
      line += ' L '+xx.toFixed(1)+' '+yy.toFixed(1);
    }
    if (!firstI) {
      area += ' L '+xS(pts.length-1).toFixed(1)+' '+(T+pH)+' Z';
      html += '<path d="'+area+'" fill="rgba(86,204,242,0.10)" stroke="none"/>';
      html += '<path d="'+line+'" fill="none" stroke="#56ccf2" stroke-width="2"/>';
      // Маркер последней
      var lastIdx = pts.length-1;
      while (lastIdx > 0 && (isNaN(parseFloat(pts[lastIdx].t)) || parseFloat(pts[lastIdx].t) <= -90)) lastIdx--;
      html += '<circle cx="'+xS(lastIdx).toFixed(1)+'" cy="'+yS(parseFloat(pts[lastIdx].t)).toFixed(1)+'" r="4" fill="#56ccf2"/>';
    }
  }
  svgEl.innerHTML = html;

  // Tooltip
  var tooltip2 = document.getElementById('tooltip-t');
  svgEl.addEventListener('mousemove', function(e){
    var rect = svgEl.getBoundingClientRect();
    var svgX = (e.clientX - rect.left) / rect.width * W;
    var best=-1, bestDist=9999;
    for (var i=0; i<pts.length; i++) {
      var d2 = Math.abs(xS(i)-svgX);
      if (d2 < bestDist) { bestDist=d2; best=i; }
    }
    if (best < 0 || bestDist > W/pts.length*2) { tooltip2.style.display='none'; return; }
    var p = pts[best];
    var tv3 = parseFloat(p.t);
    tooltip2.innerHTML = '<b>'+(isNaN(tv3)||tv3<-90?'--':tv3.toFixed(1)+' °C')+'</b><br>'+(p.dt||'');
    tooltip2.style.display = '';
    var tx = e.clientX-rect.left+12, ty = e.clientY-rect.top-40;
    if (tx+130>rect.width) tx = e.clientX-rect.left-140;
    tooltip2.style.left=tx+'px'; tooltip2.style.top=ty+'px';
  });
  svgEl.addEventListener('mouseleave', function(){ tooltip2.style.display='none'; });
}

function downloadByDate() {
  var d = document.getElementById('export-date').value;
  if (!d) {
    alert('Выберите дату');
    return;
  }
  window.open('/api/log?date=' + d, '_blank');
}

// Ставим дату по умолчанию — сегодня (если возможно)
(function() {
  var el = document.getElementById('export-date');
  if (!el) return;
  var now = new Date();
  var y = now.getFullYear();
  var m = String(now.getMonth()+1).padStart(2,'0');
  var dd = String(now.getDate()).padStart(2,'0');
  el.value = y + '-' + m + '-' + dd;
})();

loadData();
</script>
</body></html>
)rawhtml";

// ─── Страница настроек WiFi (/wifi) ──────────────────────────────────────
static const char WIFI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html lang="ru"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>📶 Wi-Fi — BeehiveScale</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0d0f0b;--panel:#141710;--border:#2e3829;--amber:#f5a623;--text1:#e8e0d0;--text2:#b0a890;--text3:#7a8c6a;--red:#e05555;--green:#6fcf97;--blue:#56ccf2}
body{background:var(--bg);color:var(--text1);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;font-size:14px;padding:16px;max-width:500px;margin:0 auto}
h1{font-size:18px;color:var(--amber);margin-bottom:16px;display:flex;align-items:center;gap:10px}
h1 a{color:var(--text3);font-size:13px;text-decoration:none;font-weight:normal;margin-left:auto}
h1 a:hover{color:var(--amber)}
.card{background:var(--panel);border:1px solid var(--border);border-top:2px solid var(--amber);border-radius:10px;padding:16px;margin-bottom:16px}
.card-title{font-size:13px;font-weight:600;color:var(--amber);margin-bottom:12px;letter-spacing:.5px}
.form-row{margin-bottom:10px}
.form-row label{display:block;font-size:11px;color:var(--text3);margin-bottom:4px;letter-spacing:.5px;text-transform:uppercase}
.form-row input{width:100%;padding:7px 10px;background:#0d0f0b;border:1px solid var(--border);color:var(--text1);border-radius:5px;font-size:13px}
.radio-group{display:flex;gap:8px;margin-bottom:12px}
.radio-opt{display:flex;align-items:center;gap:8px;cursor:pointer;flex:1;background:#1c2018;padding:12px;border:1px solid var(--border);border-radius:6px;transition:border-color .2s}
.radio-opt:hover{border-color:var(--amber)}
.radio-opt input[type=radio]{accent-color:var(--amber)}
.radio-opt span{font-size:13px;line-height:1.4}
.radio-opt small{color:var(--text3);font-size:11px}
.btn{padding:8px 18px;border:1px solid var(--border);background:var(--panel);color:var(--text1);border-radius:6px;cursor:pointer;font-size:13px}
.btn-green{border-color:var(--green);color:var(--green)}
.btn-green:hover{background:var(--green);color:#000}
.hint{font-size:11px;color:var(--text3);margin-top:12px;line-height:1.6}
.toast{position:fixed;bottom:20px;right:20px;background:var(--panel);border:1px solid var(--amber);border-radius:8px;padding:10px 16px;font-size:13px;transform:translateY(80px);transition:transform .3s;z-index:200}
.toast.show{transform:none}
.toast.err{border-color:var(--red);color:var(--red)}
</style></head><body>
<h1>📶 Настройки Wi-Fi <a href="/">← Главная</a></h1>
<div class="card">
  <div class="card-title">Режим подключения</div>
  <div class="radio-group">
    <label class="radio-opt">
      <input type="radio" name="wm" id="wm-ap" value="0" __WF_AP__ onchange="onChange()">
      <span>📡 Точка доступа (AP)<br><small>Устройство создаёт сеть BeehiveScale<br>IP: 192.168.4.1</small></span>
    </label>
    <label class="radio-opt">
      <input type="radio" name="wm" id="wm-sta" value="1" __WF_STA__ onchange="onChange()">
      <span>🌐 Роутер (STA)<br><small>Подключение к домашней сети<br>IP: от DHCP роутера</small></span>
    </label>
  </div>
  <div id="sta-block" style="display:__WF_STABLK__">
    <div class="form-row"><label>SSID роутера</label>
      <input type="text" id="ssid" value="__WF_SSID__" maxlength="32" placeholder="Название Wi-Fi сети" autocomplete="off">
    </div>
    <div class="form-row"><label>Пароль роутера</label>
      <input type="password" id="pass" value="" maxlength="32" placeholder="Пароль (оставьте пустым чтобы не менять)" autocomplete="new-password">
    </div>
  </div>
  <button class="btn btn-green" onclick="save()">💾 Сохранить и перезагрузить</button>
  <div class="hint">
    AP режим: подключайтесь напрямую, веб на 192.168.4.1<br>
    STA режим: доступен NTP-время и Telegram-уведомления
  </div>
</div>
<div class="toast" id="toast"></div>
<script>
function onChange(){
  document.getElementById('sta-block').style.display=document.getElementById('wm-sta').checked?'block':'none';
}
function showToast(msg,err,ms){
  var el=document.getElementById('toast');
  el.textContent=msg;el.className='toast'+(err?' err':'')+' show';
  setTimeout(function(){el.classList.remove('show');},ms||3000);
}
function save(){
  var mode=document.querySelector('input[name="wm"]:checked').value;
  var body={wifiMode:parseInt(mode)};
  if(mode=='1'){
    var ssid=document.getElementById('ssid').value.trim();
    var pass=document.getElementById('pass').value;
    if(!ssid){showToast('Введите SSID роутера',true);return;}
    body.wifiSsid=ssid;
    if(pass.length>0)body.wifiPass=pass;
  }
  fetch('/api/wifi/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
  .then(function(r){return r.json();})
  .then(function(d){
    if(d.ok){
      var isSta=(mode=='1');
      showToast(isSta?'Сохранено! Подключитесь к роутеру → beehivescale.local':'Сохранено! Подключитесь к сети BeehiveScale → 192.168.4.1',false,9000);
    }else{showToast('Ошибка: '+d.msg,true);}
  })
  .catch(function(){showToast('Ошибка связи',true);});
}
</script>
</body></html>
)rawhtml";

// HTML-экранирование строки (защита от XSS)
static String _htmlEscape(const char *src) {
  String out;
  out.reserve(strlen(src) + 8);
  while (*src) {
    switch (*src) {
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '&':  out += "&amp;";  break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += *src;     break;
    }
    src++;
  }
  return out;
}

static String _buildWifiPage() {
  String html = FPSTR(WIFI_HTML);
  uint8_t wfMode = get_wifi_mode();
  html.replace("__WF_AP__",     wfMode == 0 ? "checked" : "");
  html.replace("__WF_STA__",    wfMode == 1 ? "checked" : "");
  html.replace("__WF_STABLK__", wfMode == 1 ? "block" : "none");
  char wfSsid[33];
  get_wifi_ssid(wfSsid, sizeof(wfSsid));
  if (wfSsid[0] == '\0') strncpy(wfSsid, WIFI_SSID, sizeof(wfSsid)-1);
  html.replace("__WF_SSID__", _htmlEscape(wfSsid));
  return html;
}

// ─── Маршруты ─────────────────────────────────────────────────────────────
static inline void _activity() {
  lastActivityTime = millis();
  if (_wa.onActivity) _wa.onActivity();
}

// Отправка PROGMEM-строки чанками (без копирования всего в heap)
static void _sendProgmemChunked(const char *pgm) {
  _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _srv.send(200, "text/html; charset=utf-8", "");
  size_t total = strlen_P(pgm);
  size_t sent = 0;
  char chunk[1024];
  while (sent < total) {
    size_t n = min((size_t)sizeof(chunk), total - sent);
    memcpy_P(chunk, pgm + sent, n);
    _srv.sendContent(chunk, n);
    sent += n;
  }
}

static void _handleRoot() {
  if (!_auth()) return;
  _activity();
  _sendProgmemChunked(PAGE_HTML);
}

// ─── /api/config  GET — начальные значения для форм настроек ─────────────
static void _handleConfig() {
  if (!_auth()) return;
  StaticJsonDocument<384> doc;
  doc["alertDelta"]  = web_get_alert_delta();
  doc["calibWeight"] = web_get_calib_weight();
  doc["emaAlpha"]    = web_get_ema_alpha();
  doc["sleepSec"]    = (unsigned long)get_sleep_sec();
  doc["lcdBlSec"]    = (unsigned int)get_lcd_bl_sec();
  doc["wifiMode"]    = (int)get_wifi_mode();
  {
    char tgTok[50], tgCid[16];
    get_tg_token(tgTok, sizeof(tgTok));
    get_tg_chatid(tgCid, sizeof(tgCid));
    doc["tgToken"]  = tgTok;
    doc["tgChatId"] = tgCid;
  }
  String out; serializeJson(doc, out);
  _srv.send(200, "application/json", out);
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
  doc["sdFallback"] = log_using_fallback();
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

static void _handleChart() {
  if (!_auth()) return;
  _activity();
  _sendProgmemChunked(CHART_HTML);
}

static void _handleWifi() {
  if (!_auth()) return;
  _activity();
  _srv.send(200, "text/html; charset=utf-8", _buildWifiPage());
}

// ─── /api/tg/settings  POST — сохранить Telegram токен и chat_id ─────────
static void _handleTgSettings() {
  if (!_auth()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }
  if (doc.containsKey("token")) {
    const char* t = doc["token"].as<const char*>();
    if (t && strlen(t) > 0 && strlen(t) < 50) set_tg_token(t);
    else if (t && strlen(t) == 0) set_tg_token("");
  }
  if (doc.containsKey("chatId")) {
    const char* c = doc["chatId"].as<const char*>();
    if (c && strlen(c) < 16) set_tg_chatid(c);
  }
  _sendJson(true, "Telegram настройки сохранены");
}

// ─── /api/tg/test  POST — отправить тестовое сообщение ──────────────────
static void _handleTgTest() {
  if (!_auth()) return;
  _activity();
  bool ok = tg_send_message("BeehiveScale: тестовое сообщение. Весы работают!");
  _sendJson(ok, ok ? "Сообщение отправлено" : "Ошибка отправки (проверьте token/chat_id)");
}

// ─── /api/calib/set  POST — установить cal.factor и offset ───────────────
static void _handleCalibSet() {
  if (!_auth()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }
  bool changed = false;
  if (doc.containsKey("calibFactor") && _wa.doSetCalibFactor) {
    float cf = doc["calibFactor"].as<float>();
    if (cf >= 100.0f && cf <= 100000.0f) {
      _wa.doSetCalibFactor(cf);
      changed = true;
    } else { _sendJson(false,"calibFactor: 100–100000"); return; }
  }
  if (doc.containsKey("offset") && _wa.doSetCalibOffset) {
    long ofs = doc["offset"].as<long>();
    _wa.doSetCalibOffset(ofs);
    changed = true;
  }
  if (changed) _sendJson(true, "Калибровка обновлена");
  else _sendJson(false, "Нет данных для обновления");
}

// ─── /api/wifi/settings  POST — сохранить режим WiFi и credentials ──────
static void _handleWifiSettings() {
  if (!_auth()) return;
  _activity();
  if (_srv.method() != HTTP_POST) { _sendJson(false,"Только POST"); return; }
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, _srv.arg("plain"));
  if (err) { _sendJson(false,"Ошибка JSON"); return; }
  if (!doc.containsKey("wifiMode")) { _sendJson(false,"Нет wifiMode"); return; }
  uint8_t mode = doc["wifiMode"].as<uint8_t>();
  if (mode > 1) { _sendJson(false,"wifiMode: 0 или 1"); return; }
  if (mode == 1) {
    const char *ssid = doc["wifiSsid"].as<const char*>();
    const char *pass = doc["wifiPass"].as<const char*>();
    if (!ssid || strlen(ssid) == 0) { _sendJson(false,"Введите SSID роутера"); return; }
    set_wifi_ssid(ssid);
    if (pass) set_wifi_sta_pass(pass);
  }
  set_wifi_mode(mode);
  _sendJson(true, "WiFi настройки сохранены, перезагрузка...");
  _srv.client().flush();
  delay(300);
  ESP.restart();
}

static void _handleNotFound() {
  _srv.send(404, "text/plain", "Not found");
}

// ─── /api/log  GET — скачать CSV-лог (опционально: ?date=YYYY-MM-DD) ─────
static void _handleLog() {
  if (!_auth()) return;
  if (!log_exists()) {
    _srv.send(404, "text/plain", "Log not found");
    return;
  }
  String date = _srv.arg("date");  // "" если параметр не передан
  if (date.length() == 0) {
    // Без фильтра — стримим весь файл напрямую
#ifdef USE_SD_CARD
    File f = SD.open(LOG_FILE, FILE_READ);
#else
    File f = LOG_FS.open(LOG_FILE, "r");
#endif
    if (!f) { _srv.send(500, "text/plain", "Cannot open log"); return; }
    _srv.sendHeader("Content-Disposition", "attachment; filename=\"beehive_log.csv\"");
    _srv.streamFile(f, "text/csv");
    f.close();
  } else {
    // С фильтром по дате — собираем в String и отдаём с Content-Length
    // (ESP8266 требует известную длину иначе браузер обрывает соединение)
    String csv;
    csv.reserve(4096);
    {
      // Пишем через StringStream-обёртку
      class StrStream : public Stream {
      public:
        String &buf;
        StrStream(String &b) : buf(b) {}
        size_t write(uint8_t c) override { buf += (char)c; return 1; }
        size_t write(const uint8_t *b, size_t s) override {
          buf.reserve(buf.length() + s);
          for (size_t i=0; i<s; i++) buf += (char)b[i];
          return s;
        }
        int available() override { return 0; }
        int read()      override { return -1; }
        int peek()      override { return -1; }
        void flush()    override {}
      } ss(csv);
      log_stream_csv_date(ss, date);
    }
    String fname = "beehive_" + date + ".csv";
    _srv.sendHeader("Content-Disposition", "attachment; filename=\"" + fname + "\"");
    _srv.send(200, "text/csv; charset=utf-8", csv);
  }
}

// ─── /api/daystat  GET — суточная статистика (фичи 12, 17) ──────────────
static void _handleDayStat() {
  if (!_auth()) return;
  _activity();
  // Дата из параметра или текущая из RTC
  String date = _srv.arg("date");
  if (date.length() == 0) date = *_wd.datetime;  // "DD.MM.YYYY HH:MM:SS" → берём первые 10
  if (date.length() > 10) date = date.substring(0, 10);

  DayStat ds = log_day_stat(date);

  StaticJsonDocument<256> doc;
  doc["date"]   = date;
  doc["valid"]  = ds.valid;
  doc["wMin"]   = ds.valid ? ds.wMin : 0;
  doc["wMax"]   = ds.valid ? ds.wMax : 0;
  doc["tMin"]   = (ds.valid && ds.tMin < 1e8f) ? ds.tMin : (float)NAN;
  doc["tMax"]   = (ds.valid && ds.tMax > -1e8f) ? ds.tMax : (float)NAN;
  doc["count"]  = ds.count;

  // Фича 17: информация об улье
  // Сезон по месяцу
  int month = 0;
  if (date.length() >= 7) month = date.substring(3, 5).toInt();  // "DD.MM.YYYY"
  const char* season =
    (month >= 3 && month <= 5)  ? "Vesna" :
    (month >= 6 && month <= 8)  ? "Leto"  :
    (month >= 9 && month <= 11) ? "Osen"  : "Zima";
  doc["season"] = season;

  // Дней наблюдений: размер лога / (примерно 50 байт/строка / 1440 строк в сутки)
  size_t logSz = log_size();
  doc["daysSinceStart"] = (int)(logSz / (50UL * 1440UL));

  // Последнее значительное изменение — дельта текущий - опорный
  doc["deltaKg"] = *_wd.weight - *_wd.prevWeight;

  String out; serializeJson(doc, out);
  _srv.send(200, "application/json", out);
}

// ─── /api/log/clear  POST — очистить лог ─────────────────────────────────
static void _handleLogClear() {
  if (!_auth()) return;
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
  _srv.on("/api/log",          HTTP_GET,  _handleLog);
  _srv.on("/api/daystat",      HTTP_GET,  _handleDayStat);
  _srv.on("/api/log/clear",    HTTP_POST, _handleLogClear);
  _srv.on("/api/log/json",     HTTP_GET,  _handleLogJson);
  _srv.on("/chart",            HTTP_GET,  _handleChart);
  _srv.on("/api/tg/settings",  HTTP_POST, _handleTgSettings);
  _srv.on("/api/tg/test",      HTTP_POST, _handleTgTest);
  _srv.on("/api/calib/set",    HTTP_POST, _handleCalibSet);
  _srv.on("/wifi",              HTTP_GET,  _handleWifi);
  _srv.on("/api/wifi/settings", HTTP_POST, _handleWifiSettings);
  _srv.on("/api/config",        HTTP_GET,  _handleConfig);
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

void webserver_stop() {
  _srv.stop();
  Serial.println(F("[WebServer] Stopped"));
}
