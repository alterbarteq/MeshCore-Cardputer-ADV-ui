#include "UITaskRetro.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <Utils.h>
#include "target.h"

extern MyMesh the_mesh;

static bool wordContains(const Keyboard_Class::KeysState& ks, char c) {
    for (auto ch : ks.word) { if (ch == c) return true; }
    return false;
}

// ── Blokowa bitmapa 5x7 na ekran powitalny ("MESHCORE" z kafelkow) ─────────
static const uint8_t GLYPH_M[7] = {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001};
static const uint8_t GLYPH_E[7] = {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111};
static const uint8_t GLYPH_S[7] = {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110};
static const uint8_t GLYPH_H[7] = {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001};
static const uint8_t GLYPH_C[7] = {0b01111,0b10000,0b10000,0b10000,0b10000,0b10000,0b01111};
static const uint8_t GLYPH_O[7] = {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110};
static const uint8_t GLYPH_R[7] = {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001};

static const uint8_t* glyphFor(char c) {
    switch (c) {
        case 'M': return GLYPH_M;
        case 'E': return GLYPH_E;
        case 'S': return GLYPH_S;
        case 'H': return GLYPH_H;
        case 'C': return GLYPH_C;
        case 'O': return GLYPH_O;
        case 'R': return GLYPH_R;
        default:  return nullptr;
    }
}

// Rysuje jedna litere jako siatke 5x7 kwadracikow ("kafelkow")
static void drawTileGlyph(M5GFX& d, int x, int y, const uint8_t* glyph,
                          uint16_t color, int tile, int gap) {
    int pitch = tile + gap;
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (glyph[row] & (1 << (4 - col))) {
                d.fillRect(x + col * pitch, y + row * pitch, tile, tile, color);
            }
        }
    }
}

UITaskRetro::UITaskRetro(mesh::MainBoard* board, BaseSerialInterface* serial_iface)
    : AbstractUITask(board, serial_iface) {}

void UITaskRetro::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
    _display    = display;
    _sensors    = sensors;
    _node_prefs = node_prefs;

    if (_display) _display->turnOn();

    if (node_prefs && node_prefs->screen_timeout_seconds > 0)
        _screen_timeout = (unsigned long)node_prefs->screen_timeout_seconds * 1000UL;
    _auto_off = millis() + _screen_timeout;

    _chat.setSendCallback(_onSend, this);
    _settings.setPrefs(node_prefs);
    _settings.setChangeCallback(_onSettingChange, this);

    // Losowy PIN parowania BLE jest ustalany raz w MyMesh::begin() (przed
    // ui_task.begin()) — bez tego nigdzie nie bylo widac, co wpisac w
    // telefonie przy parowaniu (widoczny jest tylko w USTAWIENIACH).
    _settings.setBLEPin(the_mesh.getBLEPin());

    // Przywroc stan GPS zapisany w NodePrefs — bez tego CardputerSensorManager
    // zawsze startuje z GPS wylaczonym (patrz komentarz w
    // CardputerSensorManager::begin()), wiec ustawienie "wlaczone" z
    // poprzedniej sesji nigdy nie wracalo do zycia po restarcie.
#ifdef HAS_GPS
    if (_sensors && _node_prefs) {
        ::sensors.setNodePrefs(_node_prefs);
        if (_node_prefs->gps_enabled) {
            _sensors->setSettingValue("gps", "1");
        }
    }
#endif

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(C_BG);

    // Ekran powitalny — "MESHCORE" zbudowany z kwadratowych kafelkow (jak
    // logo TORLINK), litera po literze w gradiencie jasny liliowy -> ciemny
    // fiolet/indygo, + malutkie "v0.1" pod spodem
    {
        M5GFX& d = M5Cardputer.Display;

        const char* title = "MESHCORE";
        int n = strlen(title);
        const int tile = 4, gap = 1, pitch = tile + gap;
        const int letter_w = 4 * pitch + tile;  // 5 kolumn
        const int letter_h = 6 * pitch + tile;  // 7 wierszy
        const int letter_spacing = 5;
        int total_w = n * letter_w + (n - 1) * letter_spacing;
        int x = (SCREEN_W - total_w) / 2;
        int y = 32;

        // jasny liliowy -> ciemny fiolet/indygo
        const uint8_t r1 = 245, g1 = 240, b1 = 255;
        const uint8_t r2 = 90,  g2 = 40,  b2 = 170;

        for (int i = 0; i < n; i++) {
            float t = (n > 1) ? (float)i / (n - 1) : 0.0f;
            uint8_t r = r1 + (int)((r2 - r1) * t);
            uint8_t g = g1 + (int)((g2 - g1) * t);
            uint8_t b = b1 + (int)((b2 - b1) * t);
            const uint8_t* glyph = glyphFor(title[i]);
            if (glyph) drawTileGlyph(d, x, y, glyph, RGB565(r, g, b), tile, gap);
            x += letter_w + letter_spacing;
        }

        d.setTextSize(1);
        d.setTextColor(C_TEXT_DIM, C_BG);
        int vw = d.textWidth("v0.1");
        d.setCursor((SCREEN_W - vw) / 2, y + letter_h + 10);
        d.print("v0.1");

        delay(1200);
        d.fillScreen(C_BG);
    }

    _switchTab(Tab::CHAT);
    _updateMyNodeScreen();
}

