#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Pinos de examples/factory/pin_config.h do repositório Xinyuan-LilyGO/T-Embed.
static const int PIN_POWER_ON = 46, PIN_LCD_BL = 15, PIN_LCD_DC = 13, PIN_LCD_CS = 10, PIN_LCD_CLK = 12,
                 PIN_LCD_MOSI = 11, PIN_LCD_RES = 9, PIN_ENC_A = 2, PIN_ENC_B = 1, PIN_ENC_BTN = 0,
                 PIN_LED_CLK = 45, PIN_LED_DATA = 42;
static const int LED_COUNT = 7;
static const uint32_t STALE_MS = 20000, LONG_PRESS_MS = 700;

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 panel;
    lgfx::Bus_SPI bus;
    lgfx::Light_PWM light;

public:
    LGFX() {
        auto b = bus.config();
        b.spi_host = SPI2_HOST;
        b.freq_write = 27000000;
        b.pin_sclk = PIN_LCD_CLK;
        b.pin_mosi = PIN_LCD_MOSI;
        b.pin_miso = -1;
        b.pin_dc = PIN_LCD_DC;
        bus.config(b);
        panel.setBus(&bus);
        auto p = panel.config();
        p.pin_cs = PIN_LCD_CS;
        p.pin_rst = PIN_LCD_RES;
        p.memory_width = 240;
        p.memory_height = 320;
        p.panel_width = 170;
        p.panel_height = 320;
        p.offset_x = 35;
        p.invert = true;
        p.readable = false;
        panel.config(p);
        auto l = light.config();
        l.pin_bl = PIN_LCD_BL;
        l.freq = 2000;
        l.pwm_channel = 7;
        light.config(l);
        panel.setLight(&light);
        setPanel(&panel);
    }
};

LGFX tft;
LGFX_Sprite canvas(&tft);
Preferences prefs;

enum Str { S_TODAY, S_WEEK, S_SESSION, S_WAITING_PC, S_NO_DATA, S_PROJECTS, S_SESSIONS, S_ACTIVE, S_WAITING, S_IDLE, S_NONE,
           S_RECORDS, S_STREAK, S_RECORD, S_MODELS, S_PET, S_LEVEL, S_FOOD, S_ENERGY, S_JOY, S_FEED, S_PLAY, S_CUDDLE, S_SLEEP,
           S_WAKE, S_BACK, S_SETTINGS, S_LANGUAGE, S_BRIGHTNESS, S_LEDS, S_SCREEN_OFF, S_NEVER, S_ON, S_OFF, S_REQUEST, S_TERMINAL,
           S_HINT, S_COUNT };
static const char *const STRINGS[S_COUNT][3] = {
    {"HOJE", "TODAY", "HOY"}, {"SEMANA", "WEEK", "SEMANA"}, {"SESSAO", "SESSION", "SESION"},
    {"aguardando PC", "waiting for PC", "esperando PC"}, {"SEM DADOS", "NO DATA", "SIN DATOS"},
    {"PROJETOS 7D", "PROJECTS 7D", "PROYECTOS 7D"}, {"SESSOES", "SESSIONS", "SESIONES"},
    {"ativo", "active", "activo"}, {"aguarda", "waiting", "espera"}, {"parado", "idle", "inactivo"},
    {"sem dados", "no data", "sin datos"}, {"RECORDES", "RECORDS", "RECORDS"},
    {"sequencia: %u dias", "streak: %u days", "racha: %u dias"}, {"recorde: ", "record: ", "record: "},
    {"MODELOS HOJE", "MODELS TODAY", "MODELOS HOY"}, {"MASCOTE", "MASCOT", "MASCOTA"}, {"nivel", "level", "nivel"},
    {"Fome", "Food", "Comida"}, {"Energia", "Energy", "Energia"}, {"Alegria", "Joy", "Alegria"},
    {"Comer", "Feed", "Comer"}, {"Brincar", "Play", "Jugar"}, {"Carinho", "Pet", "Caricia"}, {"Dormir", "Sleep", "Dormir"},
    {"Acordar", "Wake", "Despertar"}, {"Voltar", "Back", "Volver"}, {"CONFIGURACOES", "SETTINGS", "AJUSTES"},
    {"Idioma", "Language", "Idioma"}, {"Brilho", "Brightness", "Brillo"}, {"LEDs", "LEDs", "LEDs"},
    {"Apagar tela", "Screen off", "Apagar pantalla"}, {"nunca", "never", "nunca"}, {"ligado", "on", "activado"},
    {"desligado", "off", "desactivado"}, {"PEDIDO DE PERMISSAO", "PERMISSION REQUEST", "SOLICITUD DE PERMISO"},
    {"Responda no terminal", "Answer in the terminal", "Responde en la terminal"},
    {"gire: tela  toque longo: menu", "turn: screen  hold: menu", "gira: pantalla  manten: menu"},
};
static const char *const LANGUAGES[] = {"Portugues", "English", "Espanol"};

