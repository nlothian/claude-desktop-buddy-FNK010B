#include "board/board.h"
#include "board/touch.h"
#include <LittleFS.h>
#include <stdarg.h>
#include "ble_bridge.h"
#include "data.h"
#include "buddy.h"

TFT_eSprite spr = TFT_eSprite(&M5.Lcd);

// Advertise as "Claude-XXXX" (last two BT MAC bytes) so multiple sticks
// in one room are distinguishable in the desktop picker. Name persists in
// btName for the BLUETOOTH info page.
static char btName[16] = "Claude";

#include "character.h"
#include "stats.h"

// Bring up BLE under a salt-derived identity. salt 0 == the factory Bluetooth
// address, so ordinary reboots keep the same address and the host reuses its
// bond. The Re-pair action bumps the salt (see rotatePairIdentity) so we come
// back as a device the host has never seen — macOS keys its bond on our
// address, not the advertised name, and gives no app-level way to evict it, so
// rotating the address is the only reliable re-pair escape.
static void startBt() {
  uint8_t base[6] = {0};
  esp_efuse_mac_get_default(base);
  base[5] ^= pairSaltLoad();
  esp_base_mac_addr_set(base);

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

// Rotate to a fresh BLE identity and reboot into it. Used by the Re-pair
// setting: bump the salt (skipping 0 so we never roll back onto the factory
// address a host might still have bonded), drop bonds + the live link, then
// restart so startBt() re-derives the address. The host then discovers a new
// Claude-XXXX and pairs cleanly with a fresh passkey.
static void rotatePairIdentity() {
  uint8_t next = pairSaltLoad() + 1;
  if (next == 0) next = 1;
  pairSaltSave(next);
  bleClearBonds();
  delay(300);          // let the bond-clear/ack flush and the confirm beep play
  ESP.restart();
}

// Reset: erase all stored user data and return the Bluetooth identity to the
// factory default. Clearing the NVS namespace drops the pairing salt back to 0,
// so startBt() re-derives the original factory Claude-XXXX name on the reboot
// below; bleClearBonds() evicts the host's now-stale bond so it re-pairs
// against the restored identity. Like rotatePairIdentity() this restarts and
// never returns — the caller plays a confirmation beep first.
static void factoryReset() {
  userDataErase();
  bleClearBonds();
  delay(300);          // let the bond-clear/ack flush and the confirm beep play
  ESP.restart();
}
const int W = 320, H = 240;
const int CX = W / 2;
const int CY_BASE = H / 2;

// Home-screen pane layout. Pet (rendered by character/buddy) lives in the
// left pane around its hardcoded BUDDY_X_CENTER=67. The right pane shows
// always-visible stats; transcript sits along the bottom of the left pane;
// a persistent taskbar runs along the very bottom across the full width.
static constexpr int HOME_PET_W       = W / 2;              // 160
static constexpr int HOME_STATS_X     = W / 2;              // 160
static constexpr int HOME_STATS_W     = W - HOME_STATS_X;   // 160
static constexpr int TASKBAR_H        = 36;
static constexpr int TASKBAR_Y        = H - TASKBAR_H;       // 204
static constexpr int TASKBAR_ICONS    = 3;
// Asymmetric widths: HOME + PET share the left half (80 each), MENU takes the
// right half (160). Wider MENU also dodges the raw-panel edge dead band.
static constexpr int TASKBAR_ICON_W[TASKBAR_ICONS] = { 80, 80, 160 };
static constexpr int TASKBAR_ICON_X[TASKBAR_ICONS] = { 0, 80, 160 };

// Colors used across multiple UI surfaces
const uint16_t HOT   = 0xFA20;   // red-orange: warnings, impatience, deny
const uint16_t PANEL = 0x2104;   // overlay panel background

enum PersonaState { P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART };
const char* stateNames[] = { "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart" };

TamaState    tama;
PersonaState baseState   = P_SLEEP;
PersonaState activeState = P_SLEEP;
uint32_t     oneShotUntil = 0;
uint32_t     lastShakeCheck = 0;
float        accelBaseline = 1.0f;
unsigned long t = 0;

uint8_t brightLevel = 4;           // 0..4 → ScreenBreath 20..100
bool    btnALong    = false;

enum DisplayMode { DISP_NORMAL, DISP_PET, DISP_COUNT };
uint8_t displayMode = DISP_NORMAL;
uint8_t msgScroll = 0;
uint16_t lastLineGen = 0;
char     lastPromptId[40] = "";
uint32_t lastInteractMs = 0;
bool     dimmed = false;
bool     screenOff = false;
bool     swallowBtnA = false;
bool     swallowBtnB = false;
bool     buddyMode = false;
bool     gifAvailable = false;
const uint8_t SPECIES_GIF = 0xFF;   // species NVS sentinel: use the installed GIF

// Cycle GIF (if installed) → ASCII species 0..N-1 → GIF. Persisted to the
// existing "species" NVS key; 0xFF means GIF mode.
static void nextPet() {
  uint8_t n = buddySpeciesCount();
  if (!buddyMode) {                          // GIF → species 0
    buddyMode = true;
    buddySetSpeciesIdx(0);
    speciesIdxSave(0);
  } else if (buddySpeciesIdx() + 1 >= n && gifAvailable) {  // last species → GIF
    buddyMode = false;
    speciesIdxSave(SPECIES_GIF);
  } else {                                   // species i → species i+1
    buddyNextSpecies();
  }
  characterInvalidate();
  if (buddyMode) buddyInvalidate();
}
uint32_t wakeTransitionUntil = 0;
const uint32_t SCREEN_OFF_MS = 30000;

bool     napping = false;
uint32_t napStartMs = 0;
uint32_t promptArrivedMs = 0;

// Face-down = Z-axis dominant and negative. Debounced so a toss doesn't count.
static bool isFaceDown() {
  float ax, ay, az;
  M5.Imu.getAccelData(&ax, &ay, &az);
  return az < -0.7f && fabsf(ax) < 0.4f && fabsf(ay) < 0.4f;
}

static void applyBrightness() { M5.Axp.ScreenBreath(20 + brightLevel * 20); }

static void wake() {
  lastInteractMs = millis();
  if (screenOff) {
    M5.Axp.SetLDO2(true);
    applyBrightness();
    screenOff = false;
    wakeTransitionUntil = millis() + 12000;
  }
  if (dimmed) { applyBrightness(); dimmed = false; }
}
bool     responseSent = false;

// Pleasant beep pitches — C-major chord tones in the soothing 500-1000 Hz band.
static constexpr uint16_t NOTE_C5 = 523;
static constexpr uint16_t NOTE_E5 = 659;
static constexpr uint16_t NOTE_G5 = 784;

static void beep(uint16_t freq, uint16_t dur) {
  if (settings().sound) M5.Beep.tone(freq, dur);
}

static void sendCmd(const char* json) {
  Serial.println(json);
  size_t n = strlen(json);
  bleWrite((const uint8_t*)json, n);
  bleWrite((const uint8_t*)"\n", 1);
}
void applyDisplayMode() {
  // Pet sprite stays at full scale across home and pet screens; peek mode is
  // only used by the charging-clock path.
  characterSetPeek(false);
  buddySetPeek(false);
  // Clear the whole sprite on mode switch. drawInfo/drawPet clear their
  // own regions when they run, but when you switch FROM info/pet TO normal,
  // those functions stop running and their stale pixels stay behind. Full
  // clear is cheap and guarantees no leftovers between modes.
  spr.fillSprite(0x0000);
  characterInvalidate();  // redraws character on next tick (text mode path)
}

bool    settingsOpen = false;
uint8_t settingsSel  = 0;

// Destructive settings (Re-pair, Reset) don't fire on the tile tap — they arm a
// confirmation dialog that draws over the panel and only acts on "Confirm".
// CONFIRM_NONE means no dialog is up. Only ever set while settingsOpen is true.
enum ConfirmAction : uint8_t { CONFIRM_NONE = 0, CONFIRM_REPAIR, CONFIRM_RESET };
static ConfirmAction confirmPending = CONFIRM_NONE;

const char* settingsItems[] = { "brightness", "sound", "bluetooth", "wifi", "alert", "transcript", "ascii pet", "re-pair", "reset" };
const uint8_t SETTINGS_N = 9;

static void applySetting(uint8_t idx) {
  Settings& s = settings();
  switch (idx) {
    case 0:
      brightLevel = (brightLevel + 1) % 5;
      applyBrightness();
      return;
    case 1: s.sound = !s.sound; break;
    case 2:
      // BT toggle is a stored preference only — BLE stays live. Turning
      // BLE off cleanly would require tearing down the BLE stack which
      // the Arduino BLE library doesn't do reliably.
      s.bt = !s.bt;
      break;
    case 3: s.wifi = !s.wifi; break;   // stored only — no WiFi stack linked
    case 4: s.led = !s.led; break;     // labelled "Alert" — drives the on-screen attention border
    case 5: s.hud = !s.hud; break;
    case 6: nextPet(); return;
    case 7:
      // Re-pair and Reset both reboot the device and can't be undone, so they
      // arm a confirmation dialog instead of firing on this tap. The action runs
      // only if the user then taps Confirm — see the confirm dispatch in loop().
      confirmPending = CONFIRM_REPAIR;
      beep(NOTE_E5, 60);   // soft chirp: dialog is up, awaiting confirmation
      return;
    case 8:
      confirmPending = CONFIRM_RESET;
      beep(NOTE_E5, 60);
      return;
  }
  settingsSave();
}

// Footer hint row inside a menu panel: "<downLbl> ↓  <rightLbl> →" with
// pixel triangles. Panels add MENU_HINT_H to height and call this at bottom.
const int MENU_HINT_H = 14;
static void drawMenuHints(const Palette& p, int mx, int mw, int hy,
                          const char* downLbl = "A", const char* rightLbl = "B") {
  spr.drawFastHLine(mx + 6, hy - 4, mw - 12, p.textDim);
  spr.setTextColor(p.textDim, PANEL);
  // 6px/glyph at size 1; triangle goes 4px after the label ends
  int x = mx + 8;
  spr.setCursor(x, hy); spr.print(downLbl);
  x += strlen(downLbl) * 6 + 4;
  spr.fillTriangle(x, hy + 1, x + 6, hy + 1, x + 3, hy + 6, p.textDim);
  x = mx + mw / 2 + 4;
  spr.setCursor(x, hy); spr.print(rightLbl);
  x += strlen(rightLbl) * 6 + 4;
  spr.fillTriangle(x, hy, x, hy + 6, x + 5, hy + 3, p.textDim);
}

// Settings panel — 2 columns × 5 rows of finger-sized tap tiles. Each item
// occupies one cell; cell 8 (bottom-left) is reserved for the volume slider.
// Item-to-cell placement is defined by SETT_CELL below, so a tile can be moved
// on screen without disturbing its behaviour (applySetting is keyed on the item
// index, not the cell).
static constexpr int SETT_PANEL_X = 8;
static constexpr int SETT_PANEL_W = W - 2 * SETT_PANEL_X;      // 304
static constexpr int SETT_PANEL_Y = 6;
static constexpr int SETT_PANEL_H = TASKBAR_Y - SETT_PANEL_Y - 6;  // 192
static constexpr int SETT_TITLE_H = 24;
static constexpr int SETT_COLS    = 2;
static constexpr int SETT_ROWS    = 5;
static constexpr int SETT_CELL_W  = SETT_PANEL_W / SETT_COLS;  // 152
static constexpr int SETT_CELL_H  = (SETT_PANEL_H - SETT_TITLE_H) / SETT_ROWS;  // 33

// Shortened labels so they fit the cell at size 2 (12 px / glyph).
static const char* const SETT_LABELS[SETTINGS_N] = {
  "Bright", "Sound",  "BT",  "WiFi",
  "Alert",  "HUD",    "Pet", "Re-pair",
  "Reset"
};

// Physical grid cell (0..9, 8 = volume slider) for each item, indexed by the
// item's logical index. The single source of truth for tile placement: change
// an entry to move a tile on screen, its behaviour follows. Bright/Re-pair are
// swapped (cells 0↔7) and Sound/Reset are swapped (cells 1↔9).
static const uint8_t SETT_CELL[SETTINGS_N] = { 7, 9, 2, 3, 4, 5, 6, 0, 1 };

// Returns the settings item index the tap landed on, or -1 if outside the grid.
// Reverse of SETT_CELL: find the item placed in the tapped cell. Cell 8 holds
// the volume slider (no item maps to it, handled separately), so it returns -1.
static int settingsHitCell(int x, int y) {
  if (x < SETT_PANEL_X || x >= SETT_PANEL_X + SETT_PANEL_W) return -1;
  int gy = y - (SETT_PANEL_Y + SETT_TITLE_H);
  if (gy < 0 || gy >= SETT_ROWS * SETT_CELL_H) return -1;
  int col = (x - SETT_PANEL_X) / SETT_CELL_W;
  int row = gy / SETT_CELL_H;
  int cell = row * SETT_COLS + col;   // physical cell 0..9
  for (int i = 0; i < SETTINGS_N; i++) if (SETT_CELL[i] == cell) return i;
  return -1;                          // cell 8 is the volume slider
}

// The volume slider is a tap-to-set control in cell 8 (col 0, row 4), bottom-
// left of the grid. Geometry shared between drawSettings() and volSliderHit().
static constexpr int VOL_CX      = SETT_PANEL_X + 0 * SETT_CELL_W;                  // 8
static constexpr int VOL_CY      = SETT_PANEL_Y + SETT_TITLE_H + 4 * SETT_CELL_H;   // 162
static constexpr int VOL_TRACK_X = VOL_CX + 14;
static constexpr int VOL_TRACK_W = SETT_CELL_W - 28;   // 124
static constexpr int VOL_TRACK_Y = VOL_CY + 24;
static constexpr int VOL_TRACK_H = 6;

// Map an x coordinate to a 0..100 level along the track, clamping past either
// end so a tap or drag that overshoots the track still reaches a clean 0 / 100.
static uint8_t volFromX(int x) {
  int rel = x - VOL_TRACK_X;
  if (rel < 0) rel = 0;
  if (rel > VOL_TRACK_W) rel = VOL_TRACK_W;
  return (uint8_t)((rel * 100 + VOL_TRACK_W / 2) / VOL_TRACK_W);
}

// True if (x,y) lands in the volume cell; fills *outVol (when non-null) with the
// level for that x. Used to grab the slider on the touch that begins in it.
static bool volSliderHit(int x, int y, uint8_t* outVol) {
  if (x < VOL_CX || x >= VOL_CX + SETT_CELL_W) return false;
  if (y < VOL_CY || y >= VOL_CY + SETT_CELL_H) return false;
  if (outVol) *outVol = volFromX(x);
  return true;
}

static void drawSettings() {
  const Palette& p = characterPalette();
  Settings& s = settings();

  // Panel + title bar
  spr.fillRoundRect(SETT_PANEL_X, SETT_PANEL_Y, SETT_PANEL_W, SETT_PANEL_H, 8, PANEL);
  spr.drawRoundRect(SETT_PANEL_X, SETT_PANEL_Y, SETT_PANEL_W, SETT_PANEL_H, 8, p.textDim);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  spr.setTextColor(p.text, PANEL);
  spr.drawString("SETTINGS", SETT_PANEL_X + SETT_PANEL_W / 2,
                             SETT_PANEL_Y + SETT_TITLE_H / 2);
  spr.drawFastHLine(SETT_PANEL_X + 8, SETT_PANEL_Y + SETT_TITLE_H,
                    SETT_PANEL_W - 16, p.textDim);

  bool vals[] = { s.sound, s.bt, s.wifi, s.led, s.hud };   // for indices 1..5
  for (int i = 0; i < SETTINGS_N; i++) {
    int cellIdx = SETT_CELL[i];       // placement decoupled from behaviour
    int col = cellIdx % SETT_COLS;
    int row = cellIdx / SETT_COLS;
    int cx = SETT_PANEL_X + col * SETT_CELL_W;
    int cy = SETT_PANEL_Y + SETT_TITLE_H + row * SETT_CELL_H;

    // Grid lines between cells.
    if (col > 0) spr.drawFastVLine(cx, cy + 2, SETT_CELL_H - 4, p.textDim);
    if (row > 0) spr.drawFastHLine(cx + 2, cy, SETT_CELL_W - 4, p.textDim);

    int midX = cx + SETT_CELL_W / 2;
    int midY = cy + SETT_CELL_H / 2;

    // Label, top half of cell, size 2.
    spr.setTextSize(2);
    spr.setTextColor(p.text, PANEL);
    spr.drawString(SETT_LABELS[i], midX, midY - 7);

    // Value, bottom half of cell, size 1.
    spr.setTextSize(1);
    if (i == 0) {
      spr.setTextColor(p.textDim, PANEL);
      char vb[8]; snprintf(vb, sizeof(vb), "%u/4", brightLevel);
      spr.drawString(vb, midX, midY + 9);
    } else if (i >= 1 && i <= 5) {
      bool on = vals[i - 1];
      spr.setTextColor(on ? GREEN : p.textDim, PANEL);
      spr.drawString(on ? "ON" : "off", midX, midY + 9);
    } else if (i == 6) {
      uint8_t total = buddySpeciesCount() + (gifAvailable ? 1 : 0);
      uint8_t pos   = buddyMode ? buddySpeciesIdx() + 1 : total;
      char vb[8]; snprintf(vb, sizeof(vb), "%u/%u", pos, total);
      spr.setTextColor(p.textDim, PANEL);
      spr.drawString(vb, midX, midY + 9);
    } else if (i == 7) {
      spr.setTextColor(p.textDim, PANEL);
      spr.drawString("tap", midX, midY + 9);
    } else if (i == 8) {
      spr.setTextColor(p.textDim, PANEL);
      spr.drawString("erase", midX, midY + 9);
    }
  }

  // Volume slider — cell 8 (col 0, row 4). The loop above skips it, so draw its
  // top separator here (no left line — it's column 0, against the panel border),
  // then the label, % readout, track, fill and thumb.
  {
    uint8_t vol = s.volume;
    spr.drawFastHLine(VOL_CX + 2, VOL_CY, SETT_CELL_W - 4, p.textDim);

    spr.setTextSize(2);
    spr.setTextColor(p.text, PANEL);
    spr.setTextDatum(ML_DATUM);
    spr.drawString("Vol", VOL_CX + 12, VOL_CY + 11);

    spr.setTextSize(1);
    spr.setTextColor(p.textDim, PANEL);
    spr.setTextDatum(MR_DATUM);
    char vb[8]; snprintf(vb, sizeof(vb), "%u%%", vol);
    spr.drawString(vb, VOL_CX + SETT_CELL_W - 12, VOL_CY + 11);

    spr.fillRoundRect(VOL_TRACK_X, VOL_TRACK_Y, VOL_TRACK_W, VOL_TRACK_H,
                      VOL_TRACK_H / 2, p.textDim);
    int fillW = VOL_TRACK_W * vol / 100;
    if (fillW > 0)
      spr.fillRoundRect(VOL_TRACK_X, VOL_TRACK_Y, fillW, VOL_TRACK_H,
                        VOL_TRACK_H / 2, GREEN);
    spr.fillCircle(VOL_TRACK_X + fillW, VOL_TRACK_Y + VOL_TRACK_H / 2, 4, p.text);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);
}

// Confirmation dialog for the destructive settings tiles (Re-pair, Reset): a
// centered modal over the settings panel with Cancel / Confirm buttons. Any tap
// off the Confirm button cancels, so a stray tap never wipes data or re-pairs.
static constexpr int CONF_W = 240;
static constexpr int CONF_H = 104;
static constexpr int CONF_X = (W - CONF_W) / 2;                          // 40
static constexpr int CONF_Y = (TASKBAR_Y - CONF_H) / 2;                  // 50
static constexpr int CONF_BTN_W = 100;
static constexpr int CONF_BTN_H = 32;
static constexpr int CONF_BTN_Y = CONF_Y + CONF_H - CONF_BTN_H - 8;      // 114
static constexpr int CONF_CANCEL_X  = CONF_X + 12;                       // 52
static constexpr int CONF_CONFIRM_X = CONF_X + CONF_W - CONF_BTN_W - 12; // 168

// 0 = Cancel, 1 = Confirm, -1 = neither (the dispatcher treats -1 as cancel).
static int confirmHitBtn(int x, int y) {
  if (y < CONF_BTN_Y || y >= CONF_BTN_Y + CONF_BTN_H) return -1;
  if (x >= CONF_CANCEL_X  && x < CONF_CANCEL_X  + CONF_BTN_W) return 0;
  if (x >= CONF_CONFIRM_X && x < CONF_CONFIRM_X + CONF_BTN_W) return 1;
  return -1;
}

static void drawConfirm() {
  const Palette& p = characterPalette();
  bool reset = (confirmPending == CONFIRM_RESET);
  int  midX  = CONF_X + CONF_W / 2;

  spr.fillRoundRect(CONF_X, CONF_Y, CONF_W, CONF_H, 8, PANEL);
  spr.drawRoundRect(CONF_X, CONF_Y, CONF_W, CONF_H, 8, p.text);

  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  spr.setTextColor(p.text, PANEL);
  spr.drawString(reset ? "Reset?" : "Re-pair?", midX, CONF_Y + 16);

  spr.setTextSize(1);
  spr.setTextColor(p.textDim, PANEL);
  spr.drawString(reset ? "Erase all data and"   : "Drop BLE bonds and",
                 midX, CONF_Y + 38);
  spr.drawString(reset ? "restore default name" : "reboot to re-pair",
                 midX, CONF_Y + 50);

  // Buttons: Cancel (neutral) on the left, Confirm (red — destructive) right.
  spr.drawRoundRect(CONF_CANCEL_X,  CONF_BTN_Y, CONF_BTN_W, CONF_BTN_H, 5, p.textDim);
  spr.drawRoundRect(CONF_CONFIRM_X, CONF_BTN_Y, CONF_BTN_W, CONF_BTN_H, 5, RED);
  spr.setTextSize(2);
  spr.setTextColor(p.text, PANEL);
  spr.drawString("Cancel",  CONF_CANCEL_X  + CONF_BTN_W / 2, CONF_BTN_Y + CONF_BTN_H / 2);
  spr.setTextColor(RED, PANEL);
  spr.drawString("Confirm", CONF_CONFIRM_X + CONF_BTN_W / 2, CONF_BTN_Y + CONF_BTN_H / 2);

  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);
}

