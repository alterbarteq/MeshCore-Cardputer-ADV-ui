#pragma once
#include "ScreenBase.h"
#include "ScreenNodes.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <M5Cardputer.h>
#include <math.h>

// ScreenMap — retro dot-grid overlay with a real OpenStreetMap tile fetched
// over WiFi when credentials + a GPS fix are available. Falls back to a
// phosphor dot-grid (no real map imagery) if WiFi/network aren't available,
// so the screen is never blank.
//
// This board (Cardputer ADV / Stamp-S3A / ESP32-S3FN8) has NO PSRAM despite
// what the board name suggests — ESP.getPsramSize() reads 0. A decoded
// 256x256 RGB565 tile (131KB) will not fit in the ~130KB of internal SRAM
// left free once WiFi/mesh/UI are running. So instead of decoding into an
// offscreen sprite, we cache only the compressed PNG bytes (~10-40KB, fits
// comfortably) and decode straight onto the real display on every redraw —
// LovyanGFX's PNG decoder streams scanline-by-scanline, it doesn't need a
// full framebuffer to do that.
//
// Tile math: standard Web Mercator slippy-map tiles (256x256 px),
// https://tile.openstreetmap.org/{z}/{x}/{y}.png

#define TILE_PX        256
#define DOT_SELF       4
#define DOT_NODE       3
#define TILE_MAX_BYTES 60000    // sane upper bound for a compressed street tile
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define TILE_FETCH_TIMEOUT_MS   8000

enum class MapStatus { NO_WIFI, CONNECTING, FETCHING, OK, ERR, NO_GPS };

static const char* const MAP_STATUS_LABELS[] = {
    "BRAK WIFI", "LACZENIE...", "POBIERANIE...", "OK", "BLAD", "BRAK GPS"
};

class ScreenMap : public ScreenBase {
public:
    // Call before first use (and again whenever creds change in Settings)
    void setWiFi(const char* ssid, const char* pass) {
        bool changed = strcmp(_ssid, ssid) != 0 || strcmp(_pass, pass) != 0;
        strncpy(_ssid, ssid, sizeof(_ssid) - 1); _ssid[sizeof(_ssid) - 1] = '\0';
        strncpy(_pass, pass, sizeof(_pass) - 1); _pass[sizeof(_pass) - 1] = '\0';
        if (changed) {
            _wifi_attempted = false;
            _invalidateTile();
            _need_redraw = true;
        }
    }

    void setOwnPos(int32_t lat6, int32_t lon6) {
        bool had_gps = _have_gps;
        int32_t old_lat = _own_lat, old_lon = _own_lon;
        _own_lat = lat6; _own_lon = lon6; _have_gps = true;

        if (!had_gps) {
            _invalidateTile();
        } else {
            int tx, ty, otx, oty;
            _latLonToTile(lat6, lon6, _zoom, tx, ty);
            _latLonToTile(old_lat, old_lon, _zoom, otx, oty);
            if (tx != otx || ty != oty) _invalidateTile();
        }
        _need_redraw = true;
    }

    void setNodes(const NodeEntry* entries, int count) {
        _nodes = entries; _node_count = count; _need_redraw = true;
    }

    void onEnter() override {
        _need_redraw = true;
        if (!_ssid[0]) {
            Serial.println("[MAP] Brak skonfigurowanego WiFi (Ustawienia -> WiFi SSID)");
        } else if (WiFi.status() != WL_CONNECTED && !_wifi_attempted) {
            Serial.printf("[MAP] Laczenie z WiFi: %s\n", _ssid);
            _status = MapStatus::CONNECTING;
            WiFi.begin(_ssid, _pass);
            _wifi_connect_start = millis();
            _wifi_attempted = true;
        }
    }

    void onLeave() override {
        // keep WiFi connected between tab visits for faster subsequent loads
    }

    ~ScreenMap() { if (_png_buf) free(_png_buf); }

