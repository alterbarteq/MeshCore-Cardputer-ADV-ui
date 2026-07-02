#pragma once
#include "ScreenBase.h"
#include "../NodePrefs.h"
#include <Preferences.h>

#define SETTINGS_ROWS  ((CONTENT_H - LINE_H) / LINE_H)

struct SettingItem {
    const char* label;
    char value[48];
    bool editable;
    uint16_t color;   // kolor wartosci gdy wiersz nie jest zaznaczony
};

// ─────────────────────────────────────────────────────────────────────────────
//  ScreenSettings
//  Zakladka WEZEL/USTAWIENIA — polaczenie danych o wlasnym wezle (tozsamosc,
//  radio, system, GPS, BLE PIN — dawniej osobny ekran ScreenMyNode) z
//  edytowalna lista ustawien, jako jedna przewijalna lista.
// ─────────────────────────────────────────────────────────────────────────────
class ScreenSettings : public ScreenBase {
public:
    using ChangeFn = void(*)(const char* key, const char* val, void* ctx);
    void setChangeCallback(ChangeFn fn, void* ctx) { _change_fn=fn; _ctx=ctx; }
    void setPrefs(NodePrefs* p) { _prefs=p; _loadWiFiFromNVS(); _buildItems(); }

    // Losowy PIN parowania BLE generowany raz na sesje (patrz MyMesh::begin).
    // Bez tego nigdzie nie widac PINu potrzebnego do sparowania telefonu.
    void setBLEPin(uint32_t pin) { _ble_pin = pin; _buildItems(); _dirty = true; }

    // ── Dane wlasnego wezla (dawniej ScreenMyNode) ─────────────────────────────
    void setNodeId(const char* hex)   { strncpy(_node_id, hex, sizeof(_node_id)-1); _node_id[sizeof(_node_id)-1]='\0'; }
    void setFirmware(const char* ver) { strncpy(_fw, ver, sizeof(_fw)-1); _fw[sizeof(_fw)-1]='\0'; }
    void setBattMV(uint16_t mv)       { _batt_mv = mv; }
    void setFreeRAM(uint32_t b)       { _free_ram = b; }
    void setGPS(int32_t lat6, int32_t lon6, bool fix) { _lat6=lat6; _lon6=lon6; _gps_fix=fix; }

    // Wywolywane okresowo (co kilka sekund) z UITaskRetro, zeby odswiezyc
    // pozycje tylko-do-odczytu (uptime/RAM/bateria/GPS) bez utraty pozycji
    // przewijania/zaznaczenia.
    void refreshLiveStats() { if (!_editing) { _buildItems(); _dirty = true; } }

    static char s_wifi_ssid[33];
    static char s_wifi_pass[65];

    void getWiFiCreds(char* ssid, char* pass, int max_len) {
        strncpy(ssid, s_wifi_ssid, max_len-1); ssid[max_len-1]='\0';
        strncpy(pass, s_wifi_pass, max_len-1); pass[max_len-1]='\0';
    }

    void onEnter() override { _buildItems(); _dirty=true; }

    void draw() override {
        if (!_dirty) return;
        _dirty = false;
        clearContent();
        M5GFX& d = M5Cardputer.Display;
        d.setTextColor(C_TEXT_DIM, C_BG); d.setTextSize(FONT_SM);
        d.setCursor(2, CONTENT_Y+1); d.print("WEZEL / USTAWIENIA");
        hline(CONTENT_Y + LINE_H - 1);

        int first=_scroll, last=min(_item_count, first+SETTINGS_ROWS);
        for (int i=first; i<last; i++) {
            int y = CONTENT_Y + LINE_H + (i-first)*LINE_H;
            bool sel=(i==_sel);
            bool inverted = sel && !_editing;
            if (inverted) selBar(y);
            d.setTextColor(inverted ? C_BG : C_TEXT_DIM, inverted ? C_TEXT : C_BG);
            d.setCursor(2,y); d.setTextSize(FONT_SM);
            d.print(_items[i].label); d.print(":");
            int lw=(strlen(_items[i].label)+2)*CHAR_W+2;
            int avail=(SCREEN_W-lw-4)/CHAR_W;
            if (sel && _editing) {
                d.setTextColor(C_TEXT, C_BG2);
                d.fillRect(lw,y,SCREEN_W-lw-2,LINE_H-1,C_BG2);
                d.setCursor(lw,y);
                bool is_pass=(strcmp(_items[i].label,"WiFi Pass")==0);
                if (is_pass) { for(int c=0;c<(int)strlen(_edit_buf);c++) d.print("*"); }
                else         { d.print(_edit_buf); }
                if((millis()/400)%2==0) {
                    int cx=lw+strlen(_edit_buf)*CHAR_W;
                    d.fillRect(cx,y+1,CHAR_W-1,CHAR_H-2,C_TEXT_MID);
                }
            } else {
                d.setTextColor(inverted?C_BG:_items[i].color, inverted?C_TEXT:C_BG);
                char tr[48]; strncpy(tr,_items[i].value,min(47,avail)); tr[min(47,avail)]='\0';
                d.setCursor(lw,y); d.print(tr);
            }
        }
        int hy=CONTENT_Y+LINE_H+SETTINGS_ROWS*LINE_H+1; hline(hy);
        d.setTextColor(C_TEXT_DIM,C_BG); d.setCursor(2,hy+1);
        d.print(_editing ? "Enter=zapisz  Del=anuluj" : "Enter=edytuj  Gor/Dol=scroll");
    }