static const uint16_t BG = 0x0000, FG = 0xFFFF, DIM = 0x8410, ACCENT = 0xDBAB, WARN = 0xFE00;
static const uint8_t MASCOT[8][12] = {
    {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}, {0, 0, 1, 2, 1, 1, 1, 1, 2, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0}, {0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0},
};
static const uint16_t MASCOT_COLOR = 0xDBAB;

struct Row {
    String name, state, tool;
    uint64_t value = 0;
};
struct Data {
    uint64_t day = 0, dayCache = 0, week = 0, weekCache = 0, sess = 0, sessCache = 0, rate = 0, forecast = 0, record = 0;
    float costDay = 0, costWeek = 0, costSess = 0;
    uint32_t block = 0, streak = 0;
    uint64_t hours[12] = {};
    uint8_t heat[28] = {};
    Row sessions[4], projects[4], models[3];
    int sessionCount = 0, projectCount = 0, modelCount = 0;
    String clock = "--:--", model = "-", agent = "off", alert, pendingTool, pendingDetail, pendingDir;
    bool pending = false;
} data;

struct Pet {
    float food = 80, energy = 80, joy = 80;
    bool asleep = false;
    uint64_t xp = 0;
} pet;

enum Mode { VIEWS, PET_SCREEN, SETTINGS };
Mode mode = VIEWS;
int language = 0, view = 0, brightness = 2, screenOffIndex = 1, menuIndex = 0;
bool ledsOn = true, received = false, screenOff = false, petDirty = false;
uint32_t lastRx = 0, lastInput = 0, petTick = 0, petSaved = 0;
uint64_t petLastDay = 0, petEpoch = 0;
uint32_t evDone = 0, evError = 0, reactUntil = 0;
int reactType = 0;
String input;
static const int VIEW_COUNT = 6, PET_ITEMS = 5, SETTING_ITEMS = 5;
static const uint8_t BRIGHT[] = {30, 70, 120, 180, 255};
static const uint16_t SCREEN_MINUTES[] = {1, 5, 15, 0};

const char *tr(Str id) { return STRINGS[id][language]; }

// Encoder por interrupção: desenhar a tela leva dezenas de ms e perderia passos.
volatile int encoderSteps = 0;
void IRAM_ATTR onEncoder() {
    static const int8_t TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    static uint8_t last = 0;
    static int8_t partial = 0;
    last = ((last << 2) | (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B)) & 0x0F;
    partial += TABLE[last];
    if (partial >= 4 || partial <= -4) {
        encoderSteps += partial > 0 ? 1 : -1;
        partial = 0;
    }
}

void ledWrite(uint8_t byteValue) {
    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_LED_DATA, (byteValue >> i) & 1);
        digitalWrite(PIN_LED_CLK, HIGH);
        digitalWrite(PIN_LED_CLK, LOW);
    }
}

void ledFill(uint8_t r, uint8_t g, uint8_t b, int lit = LED_COUNT) {
    for (int i = 0; i < 4; i++) ledWrite(0);
    for (int i = 0; i < LED_COUNT; i++) {
        bool on = i < lit;
        ledWrite(0xE0 | 3);
        ledWrite(on ? b : 0);
        ledWrite(on ? g : 0);
        ledWrite(on ? r : 0);
    }
    for (int i = 0; i < 4; i++) ledWrite(0xFF);
}

