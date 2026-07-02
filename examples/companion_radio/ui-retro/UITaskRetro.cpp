#include "UITaskRetro.h"
#include <helpers/AdvertDataHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <Utils.h>
#include "target.h"
#include "MeshCoreLogo.h"

extern MyMesh the_mesh;

// base64.hpp's functions aren't declared inline, so including the header here
// too (BaseChatMesh.cpp already does) causes duplicate-symbol link errors —
// just forward-declare the one function we need instead.
unsigned int encode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);

static bool wordContains(const Keyboard_Class::KeysState& ks, char c) {
    for (auto ch : ks.word) { if (ch == c) return true; }
    return false;
}

// Znajduje "logiczny" (skompaktowany, bez pustych slotow) indeks kanalu i
// zwraca odpowiadajacy mu prawdziwy indeks w tablicy kanalow mesh (-1 jesli
// poza zakresem). MAX_GROUP_CHANNELS slotow moze miec dziury (usuniete/nigdy
// nie uzyte), wiec UI zawsze widzi tylko kanaly z niepusta nazwa, po kolei.
static int mapChannelIndex(int logical_idx) {
    int count = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
            if (count == logical_idx) return i;
            count++;
        }
    }
    return -1;
}

// Znajduje prawdziwy indeks kanalu po nazwie (uzywane przy odbiorze
// wiadomosci — MyMesh przekazuje nazwe kanalu, nie jego indeks). Gdy nie
// znaleziono (np. wiadomosc prywatna, nazwa nadawcy zamiast kanalu), zwraca
// 0 ("Public"), zeby wiadomosc nie zniknela zamiast trafic gdziekolwiek.
static int findChannelIndexByName(const char* name) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0' && strcmp(ch.name, name) == 0)
            return i;
    }
    return 0;
}

// PSK kanalow "#nazwa" — pierwsze 16 bajtow SHA256("#nazwa"), zakodowane w
// base64. Ta sama konwencja co inne klienty MeshCore (apka/CLI), zeby dwa
// urzadzenia wpisujace ta sama nazwe kanalu dostaly identyczny klucz bez
// recznej wymiany.
static void deriveChannelPSK(const char* name, char* psk_b64_out, size_t out_len) {
    uint8_t hash[32];
    mesh::Utils::sha256(hash, sizeof(hash), (const uint8_t*)name, strlen(name));
    unsigned int enc_len = encode_base64(hash, 16, (unsigned char*)psk_b64_out);
    if (enc_len >= out_len) enc_len = out_len - 1;
    psk_b64_out[enc_len] = '\0';
}

UITaskRetro::UITaskRetro(mesh::MainBoard* board, BaseSerialInterface* serial_iface)
    : AbstractUITask(board, serial_iface) {}