    bool onKey(Keyboard_Class::KeysState& ks) override {
        bool consumed=false;

        if (_editing) {
            // Edycja pola
            if (ks.enter) {
                _commitEdit(); _editing=false; consumed=true;
            } else if (ks.del || ks.backspace) {
                int l=strlen(_edit_buf); if(l>0) _edit_buf[l-1]='\0';
                consumed=true;
            } else {
                for (auto c : ks.word) {
                    if (strlen(_edit_buf)<47 && c>=32) {
                        int l=strlen(_edit_buf); _edit_buf[l]=c; _edit_buf[l+1]='\0';
                    }
                    consumed=true;
                }
                if (ks.space && strlen(_edit_buf)<47) {
                    int l=strlen(_edit_buf); _edit_buf[l]=' '; _edit_buf[l+1]='\0';
                    consumed=true;
                }
            }
        } else {
            // Nawigacja
            if (ks.up   && _sel>0)             { _sel--; _clampScroll(); consumed=true; }
            if (ks.down && _sel<_item_count-1) { _sel++; _clampScroll(); consumed=true; }
            const char* sel_label = _items[_sel].label;
            if (ks.enter && strcmp(sel_label,"GPS")==0 && _prefs) {
                _prefs->gps_enabled = !_prefs->gps_enabled;
                strncpy(_items[_sel].value, _prefs->gps_enabled?"ON":"OFF", sizeof(_items[0].value)-1);
                if (_change_fn) _change_fn("GPS", _items[_sel].value, _ctx);
                consumed=true;
            } else if (ks.enter && strcmp(sel_label,"Sciezka")==0 && _prefs) {
                _prefs->path_hash_mode = (_prefs->path_hash_mode + 1) % 3;
                strncpy(_items[_sel].value, _pathHashLabel(_prefs->path_hash_mode), sizeof(_items[0].value)-1);
                if (_change_fn) _change_fn("Sciezka", _items[_sel].value, _ctx);
                consumed=true;
            } else if (ks.enter && strcmp(sel_label,"Wygaszanie")==0 && _prefs) {
                _prefs->screen_timeout_seconds = _nextScreenTimeout(_prefs->screen_timeout_seconds);
                strncpy(_items[_sel].value, _screenTimeoutLabel(_prefs->screen_timeout_seconds), sizeof(_items[0].value)-1);
                if (_change_fn) _change_fn("Wygaszanie", _items[_sel].value, _ctx);
                consumed=true;
            } else if (ks.enter && strcmp(sel_label,"Powiadomienie")==0 && _prefs) {
                _prefs->notify_on_message = !_prefs->notify_on_message;
                strncpy(_items[_sel].value, _prefs->notify_on_message?"ON":"OFF", sizeof(_items[0].value)-1);
                if (_change_fn) _change_fn("Powiadomienie", _items[_sel].value, _ctx);
                consumed=true;
            } else if (ks.enter && _items[_sel].editable) {
                strncpy(_edit_buf, _items[_sel].value, sizeof(_edit_buf)-1);
                _edit_buf[sizeof(_edit_buf)-1]='\0';
                _editing=true; consumed=true;
            }
        }

        if (consumed) _dirty=true;
        return consumed;
    }

private:
    static const int MAX_ITEMS=20;
    SettingItem _items[MAX_ITEMS]={};
    int  _item_count=0, _sel=0, _scroll=0;
    bool _dirty=true, _editing=false;
    char _edit_buf[48]={};
    NodePrefs* _prefs=nullptr;
    uint32_t _ble_pin=0;
    ChangeFn _change_fn=nullptr; void* _ctx=nullptr;

    // Dane wlasnego wezla (dawniej ScreenMyNode)
    char     _node_id[20] = "0x????????";
    char     _fw[24]      = "v?";
    uint16_t _batt_mv     = 0;
    uint32_t _free_ram    = 0;
    int32_t  _lat6        = 0;
    int32_t  _lon6        = 0;
    bool     _gps_fix     = false;