void updateLeds(bool stale) {
    static uint32_t last = 0;
    if (millis() - last < 120) return;
    last = millis();
    if (!ledsOn || stale || data.agent == "off") return ledFill(0, 0, 0);
    uint32_t t = millis();
    if (millis() < reactUntil) {
        return reactType == 1 ? ledFill(0, 120, 30) : ledFill((t / 150) % 2 ? 150 : 0, 0, 0);
    }
    if (data.pending || data.agent == "waiting") return ledFill((t / 300) % 2 ? 180 : 20, (t / 300) % 2 ? 110 : 10, 0);
    if (data.alert.length()) return ledFill(120, 0, 0);
    if (data.agent == "working") return ledFill(120, 45, 25, 1 + (t / 150) % LED_COUNT);
    ledFill(12, 5, 3);
}

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

String money(float v) {
    char buf[16];
    snprintf(buf, sizeof buf, "$%.2f", v);
    return String(buf);
}

void wakeScreen() {
    lastInput = millis();
    if (screenOff) {
        screenOff = false;
        tft.setBrightness(BRIGHT[brightness]);
    }
}

int petLevel() {
    int level = 1;
    while (pet.xp >= 500000ULL * level * level && level < 99) level++;
    return level;
}

void petAdvance(float hours) {
    float rate = pet.food < 20 || pet.energy < 20 ? 8 : 4;
    pet.food = constrain(pet.food - 6 * hours * (pet.asleep ? 0.5f : 1), 0, 100);
    pet.joy = constrain(pet.joy - rate * hours * (pet.asleep ? 0.5f : 1), 0, 100);
    pet.energy = constrain(pet.energy + (pet.asleep ? 30 : -5) * hours, 0, 100);
    if (pet.asleep && pet.energy >= 100) pet.asleep = false;
}

void petSave() {
    prefs.putFloat("petFood", pet.food);
    prefs.putFloat("petEnergy", pet.energy);
    prefs.putFloat("petJoy", pet.joy);
    prefs.putBool("petAsleep", pet.asleep);
    prefs.putULong64("petXp", pet.xp);
    if (petEpoch) prefs.putULong64("petEpoch", petEpoch);
    petSaved = millis();
    petDirty = false;
}

void petSync(uint64_t now, uint64_t day) {
    if (now && !petEpoch) {
        uint64_t saved = prefs.getULong64("petEpoch", 0);
        if (saved && now > saved) petAdvance(min<uint64_t>(now - saved, 7 * 86400) / 3600.0f);
    }
    if (now) petEpoch = now;
    if (petLastDay && day > petLastDay) {
        pet.xp += day - petLastDay;
        petDirty = true;
    }
    petLastDay = day;
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
    data.costDay = doc["cd"];
    data.costWeek = doc["cw"];
    data.costSess = doc["cs"];
    data.rate = doc["r"];
    data.forecast = doc["f"];
    data.record = doc["rec"];
    data.streak = doc["st"];
    data.block = doc["b"];
    data.alert = doc["al"] | "";
    data.model = doc["m"] | "-";
    String agent = doc["a"] | "off";
    for (int i = 0; i < 12; i++) data.hours[i] = doc["h"][i];
    for (int i = 0; i < 28; i++) data.heat[i] = doc["hm"][i];
    data.sessionCount = data.projectCount = data.modelCount = 0;
    for (JsonObject s : doc["ss"].as<JsonArray>()) {
        if (data.sessionCount == 4) break;
        Row &r = data.sessions[data.sessionCount++];
        r.name = s["n"] | "?";
        r.state = s["st"] | "";
        r.tool = s["tool"] | "";
    }
    for (JsonObject p : doc["pr"].as<JsonArray>()) {
        if (data.projectCount == 4) break;
        Row &r = data.projects[data.projectCount++];
        r.name = p["n"] | "?";
        r.value = p["v"];
    }
    for (JsonObject m : doc["ml"].as<JsonArray>()) {
        if (data.modelCount == 3) break;
        Row &r = data.models[data.modelCount++];
        r.name = m["n"] | "?";
        r.value = m["v"];
    }
    bool pending = !doc["p"].isNull();
    data.pendingTool = doc["p"]["tool"] | "";
    data.pendingDetail = doc["p"]["d"] | "";
    data.pendingDir = doc["p"]["dir"] | "";
    if ((pending && !data.pending) || (agent == "waiting" && data.agent != "waiting")) wakeScreen();
    data.pending = pending;
    data.agent = agent;
    uint32_t done = doc["ed"], failed = doc["ee"];
    // O primeiro pacote só define a referência: reação é para o que acontece agora.
    if (evDone && done > evDone) {
        reactType = 1;
        reactUntil = millis() + 3000;
        pet.joy = min(100.0f, pet.joy + 1);
    }
    if (evError && failed > evError) {
        reactType = 2;
        reactUntil = millis() + 3000;
    }
    evDone = done;
    evError = failed;
    petSync(doc["ts"] | 0ULL, data.day);
    lastRx = millis();
    received = true;
}

