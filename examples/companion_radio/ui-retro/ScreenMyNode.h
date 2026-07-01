#pragma once
#include "ScreenBase.h"
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  ScreenMyNode
//  Shows live stats for own node:  identity, radio params, system health.
//  Data is pushed in via setXxx() methods from UITaskRetro.
// ─────────────────────────────────────────────────────────────────────────────

class ScreenMyNode : public ScreenBase {
public:
    void setNodeName(const char* name)  { strncpy(_name,    name,   sizeof(_name)-1);    _dirty=true; }
    void setNodeId(const char* hex)     { strncpy(_node_id, hex,    sizeof(_node_id)-1); _dirty=true; }
    void setFirmware(const char* ver)   { strncpy(_fw,      ver,    sizeof(_fw)-1);      _dirty=true; }
    void setFreq(float f)               { _freq = f;    _dirty=true; }
    void setSF(uint8_t sf)              { _sf   = sf;   _dirty=true; }
    void setBW(float bw)                { _bw   = bw;   _dirty=true; }
    void setCR(uint8_t cr)              { _cr   = cr;   _dirty=true; }
    void setTxPower(uint8_t p)          { _tx_pw = p;   _dirty=true; }
    void setBattMV(uint16_t mv)         { _batt_mv = mv; _dirty=true; }
    void setFreeRAM(uint32_t b)         { _free_ram = b; _dirty=true; }
    void setGPS(int32_t lat6, int32_t lon6, bool fix) {
        _lat6 = lat6; _lon6 = lon6; _gps_fix = fix; _dirty=true;
    }
    void setSDOK(bool ok)               { _sd_ok = ok;  _dirty=true; }

    void onEnter() override { _dirty = true; }

    // ── draw ─────────────────────────────────────────────────────────────────
    void draw() override {
        // refresh every second for uptime counter
        static uint32_t last_sec = 0;
        if (millis() / 1000 != last_sec) { last_sec = millis()/1000; _dirty = true; }
        if (!_dirty) return;
        _dirty = false;

        clearContent();
        M5GFX& d = M5Cardputer.Display;
        d.setTextSize(FONT_SM);

        int y = CONTENT_Y + 2;

        // ── IDENTYFIKACJA block ───────────────────────────────────────────
        _blockHeader(y, "IDENTYFIKACJA");  y += LINE_H;
        _kvRow(y, "CALLSIGN", _name,    C_ACCENT);    y += LINE_H;
        _kvRow(y, "NODE ID",  _node_id, C_TEXT_MID); y += LINE_H;
        _kvRow(y, "FIRMWARE", _fw,      C_TEXT_MID); y += LINE_H;
        hline(y); y += 2;

        // ── RADIO block ───────────────────────────────────────────────────
        // FREQ + parametry na jednej linii (bez osobnych etykiet) — zwalnia
        // jeden wiersz wysokosci, bo na tym ekranie brakowalo miejsca i
        // dolne wiersze (GPS/SD) wjezdzaly na pasek stanu.
        _blockHeader(y, "RADIO LoRa");  y += LINE_H;
        char buf[40];
        d.setTextColor(C_WARN, C_BG);
        d.setCursor(4, y);
        snprintf(buf, sizeof(buf), "%.3fMHz ", _freq);
        d.print(buf);
        d.setTextColor(C_TEXT_MID, C_BG);
        snprintf(buf, sizeof(buf), "SF%d BW%.0f CR4/%d %ddBm", _sf, _bw, _cr, _tx_pw);
        d.print(buf);
        y += LINE_H;
        hline(y); y += 2;

        // ── SYSTEM block ──────────────────────────────────────────────────
        _blockHeader(y, "SYSTEM");  y += LINE_H;

        // Uptime + RAM na jednej linii
        unsigned long s = millis() / 1000;
        d.setTextColor(C_TEXT_DIM, C_BG);
        d.setCursor(4, y);
        d.print("UPTIME ");
        d.setTextColor(C_TEXT, C_BG);
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);
        d.print(buf);
        d.setTextColor(C_TEXT_DIM, C_BG);
        d.print("  RAM ");
        d.setTextColor(C_TEXT_MID, C_BG);
        snprintf(buf, sizeof(buf), "%luKB", (unsigned long)(_free_ram/1024));
        d.print(buf);
        y += LINE_H;

        // Battery + gauge
        uint8_t pct = _battPct(_batt_mv);
        snprintf(buf, sizeof(buf), "%d%%  (%d mV)", pct, _batt_mv);
        uint16_t bat_col = pct > 40 ? C_TEXT : (pct > 15 ? C_WARN : C_RED_ERR);
        _kvRow(y, "BATERIA", buf, bat_col); y += LINE_H - 2;
        // gauge bar
        int gw = (SCREEN_W - 4) * pct / 100;
        d.fillRect(2, y, SCREEN_W - 4, 3, C_BG2);
        d.fillRect(2, y, gw, 3, bat_col);
        y += 5;

        // GPS
        if (_gps_fix) {
            snprintf(buf, sizeof(buf), "%.4f N  %.4f E",
                     _lat6 / 1e6, _lon6 / 1e6);
            _kvRow(y, "GPS", buf, C_ACCENT);
        } else {
            _kvRow(y, "GPS", "BRAK SYGNALU", C_TEXT_DIM);
        }
    }

    // ── key handling ─────────────────────────────────────────────────────────
    bool onKey(Keyboard_Class::KeysState& ks) override {
        // No interactive elements yet; F5 etc handled by UITaskRetro
        return false;
    }

private:
    char    _name[32]    = "???";
    char    _node_id[20] = "0x????????";
    char    _fw[24]      = "v?";
    float   _freq        = 868.0f;
    uint8_t _sf          = 11;
    float   _bw          = 125.0f;
    uint8_t _cr          = 5;
    uint8_t _tx_pw       = 20;
    uint16_t _batt_mv    = 0;
    uint32_t _free_ram   = 0;
    int32_t  _lat6       = 0;
    int32_t  _lon6       = 0;
    bool     _gps_fix    = false;
    bool     _sd_ok      = false;
    bool     _dirty      = true;

    void _blockHeader(int y, const char* label) {
        M5Cardputer.Display.setTextColor(C_TEXT_DIM, C_BG);
        M5Cardputer.Display.setCursor(2, y);
        M5Cardputer.Display.print(label);
    }

    void _kvRow(int y, const char* key, const char* val, uint16_t val_col) {
        M5GFX& d = M5Cardputer.Display;
        // key (dim)
        d.setTextColor(C_TEXT_DIM, C_BG);
        d.setCursor(4, y);
        d.print(key);
        // value (coloured, right of key)
        int kw = (strlen(key) + 1) * CHAR_W + 4;
        d.setTextColor(val_col, C_BG);
        // truncate val to available width
        int avail = (SCREEN_W - kw - 2) / CHAR_W;
        char trunc[64];
        strncpy(trunc, val, min((int)sizeof(trunc)-1, avail));
        trunc[min((int)sizeof(trunc)-1, avail)] = '\0';
        d.setCursor(kw, y);
        d.print(trunc);
    }

    static uint8_t _battPct(uint16_t mv) {
        if (mv >= 4200) return 100;
        if (mv <= 3500) return 0;
        return (uint8_t)((mv - 3500) * 100UL / 700UL);
    }
};
