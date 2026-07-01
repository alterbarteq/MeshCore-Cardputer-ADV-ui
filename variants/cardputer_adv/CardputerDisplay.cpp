#include "CardputerDisplay.h"

#include "font.h"
#include "globals.h"

#include <MeshCore.h>

static void stripDiacritics(const char* src, char* dst, size_t dstLen) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dstLen; si++) {
        uint8_t c = (uint8_t)src[si];
        if (c < 0x80) {
            dst[di++] = src[si];
            continue;
        }
        uint8_t next = src[si + 1] ? (uint8_t)src[si + 1] : 0;
        const char* rep = nullptr;
        if (c == 0xC4 && next) {
            switch (next) {
                case 0x80: rep = "A"; break; // Ā
                case 0x81: rep = "a"; break; // ā
                case 0x82: rep = "A"; break; // Ă
                case 0x83: rep = "a"; break; // ă
                case 0x84: rep = "A"; break; // Ą
                case 0x85: rep = "a"; break; // ą
                case 0x86: rep = "C"; break; // Ć
                case 0x87: rep = "c"; break; // ć
                case 0x8C: rep = "C"; break; // Č
                case 0x8D: rep = "c"; break; // č
                case 0x8E: rep = "D"; break; // Ď
                case 0x8F: rep = "d"; break; // ď
                case 0x98: rep = "E"; break; // Ę
                case 0x99: rep = "e"; break; // ę
                case 0x9A: rep = "E"; break; // Ě
                case 0x9B: rep = "e"; break; // ě
            }
        } else if (c == 0xC5 && next) {
            switch (next) {
                case 0x81: rep = "L"; break; // Ł
                case 0x82: rep = "l"; break; // ł
                case 0x83: rep = "N"; break; // Ń
                case 0x84: rep = "n"; break; // ń
                case 0x90: rep = "O"; break; // Ő
                case 0x91: rep = "o"; break; // ő
                case 0x98: rep = "R"; break; // Ř
                case 0x99: rep = "r"; break; // ř
                case 0x9A: rep = "S"; break; // Ś
                case 0x9B: rep = "s"; break; // ś
                case 0xA0: rep = "S"; break; // Š
                case 0xA1: rep = "s"; break; // š
                case 0xA4: rep = "T"; break; // Ť
                case 0xA5: rep = "t"; break; // ť
                case 0xB0: rep = "U"; break; // Ű
                case 0xB1: rep = "u"; break; // ű
                case 0xB9: rep = "Z"; break; // Ź
                case 0xBA: rep = "z"; break; // ź
                case 0xBB: rep = "Z"; break; // Ż
                case 0xBC: rep = "z"; break; // ż
                case 0xBD: rep = "Z"; break; // Ž
                case 0xBE: rep = "z"; break; // ž
            }
        } else if (c == 0xC3 && next) {
            switch (next) {
                case 0x80: case 0x81: case 0x82: case 0x83:
                case 0x84: case 0x85: rep = "A"; break; // À-Å
                case 0x86: rep = "AE"; break; // Æ
                case 0x87: rep = "C"; break;  // Ç
                case 0x88: case 0x89: case 0x8A: case 0x8B: rep = "E"; break; // È-Ë
                case 0x8C: case 0x8D: case 0x8E: case 0x8F: rep = "I"; break; // Ì-Ï
                case 0x91: rep = "N"; break;  // Ñ
                case 0x92: case 0x93: case 0x94: case 0x95:
                case 0x96: case 0x98: rep = "O"; break; // Ò-Ö, Ø
                case 0x99: case 0x9A: case 0x9B: case 0x9C: rep = "U"; break; // Ù-Ü
                case 0x9D: rep = "Y"; break;  // Ý
                case 0x9F: rep = "ss"; break; // ß
                case 0xA0: case 0xA1: case 0xA2: case 0xA3:
                case 0xA4: case 0xA5: rep = "a"; break; // à-å
                case 0xA6: rep = "ae"; break; // æ
                case 0xA7: rep = "c"; break;  // ç
                case 0xA8: case 0xA9: case 0xAA: case 0xAB: rep = "e"; break; // è-ë
                case 0xAC: case 0xAD: case 0xAE: case 0xAF: rep = "i"; break; // ì-ï
                case 0xB1: rep = "n"; break;  // ñ
                case 0xB2: case 0xB3: case 0xB4: case 0xB5:
                case 0xB6: case 0xB8: rep = "o"; break; // ò-ö, ø
                case 0xB9: case 0xBA: case 0xBB: case 0xBC: rep = "u"; break; // ù-ü
                case 0xBD: case 0xBF: rep = "y"; break; // ý, ÿ
            }
        }
        if (rep) {
            dst[di++] = rep[0];
            if (rep[1] && di + 1 < dstLen) dst[di++] = rep[1];
            si++; // skip second UTF-8 byte
        } else {
            // skip entire multi-byte sequence
            if      ((c & 0xE0) == 0xC0) si += 1;
            else if ((c & 0xF0) == 0xE0) si += 2;
            else if ((c & 0xF8) == 0xF0) si += 3;
        }
    }
    dst[di] = '\0';
}