void drawMascot(int x, int y, int px, bool stale, bool forceAwake = false, bool forceSleep = false) {
    uint32_t t = millis();
    int frame = (t / 300) % 2;
    bool sleeping = forceSleep || (!forceAwake && (stale || data.agent == "off"));
    bool working = !forceAwake && !sleeping && data.agent == "working";
    bool waiting = !forceAwake && !sleeping && data.agent == "waiting";
    bool reacting = !forceAwake && !sleeping && millis() < reactUntil;
    if (waiting || (reacting && reactType == 1)) y -= frame * px * 2;
    if ((!forceAwake && !sleeping && data.alert.length()) || (reacting && reactType == 2)) x += (t / 80) % 2 ? 2 : -2;
    bool blink = (t % 4000) < 180;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 12; c++) {
            uint8_t v = MASCOT[r][c];
            if (!v) continue;
            if (working && r == 7 && ((c < 6) == (frame == 0))) continue;
            int rx = x + c * px, ry = y + r * px;
            if (v == 2 && (sleeping || blink)) {
                canvas.fillRect(rx, ry, px, px, MASCOT_COLOR);
                canvas.fillRect(rx, ry + px / 2, px, max(1, px / 3), BG);
            } else {
                canvas.fillRect(rx, ry, px, px, v == 2 ? BG : MASCOT_COLOR);
            }
        }
    }
    int level = petLevel();
    if (level >= 5) {
        for (int i = 0; i < 3; i++) canvas.fillTriangle(x + (3 + i * 2) * px, y, x + (5 + i * 2) * px, y, x + (4 + i * 2) * px, y - 2 * px, 0xFEA0);
    } else if (level >= 2) {
        canvas.fillRect(x + 3 * px, y - 2 * px, 6 * px, 2 * px, 0x4208);
        canvas.fillRect(x + 2 * px, y - px / 2, 8 * px, max(1, px / 2), 0x4208);
    }
    if (level >= 3 && !sleeping) canvas.drawRect(x + 2 * px, y + px / 2, 8 * px, 2 * px, DIM);
}

void header(const char *title, bool stale) {
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(ACCENT);
    canvas.drawString(title, 6, 4);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(stale ? WARN : DIM);
    canvas.drawString(!received ? String(tr(S_WAITING_PC)) : stale ? String(tr(S_NO_DATA)) + " " + data.clock : data.clock, 314, 4);
    canvas.setTextDatum(top_left);
}