// Clock orientation: gravity along the in-plane X axis means the stick is
// RTC and IMU share an I2C bus. Reading the RTC at 60fps starves the IMU;
// cache the time once per second. Mood logic and drawClock both read from here.
static RTC_TimeTypeDef _clkTm;
static RTC_DateTypeDef _clkDt;
uint32_t               _clkLastRead = 0;   // zeroed by data.h on time-sync
static bool            _onUsb       = false;
static void clockRefreshRtc() {
  if (millis() - _clkLastRead < 1000) return;
  _clkLastRead = millis();
  _onUsb = M5.Axp.GetVBusVoltage() > 4.0f;
  M5.Rtc.GetTime(&_clkTm);
  M5.Rtc.GetDate(&_clkDt);
}

// Clock face: shown when charging on USB with nothing else going on.
// Renders to the right side of the landscape sprite; pet keeps drawing on the left.
static const char* const MON[] = {
  "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char* const DOW[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

static uint8_t clockDow() { return _clkDt.WeekDay % 7; }
static void drawClock() {
  const Palette& p = characterPalette();
  char hm[6]; snprintf(hm, sizeof(hm), "%02u:%02u", _clkTm.Hours, _clkTm.Minutes);
  char ss[4]; snprintf(ss, sizeof(ss), ":%02u", _clkTm.Seconds);
  uint8_t mi = (_clkDt.Month >= 1 && _clkDt.Month <= 12) ? _clkDt.Month - 1 : 0;
  char dl[12]; snprintf(dl, sizeof(dl), "%s %s %02u", DOW[clockDow()], MON[mi], _clkDt.Date);

  // Right-pane clock: covers the same region as drawStatsPane so transitions
  // from stats → clock don't leave residual stat labels along the divider.
  int cx = HOME_STATS_X + HOME_STATS_W / 2;
  int cy = TASKBAR_Y / 2;
  spr.fillRect(HOME_STATS_X, 0, HOME_STATS_W, TASKBAR_Y, p.bg);
  spr.drawFastVLine(HOME_STATS_X, 0, TASKBAR_Y, p.textDim);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(5); spr.setTextColor(p.text, p.bg);    spr.drawString(hm, cx, cy - 36);
  spr.setTextSize(2); spr.setTextColor(p.textDim, p.bg); spr.drawString(ss, cx, cy + 6);
                                                          spr.drawString(dl, cx, cy + 34);
  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);
}

