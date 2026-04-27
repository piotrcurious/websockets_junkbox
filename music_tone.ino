#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

static const char *AP_SSID = "ESP32-Jam";
static const char *AP_PASS = "music1234";

static const int BUZZER_PIN = 25;   // connect piezo/buzzer here
static const int BUZZER_CH   = 0;    // LEDC channel
static const int BUZZER_RES  = 8;    // duty resolution

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>ESP32 Jam</title>
  <style>
    :root { --bg:#111; --fg:#eee; --acc:#66d9ef; --key:#222; --key2:#333; }
    body {
      margin:0; font-family:system-ui, sans-serif; background:var(--bg); color:var(--fg);
      display:grid; place-items:center; min-height:100vh;
    }
    .wrap { width:min(900px, 96vw); }
    h1 { margin:0 0 8px; font-size:clamp(28px, 5vw, 48px); }
    p { margin:0 0 16px; opacity:.85; }
    .bar {
      display:flex; gap:10px; align-items:center; margin:12px 0 18px;
      flex-wrap:wrap;
    }
    .status {
      padding:8px 12px; border:1px solid #444; border-radius:999px; background:#1a1a1a;
    }
    .kbd {
      display:grid;
      grid-template-columns: repeat(8, minmax(70px, 1fr));
      gap:10px;
    }
    button.note {
      appearance:none; border:none; border-radius:18px; padding:18px 10px;
      background:linear-gradient(180deg, var(--key2), var(--key));
      color:var(--fg); font-size:18px; cursor:pointer; user-select:none;
      box-shadow:0 6px 18px rgba(0,0,0,.35);
      touch-action:none;
    }
    button.note.active {
      outline:2px solid var(--acc);
      box-shadow:0 0 0 2px rgba(102,217,239,.2), 0 8px 26px rgba(0,0,0,.5);
      transform:translateY(-1px);
    }
    .hint { margin-top:14px; opacity:.75; line-height:1.45; }
    .small { font-size:14px; opacity:.8; }
    @media (max-width:700px) {
      .kbd { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <h1>ESP32 Jam</h1>
    <p>Tap the keys or use your computer keyboard. The browser sends note commands over WebSocket.</p>

    <div class="bar">
      <div class="status" id="status">connecting…</div>
      <div class="status small">WS: <span id="wsurl"></span></div>
    </div>

    <div class="kbd" id="keys"></div>

    <div class="hint">
      Keyboard mapping: A S D F G H J K<br>
      Release stops the tone. This is control-only audio: the ESP32 makes the sound locally.
    </div>
  </div>

<script>
(() => {
  const notes = [
    {name:"C4", freq:261.63, key:"a"},
    {name:"D4", freq:293.66, key:"s"},
    {name:"E4", freq:329.63, key:"d"},
    {name:"F4", freq:349.23, key:"f"},
    {name:"G4", freq:392.00, key:"g"},
    {name:"A4", freq:440.00, key:"h"},
    {name:"B4", freq:493.88, key:"j"},
    {name:"C5", freq:523.25, key:"k"},
  ];

  const statusEl = document.getElementById("status");
  const wsurlEl = document.getElementById("wsurl");
  const keysEl = document.getElementById("keys");

  let ws = null;
  let activeButton = null;
  let downFreq = null;
  const buttonsByKey = new Map();

  function setStatus(text) { statusEl.textContent = text; }

  function connect() {
    const url = `ws://${location.host}/ws`;
    wsurlEl.textContent = url;
    ws = new WebSocket(url);

    ws.onopen = () => setStatus("connected");
    ws.onclose = () => {
      setStatus("disconnected, reconnecting…");
      stopTone();
      setTimeout(connect, 700);
    };
    ws.onerror = () => setStatus("websocket error");
    ws.onmessage = (ev) => {
      // Optional: show any status messages from the ESP32
      console.log("ESP32:", ev.data);
    };
  }

  function send(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(msg);
  }

  function play(freq, btn) {
    if (activeButton && activeButton !== btn) activeButton.classList.remove("active");
    activeButton = btn;
    downFreq = freq;
    btn.classList.add("active");
    send(`ON:${freq}`);
  }

  function stopTone(btn) {
    if (!activeButton) return;
    if (!btn || btn === activeButton) {
      activeButton.classList.remove("active");
      activeButton = null;
      downFreq = null;
      send("OFF");
    }
  }

  notes.forEach(n => {
    const b = document.createElement("button");
    b.className = "note";
    b.textContent = `${n.name}\n${n.key.toUpperCase()}`;
    b.title = `${n.name} (${n.freq.toFixed(2)} Hz)`;
    b.addEventListener("pointerdown", e => {
      e.preventDefault();
      b.setPointerCapture(e.pointerId);
      play(n.freq, b);
    });
    b.addEventListener("pointerup", e => {
      e.preventDefault();
      stopTone(b);
    });
    b.addEventListener("pointercancel", () => stopTone(b));
    b.addEventListener("pointerleave", () => {
      // If the pointer is not captured anymore, stop the note.
      if (activeButton === b) stopTone(b);
    });
    keysEl.appendChild(b);
    buttonsByKey.set(n.key, {button: b, freq: n.freq});
  });

  window.addEventListener("keydown", e => {
    const item = buttonsByKey.get(e.key.toLowerCase());
    if (!item || e.repeat) return;
    play(item.freq, item.button);
  });

  window.addEventListener("keyup", e => {
    const item = buttonsByKey.get(e.key.toLowerCase());
    if (!item) return;
    stopTone(item.button);
  });

  document.addEventListener("visibilitychange", () => {
    if (document.hidden) stopTone();
  });

  connect();
})();
</script>
</body>
</html>
)rawliteral";

void startTone(float freq) {
  if (freq <= 0.0f) {
    ledcWriteTone(BUZZER_CH, 0);
    ledcWrite(BUZZER_CH, 0);
    return;
  }
  ledcWriteTone(BUZZER_CH, (uint32_t)freq);
  ledcWrite(BUZZER_CH, 128); // ~50% duty for a clean square wave
}

void stopTone() {
  ledcWriteTone(BUZZER_CH, 0);
  ledcWrite(BUZZER_CH, 0);
}

void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text("ESP32 ready");
    return;
  }

  if (type == WS_EVT_DISCONNECT) {
    stopTone();
    return;
  }

  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

  String msg;
  msg.reserve(len + 1);
  for (size_t i = 0; i < len; i++) msg += (char)data[i];
  msg.trim();

  if (msg.startsWith("ON:")) {
    float freq = msg.substring(3).toFloat();
    startTone(freq);
    client->text("OK");
  } else if (msg == "OFF") {
    stopTone();
    client->text("OK");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  ledcSetup(BUZZER_CH, 2000, BUZZER_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CH);
  stopTone();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("Access point started");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.begin();
}

void loop() {
  ws.cleanupClients();
}