void renderTotals(int v, bool stale) {
    const char *titles[] = {tr(S_TODAY), tr(S_WEEK), tr(S_SESSION)};
    uint64_t total[] = {data.day, data.week, data.sess};
    uint64_t cache[] = {data.dayCache, data.weekCache, data.sessCache};
    float cost[] = {data.costDay, data.costWeek, data.costSess};
    header(titles[v], stale);
    canvas.setFont(&fonts::DejaVu40);
    canvas.setTextColor(stale ? DIM : FG);
    canvas.drawString(human(total[v]), 6, 28);
    drawMascot(242, 34, 6, stale);
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(FG);
    canvas.drawString(money(cost[v]), 6, 78);
    canvas.setTextColor(DIM);
    canvas.drawString("cache " + human(cache[v]), 96, 78);
    uint64_t top = 1;
    for (uint64_t h : data.hours) top = max(top, h);
    for (int i = 0; i < 12; i++) {
        int bh = data.hours[i] ? max<int>(1, data.hours[i] * 40 / top) : 0;
        canvas.fillRect(6 + i * 14, 142 - bh, 12, bh, i == 11 ? ACCENT : DIM);
    }
    canvas.setTextColor(stale ? DIM : FG);
    canvas.drawString(human(data.rate) + "/min", 180, 102);
    char left[16] = "5h: -";
    if (data.block) snprintf(left, sizeof left, "5h: %uh%02u", data.block / 60, data.block % 60);
    canvas.setTextColor(data.alert == "janela" ? WARN : stale ? DIM : FG);
    canvas.drawString(left, 180, 124);
    canvas.setTextColor(DIM);
    canvas.drawString(data.model, 6, 148);
    canvas.setTextDatum(top_right);
    canvas.drawString(human(data.forecast), 314, 148);
    canvas.setTextDatum(top_left);
}

void renderRanked(const char *title, Row *rows, int count, bool stale) {
    header(title, stale);
    canvas.setFont(&fonts::DejaVu18);
    uint64_t top = 1;
    for (int i = 0; i < count; i++) top = max(top, rows[i].value);
    if (!count) {
        canvas.setTextColor(DIM);
        canvas.drawString(tr(S_NONE), 6, 40);
    }
    for (int i = 0; i < count; i++) {
        int y = 30 + i * 34;
        canvas.setTextColor(FG);
        canvas.drawString(rows[i].name, 6, y);
        canvas.setTextDatum(top_right);
        canvas.drawString(human(rows[i].value), 314, y);
        canvas.setTextDatum(top_left);
        canvas.fillRect(6, y + 22, rows[i].value * 308 / top, 5, ACCENT);
    }
}

void renderSessions(bool stale) {
    header(tr(S_SESSIONS), stale);
    canvas.setFont(&fonts::DejaVu18);
    if (!data.sessionCount) {
        canvas.setTextColor(DIM);
        canvas.drawString(tr(S_NONE), 6, 40);
    }
    for (int i = 0; i < data.sessionCount; i++) {
        int y = 34 + i * 32;
        const String &st = data.sessions[i].state;
        uint16_t color = st == "waiting" ? WARN : st == "working" ? ACCENT : DIM;
        canvas.fillCircle(12, y + 9, 5, color);
        canvas.setTextColor(FG);
        canvas.drawString(data.sessions[i].name, 26, y);
        canvas.setTextDatum(top_right);
        canvas.setTextColor(color);
        canvas.drawString(st == "waiting" ? tr(S_WAITING) : st == "working" ? tr(S_ACTIVE) : tr(S_IDLE), 314, y);
        canvas.setTextDatum(top_left);
    }
}

void renderRecords(bool stale) {
    header(tr(S_RECORDS), stale);
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(FG);
    char streak[32];
    snprintf(streak, sizeof streak, tr(S_STREAK), data.streak);
    canvas.drawString(streak, 6, 30);
    canvas.drawString(String(tr(S_RECORD)) + human(data.record), 6, 54);
    static const uint16_t LEVELS[] = {0x2104, 0x5180, 0x8243, 0xAB08, 0xDBAB};
    for (int i = 0; i < 28; i++) canvas.fillRect(6 + (i % 14) * 22, 90 + (i / 14) * 36, 20, 34, LEVELS[min<int>(data.heat[i], 4)]);
}

void drawStat(const char *label, float value, int y) {
    canvas.setTextColor(FG);
    canvas.drawString(label, 120, y);
    canvas.drawRect(210, y + 3, 104, 14, DIM);
    canvas.fillRect(212, y + 5, value, 10, value < 25 ? WARN : ACCENT);
}

