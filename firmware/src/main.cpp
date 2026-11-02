#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// Sem novidade do PC por esse tempo, a tela avisa em vez de exibir dado velho como atual.
static const uint32_t STALE_MS = 20000;
static const uint32_t CONFIRM_MS = 3000;
static const int UDP_PORT = 47474;
static const int SD_SCK = 40, SD_MISO = 39, SD_MOSI = 14, SD_CS = 12;

struct Theme {
    const char *name;
    uint16_t bg, fg, dim, accent, warn;
};
static const Theme THEMES[] = {
    {"Claude", 0x0000, 0xFFFF, 0x8410, 0xDBAB, 0xFBE0},
    {"Retro", 0x0000, 0xFE60, 0x9CC0, 0xFD20, 0xF800},
    {"Matrix", 0x0000, 0x07E0, 0x0400, 0x47E8, 0xFFE0},
    {"Oceano", 0x08C6, 0xE79F, 0x6C76, 0x4D1F, 0xFE4A},
    {"Ciano", 0x00E5, 0xDFDF, 0x5CD5, 0x269D, 0xFCED},
    {"Meia-noite", 0x0886, 0xDF1F, 0x7417, 0x7C7F, 0xFDCD},
    {"Rosa", 0x2843, 0xFF3E, 0xBC54, 0xFB76, 0xFE8C},
    {"Lavanda", 0x18A6, 0xF75F, 0x9C77, 0xC51F, 0xFCF8},
    {"Algodao", 0xFF9E, 0x4907, 0xABF2, 0xEA52, 0xC0CB},
    {"Pessego", 0x28A3, 0xFF7C, 0xBCB1, 0xFCF1, 0xFE8C},
};
static const int THEME_COUNT = 10;

struct Row {
    String name, state;
    uint64_t value = 0;
};

struct Data {
    uint64_t day = 0, dayCache = 0, week = 0, weekCache = 0, sess = 0, sessCache = 0, rate = 0;
    uint64_t forecast = 0, goal = 0, record = 0;
    float costDay = 0, costWeek = 0, costSess = 0;
    uint32_t block = 0, streak = 0;
    uint64_t hours[12] = {};
    uint8_t heat[28] = {};
    Row sessions[4], projects[4];
    int sessionCount = 0, projectCount = 0;
    String clock = "--:--", date, model = "-", agent = "off", alert, pendingId, pendingTool, pendingDetail, pendingDir;
} data;