void UITaskRetro::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
    _display    = display;
    _sensors    = sensors;
    _node_prefs = node_prefs;

    if (_display) _display->turnOn();

    // 0 = "Nigdy" (wybor w ustawieniach) — musi bezposrednio ustawic
    // _screen_timeout na 0, nie zostac przy domyslnej wartosci czlonka, inaczej
    // "Nigdy" nie da sie w ogole wybrac (patrz sprawdzenie > 0 w loop()).
    if (node_prefs) _screen_timeout = (unsigned long)node_prefs->screen_timeout_seconds * 1000UL;
    _auto_off = millis() + _screen_timeout;

    _chat.setSendCallback(_onSend, this);
    _settings.setPrefs(node_prefs);
    _settings.setChangeCallback(_onSettingChange, this);
    _channels.setCallbacks(_chCount, _chGetName, _chAdd, _chGetActive, _chSetActive, _chGetUnread, this);

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

    // Ekran powitalny — gotowe logo "MESHCORE" (PNG z przezroczystym tlem,
    // patrz assets/meshcore_logo.png / MeshCoreLogo.h), + malutkie "v0.1" pod spodem.
    {
        M5GFX& d = M5Cardputer.Display;

        int x = (SCREEN_W - (int)meshcore_logo_w) / 2;
        int y = 40;
        d.drawPng(meshcore_logo_png, meshcore_logo_png_len, x, y);

        d.setTextSize(1);
        d.setTextColor(C_TEXT_DIM, C_BG);
        int vw = d.textWidth("v0.1");
        d.setCursor((SCREEN_W - vw) / 2, y + (int)meshcore_logo_h + 12);
        d.print("v0.1");

        delay(1200);
        d.fillScreen(C_BG);
    }

    _switchTab(Tab::CHAT);
    _syncActiveChannelToChat();
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

    // Ikona nowej wiadomosci (koperta na srodku) — sama znika po uplywie
    // czasu; wymuszamy pelny redraw ekranu pod nia, zeby faktycznie zniknela
    // zamiast zostac "wypalona" na ekranie.
    if (_notify_icon_until != 0) {
        if (millis() >= _notify_icon_until) {
            _notify_icon_until = 0;
            M5Cardputer.Display.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
            switch (_active_tab) {
                case Tab::CHAT:     _chat.onEnter();     break;
                case Tab::NODES:    _nodes.onEnter();    break;
                case Tab::MAP:      _map.onEnter();      break;
                case Tab::SETTINGS: _settings.onEnter(); break;
                default: break;
            }
        }
        _need_refresh = true;
    }

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

    // OPT + strzalki = zmiana taba; OPT samo (na CHAT) = wybor kanalu;
    // OPT+N = od razu dodanie nowego kanalu
    if (ks.opt) {
        if (ks.right || ks.tab) {
            int next = ((int)_active_tab + 1) % (int)Tab::COUNT;
            _switchTab((Tab)next); return;
        }
        if (ks.left) {
            int prev = ((int)_active_tab + (int)Tab::COUNT - 1) % (int)Tab::COUNT;
            _switchTab((Tab)prev); return;
        }
        if (_active_tab == Tab::CHAT) {
            if (wordContains(ks,'n') || wordContains(ks,'N')) {
                _channel_overlay = true;
                _channels.onEnter();
                _channels.startAdding();
            } else if (_channel_overlay) {
                _closeChannelOverlay();
            } else {
                _channel_overlay = true;
                _channels.onEnter();
            }
            return;
        }
    }

    // Nakladka wyboru typu advertu ma najwyzszy priorytet, gdy otwarta —
    // dostepna jest z kazdej zakladki po wcisnieciu samego "tab" (bez opt).
    if (_advert_overlay) {
        if (ks.up || ks.down) { _advert_sel = 1 - _advert_sel; }
        if (ks.enter) {
            _sendAdvert(_advert_sel == 0);
            _closeAdvertOverlay();
        } else if (ks.del || ks.backspace || ks.tab) {
            _closeAdvertOverlay();
        }
        return;
    }

    // Nakladka wyboru/dodawania kanalow ma priorytet nad reszta klawiszy
    if (_channel_overlay) {
        bool was_adding = _channels.isAdding();
        bool consumed = _channels.onKey(ks);
        // Enter podczas wpisywania nazwy tylko zatwierdza kanal i wraca do
        // LISTY (w tej samej nakladce) — nakladke zamykamy dopiero gdy Enter
        // faktycznie wybral kanal z listy.
        if (consumed && ks.enter && !was_adding) _closeChannelOverlay();
        return;
    }

    // Sam "tab" (bez opt) — otwiera okienko wyboru wyslania advertu
    if (ks.tab) {
        _advert_overlay = true;
        _advert_sel = 0;
        return;
    }

    // Fn+1-4 = bezposrednie taby
    if (ks.fn) {
        if (ks.f1 || wordContains(ks,'1')) { _switchTab(Tab::CHAT);     return; }
        if (ks.f2 || wordContains(ks,'2')) { _switchTab(Tab::NODES);    return; }
        if (ks.f3 || wordContains(ks,'3')) { _switchTab(Tab::MAP);      return; }
        if (ks.f4 || wordContains(ks,'4')) { _switchTab(Tab::SETTINGS); return; }
    }

    // F1-F4 bezposrednio
    if (ks.f1) { _switchTab(Tab::CHAT);     return; }
    if (ks.f2) { _switchTab(Tab::NODES);    return; }
    if (ks.f3) { _switchTab(Tab::MAP);      return; }
    if (ks.f4) { _switchTab(Tab::SETTINGS); return; }

    // Dispatch do aktywnego ekranu
    switch (_active_tab) {
        case Tab::CHAT:     _chat.onKey(ks);     break;
        case Tab::NODES:    _nodes.onKey(ks);    break;
        case Tab::MAP:      _map.onKey(ks);      break;
        case Tab::SETTINGS: _settings.onKey(ks); break;
        default: break;
    }
}