void renderPet() {
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ACCENT);
    canvas.drawString(String(tr(S_PET)) + " - " + tr(S_LEVEL) + " " + petLevel(), 6, 4);
    drawMascot(12, 54, 8, false, !pet.asleep, pet.asleep);
    drawStat(tr(S_FOOD), pet.food, 32);
    drawStat(tr(S_ENERGY), pet.energy, 56);
    drawStat(tr(S_JOY), pet.joy, 80);
    const char *items[] = {tr(S_FEED), tr(S_PLAY), tr(S_CUDDLE), pet.asleep ? tr(S_WAKE) : tr(S_SLEEP), tr(S_BACK)};
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(ACCENT);
    canvas.drawString(String("< ") + items[menuIndex] + " >", 215, 128);
    canvas.setTextDatum(top_left);
}

void petAction(int item) {
    if (item == 4) {
        mode = VIEWS;
        return;
    }
    if (item == 3) {
        pet.asleep = !pet.asleep;
    } else if (!pet.asleep) {
        if (item == 0) pet.food = min(100.0f, pet.food + 25);
        if (item == 2) pet.joy = min(100.0f, pet.joy + 10);
        if (item == 1 && pet.energy >= 15 && pet.food >= 10) {
            pet.joy = min(100.0f, pet.joy + 20);
            pet.energy = max(0.0f, pet.energy - 10);
            pet.food = max(0.0f, pet.food - 5);
        }
    }
    petDirty = true;
}

void renderSettings() {
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ACCENT);
    canvas.drawString(tr(S_SETTINGS), 6, 4);
    String off = SCREEN_MINUTES[screenOffIndex] ? String(SCREEN_MINUTES[screenOffIndex]) + " min" : String(tr(S_NEVER));
    const char *labels[] = {tr(S_LANGUAGE), tr(S_BRIGHTNESS), tr(S_LEDS), tr(S_SCREEN_OFF), tr(S_BACK)};
    String values[] = {LANGUAGES[language], String(brightness + 1) + "/5", ledsOn ? tr(S_ON) : tr(S_OFF), off, ""};
    for (int i = 0; i < SETTING_ITEMS; i++) {
        int y = 32 + i * 26;
        canvas.setTextColor(i == menuIndex ? ACCENT : FG);
        canvas.drawString(String(i == menuIndex ? "> " : "  ") + labels[i], 6, y);
        canvas.setTextDatum(top_right);
        canvas.drawString(values[i], 314, y);
        canvas.setTextDatum(top_left);
    }
}

void settingAction(int item) {
    switch (item) {
        case 0:
            language = (language + 1) % 3;
            prefs.putInt("lang", language);
            break;
        case 1:
            brightness = (brightness + 1) % 5;
            prefs.putInt("bright", brightness);
            tft.setBrightness(BRIGHT[brightness]);
            break;
        case 2:
            ledsOn = !ledsOn;
            prefs.putBool("leds", ledsOn);
            break;
        case 3:
            screenOffIndex = (screenOffIndex + 1) % 4;
            prefs.putInt("screenOff", screenOffIndex);
            break;
        default:
            mode = VIEWS;
    }
}

void renderPending() {
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(WARN);
    canvas.drawString(tr(S_REQUEST), 6, 4);
    canvas.setFont(&fonts::DejaVu24);
    canvas.setTextColor(FG);
    canvas.drawString(data.pendingTool, 6, 34);
    canvas.setFont(&fonts::DejaVu18);
    canvas.setTextColor(ACCENT);
    canvas.drawString(data.pendingDir, 6, 66);
    canvas.setTextColor(DIM);
    canvas.setCursor(6, 90);
    canvas.setTextWrap(true);
    canvas.print(data.pendingDetail);
    canvas.setTextWrap(false);
    canvas.setTextColor(FG);
    canvas.drawString(tr(S_TERMINAL), 6, 148);
}

void render(bool stale) {
    canvas.fillSprite(BG);
    if (data.pending && !stale) renderPending();
    else if (mode == SETTINGS) renderSettings();
    else if (mode == PET_SCREEN) renderPet();
    else if (view < 3) renderTotals(view, stale);
    else if (view == 3) renderRanked(tr(S_PROJECTS), data.projects, data.projectCount, stale);
    else if (view == 4) renderSessions(stale);
    else renderRecords(stale);
    canvas.pushSprite(0, 0);
}