// Mascote: 12x8, como em docs/mascot.png. 1 = corpo, 2 = olho.
static const uint8_t MASCOT[8][12] = {
    {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}, {0, 0, 1, 2, 1, 1, 1, 1, 2, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
    {0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0}, {0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0},
};
static const struct { const char *name; uint16_t color; } MASCOT_COLORS[] = {
    {"Coral", 0xDB8A},
    {"Azul", 0x4D1F},
    {"Ciano", 0x269D},
    {"Rosa", 0xFB76},
    {"Lilas", 0xC51F},
    {"Verde", 0x4EF0},
    {"Amarelo", 0xFE62},
    {"Branco", 0xF7BE},
};
static const int MASCOT_COLOR_COUNT = 8;

M5Canvas canvas(&M5Cardputer.Display);
Preferences prefs;
WiFiUDP udp;
String input, sdDate;
uint64_t usbDay = 0;
float usbCostDay = 0;
uint32_t lastRx = 0, lastUsb = 0, confirmUntil = 0, pendingSince = 0;
String armedId;
bool pendingAllowed = false;
bool received = false, muted = false, sdReady = false, udpReady = false, paused = false;
int view = 0, themeIndex = 0, mascotIndex = 0;
String toast;
uint32_t toastUntil = 0;
String prevAgent = "off", prevAlert, prevPending;
const int VIEW_COUNT = 6;

// Bichinho: valores de 0 a 100. Ele nunca morre; só fica triste até receber atenção.
struct Pet {
    float food = 80, energy = 80, joy = 80;
    bool asleep = false;
    uint64_t xp = 0;
} pet;
bool petOpen = false, petDirty = false;
uint32_t petTick = 0, petSaved = 0, petAnimUntil = 0, petMsgUntil = 0;
uint64_t petLastDay = 0, petEpoch = 0;
int petAnim = 0;  // 1 comida, 2 coração, 3 bola
int mascotForce = 0;  // 1 dormindo, 2 andando, 3 pulando
String petMsg;

void petSync(uint64_t now, uint64_t day);
void renderPet();

const Theme &theme() { return THEMES[themeIndex]; }

void showToast(const String &text) {
    toast = text;
    toastUntil = millis() + 1500;
}

// Escurece uma cor RGB565 por componente, para a escala do mapa de atividade.
uint16_t shade(uint16_t color, int percent) {
    int r = ((color >> 11) & 31) * percent / 100;
    int g = ((color >> 5) & 63) * percent / 100;
    int b = (color & 31) * percent / 100;
    return (r << 11) | (g << 5) | b;
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

void beep(int freq, int ms) {
    if (!muted) M5Cardputer.Speaker.tone(freq, ms);
}

void appendHistory(const String &newDate) {
    // O total guardado é o último recebido do dia anterior, antes de os novos dados entrarem.
    if (sdReady && sdDate.length() && sdDate != newDate) {
        File f = SD.open("/token-history.csv", FILE_APPEND);
        if (f) {
            f.printf("%s,%llu,%.2f\n", sdDate.c_str(), usbDay, usbCostDay);
            f.close();
        }
    }
    sdDate = newDate;
}

void applyLine(const String &line, bool fromUsb) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;
    // A rede não é autenticada: só a USB escreve no cartão.
    if (fromUsb) {
        appendHistory(doc["dt"] | "");
        usbDay = doc["d"];
        usbCostDay = doc["cd"];
        petSync(doc["ts"] | 0ULL, usbDay);
    }
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
    data.goal = doc["goal"];
    data.record = doc["rec"];
    data.streak = doc["st"];
    data.block = doc["b"];
    data.alert = doc["al"] | "";
    data.agent = doc["a"] | "off";
    if (fromUsb) data.model = doc["m"] | "-";
    for (int i = 0; i < 12; i++) data.hours[i] = doc["h"][i];
    for (int i = 0; i < 28; i++) data.heat[i] = doc["hm"][i];
    if (!fromUsb) {
        lastRx = millis();
        received = true;
        return;  // Wi-Fi só atualiza totais; alertas e listas dependem da USB.
    }
    data.sessionCount = 0;
    for (JsonObject s : doc["ss"].as<JsonArray>()) {
        if (data.sessionCount == 4) break;
        Row &r = data.sessions[data.sessionCount++];
        r.name = s["n"] | "?";
        r.state = s["st"] | "";
    }
    data.projectCount = 0;
    for (JsonObject p : doc["pr"].as<JsonArray>()) {
        if (data.projectCount == 4) break;
        Row &r = data.projects[data.projectCount++];
        r.name = p["n"] | "?";
        r.value = p["v"];
    }
    // Aprovação só existe pela USB: o Wi-Fi é transmissão aberta e não traz esse campo.
    data.pendingDir = doc["p"]["dir"] | "";
    String newId = doc["p"]["id"] | "";
    if (newId != data.pendingId) {
        // Pedido novo cancela qualquer confirmação em curso e ignora teclas por um instante.
        armedId = "";
        confirmUntil = 0;
        pendingSince = millis();
    }
    data.pendingId = newId;
    data.pendingTool = doc["p"]["tool"] | "";
    data.pendingDetail = doc["p"]["d"] | "";
    pendingAllowed = doc["p"]["ok"] | false;
    lastUsb = millis();
    lastRx = millis();
    received = true;
    if (data.agent == "waiting" && prevAgent != "waiting") beep(1800, 200);
    if (data.alert.length() && data.alert != prevAlert) beep(600, 400);
    if (data.pendingId.length() && data.pendingId != prevPending) beep(2200, 300);
    prevAgent = data.agent;
    prevAlert = data.alert;
    prevPending = data.pendingId;
}

void drawMascot(int x, int y, int px, bool stale) {
    uint32_t t = millis();
    int frame = (t / 300) % 2;
    bool sleeping = mascotForce ? mascotForce == 1 : stale || data.agent == "off";
    bool working = mascotForce ? mascotForce == 2 : !sleeping && data.agent == "working";
    bool waiting = mascotForce ? mascotForce == 3 : !sleeping && data.agent == "waiting";
    bool panic = !mascotForce && !sleeping && data.alert.length();
    if (waiting) y -= frame * px * 2;
    if (panic) x += (t / 80) % 2 ? 2 : -2;
    bool blink = (t % 4000) < 180;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 12; c++) {
            uint8_t v = MASCOT[r][c];
            if (!v) continue;
            int rx = x + c * px, ry = y + r * px;
            // Andar: as pernas de cada lado encolhem em turnos.
            if (working && r == 7 && ((c < 6) == (frame == 0))) continue;
            uint16_t body = MASCOT_COLORS[mascotIndex].color;
            uint16_t color = body;
            if (v == 2) {
                color = 0x0000;
                if (sleeping || blink) {
                    canvas.fillRect(rx, ry, px, px, body);
                    canvas.fillRect(rx, ry + px / 2, px, max(1, px / 3), 0x0000);
                    continue;
                }
            }
            canvas.fillRect(rx, ry, px, px, color);
        }
    }
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_left);
    if (sleeping) {
        canvas.setTextColor(theme().dim);
        canvas.drawString(frame ? "z" : "zZ", x + 12 * px - 4, y - 8);
    } else if (waiting && !mascotForce) {
        canvas.setTextColor(theme().warn);
        canvas.drawString("!", x + 12 * px, y);
    }
}