void UITaskRetro::_drawFrame() {
    TopBar::draw(_active_tab,
                 the_mesh.getNumContacts(),
                 getBattMilliVolts(),
                 _gps_fix,
                 _tx_count, _rx_count, _err_count,
                 _anyUnreadChannel());

    if (_advert_overlay) {
        _drawAdvertOverlay();
        return;
    }

    if (_channel_overlay) {
        _channels.draw();
        return;
    }

    switch (_active_tab) {
        case Tab::CHAT:     _chat.draw();     break;
        case Tab::NODES:    _nodes.draw();    break;
        case Tab::MAP:      _map.draw();      break;
        case Tab::SETTINGS: _settings.draw(); break;
        default: break;
    }

    if (_notify_icon_until != 0 && millis() < _notify_icon_until) {
        _drawNotifyIcon();
    }
}

// Fioletowa koperta na srodku ekranu, widoczna przez 5s po nowej wiadomosci
// (jesli "Powiadomienie" w ustawieniach jest wlaczone) — rysowana NA WIERZCHU
// biezacej zawartosci zakladki, niezaleznie ktora jest akurat otwarta.
void UITaskRetro::_drawNotifyIcon() {
    M5GFX& d = M5Cardputer.Display;
    int cx = SCREEN_W / 2;
    int cy = CONTENT_Y + CONTENT_H / 2 - 4;

    int w = 36, h = 24;
    int x = cx - w / 2, y = cy - h / 2;

    // Panel pod koperta, zeby byla czytelna niezaleznie od tla pod nia
    d.fillRect(x - 6, y - 6, w + 12, h + 18, C_BG);
    d.drawRect(x - 6, y - 6, w + 12, h + 18, C_TEXT_DIM);

    // Koperta — fioletowa ramka, klapka jako dwie przekatne schodzace sie
    // na srodku gory (blokowy, pixel-artowy styl reszty UI)
    d.fillRect(x, y, w, h, C_ACCENT);
    d.fillRect(x + 2, y + 2, w - 4, h - 4, C_BG);
    for (int i = 0; i < 3; i++) {
        d.drawLine(x + 2, y + 2 + i, x + w / 2, y + h / 2 + i, C_ACCENT);
        d.drawLine(x + w - 3, y + 2 + i, x + w / 2, y + h / 2 + i, C_ACCENT);
    }

    const char* label = "NOWA WIADOMOSC";
    d.setTextSize(FONT_SM);
    d.setTextColor(C_ACCENT, C_BG);
    int tw = strlen(label) * CHAR_W;
    d.setCursor(cx - tw / 2, y + h + 4);
    d.print(label);
}

void UITaskRetro::_closeChannelOverlay() {
    _channel_overlay = false;
    // ScreenChat ma wlasna wewnetrzna flage "trzeba przerysowac", ktora nie
    // byla ustawiana podczas gdy klawisze szly do nakladki kanalow — bez
    // tego czat zostawal na ekranie w stanie sprzed otwarcia nakladki,
    // dopoki cos innego (np. wpisanie znaku) nie wymusilo redraw.
    M5Cardputer.Display.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    _chat.onEnter();
}