PersonaState derive(const TamaState& s) {
  if (!s.connected)            return P_IDLE;
  if (s.sessionsWaiting > 0)   return P_ATTENTION;
  if (s.recentlyCompleted)     return P_CELEBRATE;
  if (s.sessionsRunning >= 3)  return P_BUSY;
  return P_IDLE;   // connected, 0+ sessions, nothing urgent — hang out
}

void triggerOneShot(PersonaState s, uint32_t durMs) {
  activeState = s;
  oneShotUntil = millis() + durMs;
}

bool checkShake() {
  float ax, ay, az;
  M5.Imu.getAccelData(&ax, &ay, &az);
  float mag = sqrtf(ax*ax + ay*ay + az*az);
  float delta = fabsf(mag - accelBaseline);
  accelBaseline = accelBaseline * 0.95f + mag * 0.05f;
  return delta > 0.8f;
}




void drawPasskey() {
  const Palette& p = characterPalette();
  spr.fillSprite(p.bg);
  spr.setTextSize(1);
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(8, 56);  spr.print("BLUETOOTH PAIRING");
  spr.setCursor(8, 184); spr.print("enter on desktop:");
  spr.setTextSize(3);
  spr.setTextColor(p.text, p.bg);
  char b[8]; snprintf(b, sizeof(b), "%06lu", (unsigned long)blePasskey());
  spr.setCursor((W - 18 * 6) / 2, 110);
  spr.print(b);
}