    static const char* _pathHashLabel(uint8_t mode) {
        switch (mode) {
            case 0:  return "1 bajt";
            case 1:  return "2 bajty";
            default: return "3 bajty";
        }
    }

    // Cykl wygaszania ekranu: 5/10/15/30s, potem "Nigdy" (0 = wylaczone —
    // patrz UITaskRetro::_screen_timeout, 0 oznacza brak auto-wygaszania).
    static const int SCREEN_TIMEOUT_STEP_COUNT = 5;
    static constexpr uint16_t SCREEN_TIMEOUT_STEPS[SCREEN_TIMEOUT_STEP_COUNT] = {5, 10, 15, 30, 0};

    static const char* _screenTimeoutLabel(uint16_t secs) {
        switch (secs) {
            case 5:  return "5s";
            case 10: return "10s";
            case 15: return "15s";
            case 30: return "30s";
            default: return "Nigdy";
        }
    }

    static uint16_t _nextScreenTimeout(uint16_t secs) {
        for (int i = 0; i < SCREEN_TIMEOUT_STEP_COUNT; i++) {
            if (SCREEN_TIMEOUT_STEPS[i] == secs) return SCREEN_TIMEOUT_STEPS[(i + 1) % SCREEN_TIMEOUT_STEP_COUNT];
        }
        return SCREEN_TIMEOUT_STEPS[0];   // nieznana wartosc (np. stare "Timeout s" w sekundach) -> zacznij od 5s
    }

    static uint8_t _battPct(uint16_t mv) {
        if (mv >= 4200) return 100;
        if (mv <= 3500) return 0;
        return (uint8_t)((mv - 3500) * 100UL / 700UL);
    }

    void _loadWiFiFromNVS() {
        Preferences pref;
        pref.begin("retro-ui", true);
        pref.getString("wifi_ssid", s_wifi_ssid, sizeof(s_wifi_ssid));
        pref.getString("wifi_pass", s_wifi_pass, sizeof(s_wifi_pass));
        pref.end();
    }

    void _buildItems() {
        _item_count=0;

        // ── Tozsamosc ─────────────────────────────────────────────────────
        // Kolory jak w dawnym ScreenMyNode: nazwa/callsign i pozycja GPS na
        // akcent, uptime na jasno, bateria wg poziomu, reszta neutralnie.
        _addItem("Node Name", _prefs?_prefs->node_name:"???", true, C_ACCENT);
        _addItem("Node ID",   _node_id, false, C_TEXT_MID);
        _addItem("Firmware",  _fw,      false, C_TEXT_MID);

        // ── Radio LoRa ──────────────────────────────────────────────────────
        char freq[16]; snprintf(freq,sizeof(freq),"%.3f",_prefs?_prefs->freq:868.0f);
        _addItem("Freq MHz", freq, true, C_WARN);
        char sf[8]; snprintf(sf,sizeof(sf),"%d",_prefs?_prefs->sf:11);
        _addItem("SF", sf, true);
        char bw[16]; snprintf(bw,sizeof(bw),"%.0f",_prefs?_prefs->bw:125.0f);
        _addItem("BW kHz", bw, true);
        char tx[8]; snprintf(tx,sizeof(tx),"%d",_prefs?_prefs->tx_power_dbm:20);
        _addItem("TX Power", tx, true);
        // Rozmiar hasha sciezki (0/1/2 -> 1/2/3 bajty) — nowosc w MeshCore,
        // wieksze pomieszczenie sciezki kosztem wiekszej szansy kolizji hasha
        _addItem("Sciezka", _pathHashLabel(_prefs?_prefs->path_hash_mode:0), false);

        // ── Siec / WiFi (do mapy) ───────────────────────────────────────────
        _addItem("WiFi SSID", s_wifi_ssid[0]?s_wifi_ssid:"", true);
        _addItem("WiFi Pass", s_wifi_pass[0]?"****":"", true);
        _addItem("Wygaszanie", _screenTimeoutLabel(_prefs?_prefs->screen_timeout_seconds:300), false);
        _addItem("GPS", _prefs?(_prefs->gps_enabled?"ON":"OFF"):"?", true);
        _addItem("Powiadomienie", _prefs?(_prefs->notify_on_message?"ON":"OFF"):"?", true);

        char posbuf[32];
        if (_gps_fix) snprintf(posbuf,sizeof(posbuf),"%.4fN %.4fE", _lat6/1e6, _lon6/1e6);
        _addItem("Pozycja", _gps_fix?posbuf:"BRAK SYGNALU", false, _gps_fix?C_ACCENT:C_TEXT_DIM);

        // ── System ──────────────────────────────────────────────────────────
        unsigned long s = millis()/1000;
        char up[16]; snprintf(up,sizeof(up),"%02lu:%02lu:%02lu", s/3600,(s%3600)/60,s%60);
        _addItem("Uptime", up, false, C_TEXT);

        char ram[16]; snprintf(ram,sizeof(ram),"%lu KB", (unsigned long)(_free_ram/1024));
        _addItem("RAM", ram, false, C_TEXT_MID);

        uint8_t pct = _battPct(_batt_mv);
        char bat[24]; snprintf(bat,sizeof(bat),"%d%% (%d mV)", pct, _batt_mv);
        uint16_t bat_col = pct > 40 ? C_TEXT : (pct > 15 ? C_WARN : C_RED_ERR);
        _addItem("Bateria", bat, false, bat_col);

        // Losowy PIN parowania BLE (0 lub domyslne 123456 = brak wyswietlania,
        // patrz MyMesh::begin — wtedy uzywany jest tryb bez wyswietlacza).
        if (_ble_pin != 0 && _ble_pin != 123456) {
            char pin[12]; snprintf(pin,sizeof(pin),"%lu",(unsigned long)_ble_pin);
            _addItem("BLE PIN", pin, false, C_ACCENT);
        }
    }