void onTurn(int steps) {
    int dir = steps > 0 ? 1 : -1;
    if (mode == VIEWS) view = (view + VIEW_COUNT + dir) % VIEW_COUNT;
    else if (mode == PET_SCREEN) menuIndex = (menuIndex + PET_ITEMS + dir) % PET_ITEMS;
    else menuIndex = (menuIndex + SETTING_ITEMS + dir) % SETTING_ITEMS;
}

void onClick() {
    if (mode == VIEWS) {
        mode = PET_SCREEN;
        menuIndex = 0;
    } else if (mode == PET_SCREEN) {
        petAction(menuIndex);
    } else {
        settingAction(menuIndex);
    }
}

void onLongPress() {
    mode = mode == SETTINGS ? VIEWS : SETTINGS;
    menuIndex = 0;
}

void readInput() {
    noInterrupts();
    int steps = encoderSteps;
    encoderSteps = 0;
    interrupts();
    static bool down = false, longFired = false;
    static uint32_t downAt = 0;
    bool pressed = digitalRead(PIN_ENC_BTN) == LOW;
    bool activity = steps || (pressed && !down);
    // Com a tela apagada, o gesto só acorda: nada é acionado às cegas.
    if (activity && screenOff) {
        wakeScreen();
        down = pressed;
        longFired = true;
        return;
    }
    if (activity) wakeScreen();
    for (int i = 0; i < abs(steps); i++) onTurn(steps);
    if (pressed && !down) {
        down = true;
        longFired = false;
        downAt = millis();
    } else if (pressed && !longFired && millis() - downAt > LONG_PRESS_MS) {
        longFired = true;
        onLongPress();
    } else if (!pressed && down) {
        down = false;
        if (!longFired && millis() - downAt > 30) onClick();
    }
}

void setup() {
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    pinMode(PIN_LED_CLK, OUTPUT);
    pinMode(PIN_LED_DATA, OUTPUT);
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_ENC_BTN, INPUT_PULLUP);
    attachInterrupt(PIN_ENC_A, onEncoder, CHANGE);
    attachInterrupt(PIN_ENC_B, onEncoder, CHANGE);
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    input.reserve(2048);
    prefs.begin("tokenmon");
    language = constrain(prefs.getInt("lang", 0), 0, 2);
    brightness = constrain(prefs.getInt("bright", 2), 0, 4);
    screenOffIndex = constrain(prefs.getInt("screenOff", 1), 0, 3);
    ledsOn = prefs.getBool("leds", true);
    pet.food = prefs.getFloat("petFood", 80);
    pet.energy = prefs.getFloat("petEnergy", 80);
    pet.joy = prefs.getFloat("petJoy", 80);
    pet.asleep = prefs.getBool("petAsleep", false);
    pet.xp = prefs.getULong64("petXp", 0);
    tft.init();
    tft.setRotation(3);
    tft.setBrightness(BRIGHT[brightness]);
    canvas.setColorDepth(16);
    canvas.createSprite(320, 170);
}

void loop() {
    readInput();
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            applyLine(input);
            input = "";
        } else if (input.length() < 4096) {
            input += c;
        }
    }
    if (millis() - petTick >= 60000) {
        petAdvance((millis() - petTick) / 3600000.0f);
        petTick = millis();
        petDirty = true;
    }
    // Grava com folga: a flash se desgasta a cada escrita.
    if (petDirty && millis() - petSaved > 600000) petSave();
    bool stale = !received || millis() - lastRx > STALE_MS;
    updateLeds(stale);
    uint16_t offAfter = SCREEN_MINUTES[screenOffIndex];
    if (!screenOff && offAfter && millis() - lastInput > offAfter * 60000UL) {
        screenOff = true;
        tft.setBrightness(0);
    }
    static uint32_t lastDraw = 0;
    if (!screenOff && millis() - lastDraw > 80) {
        render(stale);
        lastDraw = millis();
    }
    delay(2);
}