// Greedy word-wrap into fixed-width rows. Continuation rows get a leading
// space. Returns number of rows written.
static uint8_t wrapInto(const char* in, char out[][24], uint8_t maxRows, uint8_t width) {
  uint8_t row = 0, col = 0;
  const char* p = in;
  while (*p && row < maxRows) {
    while (*p == ' ') p++;                     // skip leading spaces
    // measure next word
    const char* w = p;
    while (*p && *p != ' ') p++;
    uint8_t wlen = p - w;
    if (wlen == 0) break;
    uint8_t need = (col > 0 ? 1 : 0) + wlen;
    if (col + need > width) {
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;              // continuation indent
    }
    if (col > 1 || (col == 1 && out[row][0] != ' ')) out[row][col++] = ' ';
    else if (col == 1 && row > 0) {}           // already have the indent space
    // hard-break words that still don't fit
    while (wlen > width - col) {
      uint8_t take = width - col;
      memcpy(&out[row][col], w, take); col += take; w += take; wlen -= take;
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    memcpy(&out[row][col], w, wlen); col += wlen;
  }
  if (col > 0 && row < maxRows) { out[row][col] = 0; row++; }
  return row;
}

// Approval card: two full-height half-screen tap zones, with a slim header
// band along the top for the tool name + countdown. No taskbar (suppressed in
// the render path) so the whole screen below the header is hit-active.
static constexpr int APPROVE_HEADER_H = 44;
static constexpr int APPROVE_GAP      = 4;

static void drawApproval() {
  const Palette& p = characterPalette();
  spr.fillSprite(p.bg);

  // Header strip: tool name on the left, countdown on the right.
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  uint16_t countdownCol = (waited >= 10) ? HOT : p.textDim;

  spr.setTextDatum(ML_DATUM);
  spr.setTextColor(p.text, p.bg);
  int toolLen = strlen(tama.promptTool);
  spr.setTextSize(toolLen <= 12 ? 3 : (toolLen <= 18 ? 2 : 1));
  spr.drawString(tama.promptTool, 8, APPROVE_HEADER_H / 2);

  spr.setTextDatum(MR_DATUM);
  spr.setTextSize(2);
  spr.setTextColor(countdownCol, p.bg);
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "%lus", (unsigned long)waited);
  spr.drawString(hdr, W - 8, APPROVE_HEADER_H / 2);

  spr.setTextDatum(TL_DATUM);
  spr.drawFastHLine(0, APPROVE_HEADER_H, W, p.textDim);

  // Two giant buttons, edge-to-edge, full remaining height.
  int btnTop = APPROVE_HEADER_H + APPROVE_GAP;
  int btnBot = H - APPROVE_GAP;
  int btnH   = btnBot - btnTop;
  int btnW   = W / 2 - APPROVE_GAP - APPROVE_GAP / 2;
  int lx     = APPROVE_GAP;
  int rx     = W / 2 + APPROVE_GAP / 2;
  spr.fillRoundRect(lx, btnTop, btnW, btnH, 12, GREEN);
  spr.fillRoundRect(rx, btnTop, btnW, btnH, 12, HOT);

  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(3);   // 18px per char — "APPROVE" (7) = 126px, fits in 154px button
  spr.setTextColor(WHITE, GREEN);
  spr.drawString("APPROVE", lx + btnW / 2, btnTop + btnH / 2);
  spr.setTextColor(WHITE, HOT);
  spr.drawString("DENY", rx + btnW / 2, btnTop + btnH / 2);
  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);

  if (responseSent) {
    spr.fillRect(0, H / 2 - 14, W, 28, p.bg);
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(2);
    spr.setTextColor(p.textDim, p.bg);
    spr.drawString("sent...", W / 2, H / 2);
    spr.setTextDatum(TL_DATUM);
    spr.setTextSize(1);
  }
}