    // ── draw ─────────────────────────────────────────────────────────────────
    void draw() override {
        _checkWiFi();
        if (!_need_redraw && !_tile_dirty) return;
        _need_redraw = false;
        _tile_dirty  = false;

        M5GFX& d = M5Cardputer.Display;
        clearContent();

        if (!_have_gps) {
            _status = MapStatus::NO_GPS;
            textLine(4, CONTENT_Y + 30, "Brak sygnalu GPS.", C_WARN);
            textLine(4, CONTENT_Y + 42, "Mapa wymaga pozycji GPS.", C_TEXT_DIM);
            return;
        }

        int cx = SCREEN_W / 2;
        int cy = CONTENT_Y + CONTENT_H / 2;

        if (_tile_ready && _png_buf) {
            int off_x, off_y;
            _pixelOffsetInTile(_own_lat, _own_lon, _zoom, off_x, off_y);
            // Decode straight to the real screen (no PSRAM for an offscreen
            // buffer) - clip to the content area so it can't paint over the
            // top/bottom bars.
            d.setClipRect(0, CONTENT_Y, SCREEN_W, CONTENT_H);
            if (!d.drawPng(_png_buf, _png_len, cx - off_x, cy - off_y)) {
                Serial.println("[MAP] drawPng() (redraw) nie powiodlo sie - kasuje kafelek");
                _tile_ready = false;
            }
            d.clearClipRect();
        }
        if (!_tile_ready) {
            _drawGrid(d);
        }

        _drawNodes(d, cx, cy);

        // Own position dot (accent, pulsing ring)
        d.fillCircle(cx, cy, DOT_SELF, C_ACCENT);
        d.drawCircle(cx, cy, DOT_SELF + 2, C_ACCENT);

        _drawStatusOverlay(d);
    }

    // ── key handling ─────────────────────────────────────────────────────────
    bool onKey(Keyboard_Class::KeysState& ks) override {
        bool consumed = false;
        if (ks.fn) {
            if (ks.up   && _zoom < 17) { _zoom++; _invalidateTile(); consumed = true; }
            if (ks.down && _zoom > 10) { _zoom--; _invalidateTile(); consumed = true; }
        }
        if (consumed) _need_redraw = true;
        return consumed;
    }

private:
    // WiFi creds
    char _ssid[33] = {};
    char _pass[65] = {};
    bool _wifi_attempted = false;
    unsigned long _wifi_connect_start = 0;

    // GPS state
    int32_t _own_lat = 0, _own_lon = 0;
    bool    _have_gps = false;

    // Node overlay
    const NodeEntry* _nodes = nullptr;
    int _node_count = 0;

    // Map state
    int _zoom = 15;
    MapStatus _status = MapStatus::NO_WIFI;
    bool _need_redraw = true;
    bool _tile_dirty  = false;
    bool _tile_ready  = false;
    bool _fetching    = false;

    // Cached compressed PNG bytes for the current tile (regular heap - this
    // board has no PSRAM). Re-decoded to the screen on every redraw.
    uint8_t* _png_buf = nullptr;
    int      _png_len = 0;

    // ── helpers ───────────────────────────────────────────────────────────────

    void _checkWiFi() {
        if (_ssid[0] == '\0') return;

        if (WiFi.status() == WL_CONNECTED) {
            if (_status == MapStatus::CONNECTING || _status == MapStatus::NO_WIFI) {
                Serial.printf("[MAP] WiFi polaczone, IP: %s\n", WiFi.localIP().toString().c_str());
                _status = MapStatus::OK;
                _tile_dirty = true;
            }
            if (!_tile_ready && !_fetching && _have_gps) _tryFetchTile();
        } else if (_status == MapStatus::CONNECTING) {
            if (millis() - _wifi_connect_start > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("[MAP] Timeout laczenia z WiFi");
                _status = MapStatus::NO_WIFI;
                _tile_dirty = true;
            }
        } else {
            _status = MapStatus::NO_WIFI;
        }
    }

    void _invalidateTile() {
        _tile_ready = false;
        _tile_dirty = true;
    }

    // Convert lat/lon (6-decimal int) + zoom to OSM tile x,y (floor)
    static void _latLonToTile(int32_t lat6, int32_t lon6, int zoom, int& tx, int& ty) {
        double fx, fy;
        _latLonToTileFrac(lat6, lon6, zoom, fx, fy);
        tx = (int)fx;
        ty = (int)fy;
    }

    // Continuous (fractional) tile-space coordinates — usable directly for
    // pixel-accurate placement of anything relative to anything else at a
    // given zoom, without needing to care about tile boundaries.
    static void _latLonToTileFrac(int32_t lat6, int32_t lon6, int zoom, double& fx, double& fy) {
        double lat = lat6 / 1e6, lon = lon6 / 1e6;
        double lat_rad = lat * M_PI / 180.0;
        int n = 1 << zoom;
        fx = (lon + 180.0) / 360.0 * n;
        fy = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n;
    }

    // Pixel offset of a lat/lon within its own tile (0..TILE_PX range)
    static void _pixelOffsetInTile(int32_t lat6, int32_t lon6, int zoom, int& px, int& py) {
        double fx, fy;
        _latLonToTileFrac(lat6, lon6, zoom, fx, fy);
        px = (int)((fx - floor(fx)) * TILE_PX);
        py = (int)((fy - floor(fy)) * TILE_PX);
    }