void drawBars(int x, int y, int w, int h) {
    uint64_t top = 1;
    for (uint64_t v : data.hours) top = max(top, v);
    int bw = w / 12;
    for (int i = 0; i < 12; i++) {
        int bh = data.hours[i] ? max<int>(1, data.hours[i] * h / top) : 0;
        canvas.fillRect(x + i * bw, y + h - bh, bw - 2, bh, i == 11 ? theme().accent : theme().dim);
        canvas.drawFastHLine(x + i * bw, y + h, bw - 2, theme().dim);
    }
}

void header(const char *title, bool stale) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(theme().accent);
    canvas.drawString(title, 4, 2);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(stale ? theme().warn : theme().dim);
    String right = !received ? "aguardando PC" : stale ? "SEM DADOS " + data.clock : data.clock;
    if (paused) right = "PAUSADO";
    if (muted) right = "mudo " + right;
    if (millis() < toastUntil) right = toast;
    canvas.drawString(right, 236, 2);
    canvas.setTextDatum(top_left);
}

void renderTotals(int v, bool stale) {
    static const char *titles[] = {"HOJE", "SEMANA", "SESSAO"};
    uint64_t total[] = {data.day, data.week, data.sess};
    uint64_t cache[] = {data.dayCache, data.weekCache, data.sessCache};
    float cost[] = {data.costDay, data.costWeek, data.costSess};
    header(titles[v], stale);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(stale ? theme().dim : data.alert == "meta" ? theme().warn : theme().fg);
    canvas.drawString(human(total[v]), 4, 20);
    drawMascot(184, 24, 4, stale);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(theme().fg);
    canvas.drawString(money(cost[v]), 4, 60);
    canvas.setTextColor(theme().dim);
    canvas.drawString("cache " + human(cache[v]), 64, 60);
    drawBars(4, 80, 120, 34);
    canvas.setTextColor(stale ? theme().dim : theme().fg);
    canvas.drawString(human(data.rate) + "/min", 132, 78);
    char left[16] = "5h: -";
    if (data.block) snprintf(left, sizeof left, "5h: %uh%02u", data.block / 60, data.block % 60);
    canvas.setTextColor(data.alert == "janela" ? theme().warn : stale ? theme().dim : theme().fg);
    canvas.drawString(left, 132, 94);
    canvas.setTextColor(theme().dim);
    canvas.drawString("prev " + human(data.forecast), 4, 118);
    canvas.setTextDatum(top_right);
    canvas.drawString(data.model, 236, 118);
    canvas.setTextDatum(top_left);
}