// Hit-test: any tap below the header strip lands on Approve (left half) or
// Deny (right half). Generous — the whole panel surface is a button.
static bool approvalHitApprove(int x, int y) {
  return y >= APPROVE_HEADER_H && x < W / 2;
}
static bool approvalHitDeny(int x, int y) {
  return y >= APPROVE_HEADER_H && x >= W / 2;
}

static const char* const TASKBAR_LABELS[TASKBAR_ICONS] = {
  "HOME", "PET", "MENU"
};

static void drawTaskbar(const Palette& p) {
  spr.fillRect(0, TASKBAR_Y, W, TASKBAR_H, PANEL);
  spr.drawFastHLine(0, TASKBAR_Y, W, p.textDim);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  for (int i = 0; i < TASKBAR_ICONS; i++) {
    int x = TASKBAR_ICON_X[i];
    int w = TASKBAR_ICON_W[i];
    int cx = x + w / 2;
    if (i > 0) spr.drawFastVLine(x, TASKBAR_Y + 4, TASKBAR_H - 8, p.textDim);
    // Highlight the icon corresponding to the active screen.
    bool active =
      (i == 0 && displayMode == DISP_NORMAL && !settingsOpen) ||
      (i == 1 && displayMode == DISP_PET) ||
      (i == 2 && settingsOpen);
    if (active) {
      spr.fillRect(x + 1, TASKBAR_Y + 1, w - 1, TASKBAR_H - 2, p.body);
      spr.setTextColor(p.bg, p.body);
    } else {
      spr.setTextColor(p.text, PANEL);
    }
    spr.drawString(TASKBAR_LABELS[i], cx, TASKBAR_Y + TASKBAR_H / 2);
  }
  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);
}

static int taskbarHit(int x, int y) {
  if (y < TASKBAR_Y) return -1;
  for (int i = 0; i < TASKBAR_ICONS; i++) {
    int ix = TASKBAR_ICON_X[i];
    if (x >= ix && x < ix + TASKBAR_ICON_W[i]) return i;
  }
  return -1;
}

// Compact stats pane drawn on the right side of the home screen. Pet keeps
// drawing on the left untouched; this just paints over the right slice.
static void drawStatsPane(const Palette& p) {
  spr.fillRect(HOME_STATS_X, 0, HOME_STATS_W, TASKBAR_Y, p.bg);
  spr.drawFastVLine(HOME_STATS_X, 0, TASKBAR_Y, p.textDim);

  const int X = HOME_STATS_X + 8;
  int y = 8;
  spr.setTextSize(1);

  spr.setTextColor(p.text, p.bg);
  spr.setCursor(X, y);
  if (ownerName()[0]) spr.printf("%s's %s", ownerName(), petName());
  else                spr.print(petName());

  y += 14;
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(X, y); spr.print("mood");
  uint8_t mood = statsMoodTier();
  uint16_t moodCol = (mood >= 3) ? RED : (mood >= 2) ? HOT : p.textDim;
  for (int i = 0; i < 4; i++) {
    int px = X + 50 + i * 12;
    if (i < mood) { spr.fillCircle(px - 2, y + 4, 2, moodCol);
                    spr.fillCircle(px + 2, y + 4, 2, moodCol);
                    spr.fillTriangle(px - 4, y + 5, px + 4, y + 5, px, y + 9, moodCol); }
    else          { spr.drawCircle(px - 2, y + 4, 2, moodCol);
                    spr.drawCircle(px + 2, y + 4, 2, moodCol);
                    spr.drawLine(px - 4, y + 5, px, y + 9, moodCol);
                    spr.drawLine(px + 4, y + 5, px, y + 9, moodCol); }
  }

  y += 16;
  spr.setCursor(X, y); spr.print("fed");
  uint8_t fed = statsFedProgress();
  for (int i = 0; i < 10; i++) {
    int px = X + 36 + i * 9;
    if (i < fed) spr.fillCircle(px, y + 4, 2, p.body);
    else         spr.drawCircle(px, y + 4, 2, p.textDim);
  }

  y += 18;
  spr.fillRoundRect(X, y, 44, 14, 3, p.body);
  spr.setTextColor(p.bg, p.body);
  spr.setCursor(X + 5, y + 3); spr.printf("Lv %u", stats().level);

  y += 22;
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(X, y);      spr.printf("approved %u", stats().approvals);
  spr.setCursor(X, y + 10); spr.printf("denied   %u", stats().denials);
  uint32_t nap = stats().napSeconds;
  spr.setCursor(X, y + 20); spr.printf("napped   %luh%02lum", nap/3600, (nap/60)%60);
  spr.setCursor(X, y + 30); spr.printf("tokens   %lu", (unsigned long)stats().tokens);

  // Live Claude session counters under the bookkeeping row.
  y += 46;
  spr.setTextColor(p.text, p.bg);
  spr.setCursor(X, y);      spr.print("CLAUDE");
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(X, y + 10); spr.printf("running %u", tama.sessionsRunning);
  spr.setCursor(X, y + 20); spr.printf("waiting %u", tama.sessionsWaiting);
}

static void tinyHeart(int x, int y, bool filled, uint16_t col) {
  if (filled) {
    spr.fillCircle(x - 2, y, 2, col);
    spr.fillCircle(x + 2, y, 2, col);
    spr.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, col);
  } else {
    spr.drawCircle(x - 2, y, 2, col);
    spr.drawCircle(x + 2, y, 2, col);
    spr.drawLine(x - 4, y + 1, x, y + 5, col);
    spr.drawLine(x + 4, y + 1, x, y + 5, col);
  }
}

// Pet screen — split into two panes. Left pane keeps the pet sprite that the
// character/buddy systems already painted, plus visual status bars below.
// Right pane shows textual session stats and a short how-to.
static void drawPetLeftPane(const Palette& p) {
  // Wipe the area below the pet sprite (sprite occupies roughly y=0..125 in
  // scale-2 mode); status bars stack below.
  int y = 130;
  spr.fillRect(0, y, HOME_PET_W, TASKBAR_Y - y, p.bg);
  spr.setTextSize(1);

  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(6, y); spr.print("mood");
  uint8_t mood = statsMoodTier();
  uint16_t moodCol = (mood >= 3) ? RED : (mood >= 2) ? HOT : p.textDim;
  for (int i = 0; i < 4; i++) tinyHeart(54 + i * 16, y + 4, i < mood, moodCol);

  y += 16;
  spr.setCursor(6, y); spr.print("fed");
  uint8_t fed = statsFedProgress();
  for (int i = 0; i < 10; i++) {
    int px = 38 + i * 9;
    if (i < fed) spr.fillCircle(px, y + 3, 2, p.body);
    else         spr.drawCircle(px, y + 3, 2, p.textDim);
  }

  y += 16;
  spr.fillRoundRect(6, y, 50, 16, 3, p.body);
  spr.setTextColor(p.bg, p.body);
  spr.setTextSize(2);
  spr.setCursor(10, y + 2); spr.printf("Lv%u", stats().level);
  spr.setTextSize(1);
}