void UITaskRetro::_drawAdvertOverlay() {
    M5GFX& d = M5Cardputer.Display;
    d.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    d.setTextSize(FONT_SM);

    static const char* opts[2] = { "Advert (flood)", "Zero-hop (blisko)" };

    d.setTextColor(C_TEXT_MID, C_BG);
    d.setCursor(4, CONTENT_Y + 6);
    d.print("Wyslij advertisement:");

    int y = CONTENT_Y + 22;
    for (int i = 0; i < 2; i++) {
        bool sel = (i == _advert_sel);
        if (sel) d.fillRect(0, y, SCREEN_W, LINE_H, C_TEXT);
        d.setTextColor(sel ? C_BG : C_TEXT_MID, sel ? C_TEXT : C_BG);
        d.setCursor(4, y);
        d.print(opts[i]);
        y += LINE_H;
    }

    int hy = CONTENT_Y + CONTENT_H - LINE_H;
    d.drawFastHLine(0, hy - 1, SCREEN_W, C_TEXT_DIM);
    d.setTextColor(C_TEXT_DIM, C_BG);
    d.setCursor(2, hy);
    d.print("Enter=wyslij  Tab/Del=anuluj");
}

void UITaskRetro::_closeAdvertOverlay() {
    _advert_overlay = false;
    // Popup zajmowal cala tresc, wiec po zamknieciu trzeba wymusic pelny
    // redraw aktywnej zakladki — jej wewnetrzne flagi "trzeba przerysowac"
    // mogly nie byc ustawione, skoro klawisze szly do nakladki.
    M5Cardputer.Display.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    switch (_active_tab) {
        case Tab::CHAT:     _chat.onEnter();     break;
        case Tab::NODES:    _nodes.onEnter();    break;
        case Tab::MAP:      _map.onEnter();      break;
        case Tab::SETTINGS: _settings.onEnter(); break;
        default: break;
    }
}

void UITaskRetro::_sendAdvert(bool flood) {
    the_mesh.advert(flood);
}

void UITaskRetro::_switchTab(Tab t) {
    switch (_active_tab) {
        case Tab::CHAT:     _chat.onLeave();     break;
        case Tab::NODES:    _nodes.onLeave();    break;
        case Tab::MAP:      _map.onLeave();      break;
        case Tab::SETTINGS: _settings.onLeave(); break;
        default: break;
    }

    _active_tab = t;
    _channel_overlay = false;  // nakladka kanalow jest tylko dla CHAT
    M5Cardputer.Display.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    _need_refresh = true;

    switch (_active_tab) {
        case Tab::CHAT:     _chat.onEnter();     break;
        case Tab::NODES:    _nodes.onEnter();    _updateNodeList(); break;
        case Tab::MAP:      _map.onEnter();      break;
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
    _settings.setBattMV(getBattMilliVolts());
    _settings.setFreeRAM(ESP.getFreeHeap());
    _settings.setFirmware(FIRMWARE_VERSION);

    char hex[20] = "0x";
    mesh::Utils::toHex(hex+2, the_mesh.self_id.pub_key, 4);
    hex[10] = '\0';
    _settings.setNodeId(hex);

    if (_sensors) {
        LocationProvider* loc = _sensors->getLocationProvider();
        if (loc && loc->isValid()) {
            _gps_fix = true;
            int32_t lat6 = (int32_t)loc->getLatitude();
            int32_t lon6 = (int32_t)loc->getLongitude();
            _settings.setGPS(lat6, lon6, true);
            _map.setOwnPos(lat6, lon6);
        } else {
            _settings.setGPS(0, 0, false);
        }
    }
    _settings.refreshLiveStats();
}

void UITaskRetro::_syncActiveChannelToChat() {
    ChannelDetails ch;
    if (the_mesh.getChannel(_active_channel, ch) && ch.name[0] != '\0') {
        _chat.setActiveChannel(_active_channel, ch.name);
    }
    // Kanal, ktory wlasnie stal sie aktywny, jest teraz ogladany — jego
    // ewentualny licznik nieprzeczytanych wraca do zera.
    if (_active_channel >= 0 && _active_channel < MAX_GROUP_CHANNELS) {
        _unread_count[_active_channel] = 0;
    }
}

bool UITaskRetro::_anyUnreadChannel() const {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        if (_unread_count[i] > 0) return true;
    }
    return false;
}