void renderProjects(bool stale) {
    header("PROJETOS 7D", stale);
    canvas.setFont(&fonts::Font2);
    uint64_t top = 1;
    for (int i = 0; i < data.projectCount; i++) top = max(top, data.projects[i].value);
    if (!data.projectCount) {
        canvas.setTextColor(theme().dim);
        canvas.drawString("sem dados (nao vem pelo Wi-Fi)", 4, 30);
    }
    for (int i = 0; i < data.projectCount; i++) {
        int y = 24 + i * 27;
        canvas.setTextColor(theme().fg);
        canvas.drawString(data.projects[i].name, 4, y);
        canvas.setTextDatum(top_right);
        canvas.drawString(human(data.projects[i].value), 236, y);
        canvas.setTextDatum(top_left);
        canvas.fillRect(4, y + 17, data.projects[i].value * 232 / top, 4, theme().accent);
    }
}

void renderSessions(bool stale) {
    header("SESSOES", stale);
    canvas.setFont(&fonts::Font2);
    if (!data.sessionCount) {
        canvas.setTextColor(theme().dim);
        canvas.drawString("nenhuma (instale os hooks)", 4, 30);
    }
    for (int i = 0; i < data.sessionCount; i++) {
        int y = 26 + i * 26;
        const String &st = data.sessions[i].state;
        uint16_t color = st == "waiting" ? theme().warn : st == "working" ? theme().accent : theme().dim;
        canvas.fillCircle(10, y + 8, 4, color);
        canvas.setTextColor(theme().fg);
        canvas.drawString(data.sessions[i].name, 22, y);
        canvas.setTextDatum(top_right);
        canvas.setTextColor(color);
        canvas.drawString(st == "waiting" ? "aguarda" : st == "working" ? "ativo" : "parado", 236, y);
        canvas.setTextDatum(top_left);
    }
}

void renderStats(bool stale) {
    header("RECORDES", stale);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(theme().fg);
    canvas.drawString("sequencia: " + String(data.streak) + " dias", 4, 22);
    canvas.drawString("recorde: " + human(data.record), 4, 40);
    if (data.goal) canvas.drawString("meta: " + human(data.day) + " / " + human(data.goal), 4, 58);
    const uint16_t LEVELS[] = {shade(theme().dim, 35), shade(theme().accent, 40), shade(theme().accent, 60),
                               shade(theme().accent, 80), theme().accent};
    for (int i = 0; i < 28; i++) {
        int x = 4 + (i % 14) * 16, y = 86 + (i / 14) * 22;
        canvas.fillRect(x, y, 14, 20, LEVELS[min<int>(data.heat[i], 4)]);
    }
}

void renderPending() {
    canvas.fillSprite(theme().bg);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(theme().warn);
    canvas.drawString("PEDIDO DE PERMISSAO", 4, 2);
    drawMascot(190, 2, 3, false);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(theme().fg);
    canvas.drawString(data.pendingTool, 4, 24);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(theme().accent);
    canvas.drawString(data.pendingDir, 4, 42);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(theme().dim);
    canvas.setCursor(4, 60);
    canvas.setTextWrap(true);
    canvas.print(data.pendingDetail);
    canvas.setTextWrap(false);
    bool arming = armedId == data.pendingId && millis() < confirmUntil;
    canvas.setTextColor(arming ? theme().warn : theme().fg);
    canvas.drawString(!pendingAllowed ? "veja no terminal   n negar"
                      : arming ? "y de novo para APROVAR" : "y aprovar (2x)    n negar", 4, 116);
}