static void drawPetRightPane(const Palette& p) {
  // Wipe the full right pane and divider.
  spr.fillRect(HOME_STATS_X, 0, HOME_STATS_W, TASKBAR_Y, p.bg);
  spr.drawFastVLine(HOME_STATS_X, 0, TASKBAR_Y, p.textDim);

  const int X = HOME_STATS_X + 8;
  int y = 6;

  // Header: owner's pet name.
  spr.setTextSize(1);
  spr.setTextColor(p.text, p.bg);
  spr.setCursor(X, y);
  if (ownerName()[0]) spr.printf("%s's %s", ownerName(), petName());
  else                spr.print(petName());
  y += 14;

  // Session stats.
  auto tokFmt = [&](const char* label, uint32_t v, int yPx) {
    spr.setCursor(X, yPx);
    if (v >= 1000000)   spr.printf("%s%lu.%luM", label, v/1000000, (v/100000)%10);
    else if (v >= 1000) spr.printf("%s%lu.%luK", label, v/1000, (v/100)%10);
    else                spr.printf("%s%lu", label, v);
  };
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(X, y);      spr.printf("approved %u", stats().approvals);
  spr.setCursor(X, y + 10); spr.printf("denied   %u", stats().denials);
  uint32_t nap = stats().napSeconds;
  spr.setCursor(X, y + 20); spr.printf("napped   %luh%02lum", nap/3600, (nap/60)%60);
  tokFmt("tokens   ", stats().tokens,        y + 30);
  tokFmt("today    ", tama.tokensToday,      y + 40);

  // How-to.
  y += 56;
  spr.drawFastHLine(X, y - 2, HOME_STATS_W - 16, p.textDim);
  auto ln = [&](uint16_t c, const char* s) {
    spr.setTextColor(c, p.bg); spr.setCursor(X, y); spr.print(s); y += 9;
  };
  ln(p.body,    "MOOD");
  ln(p.textDim, " approve fast = up");
  ln(p.textDim, " deny lots = down");
  ln(p.body,    "FED");
  ln(p.textDim, " 50K tok = level up");
}

void drawPet() {
  const Palette& p = characterPalette();
  drawPetLeftPane(p);
  drawPetRightPane(p);
}

void drawHUD() {
  const Palette& p = characterPalette();
  // Transcript band lives along the bottom of the LEFT pane only (above the
  // taskbar). ~22 char wrap at size 1 fits in 150 px, matching the 24-wide
  // wrapInto() buffer.
  const int SHOW = 4, LH = 8, WIDTH = 22;
  const int AREA = SHOW * LH + 4;
  const int TOP = TASKBAR_Y - AREA;
  spr.fillRect(0, TOP, HOME_PET_W, AREA, p.bg);
  spr.setTextSize(1);

  if (tama.lineGen != lastLineGen) { msgScroll = 0; lastLineGen = tama.lineGen; wake(); }

  if (tama.nLines == 0) {
    spr.setTextColor(p.text, p.bg);
    spr.setCursor(4, TOP + 2);
    spr.print(tama.msg);
    return;
  }

  // Wrap all transcript lines into a flat display buffer. Track which
  // transcript index each display row came from, so we can dim older ones.
  static char disp[48][24];
  static uint8_t srcOf[48];
  uint8_t nDisp = 0;
  for (uint8_t i = 0; i < tama.nLines && nDisp < 48; i++) {
    uint8_t got = wrapInto(tama.lines[i], &disp[nDisp], 48 - nDisp, WIDTH);
    for (uint8_t j = 0; j < got; j++) srcOf[nDisp + j] = i;
    nDisp += got;
  }

  uint8_t maxBack = (nDisp > SHOW) ? (nDisp - SHOW) : 0;
  if (msgScroll > maxBack) msgScroll = maxBack;

  int end = (int)nDisp - msgScroll;
  int start = end - SHOW; if (start < 0) start = 0;
  uint8_t newest = tama.nLines - 1;
  for (int i = 0; start + i < end; i++) {
    uint8_t row = start + i;
    bool fresh = (srcOf[row] == newest) && (msgScroll == 0);
    spr.setTextColor(fresh ? p.text : p.textDim, p.bg);
    spr.setCursor(4, TOP + 2 + i * LH);
    spr.print(disp[row]);
  }
  if (msgScroll > 0) {
    spr.setTextColor(p.body, p.bg);
    spr.setCursor(HOME_PET_W - 22, TASKBAR_Y - LH - 2);
    spr.printf("-%u", msgScroll);
  }
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Imu.Init();
  M5.Beep.begin();
  startBt();
  applyBrightness();
  lastInteractMs = millis();
  statsLoad();
  settingsLoad();
  M5.Beep.volume(settings().volume);   // apply the persisted beep volume
  petNameLoad();
  buddyInit();

  // BLE stays always-on; s.bt is stored as a preference only.
  spr.createSprite(W, H);
  characterInit(nullptr);  // scan /characters/ for whatever is installed
  gifAvailable = characterLoaded();
  // species NVS: 0..N-1 = ASCII species, 0xFF = use GIF (also the default,
  // so a fresh install lands on the GIF). With no GIF installed, 0xFF falls
  // through to buddyInit()'s clamped default.
  buddyMode = !(gifAvailable && speciesIdxLoad() == SPECIES_GIF);
  applyDisplayMode();

  {
    const Palette& p = characterPalette();
    spr.fillSprite(p.bg);
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(2);
    if (ownerName()[0]) {
      char line[40];
      snprintf(line, sizeof(line), "%s's", ownerName());
      spr.setTextColor(p.text, p.bg);   spr.drawString(line, W/2, H/2 - 12);
      spr.setTextColor(p.body, p.bg);   spr.drawString(petName(), W/2, H/2 + 12);
    } else {
      // First boot, no owner pushed yet — say hi.
      spr.setTextColor(p.body, p.bg);   spr.drawString("Hello!", W/2, H/2 - 12);
      spr.setTextSize(1);
      spr.setTextColor(p.textDim, p.bg);
      spr.drawString("a buddy appears", W/2, H/2 + 12);
    }
    spr.setTextDatum(TL_DATUM); spr.setTextSize(1);
    spr.pushSprite(0, 0);
    delay(1800);
  }

  Serial.printf("buddy: %s\n", buddyMode ? "ASCII mode" : "GIF character loaded");
}