void UITaskRetro::loop() {
    M5Cardputer.update();

    // Typewriter tick
    _chat.tick();
    if (_active_tab == Tab::CHAT && _chat.isTyping()) _need_refresh = true;

    // Screen sleep
    if (_screen_timeout > 0 && _auto_off > 0 && millis() > _auto_off && !_screen_sleeping) {
        _screen_sleeping = true;
        if (_display) _display->turnOff();
        return;
    }

    // Keyboard
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        if (_screen_sleeping) { _wakScreen(); return; }
        _wakScreen();
        _handleKeys();
        _need_refresh = true;
    }

    if (_screen_sleeping) return;

    // Update node list every 3s
    static uint32_t last_node_update = 0;
    if (millis() - last_node_update > 3000) {
        last_node_update = millis();
        _updateNodeList();
        _updateMyNodeScreen();
        _need_refresh = true;
    }

    // Draw only when needed
    if (_need_refresh) {
        _need_refresh = false;
        _drawFrame();
    }
}

void UITaskRetro::_handleKeys() {
    if (millis() - _last_key_ms < KEY_DEBOUNCE_MS) return;
    _last_key_ms = millis();

    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();

    // OPT + strzalki = zmiana taba
    if (ks.opt) {
        if (ks.right || ks.tab) {
            int next = ((int)_active_tab + 1) % (int)Tab::COUNT;
            _switchTab((Tab)next); return;
        }
        if (ks.left) {
            int prev = ((int)_active_tab + (int)Tab::COUNT - 1) % (int)Tab::COUNT;
            _switchTab((Tab)prev); return;
        }
    }

    // Fn+1-5 = bezposrednie taby
    if (ks.fn) {
        if (ks.f1 || wordContains(ks,'1')) { _switchTab(Tab::CHAT);     return; }
        if (ks.f2 || wordContains(ks,'2')) { _switchTab(Tab::NODES);    return; }
        if (ks.f3 || wordContains(ks,'3')) { _switchTab(Tab::MAP);      return; }
        if (ks.f4 || wordContains(ks,'4')) { _switchTab(Tab::MYNODE);   return; }
        if (ks.f5 || wordContains(ks,'5')) { _switchTab(Tab::SETTINGS); return; }
    }

    // F1-F5 bezposrednio
    if (ks.f1) { _switchTab(Tab::CHAT);     return; }
    if (ks.f2) { _switchTab(Tab::NODES);    return; }
    if (ks.f3) { _switchTab(Tab::MAP);      return; }
    if (ks.f4) { _switchTab(Tab::MYNODE);   return; }
    if (ks.f5) { _switchTab(Tab::SETTINGS); return; }

    // Dispatch do aktywnego ekranu
    switch (_active_tab) {
        case Tab::CHAT:     _chat.onKey(ks);     break;
        case Tab::NODES:    _nodes.onKey(ks);    break;
        case Tab::MAP:      _map.onKey(ks);      break;
        case Tab::MYNODE:   _mynode.onKey(ks);   break;
        case Tab::SETTINGS: _settings.onKey(ks); break;
        default: break;
    }
}

void UITaskRetro::_drawFrame() {
    TopBar::draw(_active_tab,
                 the_mesh.getNumContacts(),
                 getBattMilliVolts(),
                 _gps_fix,
                 _tx_count, _rx_count, _err_count);

    switch (_active_tab) {
        case Tab::CHAT:     _chat.draw();     break;
        case Tab::NODES:    _nodes.draw();    break;
        case Tab::MAP:      _map.draw();      break;
        case Tab::MYNODE:   _mynode.draw();   break;
        case Tab::SETTINGS: _settings.draw(); break;
        default: break;
    }
}

void UITaskRetro::_switchTab(Tab t) {
    switch (_active_tab) {
        case Tab::CHAT:     _chat.onLeave();     break;
        case Tab::NODES:    _nodes.onLeave();    break;
        case Tab::MAP:      _map.onLeave();      break;
        case Tab::MYNODE:   _mynode.onLeave();   break;
        case Tab::SETTINGS: _settings.onLeave(); break;
        default: break;
    }

    _active_tab = t;
    M5Cardputer.Display.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    _need_refresh = true;

    switch (_active_tab) {
        case Tab::CHAT:     _chat.onEnter();     break;
        case Tab::NODES:    _nodes.onEnter();    _updateNodeList(); break;
        case Tab::MAP:      _map.onEnter();      break;
        case Tab::MYNODE:   _mynode.onEnter();   _updateMyNodeScreen(); break;
        case Tab::SETTINGS: _settings.onEnter(); break;
        default: break;
    }
}