void render() {
    bool stale = !received || millis() - lastRx > STALE_MS;
    canvas.fillSprite(theme().bg);
    if (data.pendingId.length() && millis() - lastUsb < STALE_MS) {
        renderPending();
    } else if (petOpen) {
        renderPet();
    } else if (view < 3) {
        renderTotals(view, stale);
    } else if (view == 3) {
        renderProjects(stale);
    } else if (view == 4) {
        renderSessions(stale);
    } else {
        renderStats(stale);
    }
    canvas.pushSprite(0, 0);
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

void petLoad() {
    pet.food = prefs.getFloat("petFood", 80);
    pet.energy = prefs.getFloat("petEnergy", 80);
    pet.joy = prefs.getFloat("petJoy", 80);
    pet.asleep = prefs.getBool("petAsleep", false);
    pet.xp = prefs.getULong64("petXp", 0);
}

// O relógio vem do PC; só ele permite contar o tempo com o aparelho desligado.
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

int petLevel() {
    int level = 1;
    while (pet.xp >= 500000ULL * level * level && level < 99) level++;
    return level;
}

const char *petMood() {
    if (pet.asleep) return "dormindo...";
    if (pet.food < 25) return "com fome!";
    if (pet.energy < 25) return "com sono...";
    if (pet.joy < 25) return "triste, quer atencao";
    if (pet.joy > 75 && pet.food > 50) return "muito feliz!";
    return "tranquilo";
}

void petSay(const String &text, int anim, int freq) {
    petMsg = text;
    petMsgUntil = millis() + 2500;
    petAnim = anim;
    petAnimUntil = anim ? millis() + 1500 : 0;
    if (freq) beep(freq, 80);
    petDirty = true;
}

void petKey(char c) {
    if (c == 's') {
        pet.asleep = !pet.asleep;
        petSay(pet.asleep ? "boa noite" : "bom dia!", 0, 500);
    } else if (pet.asleep) {
        petSay("zzz... aperte s para acordar", 0, 0);
    } else if (c == 'f') {
        if (pet.food >= 95) return petSay("estou cheio!", 0, 0);
        pet.food = min(100.0f, pet.food + 25);
        petSay("nham nham!", 1, 900);
    } else if (c == 'h') {
        pet.joy = min(100.0f, pet.joy + 10);
        petSay("que carinho bom", 2, 1400);
    } else if (c == 'b') {
        if (pet.energy < 15) return petSay("cansado demais...", 0, 0);
        if (pet.food < 10) return petSay("fome demais...", 0, 0);
        pet.joy = min(100.0f, pet.joy + 20);
        pet.energy = max(0.0f, pet.energy - 10);
        pet.food = max(0.0f, pet.food - 5);
        petSay("oba, bola!", 3, 1200);
    }
}

void drawStat(const char *label, float value, int y) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(theme().fg);
    canvas.drawString(label, 104, y);
    canvas.drawRect(160, y + 3, 74, 10, theme().dim);
    canvas.fillRect(162, y + 5, value * 70 / 100, 6, value < 25 ? theme().warn : theme().accent);
}

void renderPet() {
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(theme().accent);
    canvas.drawString("MASCOTE  nivel " + String(petLevel()), 4, 2);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(theme().dim);
    if (millis() < toastUntil) canvas.drawString(toast, 236, 2);
    canvas.setTextDatum(top_left);
    bool animating = millis() < petAnimUntil;
    mascotForce = pet.asleep ? 1 : animating && petAnim == 3 ? 3 : animating && petAnim == 1 ? 2 : 0;
    // 4 = parado e acordado: sem isso ele imitaria o estado do agente.
    if (!mascotForce) mascotForce = 4;
    drawMascot(14, 44, 6, false);
    mascotForce = 0;
    if (animating) {
        int frame = (millis() / 200) % 4;
        if (petAnim == 1) {
            canvas.fillCircle(44, 32 - frame, 4, 0xFD20);
        } else if (petAnim == 2) {
            canvas.setTextColor(0xF8B2);
            canvas.drawString("<3", 12 + frame * 18, 26 - frame * 2);
        } else if (petAnim == 3) {
            canvas.fillCircle(8 + frame * 26, 30 + (frame % 2) * 8, 5, theme().accent);
        }
    }
    drawStat("Fome", pet.food, 24);
    drawStat("Energia", pet.energy, 42);
    drawStat("Alegria", pet.joy, 60);
    canvas.setTextColor(theme().fg);
    canvas.drawString(millis() < petMsgUntil ? petMsg : String(petMood()), 104, 84);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(theme().dim);
    canvas.drawString("f comer  b brincar  h carinho", 4, 110);
    canvas.drawString("s dormir  g sair", 4, 122);
}