void UITaskRetro::newMsg(uint8_t path_len, const char* from_name,
                          const char* text, int msgcount) {
    _rx_count++;
    // from_name to nazwa kanalu (patrz MyMesh::onChannelMessageRecv) —
    // trzeba ja zamapowac na prawdziwy indeks kanalu, zeby wiadomosc trafila
    // do wlasciwego, oddzielnego widoku, niezaleznie od tego, ktory kanal
    // jest akurat aktywny na ekranie.
    int ch_idx = findChannelIndexByName(from_name);
    _chat.addMessage(from_name, text, false, false, ch_idx);
    // Wiadomosc przyszla na kanale, ktory nie jest teraz ogladany — zwieksz
    // jego licznik nieprzeczytanych (pokazywany w nakladce kanalow) i zapal
    // wykrzyknik obok F1:CZAT, zeby uzytkownik wiedzial, ze cos umknelo.
    if (ch_idx != _active_channel && ch_idx >= 0 && ch_idx < MAX_GROUP_CHANNELS) {
        _unread_count[ch_idx]++;
    }
    _need_refresh = true;
    // "Powiadomienie" w ustawieniach — gdy wylaczone, wiadomosc po prostu
    // czeka w tle (ekran nie budzi sie, zadnej koperty).
    if (!_node_prefs || _node_prefs->notify_on_message) {
        _wakScreen();
        _notify_icon_until = millis() + 5000;
    }
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
    if (the_mesh.getChannel(self->_active_channel, ch) && ch.name[0] != '\0') {
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

    // Nowy czas wygaszania obowiazuje natychmiast (nie dopiero po restarcie) —
    // 0 = "Nigdy" wylacza auto-wygaszanie calkowicie (patrz sprawdzenie > 0 w loop()).
    if (strcmp(key, "Wygaszanie") == 0 && self->_node_prefs) {
        self->_screen_timeout = (unsigned long)self->_node_prefs->screen_timeout_seconds * 1000UL;
        self->_auto_off = millis() + self->_screen_timeout;
    }

    if (!self->_node_prefs) return;
    the_mesh.savePrefs();
}

// ── Trampoliny kanalow dla ScreenChannels ──────────────────────────────────

int UITaskRetro::_chCount(void* ctx) {
    int count = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') count++;
    }
    return count;
}

bool UITaskRetro::_chGetName(int idx, char* out, int maxlen, void* ctx) {
    int real = mapChannelIndex(idx);
    if (real < 0) return false;
    ChannelDetails ch;
    if (!the_mesh.getChannel(real, ch)) return false;
    strncpy(out, ch.name, maxlen - 1);
    out[maxlen - 1] = '\0';
    return true;
}

bool UITaskRetro::_chAdd(const char* name, void* ctx) {
    UITaskRetro* self = (UITaskRetro*)ctx;
    char psk[32];
    deriveChannelPSK(name, psk, sizeof(psk));
    ChannelDetails* added = the_mesh.addChannel(name, psk);
    if (!added) return false;

    the_mesh.saveChannels();

    // nowo dodany kanal jest w ostatnim niepustym slocie — od razu go wybierz
    int last = -1;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') last = i;
    }
    if (last >= 0) {
        self->_active_channel = last;
        self->_syncActiveChannelToChat();
    }
    return true;
}

int UITaskRetro::_chGetActive(void* ctx) {
    UITaskRetro* self = (UITaskRetro*)ctx;
    int count = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
            if (i == self->_active_channel) return count;
            count++;
        }
    }
    return -1;
}

void UITaskRetro::_chSetActive(int idx, void* ctx) {
    UITaskRetro* self = (UITaskRetro*)ctx;
    int real = mapChannelIndex(idx);
    if (real >= 0) {
        self->_active_channel = real;
        self->_syncActiveChannelToChat();
    }
}

int UITaskRetro::_chGetUnread(int idx, void* ctx) {
    UITaskRetro* self = (UITaskRetro*)ctx;
    int real = mapChannelIndex(idx);
    if (real < 0 || real >= MAX_GROUP_CHANNELS) return 0;
    return self->_unread_count[real];
}

// Static member definitions

char ScreenSettings::s_wifi_ssid[33] = {};
char ScreenSettings::s_wifi_pass[65] = {};