void UITaskRetro::_updateNodeList() {
    int n = the_mesh.getNumContacts();
    static NodeEntry entries[NODES_MAX];
    int count = 0;

    for (int i = 0; i < n && count < NODES_MAX; i++) {
        ContactInfo ci;
        if (!the_mesh.getContactByIdx(i, ci)) continue;
        if (ci.name[0] == '\0') continue;
        NodeEntry& e = entries[count++];
        strncpy(e.name, ci.name, sizeof(e.name)-1);
        e.name[sizeof(e.name)-1] = '\0';
        e.type         = ci.type;
        e.out_path_len = ci.out_path_len;
        e.gps_lat      = ci.gps_lat;
        e.gps_lon      = ci.gps_lon;
        uint32_t now_s = millis()/1000;
        e.last_seen = (ci.lastmod > 0 && ci.lastmod < now_s) ? (now_s - ci.lastmod) : 0;
    }

    _nodes.setNodes(entries, count);
    if (_gps_fix) _map.setNodes(entries, count);
}

void UITaskRetro::_updateMyNodeScreen() {
    _mynode.setNodeName(the_mesh.getNodeName());
    _mynode.setBattMV(getBattMilliVolts());
    _mynode.setFreeRAM(ESP.getFreeHeap());
    _mynode.setFirmware(FIRMWARE_VERSION);

    if (_node_prefs) {
        _mynode.setFreq(_node_prefs->freq);
        _mynode.setSF(_node_prefs->sf);
        _mynode.setBW(_node_prefs->bw);
        _mynode.setCR(_node_prefs->cr);
        _mynode.setTxPower(_node_prefs->tx_power_dbm);
    }

    char hex[20] = "0x";
    mesh::Utils::toHex(hex+2, the_mesh.self_id.pub_key, 4);
    hex[10] = '\0';
    _mynode.setNodeId(hex);

    if (_sensors) {
        LocationProvider* loc = _sensors->getLocationProvider();
        if (loc && loc->isValid()) {
            _gps_fix = true;
            int32_t lat6 = (int32_t)loc->getLatitude();
            int32_t lon6 = (int32_t)loc->getLongitude();
            _mynode.setGPS(lat6, lon6, true);
            _map.setOwnPos(lat6, lon6);
        } else {
            _mynode.setGPS(0, 0, false);
        }
    }
    _mynode.setSDOK(false);
}

void UITaskRetro::newMsg(uint8_t path_len, const char* from_name,
                          const char* text, int msgcount) {
    _rx_count++;
    _chat.addMessage(from_name, text, false, false);
    _need_refresh = true;
    _wakScreen();
}

void UITaskRetro::msgRead(int msgcount) {}
void UITaskRetro::notify(UIEventType t) { _need_refresh = true; _wakScreen(); }

void UITaskRetro::_wakScreen() {
    if (_screen_sleeping) {
        _screen_sleeping = false;
        if (_display) _display->turnOn();
        _need_refresh = true;
    }
    if (_screen_timeout > 0)
        _auto_off = millis() + _screen_timeout;
}

void UITaskRetro::_onSend(const char* text, void* ctx) {
    UITaskRetro* self = (UITaskRetro*)ctx;
    if (!text || text[0] == '\0') return;
    uint32_t timestamp = rtc_clock.getCurrentTime();
    int text_len = strlen(text);
    ChannelDetails ch;
    if (the_mesh.getChannel(0, ch) && ch.name[0] != '\0') {
        the_mesh.sendGroupMessage(timestamp, ch.channel,
            self->_node_prefs ? self->_node_prefs->node_name : "?",
            text, text_len);
    }
    self->_tx_count++;
}

void UITaskRetro::_onSettingChange(const char* key, const char* val, void* ctx) {
    UITaskRetro* self = (UITaskRetro*)ctx;

    // Przekaz stan GPS do CardputerSensorManager — sama zmiana w NodePrefs
    // niczego fizycznie nie wlacza/wylacza, trzeba to jeszcze zawolac na
    // _sensors (patrz CardputerSensorManager::setSettingValue).
    if (strcmp(key, "GPS") == 0 && self->_sensors && self->_node_prefs) {
        self->_sensors->setSettingValue("gps",
            self->_node_prefs->gps_enabled ? "1" : "0");
    }

    if (!self->_node_prefs) return;
    the_mesh.savePrefs();
}

// Static member definitions

char ScreenSettings::s_wifi_ssid[33] = {};
char ScreenSettings::s_wifi_pass[65] = {};