    void _tryFetchTile() {
        if (_fetching) return;

        int tx, ty;
        _latLonToTile(_own_lat, _own_lon, _zoom, tx, ty);

        _fetching = true;
        _status = MapStatus::FETCHING;
        _need_redraw = true;

        char url[128];
        snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", _zoom, tx, ty);
        Serial.printf("[MAP] Pobieram kafelek: %s (wolny heap: %u)\n", url, (unsigned)ESP.getFreeHeap());

        bool ok = false;
        HTTPClient http;
        http.setTimeout(TILE_FETCH_TIMEOUT_MS);
        if (http.begin(url)) {
            http.addHeader("User-Agent", "MeshCore-Cardputer-ADV-retro-ui/1.0");
            int code = http.GET();
            Serial.printf("[MAP] HTTP GET -> %d\n", code);
            if (code == 200) {
                int len = http.getSize();
                if (len > 0 && len < TILE_MAX_BYTES) {
                    uint8_t* buf = (uint8_t*)malloc(len);
                    if (buf) {
                        WiFiClient* stream = http.getStreamPtr();
                        int read = 0;
                        unsigned long t0 = millis();
                        while (read < len && millis() - t0 < TILE_FETCH_TIMEOUT_MS) {
                            int avail = stream->available();
                            if (avail > 0) {
                                int chunk = stream->readBytes(buf + read, min(avail, len - read));
                                read += chunk;
                            } else {
                                delay(1);
                            }
                        }
                        Serial.printf("[MAP] Odebrano %d / %d bajtow\n", read, len);
                        if (read == len) {
                            if (_png_buf) free(_png_buf);
                            _png_buf = buf;
                            _png_len = len;
                            ok = true;
                        } else {
                            free(buf);
                        }
                    } else {
                        Serial.println("[MAP] malloc() na bufor PNG nie powiodlo sie");
                    }
                } else {
                    Serial.printf("[MAP] Nieprawidlowy rozmiar odpowiedzi: %d\n", len);
                }
            }
            http.end();
        }

        _fetching = false;
        _tile_ready = ok;
        _status = ok ? MapStatus::OK : MapStatus::ERR;
        _tile_dirty  = true;
        _need_redraw = true;
    }

    void _drawGrid(M5GFX& d) {
        for (int x = 0; x < SCREEN_W; x += 16)
            d.drawFastVLine(x, CONTENT_Y, CONTENT_H, C_TEXT_DIM);
        for (int y = CONTENT_Y; y < CONTENT_Y + CONTENT_H; y += 16)
            d.drawFastHLine(0, y, SCREEN_W, C_TEXT_DIM);
    }

    void _drawNodes(M5GFX& d, int cx, int cy) {
        if (!_nodes) return;
        double own_fx, own_fy;
        _latLonToTileFrac(_own_lat, _own_lon, _zoom, own_fx, own_fy);

        for (int i = 0; i < _node_count; i++) {
            const NodeEntry& n = _nodes[i];
            if (n.gps_lat == 0 && n.gps_lon == 0) continue;

            double n_fx, n_fy;
            _latLonToTileFrac(n.gps_lat, n.gps_lon, _zoom, n_fx, n_fy);
            int sx = cx + (int)((n_fx - own_fx) * TILE_PX);
            int sy = cy + (int)((n_fy - own_fy) * TILE_PX);
            if (sx < 0 || sx >= SCREEN_W || sy < CONTENT_Y || sy >= CONTENT_Y + CONTENT_H) continue;

            uint16_t col = (n.type == ADV_TYPE_REPEATER) ? C_WARN : C_TEXT;
            d.fillCircle(sx, sy, DOT_NODE, col);
            char lbl[5]; strncpy(lbl, n.name, 4); lbl[4] = '\0';
            d.setTextSize(FONT_SM);
            d.setTextColor(col, C_BG);
            d.setCursor(sx + DOT_NODE + 1, sy - 3);
            d.print(lbl);
        }
    }

    void _drawStatusOverlay(M5GFX& d) {
        char zbuf[12]; snprintf(zbuf, sizeof(zbuf), "z:%d", _zoom);
        textLine(2, CONTENT_Y + CONTENT_H - LINE_H - 1, zbuf, C_TEXT_DIM, C_BG);

        const char* st = MAP_STATUS_LABELS[(int)_status];
        int sx = SCREEN_W - (int)strlen(st) * CHAR_W - 2;
        uint16_t col = (_status == MapStatus::OK) ? C_TEXT_MID : C_WARN;
        textLine(sx, CONTENT_Y + CONTENT_H - LINE_H - 1, st, col, C_BG);
    }
};
