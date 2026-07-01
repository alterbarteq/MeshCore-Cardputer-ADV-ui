#pragma once
#include <SD.h>
#include <SPI.h>
#include "target.h"   // extern SPIClass spi; — resolved via the board variant's include path, same as UITaskRetro.cpp

// Shared SD-card mount helper, used by both ScreenMap (map tiles) and
// ScreenChat (emoji glyphs) so the pin setup only lives in one place.
//
// IMPORTANT: this board's built-in microSD slot shares its physical
// SCK/MISO/MOSI lines with the Cap LoRa module (only CS differs — LoRa
// NSS=5, SD CS=12) — see P_LORA_SCLK/MISO/MOSI in
// variants/m5stack_cardputer/platformio.ini, all identical to the SD pins
// below. The radio already calls spi.begin(SCK, MISO, MOSI) once at boot
// (RadioLib's std_init, see CustomSX1262.h) — SD.begin() MUST reuse that
// same `spi` object (declared extern in target.h), not a second SPIClass
// instance. Two separate SPIClass objects driving the same physical pins is
// two hardware SPI peripherals fighting over one bus: mesh sending/receiving
// broke within about a minute of enabling SD until this was fixed
// (2026-07-01) — not a "no token received" mount failure, an active bus
// conflict with the radio.
#define SD_SPI_HZ 25000000

// Attempts the mount at most once per boot (cheap to call repeatedly after
// that — just returns the cached result).
inline bool ensureSDCard() {
    static bool attempted = false;
    static bool ready     = false;
    if (attempted) return ready;
    attempted = true;
#ifdef PIN_SD_CS
    // No SPI.begin() here — the radio's spi.begin() at boot already set up
    // these exact pins; re-configuring them would be the bug we just fixed.
    ready = SD.begin(PIN_SD_CS, spi, SD_SPI_HZ);
    Serial.println(ready ? "[SD] Karta zamontowana" : "[SD] Nie udalo sie zamontowac karty");
#else
    Serial.println("[SD] PIN_SD_CS nie zdefiniowany dla tego wariantu plytki");
#endif
    return ready;
}