void answerPending(const char *value) {
    Serial.printf("A %s %s\n", data.pendingId.c_str(), value);
    data.pendingId = "";
    armedId = "";
    confirmUntil = 0;
}

void handleKey(char c) {
    if (data.pendingId.length() && millis() - lastUsb < STALE_MS) {
        // Outras teclas não fazem nada aqui: uma batida acidental não pode decidir um pedido.
        if (millis() - pendingSince < 700) return;
        if (c == 'n') {
            answerPending("n");
        } else if (c == 'y' && pendingAllowed) {
            if (armedId == data.pendingId && millis() < confirmUntil) {
                answerPending("y");
            } else {
                armedId = data.pendingId;
                confirmUntil = millis() + CONFIRM_MS;
            }
        }
        return;
    }
    if (c == 'g') {
        petOpen = !petOpen;
        return;
    }
    if (petOpen && c != 't' && c != 'c' && c != 'm') {
        petKey(c);
        return;
    }
    if (c == 't') {
        themeIndex = (themeIndex + 1) % THEME_COUNT;
        prefs.putInt("theme", themeIndex);
        showToast(theme().name);
    } else if (c == 'c') {
        mascotIndex = (mascotIndex + 1) % MASCOT_COLOR_COUNT;
        prefs.putInt("mascot", mascotIndex);
        showToast(MASCOT_COLORS[mascotIndex].name);
    } else if (c == 'm') {
        muted = !muted;
        prefs.putBool("muted", muted);
    } else if (c == 'p') {
        paused = !paused;
        Serial.println("P");
    } else if (c >= '1' && c <= '9') {
        Serial.printf("C %c\n", c);
    } else if (c == ',') {
        view = (view + VIEW_COUNT - 1) % VIEW_COUNT;
    } else {
        view = (view + 1) % VIEW_COUNT;
    }
}

void startWifi() {
    String ssid = prefs.getString("ssid", "");
    if (!ssid.length()) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), prefs.getString("pass", "").c_str());
}

void handleSerialLine(const String &line) {
    if (line.startsWith("W ")) {
        int tab = line.indexOf('\t');
        if (tab < 0) return;
        prefs.putString("ssid", line.substring(2, tab));
        prefs.putString("pass", line.substring(tab + 1));
        startWifi();
        return;
    }
    applyLine(line, true);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(70);
    M5Cardputer.Speaker.setVolume(90);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    input.reserve(2048);
    prefs.begin("tokenmon");
    themeIndex = constrain(prefs.getInt("theme", 0), 0, THEME_COUNT - 1);
    mascotIndex = constrain(prefs.getInt("mascot", 0), 0, MASCOT_COLOR_COUNT - 1);
    muted = prefs.getBool("muted", false);
    petLoad();
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    sdReady = SD.begin(SD_CS, SPI, 20000000);
    startWifi();
}

void loop() {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto state = M5Cardputer.Keyboard.keysState();
        for (char c : state.word) handleKey(c);
        if (state.enter) handleKey('\n');
    }
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            handleSerialLine(input);
            input = "";
        } else if (input.length() < 4096) {
            input += c;
        }
    }
    if (WiFi.status() == WL_CONNECTED) {
        if (!udpReady) udpReady = udp.begin(UDP_PORT);
        int size = udp.parsePacket();
        if (size > 0 && size < 2048) {
            static char packet[2048];
            int n = udp.read(packet, sizeof packet - 1);
            packet[max(n, 0)] = 0;
            // Com a USB ativa, ela manda: traz mais dados e é a única fonte confiável.
            if (millis() - lastUsb > STALE_MS || !lastUsb) applyLine(String(packet), false);
        }
    }
    if (millis() - petTick >= 60000) {
        petAdvance((millis() - petTick) / 3600000.0f);
        petTick = millis();
        petDirty = true;
    }
    // Grava com folga: a flash se desgasta a cada escrita.
    if (petDirty && millis() - petSaved > 600000) petSave();
    static uint32_t lastDraw = 0;
    if (millis() - lastDraw > 80) {
        render();
        lastDraw = millis();
    }
    delay(5);
}
