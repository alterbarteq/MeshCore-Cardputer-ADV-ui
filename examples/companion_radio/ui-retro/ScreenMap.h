#pragma once
#include "ScreenBase.h"
#include "ScreenNodes.h"
#include <SD.h>
#include <M5Cardputer.h>
#include <math.h>

// ScreenMap — retro dot-grid overlay with a real OpenStreetMap tile read
// from a microSD card, with a phosphor dot-grid fallback if no card/tile is
// available, so the screen is never blank.
//
// WHY SD AND NOT WIFI: this board (Cardputer ADV / Stamp-S3A / ESP32-S3FN8)
// has no PSRAM, and fetching tiles live over HTTPS needs more free heap for
// the TLS handshake (mbedTLS) than is left once mesh+WiFi+BLE+UI are
// running (~20KB free vs 30-40KB+ needed) — confirmed via serial log
// ("SSL - Memory allocation failed") when this screen tried HTTPClient.
// Reading a file from SD needs no TLS and barely any RAM.
//
// Tile convention: standard Web Mercator slippy-map tiles (256x256 px PNG),
// stored on the SD card as /maptiles/{z}/{x}/{y}.png — the same directory
// layout most offline-map tools use, so tiles downloaded on a computer (e.g.
// from tile.openstreetmap.org, respecting their usage policy) for the area
// of interest can just be copied onto the card as-is.

#define TILE_PX        256
#define DOT_SELF       4
#define DOT_NODE       3
#define TILE_MAX_BYTES 60000    // sane upper bound for a compressed street tile
#define SD_SPI_HZ      25000000

enum class MapStatus { NO_SD, NOT_FOUND, OK, ERR, NO_GPS };

static const char* const MAP_STATUS_LABELS[] = {
    "BRAK KARTY SD", "BRAK KAFELKA", "OK", "BLAD", "BRAK GPS"
};

class ScreenMap : public ScreenBase {
public:
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
        _ensureSD();
    }

    void onLeave() override {}

    ~ScreenMap() { if (_png_buf) free(_png_buf); }

    // ── draw ─────────────────────────────────────────────────────────────────
    void draw() override {
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

        if (_sd_ready && !_tile_ready) _tryLoadTile();

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
                Serial.println("[MAP] drawPng() nie powiodlo sie - kasuje kafelek");
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
    bool _sd_ready      = false;
    bool _sd_attempted  = false;

    // GPS state
    int32_t _own_lat = 0, _own_lon = 0;
    bool    _have_gps = false;

    // Node overlay
    const NodeEntry* _nodes = nullptr;
    int _node_count = 0;

    // Map state
    int _zoom = 15;
    MapStatus _status = MapStatus::NO_SD;
    bool _need_redraw = true;
    bool _tile_dirty  = false;
    bool _tile_ready  = false;

    // Cached compressed PNG bytes for the current tile, read from SD.
    // Re-decoded to the screen on every redraw (cheap, local, no PSRAM).
    uint8_t* _png_buf = nullptr;
    int      _png_len = 0;

    // ── helpers ───────────────────────────────────────────────────────────────

    void _ensureSD() {
        if (_sd_ready || _sd_attempted) return;
        _sd_attempted = true;
#ifdef PIN_SD_CS
        if (SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ)) {
            _sd_ready = true;
            Serial.println("[MAP] Karta SD zamontowana");
        } else {
            Serial.println("[MAP] Nie udalo sie zamontowac karty SD");
            _status = MapStatus::NO_SD;
        }
#else
        Serial.println("[MAP] PIN_SD_CS nie zdefiniowany dla tego wariantu plytki");
#endif
        _tile_dirty = true;
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

    void _tryLoadTile() {
        if (!_sd_ready) { _status = MapStatus::NO_SD; return; }

        int tx, ty;
        _latLonToTile(_own_lat, _own_lon, _zoom, tx, ty);

        char path[48];
        snprintf(path, sizeof(path), "/maptiles/%d/%d/%d.png", _zoom, tx, ty);

        File f = SD.open(path, FILE_READ);
        if (!f) {
            Serial.printf("[MAP] Brak kafelka na SD: %s\n", path);
            _status = MapStatus::NOT_FOUND;
            return;
        }

        int len = f.size();
        if (len <= 0 || len > TILE_MAX_BYTES) {
            Serial.printf("[MAP] Nieprawidlowy rozmiar pliku %s (%d bajtow)\n", path, len);
            f.close();
            _status = MapStatus::ERR;
            return;
        }

        uint8_t* buf = (uint8_t*)malloc(len);
        if (!buf) {
            Serial.println("[MAP] malloc() na bufor PNG nie powiodlo sie");
            f.close();
            _status = MapStatus::ERR;
            return;
        }

        int read = f.read(buf, len);
        f.close();

        if (read != len) {
            Serial.printf("[MAP] Odczytano tylko %d / %d bajtow z %s\n", read, len, path);
            free(buf);
            _status = MapStatus::ERR;
            return;
        }

        if (_png_buf) free(_png_buf);
        _png_buf = buf;
        _png_len = len;
        _tile_ready = true;
        _status = MapStatus::OK;
        Serial.printf("[MAP] Wczytano kafelek: %s (%d bajtow)\n", path, len);
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
