#include <ArduinoJson.h>
#include <M5Cardputer.h>

// Sem novidade do PC por esse tempo, a tela avisa em vez de exibir dado velho como atual.
static const uint32_t STALE_MS = 20000;
static const uint32_t BG = 0x0000, DIM = 0x8410, FG = 0xFFFF, ACCENT = 0xDBAB, WARN = 0xFBE0;

struct Data {
    uint64_t day = 0, dayCache = 0, week = 0, weekCache = 0, sess = 0, sessCache = 0, rate = 0;
    uint32_t block = 0;
    uint64_t hours[12] = {};
    String clock = "--:--", model = "-";
} data;

M5Canvas canvas(&M5Cardputer.Display);
String input;
uint32_t lastRx = 0;
bool received = false, dirty = true;
int view = 0;

String human(uint64_t n) {
    char buf[16];
    if (n >= 1000000000ULL) snprintf(buf, sizeof buf, "%.2f G", n / 1e9);
    else if (n >= 1000000ULL) snprintf(buf, sizeof buf, "%.2f M", n / 1e6);
    else if (n >= 1000ULL) snprintf(buf, sizeof buf, "%.1f k", n / 1e3);
    else snprintf(buf, sizeof buf, "%llu", n);
    String s(buf);
    s.replace('.', ',');
    return s;
}

void applyLine(const String &line) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;
    data.clock = doc["t"] | "--:--";
    data.day = doc["d"];
    data.dayCache = doc["dc"];
    data.week = doc["w"];
    data.weekCache = doc["wc"];
    data.sess = doc["s"];
    data.sessCache = doc["sc"];
    data.rate = doc["r"];
    data.model = doc["m"] | "-";
    data.block = doc["b"];
    for (int i = 0; i < 12; i++) data.hours[i] = doc["h"][i];
    lastRx = millis();
    received = dirty = true;
}

void drawBars(int x, int y, int w, int h) {
    uint64_t top = 1;
    for (uint64_t v : data.hours) top = max(top, v);
    int bw = w / 12;
    for (int i = 0; i < 12; i++) {
        int bh = data.hours[i] ? max<int>(1, data.hours[i] * h / top) : 0;
        canvas.fillRect(x + i * bw, y + h - bh, bw - 2, bh, i == 11 ? ACCENT : DIM);
        canvas.drawFastHLine(x + i * bw, y + h, bw - 2, DIM);
    }
}

void render() {
    static const char *titles[] = {"HOJE", "SEMANA", "SESSAO"};
    uint64_t total[] = {data.day, data.week, data.sess};
    uint64_t cache[] = {data.dayCache, data.weekCache, data.sessCache};
    bool stale = !received || millis() - lastRx > STALE_MS;

    canvas.fillSprite(BG);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ACCENT);
    canvas.drawString(titles[view], 4, 2);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(stale ? WARN : DIM);
    canvas.drawString(!received ? "aguardando PC" : stale ? "SEM DADOS " + data.clock : data.clock, 236, 2);

    canvas.setTextDatum(top_left);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(stale ? DIM : FG);
    canvas.drawString(human(total[view]), 4, 20);

    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(DIM);
    canvas.drawString("cache " + human(cache[view]), 236, 62);
    canvas.setTextDatum(top_left);
    canvas.drawString(data.model, 4, 62);

    drawBars(4, 80, 120, 34);
    canvas.setTextColor(stale ? DIM : FG);
    canvas.drawString(human(data.rate) + "/min", 132, 80);
    char left[16] = "janela: -";
    if (data.block) snprintf(left, sizeof left, "5h: %uh%02u", data.block / 60, data.block % 60);
    canvas.drawString(left, 132, 98);
    canvas.setTextColor(DIM);
    canvas.drawString("qualquer tecla: trocar", 4, 118);
    canvas.pushSprite(0, 0);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(70);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    Serial.begin(115200);
    input.reserve(512);
}

void loop() {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        view = (view + 1) % 3;
        dirty = true;
    }
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            applyLine(input);
            input = "";
        } else if (input.length() < 1024) {
            input += c;
        }
    }
    static uint32_t lastDraw = 0;
    if (dirty || millis() - lastDraw > 1000) {
        render();
        dirty = false;
        lastDraw = millis();
    }
    delay(5);
}