    void _addItem(const char* label, const char* val, bool editable, uint16_t color = C_TEXT_MID) {
        if (_item_count>=MAX_ITEMS) return;
        _items[_item_count].label=label;
        _items[_item_count].editable=editable;
        _items[_item_count].color=color;
        strncpy(_items[_item_count].value, val, sizeof(_items[0].value)-1);
        _items[_item_count].value[sizeof(_items[0].value)-1]='\0';
        _item_count++;
    }

    void _commitEdit() {
        if (_sel>=_item_count) return;
        const char* lbl=_items[_sel].label;
        if (strcmp(lbl,"WiFi SSID")==0) {
            strncpy(s_wifi_ssid,_edit_buf,sizeof(s_wifi_ssid)-1);
            Preferences pref; pref.begin("retro-ui", false);
            pref.putString("wifi_ssid", s_wifi_ssid);
            pref.end();
            strncpy(_items[_sel].value,"(zapisano)",sizeof(_items[0].value)-1);
            if (_change_fn) _change_fn(lbl, s_wifi_ssid, _ctx);
            return;
        }
        if (strcmp(lbl,"WiFi Pass")==0) {
            strncpy(s_wifi_pass,_edit_buf,sizeof(s_wifi_pass)-1);
            Preferences pref; pref.begin("retro-ui", false);
            pref.putString("wifi_pass", s_wifi_pass);
            pref.end();
            strncpy(_items[_sel].value,"****",sizeof(_items[0].value)-1);
            if (_change_fn) _change_fn(lbl, s_wifi_pass, _ctx);
            return;
        }
        strncpy(_items[_sel].value, _edit_buf, sizeof(_items[0].value)-1);
        if (_prefs) {
            if      (strcmp(lbl,"Node Name")==0) strncpy(_prefs->node_name,_edit_buf,sizeof(_prefs->node_name)-1);
            else if (strcmp(lbl,"Freq MHz")==0)  _prefs->freq=atof(_edit_buf);
            else if (strcmp(lbl,"SF")==0)        _prefs->sf=atoi(_edit_buf);
            else if (strcmp(lbl,"BW kHz")==0)    _prefs->bw=atof(_edit_buf);
            else if (strcmp(lbl,"TX Power")==0)  _prefs->tx_power_dbm=atoi(_edit_buf);
        }
        if (strcmp(lbl,"GPS")==0 && _prefs) { _prefs->gps_enabled=!_prefs->gps_enabled; strncpy(_items[_sel].value,_prefs->gps_enabled?"ON":"OFF",sizeof(_items[0].value)-1); }
        if (_change_fn) _change_fn(lbl, _edit_buf, _ctx);
    }

    void _clampScroll() {
        if (_sel<_scroll)                  _scroll=_sel;
        if (_sel>=_scroll+SETTINGS_ROWS)   _scroll=_sel-SETTINGS_ROWS+1;
    }
};

// Ten build nie uzywa C++17 (brak inline static constexpr data members) —
// bez tej definicji dostep do SCREEN_TIMEOUT_STEPS przez indeks w runtime
// (nie stala czasu kompilacji) daje "undefined reference" przy linkowaniu.
// `inline` (nie tylko constexpr) jest tu konieczne, bo ten naglowek jest
// wlaczany do wiecej niz jednej jednostki translacji (main.cpp, UITaskRetro.cpp)
// — bez inline linker widzi dwie definicje tego samego symbolu.
inline constexpr uint16_t ScreenSettings::SCREEN_TIMEOUT_STEPS[];