int32_t emoji_draw_callback(lgfx::LGFXBase *gfx, int32_t x, int32_t y, uint32_t code, int32_t font_height) {
  int w = gfx->textWidth("O");
  int h = gfx->fontHeight();
  gfx->drawRect(x, y + (h - w - w / 2), w, w, TFT_DARKGRAY);
  return w;
}

void CardputerDisplay::updateFontYAdvance() {
  lgfx::FontMetrics metrics;
  LCD.getFont()->getDefaultMetric(&metrics);
  _fontYAdvance = metrics.y_advance;
}

bool CardputerDisplay::begin() {
  LCD.setBaseColor(TFT_BLACK);

  bool success = LCD.begin();

  if (!success) {
    return false;
  }

  LCD.setTextColor(TFT_WHITE, TFT_BLACK);
  LCD.loadFont(DejaVuSans_11);
  LCD.setEmojiCallback(emoji_draw_callback);

  updateFontYAdvance();
  return true;
}

void CardputerDisplay::tryLoadUserFont() {
#if USE_SD_CARD
  // fixme: Do not use, redraw is incredibly slow for some reason
  if (SD.exists(USER_FONT_NAME)) {
    MESH_DEBUG_PRINTLN("User font found");
    if (LCD.loadFont(SD, USER_FONT_NAME)) {
      MESH_DEBUG_PRINTLN("%s loaded", USER_FONT_NAME);
      updateFontYAdvance();
    } else {
      MESH_DEBUG_PRINTLN("%s load failed", USER_FONT_NAME);
    }
  }
#endif
}

void CardputerDisplay::turnOn() {
  _isOn = true;
  LCD.wakeup();
}

void CardputerDisplay::turnOff() {
  _isOn = false;
  LCD.sleep();
}

void CardputerDisplay::clear() {
  LCD.clear();
}

void CardputerDisplay::startFrame(Color bkg) {
  LCD.startWrite();
  LCD.clear(convertColor(bkg));
}

void CardputerDisplay::endFrame() {
  LCD.endWrite();
}

int32_t CardputerDisplay::getFontHeight() const {
  return LCD.fontHeight();
}

int16_t CardputerDisplay::getFontLineHeight() const {
  return _fontYAdvance;
}

void CardputerDisplay::setTextSize(int sz) {
  LCD.setTextSize(sz);
}

void CardputerDisplay::setColor(Color c) {
  _lastColor = convertColor(c);
  LCD.setColor(_lastColor);
  LCD.setTextColor(_lastColor);
}

void CardputerDisplay::setCursor(int x, int y) {
  LCD.setCursor(x, y);
}

void CardputerDisplay::print(const char *str) {
  char buf[512];
  stripDiacritics(str, buf, sizeof(buf));
  LCD.print(buf);
}

void CardputerDisplay::fillRect(int x, int y, int w, int h) {
  LCD.fillRect(x, y, w, h);
}

void CardputerDisplay::drawRect(int x, int y, int w, int h) {
  LCD.drawRect(x, y, w, h);
}

void CardputerDisplay::drawXbm(int x, int y, const uint8_t *bits, int w, int h) {
  LCD.drawBitmap(x, y, bits, w, h, _lastColor);
}

uint16_t CardputerDisplay::getTextWidth(const char *str) {
  char buf[512];
  stripDiacritics(str, buf, sizeof(buf));
  return LCD.textWidth(buf);
}