void loop() {
  M5.update();
  M5.Beep.update();
  t++;
  uint32_t now = millis();

  dataPoll(&tama);
  if (statsPollLevelUp()) triggerOneShot(P_CELEBRATE, 3000);
  baseState = derive(tama);

  // After waking the screen, hold sleep for 12s so users see the wake-up
  // animation. Urgent states (attention, celebrate, busy) override this.
  if (baseState == P_IDLE && (int32_t)(now - wakeTransitionUntil) < 0) baseState = P_SLEEP;

  if ((int32_t)(now - oneShotUntil) >= 0) activeState = baseState;

  // LED: pulse on attention, otherwise off (on-screen border on fnk010b)
  fnk::setAttentionPulse(activeState == P_ATTENTION && settings().led);

  // shake → dizzy + force scenario advance
  if (now - lastShakeCheck > 50) {
    lastShakeCheck = now;
    if (!settingsOpen && !screenOff && checkShake() && (int32_t)(now - oneShotUntil) >= 0) {
      wake();
      triggerOneShot(P_DIZZY, 2000);
      Serial.println("shake: dizzy");
    }
  }

  // BtnA: step through fake scenarios
  // Prompt arrival: beep, reset response flag
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId)-1);
    lastPromptId[sizeof(lastPromptId)-1] = 0;
    responseSent = false;
    if (tama.promptId[0]) {
      promptArrivedMs = millis();
      wake();
      beep(NOTE_E5, 80);   // alert chirp on prompt arrival
      // Jump to the approval screen no matter what was open — drawApproval
      // only runs from drawHUD which only runs in DISP_NORMAL.
      displayMode = DISP_NORMAL;
      settingsOpen = false;
      applyDisplayMode();
      characterInvalidate();
      if (buddyMode) buddyInvalidate();
    }
  }

  bool inPrompt = tama.promptId[0] && !responseSent;

  // Live volume-slider drag. The gesture layer only reports completed taps and
  // swipes, so the slider reads the raw contact each frame instead: a touch
  // that begins in the volume cell grabs the slider, and the level tracks the
  // finger's x (clamped past the track ends) until release. settings().volume
  // drives the on-screen thumb, so it moves live; we commit to NVS and play the
  // preview beep once, on release — not per frame — to spare both NVS and ears.
  {
    static bool volTouchWas = false;
    static bool volDragging = false;
    int16_t tx, ty;
    bool active   = fnk::touch_active(tx, ty);
    bool sliderOn = settingsOpen && confirmPending == CONFIRM_NONE && !inPrompt;

    if (active && sliderOn) {
      if (!volTouchWas && volSliderHit(tx, ty, nullptr)) volDragging = true;  // down-edge in cell
      if (volDragging) {
        wake();
        settings().volume = volFromX(tx);     // RAM only — thumb tracks live
        swallowBtnA = swallowBtnB = true;      // eat the BtnA/B edge this touch raises
      }
    }
    if (volDragging && (!active || !sliderOn)) {
      uint8_t vol = settings().volume;
      settingsSave();
      M5.Beep.volume(vol);
      if (!active) beep(NOTE_E5, 60);   // preview at the final level, on real release only
      volDragging = false;
    }
    volTouchWas = active;
  }

  // Touch event dispatch — runs before the legacy ButtonShim path so taps on
  // approval / taskbar hit-zones beat the BtnA/B edge that the same touch
  // produces.
  fnk::TouchEvent ev;
  while (fnk::touch_event(ev)) {
    if (inPrompt && ev.kind == fnk::GESTURE_TAP) {
      if (approvalHitApprove(ev.x, ev.y)) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}", tama.promptId);
        sendCmd(cmd);
        responseSent = true;
        uint32_t tookS = (millis() - promptArrivedMs) / 1000;
        statsOnApproval(tookS);
        beep(NOTE_G5, 140);   // approve: the bright, high chord tone
        if (tookS < 5) triggerOneShot(P_HEART, 2000);
        // Full sprite clear + buddy/character invalidate so the green button
        // fill behind a partial-clear pet pane doesn't bleed through.
        applyDisplayMode();
        if (buddyMode) buddyInvalidate();
        swallowBtnA = swallowBtnB = true;   // eat the BtnA/B edge from this tap
      } else if (approvalHitDeny(ev.x, ev.y)) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}", tama.promptId);
        sendCmd(cmd);
        responseSent = true;
        statsOnDenial();
        beep(NOTE_C5, 180);   // deny: the low chord tone, longer than approve
        applyDisplayMode();
        if (buddyMode) buddyInvalidate();
        swallowBtnA = swallowBtnB = true;
      }
      continue;
    }

    // Confirmation dialog for the destructive settings (Re-pair / Reset). It's
    // modal: while a confirm is armed it captures every tap, so the panel and
    // taskbar beneath are inert. Tap Confirm to run the action (it reboots and
    // never returns); any other tap cancels and returns to the panel.
    if (confirmPending != CONFIRM_NONE && ev.kind == fnk::GESTURE_TAP) {
      wake();
      if (confirmHitBtn(ev.x, ev.y) == 1) {
        ConfirmAction act = confirmPending;
        confirmPending = CONFIRM_NONE;
        beep(NOTE_C5, 120);   // confirmation tone before the action restarts us
        if (act == CONFIRM_REPAIR) rotatePairIdentity();
        else                       factoryReset();
      } else {
        confirmPending = CONFIRM_NONE;
        beep(NOTE_C5, 60);    // short chirp: cancelled
      }
      swallowBtnA = swallowBtnB = true;
      continue;
    }

    // Open settings panel: tap a cell to invoke, tap outside to close. Runs
    // before the taskbar so the SET icon tapped while settings is open closes
    // the panel via the taskbar branch below.
    if (settingsOpen && ev.kind == fnk::GESTURE_TAP && taskbarHit(ev.x, ev.y) < 0) {
      int cell = settingsHitCell(ev.x, ev.y);
      if (volSliderHit(ev.x, ev.y, nullptr)) {
        // Volume is handled live by the drag logic above (a tap is just a zero-
        // distance drag). Consume the tap so it doesn't fall through to close.
      } else if (cell >= 0) {
        wake();
        settingsSel = (uint8_t)cell;
        applySetting((uint8_t)cell);
      } else {
        wake();
        settingsOpen = false;
        characterInvalidate();
      }
      swallowBtnA = swallowBtnB = true;
      continue;
    }

    if (ev.kind == fnk::GESTURE_TAP) {
      int tb = taskbarHit(ev.x, ev.y);
      if (tb >= 0) {
        wake();
        switch (tb) {
          case 0:  // HOME
            settingsOpen = false;
            displayMode = DISP_NORMAL;
            applyDisplayMode();
            break;
          case 1:  // PET — always navigates to the pet screen.
            settingsOpen = false;
            displayMode = DISP_PET;
            applyDisplayMode();
            break;
          case 2:  // MENU — opens the Settings panel directly.
            settingsOpen = !settingsOpen;
            settingsSel = 0;
            confirmPending = CONFIRM_NONE;   // never reopen onto a stale dialog
            if (!settingsOpen) characterInvalidate();
            break;
        }
        swallowBtnA = swallowBtnB = true;
        continue;
      }
    }

    // Swipe is a no-op now that PET / HOME are single-page screens. Left here
    // in case future panes want pagination.
  }

  // Button-press wake. Track which button woke the screen so its full
  // press cycle (including long-press) is swallowed — you don't want
  // BtnA-to-wake to also cycle displayMode or open the menu.
  if (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
    if (screenOff) {
      if (M5.BtnA.isPressed()) swallowBtnA = true;
      if (M5.BtnB.isPressed()) swallowBtnB = true;
    }
    wake();
  }

  // AXP power button (left side): short-press toggles screen off.
  // Long-press (6s) still powers off the device via AXP hardware.
  if (M5.Axp.GetBtnPress() == 0x02) {
    if (screenOff) {
      wake();
    } else {
      M5.Axp.SetLDO2(false);
      M5.Beep.amp(false);   // power down the speaker amp while the screen sleeps
      screenOff = true;
    }
  }

  // Long-press fallback for closing the settings panel — menu and approval
  // are handled exclusively by tap dispatch above.
  if (M5.BtnA.pressedFor(600) && !btnALong && !swallowBtnA && !inPrompt) {
    btnALong = true;
    if (settingsOpen) { settingsOpen = false; confirmPending = CONFIRM_NONE; characterInvalidate(); }
  }
  // Reset edges so the press flag doesn't latch across taps.
  if (M5.BtnA.wasReleased()) { btnALong = false; swallowBtnA = false; }
  if (M5.BtnB.wasPressed())  { if (swallowBtnB) swallowBtnB = false; }

  // blink bookkeeping

  // Charging clock: takes over the home screen when on USB power, no
  // overlays, no prompt, no live Claude data, and the RTC has been set
  // by the bridge. Pet sleeps underneath. Exit restores Y via
  // applyDisplayMode() so the next mode-switch isn't visually offset.
  clockRefreshRtc();   // 1Hz internal throttle; also caches _onUsb
  // Show the clock when nothing is happening — bridge heartbeat alone
  // doesn't count as activity (it's the only way to get the RTC synced).
  bool clocking = displayMode == DISP_NORMAL
               && !settingsOpen && !inPrompt
               && tama.sessionsRunning == 0 && tama.sessionsWaiting == 0
               && dataRtcValid() && _onUsb;
  static bool wasClocking = false;
  if (clocking != wasClocking) {
    if (clocking) characterSetPeek(true);
    else applyDisplayMode();
    characterInvalidate();
    if (buddyMode) buddyInvalidate();
    wasClocking = clocking;
  }
  if (clocking) {
    uint8_t dow = clockDow();
    bool weekend = (dow == 0 || dow == 6);
    bool friday  = (dow == 5);

    uint8_t h = _clkTm.Hours;
    if (h >= 1 && h < 7)             activeState = P_SLEEP;
    else if (weekend)                activeState = (now/8000 % 6 == 0) ? P_HEART : P_SLEEP;
    else if (h < 9)                  activeState = (now/6000 % 4 == 0) ? P_IDLE  : P_SLEEP;
    else if (h == 12)                activeState = (now/5000 % 3 == 0) ? P_HEART : P_IDLE;
    else if (friday && h >= 15)      activeState = (now/4000 % 3 == 0) ? P_CELEBRATE : P_IDLE;
    else if (h >= 22 || h == 0)      activeState = (now/7000 % 3 == 0) ? P_DIZZY : P_SLEEP;
    else                             activeState = (now/10000 % 5 == 0) ? P_SLEEP : P_IDLE;
  }

  static uint32_t lastPasskey = 0;
  uint32_t pk = blePasskey();
  if (pk && !lastPasskey) { wake(); beep(NOTE_G5, 60); }
  lastPasskey = pk;

  // Any open → closed transition on a modal panel needs a full repaint:
  // characterInvalidate is a no-op in ASCII buddy mode, and the partial
  // clears in buddyTick / drawHUD don't cover the panel area. Trip a single
  // applyDisplayMode() per closed-this-frame transition.
  static bool wasSettingsOpen = false;
  static bool wasInPrompt = false;
  bool inPromptNow = tama.promptId[0] && !responseSent;
  if ((wasSettingsOpen && !settingsOpen) ||
      (wasInPrompt     && !inPromptNow)) {
    applyDisplayMode();
    if (buddyMode) buddyInvalidate();
  }
  wasSettingsOpen = settingsOpen;
  wasInPrompt     = inPromptNow;

  if (napping || screenOff) {
    // skip sprite render — face-down or powered off
  } else if (buddyMode) {
    buddyTick(activeState);
  } else if (characterLoaded()) {
    characterSetState(activeState);
    characterTick();
  } else {
    const Palette& p = characterPalette();
    spr.fillSprite(p.bg);
    spr.setTextColor(p.textDim, p.bg);
    spr.setTextSize(1);
    if (xferActive()) {
      uint32_t done = xferProgress(), total = xferTotal();
      spr.setCursor(8, 90);
      spr.print("installing");
      spr.setCursor(8, 102);
      spr.printf("%luK / %luK", done/1024, total/1024);
      int barW = W - 16;
      spr.drawRect(8, 116, barW, 8, p.textDim);
      if (total > 0) {
        int fill = (int)((uint64_t)barW * done / total);
        if (fill > 1) spr.fillRect(9, 117, fill - 1, 6, p.body);
      }
    } else {
      spr.setCursor(8, 100);
      spr.print("no character loaded");
    }
  }
  if (!napping && !screenOff) {
    // Re-evaluate: the touch dispatcher above may have flipped responseSent
    // mid-frame after the user tapped APPROVE / DENY. The local `inPrompt`
    // computed at the top of loop is stale by the time we render.
    if (tama.promptId[0] && !responseSent) {
      drawApproval();
    } else if (blePasskey()) {
      drawPasskey();
    } else if (displayMode == DISP_PET) {
      drawPet();
    } else {
      // Home: pet has already painted the sprite. The right pane shows either
      // the clock (when idle on USB) or the stats grid; transcript lives along
      // the left pane bottom.
      if (clocking) drawClock();
      else          drawStatsPane(characterPalette());
      if (settings().hud) drawHUD();
    }
    if (settingsOpen) drawSettings();
    if (confirmPending != CONFIRM_NONE) drawConfirm();   // modal, over the panel
    // Taskbar is persistent across all non-approval views.
    if (!inPrompt) drawTaskbar(characterPalette());
    spr.pushSprite(0, 0);
  }

  // Face-down nap: dim immediately, pause animations, accumulate sleep time.
  // Skipped during approval — you're holding it to read, not sleeping it.
  // Exit needs sustained not-down so IMU noise at the threshold doesn't
  // bounce brightness between 8 and full every few frames.
  static int8_t faceDownFrames = 0;
  if (!inPrompt) {
    bool down = isFaceDown();
    if (down)       { if (faceDownFrames < 20) faceDownFrames++; }
    else            { if (faceDownFrames > -10) faceDownFrames--; }
  }

  if (!napping && faceDownFrames >= 15) {
    napping = true;
    napStartMs = now;
    M5.Axp.ScreenBreath(8);
    dimmed = true;
  } else if (napping && faceDownFrames <= -8) {
    napping = false;
    statsOnNapEnd((now - napStartMs) / 1000);
    statsOnWake();
    wake();
  }

  // millis() not the cached `now`: wake() runs after `now` is captured,
  // so now - lastInteractMs underflows when a button is held → flicker.
  // No auto-off on USB power — clock face wants to stay visible while charging.
  if (!screenOff && !inPrompt && !_onUsb
      && millis() - lastInteractMs > SCREEN_OFF_MS) {
    M5.Axp.SetLDO2(false);
    M5.Beep.amp(false);   // power down the speaker amp while the screen sleeps
    screenOff = true;
  }

  delay(screenOff ? 100 : 16);
}
