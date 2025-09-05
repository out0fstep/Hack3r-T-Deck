/*
  Hacker T-Deck — menu + USB deploy overlay (PNG + fallback sprite)
  - Draws /deploy.png (320x240) from SD if present; else uses a built-in fallback sprite.
  - Centered "Deploying payload..." stays visible while HID script runs, then returns to menu.
  - HID init for ESP32-S3: Keyboard.begin() before USB.begin().
*/

// --- ensure LovyanGFX builds with PNG support ---
#ifndef LGFX_USE_PNG
#define LGFX_USE_PNG 1
#endif

#include <Arduino.h>
#include <pgmspace.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "lgfx_tdeck.hpp"
#include <driver/i2c.h>

// ---------------------- Native ESP32 USB (TinyUSB) ----------------------
#include <USB.h>
#include <USBHIDKeyboard.h>
USBHIDKeyboard DuckKB;

static bool usbHIDReady = false;
// On ESP32-S3, add HID before starting USB:
static void initUSBHID(){
  if (!usbHIDReady) {
    DuckKB.begin();  // add HID interface first
    USB.begin();     // start composite USB device
    usbHIDReady = true;
    delay(50);       // small grace period for host to enumerate
  }
}

// --------- embedded fallback sprite (no external header needed) ---------
#define BOMB_W 32
#define BOMB_H 24
#define BOMB_TRANSPARENCY_KEY 0xF81F
static const uint16_t bomb_sprite[BOMB_W * BOMB_H] PROGMEM = { 0x0000 };
// ------------------------------------------------------------------------

// ---------------------- UI rects / softkeys (avoid autoproto issues) -----
struct UiRect { int x,y,w,h; };
enum class SoftBtn : uint8_t { NONE, LEFT, MID, RIGHT };

static SoftBtn  g_flashSoftBtn = SoftBtn::NONE;
static uint32_t g_flashUntil   = 0;

// Use id (uint8_t) helpers so Arduino doesn't autogenerate bad prototypes
static inline void flashBtnId(uint8_t b, uint16_t durMs=140){ g_flashSoftBtn=(SoftBtn)b; g_flashUntil=millis()+durMs; }
static inline bool btnPressedId(uint8_t b){ return (g_flashSoftBtn==(SoftBtn)b) && ((int32_t)(g_flashUntil - millis()) > 0); }

#define FLASH(b)         flashBtnId((uint8_t)(b))
#define BTN_PRESSED(b)   btnPressedId((uint8_t)(b))

// ---------------------- Optional charge sense ----------------------
#define CHARGE_SENSE_PIN  (-1)

// ---------------------- Display / GFX ----------------------
LGFX_TDeck tft;
lgfx::LGFX_Sprite frame(&tft);
static inline uint16_t C(uint8_t r,uint8_t g,uint8_t b){ return tft.color565(r,g,b); }

// Layout
static constexpr int SCR_W=320, SCR_H=240;
static constexpr int TITLE_H=22;
static constexpr int ASCII_ROWS=10;   // 10*8 = 80px banner
static constexpr int SOFTKEY_H=44;

static UiRect CARD={16,100,SCR_W-32,92};
static constexpr int CARD_UPWARD_BIAS=12;

static const UiRect BTN_LEFT  ={0,              SCR_H-SOFTKEY_H, SCR_W/3, SOFTKEY_H};
static const UiRect BTN_MID   ={SCR_W/3,        SCR_H-SOFTKEY_H, SCR_W/3, SOFTKEY_H};
static const UiRect BTN_RIGHT ={(SCR_W*2)/3,    SCR_H-SOFTKEY_H, SCR_W/3, SOFTKEY_H};

#define UI_POINT_IN(R,PX,PY) ( ((PX)>=(R).x) && ((PX)<((R).x+(R).w)) && ((PY)>=(R).y) && ((PY)<((R).y+(R).h)) )

// Accent color (user-adjustable) --------------
// Added Blue + names; keep arrays in sync
static uint16_t ACCENT=C(0,255,0);
static void setAccent(uint16_t v){ ACCENT=v; }
static const uint16_t kColors[]={
  C(0,255,0),     // Green
  C(0,255,255),   // Cyan
  C(255,0,255),   // Magenta
  C(255,255,0),   // Yellow
  C(255,255,255), // White
  C(255,64,64),   // Red
  C(50,100,255)   // Blue (new)
};
static const char* kColorNames[]={
  "Green","Cyan","Magenta","Yellow","White","Red","Blue"
};
static constexpr int kColorCount = sizeof(kColors)/sizeof(kColors[0]);
static int colorIdx=0;
static int findAccentIndex(){ for(int i=0;i<kColorCount;i++) if(kColors[i]==ACCENT) return i; return 0; }

// ---------------------- ASCII background ----------------------
static constexpr int CELL_W=6, CELL_H=8;
static constexpr int COLS=SCR_W/CELL_W;
static constexpr int ROWS=SCR_H/CELL_H;
static int IMG_X_OFFSET=0, IMG_Y_OFFSET=-4;
static int BG_GRAY_MIN=10, BG_GRAY_MAX=90;

static const char image[] PROGMEM =
"                                                     \
                                                     \
                                                     \
                                                     \
                                                     \
                                                     \
                                                     \
                                                     \
   @@@   @@   @@@@    @@@@@@  @@  @@ @@@/@@ @@@@@@@  \
   @@@   @@  @@@@@@  @@@ @@@  @@@@@    @@@  @@@ @@   \
  @@@@@@@@@  @@@ @@  @@/      @@(@@#  ,@@@@ @@@@@    \
   @@@  #@@  @@@@@@@ @@@   @@ @@( @@@    @@ @@@ @@   \
   @@@   @@ @@@  @@@  @@@@@@  @@@   @@ @@@  @@@  @@@ \
                                                     \
                                                     \
                                                     \
                                                     \
                                                     \
                                                     \
                                                     \
";
static char charsSet[]="%&@G8";
static size_t IMAGE_LEN=0;
static void compute_image_len_once(){ if(!IMAGE_LEN){ while(pgm_read_byte(&image[IMAGE_LEN])!=0) ++IMAGE_LEN; } }

// ---------------------- App state ----------------------
enum class ScreenState{
  MAIN, USB, WIFI, BTE, OUI, SETTINGS, ABOUT,
  FILE_PICKER, OUI_PICKER,
  SETTINGS_COLOR, SETTINGS_TIME, SETTINGS_WIFI,
  RUN_DUCKY,
  KBD_EDIT
};
static ScreenState state=ScreenState::MAIN;

// Where the About page should go "Back" to:
static ScreenState g_aboutBackTo = ScreenState::SETTINGS;

static const char* mainItems[]={"USB HID","WiFi","BTE","OUI Foxhunter","Settings","About"};
static const char* usbItems[] ={"Select payload","Deploy payload","Back"};
static const char* ouiItems[] ={"Pick OUI","Start Hunt","Back"};
// Removed "Input" from Settings menu
static const char* setItems[] ={"UI Color","Clock","Wi-Fi","Back"};
static int mainIndex=0, usbIndex=0, ouiIndex=0, setIndex=0;

// ---------------------- SD (on-demand) ----------------------
SPIClass spiSD(FSPI);
static const int PIN_SD_CS=39, PIN_SCK=40, PIN_MOSI=41, PIN_MISO=38;
static bool sdOK=false, sdBusReady=false;
static const char* DUCKY_DIR="/duckyscripts";
static const char* LOOT_DIR ="/Loot";
static String duckyFiles[64]; static int duckyCount=0, duckyIdx=0;
static String selectedPayload="";

static void ensureDirs(){ if(!SD.exists(DUCKY_DIR)) SD.mkdir(DUCKY_DIR); if(!SD.exists(LOOT_DIR)) SD.mkdir(LOOT_DIR); }
static bool initSD_ondemand(){
  if(sdOK) return true;
  if(!sdBusReady){ spiSD.begin(PIN_SCK,PIN_MISO,PIN_MOSI,PIN_SD_CS); sdBusReady=true; }
  const uint32_t speeds[]={12000000,4000000};
  for(uint8_t i=0;i<2 && !sdOK;i++){ sdOK=SD.begin(PIN_SD_CS, spiSD, speeds[i]); delay(1); }
  if(sdOK) ensureDirs(); else Serial.println("[SD] init failed");
  return sdOK;
}
static void sdScanDucky(){
  duckyCount=0; duckyIdx=0;
  if(!SD.exists(DUCKY_DIR)){ SD.mkdir(DUCKY_DIR); return; }
  File dir=SD.open(DUCKY_DIR); if(!dir) return;
  while(true){
    File e=dir.openNextFile(); if(!e) break;
    if(!e.isDirectory()){
      String full=String(DUCKY_DIR)+"/"+String(e.name());
      if(duckyCount<(int)(sizeof(duckyFiles)/sizeof(duckyFiles[0]))) duckyFiles[duckyCount++]=full;
    }
    e.close(); delay(0);
  }
  dir.close();
  if(duckyCount==0){
    File f=SD.open(String(DUCKY_DIR)+"/README.txt", FILE_WRITE);
    if(f){ f.println("Put your rubber ducky scripts here."); f.close(); }
  }
}
static String currentDuckyName(){
  if(!duckyCount) return String("(no files in ")+DUCKY_DIR+")";
  String p=duckyFiles[duckyIdx]; int s=p.lastIndexOf('/'); return (s>=0)? p.substring(s+1):p;
}

// ---------------------- Clock / NTP ----------------------
static uint32_t lastClockTick=0;
static int clk_h=12, clk_m=0;
static bool clockUseNTP=false;
static bool clockIs12h=false;  // persisted 12/24h format

static void setClockManual(int hh,int mm){ clk_h=constrain(hh,0,23); clk_m=constrain(mm,0,59); lastClockTick=millis(); clockUseNTP=false; }
static void tickClock(){
  if(clockUseNTP) return;
  uint32_t now=millis();
  if(now-lastClockTick>=60000 || lastClockTick==0){
    lastClockTick=(lastClockTick==0)?now:lastClockTick+60000;
    if(++clk_m>=60){ clk_m=0; clk_h=(clk_h+1)%24; }
  }
}
static void applyTZ(){ setenv("TZ","EST5EDT,M3.2.0/2,M11.1.0/2",1); tzset(); }
static void startNTP(){ applyTZ(); configTime(0,0,"pool.ntp.org","time.nist.gov"); }
static bool getClockStr(char* out5){
  int h = clk_h;
  if(clockUseNTP){
    struct tm tinfo;
    if(getLocalTime(&tinfo, 100)){ h = tinfo.tm_hour; clk_m = tinfo.tm_min; }
  }
  int disp_h = h;
  if (clockIs12h) {
    disp_h = h % 12;
    if (disp_h == 0) disp_h = 12;
  }
  snprintf(out5,6,"%02d:%02d",disp_h,clk_m);
  return true;
}

// ---------------------- Battery (ADC4 ÷2) + charging ----------------
static const int BAT_ADC_PIN=4;
static const float BAT_DIV=2.00f;
static float batEMA=-1.0f;
static int readBatteryPercent(){
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  uint32_t mv=analogReadMilliVolts(BAT_ADC_PIN);
  float vbat=(mv/1000.0f)*BAT_DIV;
  if(batEMA<0) batEMA=vbat;
  batEMA=batEMA*0.85f + vbat*0.15f;
  float pct=(batEMA-3.30f)/(4.20f-3.30f);
  if(pct<0) pct=0; if(pct>1) pct=1;
  return int(pct*100+0.5f);
}
static bool isCharging(){
#if CHARGE_SENSE_PIN >= 0
  pinMode(CHARGE_SENSE_PIN, INPUT);
  return digitalRead(CHARGE_SENSE_PIN)==HIGH;
#else
  return usbHIDReady;   // show bolt when HID/USB active
#endif
}

// ---------------------- Prefs ----------------------
Preferences prefs;
static bool touchFlipX=false, touchFlipY=false;
static String wifiSSID="", wifiPASS="";
static bool wifiAuto=true;
static void loadPrefs(){
  prefs.begin("hdeck", true);
  ACCENT = prefs.getUShort("accent", C(0,255,0));
  colorIdx = prefs.getUChar("accentIdx", findAccentIndex());
  touchFlipX = prefs.getBool("flipX", false);
  touchFlipY = prefs.getBool("flipY", false);
  int h=prefs.getUChar("clockH",12), m=prefs.getUChar("clockM",0);
  clockIs12h = prefs.getBool("clock12", false);
  wifiSSID = prefs.getString("w_ssid","");
  wifiPASS = prefs.getString("w_pass","");
  wifiAuto = prefs.getBool("w_auto", true);
  prefs.end();
  setClockManual(h,m);
  if(colorIdx<0 || colorIdx>=kColorCount) colorIdx=findAccentIndex();
  setAccent(kColors[colorIdx]);
}
static void savePrefs(){
  prefs.begin("hdeck", false);
  prefs.putUShort("accent", ACCENT);
  prefs.putUChar("accentIdx", (uint8_t)colorIdx);
  prefs.putBool("flipX", touchFlipX);
  prefs.putBool("flipY", touchFlipY);
  prefs.putUChar("clockH", (uint8_t)clk_h);
  prefs.putUChar("clockM", (uint8_t)clk_m);
  prefs.putBool("clock12", clockIs12h);
  prefs.putString("w_ssid", wifiSSID);
  prefs.putString("w_pass", wifiPASS);
  prefs.putBool("w_auto", wifiAuto);
  prefs.end();
}

// ---------------------- Trackball on shared I2C (IDF API) ----------
static bool hasTrackball=false;
static uint8_t tbAddr=0x0A;

static bool i2c_read_u8(uint8_t addr, uint8_t reg, uint8_t* out){
  uint8_t r = reg;
  esp_err_t ok = i2c_master_write_read_device(I2C_NUM_1, addr, &r, 1, out, 1, pdMS_TO_TICKS(50));
  return ok == ESP_OK;
}
static void tb_autodetect_once(){
  if(hasTrackball) return;
  for(uint8_t a : {uint8_t(0x0A), uint8_t(0x0B), uint8_t(0x0C), uint8_t(0x0D)}){
    uint8_t v=0;
    if(i2c_read_u8(a, 0x01, &v)){ hasTrackball=true; tbAddr=a; Serial.printf("[trackball] found at 0x%02X\n", a); break; }
    delay(2);
  }
  if(!hasTrackball) Serial.println("[trackball] not found on I2C_NUM_1");
}
static bool tb_read(uint8_t reg,uint8_t* val){
  if(!hasTrackball) return false;
  return i2c_read_u8(tbAddr, reg, val);
}
static volatile bool g_tbSelectRequested=false;

static void pollTrackball(){
  if(!hasTrackball) return;
  uint8_t L=0,R=0,U=0,D=0,B=0;
  tb_read(0x01,&B);
  tb_read(0x04,&L);
  tb_read(0x05,&R);
  tb_read(0x06,&U);
  tb_read(0x07,&D);

  auto navLeft=[&](){
    switch(state){
      case ScreenState::MAIN: mainIndex=(mainIndex-1+6)%6; break;
      case ScreenState::USB:  usbIndex =(usbIndex -1+3)%3; break;
      case ScreenState::OUI:  ouiIndex =(ouiIndex -1+3)%3; break;
      case ScreenState::SETTINGS: setIndex=(setIndex-1+4)%4; break; // 4 items now
      case ScreenState::FILE_PICKER: if(duckyCount) duckyIdx=(duckyIdx-1+duckyCount)%duckyCount; break;
      case ScreenState::SETTINGS_COLOR: colorIdx=(colorIdx-1+kColorCount)%kColorCount; setAccent(kColors[colorIdx]); break;
      case ScreenState::SETTINGS_TIME: clk_h=(clk_h+23)%24; break;
      default: break;
    }
  };
  auto navRight=[&](){
    switch(state){
      case ScreenState::MAIN: mainIndex=(mainIndex+1)%6; break;
      case ScreenState::USB:  usbIndex =(usbIndex +1)%3; break;
      case ScreenState::OUI:  ouiIndex =(ouiIndex +1)%3; break;
      case ScreenState::SETTINGS: setIndex=(setIndex+1)%4; break; // 4 items now
      case ScreenState::FILE_PICKER: if(duckyCount) duckyIdx=(duckyIdx+1)%duckyCount; break;
      case ScreenState::SETTINGS_COLOR: colorIdx=(colorIdx+1)%kColorCount; setAccent(kColors[colorIdx]); break;
      case ScreenState::SETTINGS_TIME: clk_m=(clk_m+1)%60; break;
      default: break;
    }
  };

  if(L>2) navLeft();
  if(R>2) navRight();
  if(B & 0x01) { g_tbSelectRequested=true; }
}

// ---------------------- Ducky runner + overlay ----------------------
static bool isRunningDucky=false; static uint32_t duckyDefaultDelay=0;
static const char* OVERLAY_PNG_PATH = "/deploy.png";

static const char* findOverlayPNG(){
  if(!initSD_ondemand()) return nullptr;
  if(SD.exists(OVERLAY_PNG_PATH)) return OVERLAY_PNG_PATH;
  return nullptr;
}
static bool drawOverlayPNGFullScreen(){
  const char* p = findOverlayPNG();
  if(!p) { Serial.println("[overlay] /deploy.png not found"); return false; }
#if defined(LGFX_USE_PNG) && LGFX_USE_PNG
  auto ok = frame.drawPngFile(SD, p, 0, 0);
  if(!ok) Serial.println("[overlay] PNG decode failed; falling back to sprite");
  return ok;
#else
  Serial.println("[overlay] LGFX_USE_PNG disabled at build time");
  return false;
#endif
}
static void drawBombSpriteCoverFull(){
  int scaleX = (SCR_W + BOMB_W - 1) / (int)BOMB_W;
  int scaleY = (SCR_H + BOMB_H - 1) / (int)BOMB_H;
  int scale  = (scaleX > scaleY) ? scaleX : scaleY;
  const int dstW = BOMB_W * scale;
  const int dstH = BOMB_H * scale;
  const int x0 = (SCR_W - dstW) / 2;
  const int y0 = (SCR_H - dstH) / 2;

  frame.startWrite();
  for (int y = 0; y < (int)BOMB_H; ++y) {
    for (int x = 0; x < (int)BOMB_W; ++x) {
      uint16_t col = pgm_read_word(&bomb_sprite[y * BOMB_W + x]);
      if (col == BOMB_TRANSPARENCY_KEY) continue;
      frame.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, col);
    }
  }
  frame.endWrite();
}
static void drawCenterDeployText(){
  frame.setTextFont(1);
  frame.setTextSize(2);
  frame.setTextColor(ACCENT, TFT_BLACK);
  const char* s="Deploying payload...";
  int tw = frame.textWidth(s);
  int th = frame.fontHeight();
  int tx = (SCR_W - tw) / 2;
  int ty = (SCR_H - th) / 2 + th * 2;   // nudged down ~2 lines
  frame.setCursor(tx, ty);
  frame.print(s);
}
static void showDeployOverlayBegin(){
  if(!drawOverlayPNGFullScreen()){
    frame.fillScreen(TFT_BLACK);
    drawBombSpriteCoverFull();
  }
  drawCenterDeployText();
  frame.pushSprite(0,0);
}
static void showDeployOverlayPulse(){
  static uint8_t d=0; d=(d+1)%8;
  frame.fillRect(20, SCR_H-18, SCR_W-40, 6, C(10,10,10));
  frame.fillRect(20, SCR_H-18, map(d,0,7,20,SCR_W-40), 6, ACCENT);
  frame.pushSprite(0,0);
}
static void showDeployOverlayEnd(){}

static void duckyPressEnter(){ DuckKB.press(KEY_RETURN); DuckKB.releaseAll(); }
static void duckyPressGUI(const String& rest){
  DuckKB.press(KEY_LEFT_GUI);
  delay(100);
  if(rest.length()){ DuckKB.print(rest.c_str()); }
  DuckKB.releaseAll();
}
static void duckyChord(bool ctrl,bool alt,bool shift,bool gui,const String& tail){
  if(ctrl) DuckKB.press(KEY_LEFT_CTRL);
  if(alt)  DuckKB.press(KEY_LEFT_ALT);
  if(shift)DuckKB.press(KEY_LEFT_SHIFT);
  if(gui)  DuckKB.press(KEY_LEFT_GUI);
  delay(60);
  if(tail.length()) DuckKB.print(tail.c_str());
  DuckKB.releaseAll();
}
static void waitForHostReady(){
  uint32_t t0 = millis();
  while(millis()-t0 < 2500){ showDeployOverlayPulse(); delay(120); }
}
static void runDuckyFile(const String& path){
  initUSBHID();
  File f=SD.open(path);
  if(!f){ Serial.println("[ducky] failed to open payload"); return; }

  isRunningDucky=true; duckyDefaultDelay=0;
  showDeployOverlayBegin();
  waitForHostReady();

  while(f.available() && isRunningDucky){
    String line=f.readStringUntil('\n');
    line.replace("\r",""); line.trim();   // normalize EOLs
    if(line.length()==0){ if(duckyDefaultDelay) delay(duckyDefaultDelay); showDeployOverlayPulse(); continue; }
    if(line.startsWith("REM")) { showDeployOverlayPulse(); continue; }
    if(line.startsWith("DELAY ")){ delay(line.substring(6).toInt()); showDeployOverlayPulse(); continue; }
    if(line.startsWith("DEFAULT_DELAY ")||line.startsWith("DEFAULTDELAY ")){
      duckyDefaultDelay=line.substring(line.indexOf(' ')+1).toInt(); showDeployOverlayPulse(); continue; }
    if(line.startsWith("STRING ")){ DuckKB.print(line.substring(7).c_str()); delay(40); if(duckyDefaultDelay) delay(duckyDefaultDelay); showDeployOverlayPulse(); continue; }
    if(line=="ENTER"||line=="RETURN"){ duckyPressEnter(); if(duckyDefaultDelay) delay(duckyDefaultDelay); showDeployOverlayPulse(); continue; }
    if(line.startsWith("GUI ")){ duckyPressGUI(line.substring(4)); if(duckyDefaultDelay) delay(duckyDefaultDelay); showDeployOverlayPulse(); continue; }

    bool ctrl=false,alt=false,shift=false,gui=false; String tail=""; int p=0;
    while(p<(int)line.length()){
      int sp=line.indexOf(' ',p); if(sp<0) sp=line.length();
      String tok=line.substring(p,sp); tok.trim();
      if(tok=="CTRL"||tok=="CONTROL") ctrl=true;
      else if(tok=="ALT") alt=true;
      else if(tok=="SHIFT") shift=true;
      else if(tok=="GUI"||tok=="WIN") gui=true;
      else tail=tok; p=sp+1;
    }
    duckyChord(ctrl,alt,shift,gui,tail);
    if(duckyDefaultDelay) delay(duckyDefaultDelay);
    showDeployOverlayPulse();
    delay(0);
  }
  f.close();
  isRunningDucky=false;
  showDeployOverlayEnd();
}

// ---------------------- Drawing helpers ----------------------
static void drawIconWiFi(int x,int y,uint16_t col){
  int cx=x+6, cy=y+8;
  frame.fillRect(x,y,12,6,TFT_BLACK);
  frame.drawCircle(cx,cy,5,col);
  frame.drawCircle(cx,cy,3,col);
  frame.fillCircle(cx,cy,1,col);
}
static void drawIconBLE(int x,int y,uint16_t col){
  int cx=x+6; int y0=y+1, y1=y+10;
  frame.drawLine(cx,y0,cx,y1,col);
  frame.drawLine(cx,y0,cx+4,y+4,col);
  frame.drawLine(cx,y1,cx+4,y+6,col);
  frame.drawLine(cx,y0,cx-4,y+4,col);
  frame.drawLine(cx,y1,cx-4,y+6,col);
}
static void drawIconGPS(int x,int y,uint16_t col){
  int cx=x+6, cy=y+6;
  frame.drawCircle(cx,cy,5,col);
  frame.drawLine(cx-6,cy, cx+6,cy,col);
  frame.drawLine(cx,cy-6, cx,cy+6,col);
}
static void drawIconUSB(int x,int y,uint16_t col){
  int cx=x+6, y0=y+2, y1=y+10;
  frame.drawLine(cx,y0,cx,y1,col);
  frame.drawTriangle(cx,y0-2, cx-2,y0+1, cx+2,y0+1, col);
  frame.drawLine(cx,y0+4, cx-4,y0+4, col);
  frame.fillCircle(cx-5,y0+4,1,col);
  frame.drawLine(cx,y0+6, cx+4,y0+6, col);
  frame.fillRect(cx+4,y0+5,2,2,col);
}
static void drawIconBolt(int x,int y,uint16_t col){
  frame.drawLine(x+3,y,   x,  y+6, col);
  frame.drawLine(x,  y+6, x+4,y+6, col);
  frame.drawLine(x+4,y+6, x+1,y+12,col);
}

static void drawBackground(){
  frame.fillScreen(TFT_BLACK);
  frame.setTextFont(1); frame.setTextSize(1);
  for(int row=0; row<ROWS; ++row){
    for(int col=0; col<COLS; ++col){
      int x=col*CELL_W, y=row*CELL_H;
      int ic=col-IMG_X_OFFSET, ir=row-IMG_Y_OFFSET;
      char src=' ';
      if(ic>=0 && ir>=0){
        size_t idx=(size_t)ir*COLS+(size_t)ic;
        if(idx<IMAGE_LEN) src=pgm_read_byte(&image[idx]);
      }
      if(src!=' '){
        frame.setTextColor(ACCENT,TFT_BLACK);
        frame.drawChar(charsSet[random(0,5)], x,y);
      }else{
        int g=random(BG_GRAY_MIN, BG_GRAY_MAX+1);
        frame.setTextColor(C(g,g,g),TFT_BLACK);
        frame.drawChar(char('0'+random(0,10)), x,y);
      }
    }
  }
}
static void computeCardRect(){
  int topY = TITLE_H + ASCII_ROWS*8 + 6;
  int botY = SCR_H - SOFTKEY_H - 6;
  int availH = max(24, botY - topY);
  int desired = min(92, availH - 12);
  int y = topY + (availH - desired)/2 - CARD_UPWARD_BIAS;
  if (y < topY) y = topY;
  CARD.y = y; CARD.h = desired;
}
static const char* titleForState(){
  switch(state){
    case ScreenState::MAIN:           return "Hacker T-Deck";
    case ScreenState::USB:
    case ScreenState::FILE_PICKER:
    case ScreenState::RUN_DUCKY:      return "USB Tools";
    case ScreenState::WIFI:
    case ScreenState::SETTINGS_WIFI:  return "WiFi Tools";
    case ScreenState::BTE:            return "BLE Tools";
    case ScreenState::OUI:
    case ScreenState::OUI_PICKER:     return "OUI Foxhunter";
    case ScreenState::SETTINGS:
    case ScreenState::SETTINGS_COLOR:
    case ScreenState::SETTINGS_TIME:  return "Settings";
    case ScreenState::ABOUT:          return "About";
    default:                          return "Hacker T-Deck";
  }
}
static void drawTitleBar(){
  frame.fillRect(0,0,SCR_W,TITLE_H,TFT_BLACK);
  frame.drawLine(0,TITLE_H-1,SCR_W,TITLE_H-1, C(0,60,0));
  frame.setTextFont(1); frame.setTextSize(1); frame.setTextColor(ACCENT,TFT_BLACK);
  frame.setCursor(6,(TITLE_H-8)/2); frame.print(titleForState());

  char clockBuf[6]; getClockStr(clockBuf);
  int w=frame.textWidth(clockBuf); frame.setCursor((SCR_W-w)/2,(TITLE_H-8)/2); frame.print(clockBuf);

  bool wifiAct = (WiFi.status()==WL_CONNECTED) || (state==ScreenState::WIFI || state==ScreenState::SETTINGS_WIFI);
  bool bleAct  = (state==ScreenState::BTE);
  bool usbAct  = (state==ScreenState::USB) || isRunningDucky;
  bool chg     = isCharging();

  int iconX = SCR_W - 6;
  if(usbAct){ iconX -= 14; drawIconUSB(iconX, (TITLE_H-12)/2, ACCENT); }
  if(bleAct){ iconX -= 14; drawIconBLE(iconX, (TITLE_H-12)/2, ACCENT); }
  if(wifiAct){ iconX -= 14; drawIconWiFi(iconX, (TITLE_H-12)/2, ACCENT); }

  String btxt=String(readBatteryPercent())+"%";
  int bw=frame.textWidth(btxt);
  int bx = max(iconX - bw - 6, 0);
  if(chg){ drawIconBolt(bx-10, (TITLE_H-12)/2, ACCENT); }
  frame.setCursor(bx,(TITLE_H-8)/2); frame.print(btxt);

  frame.setTextFont(1); frame.setTextSize(2);
  frame.setTextColor(ACCENT, TFT_BLACK);
  const char* tag="T-Deck";
  int tw=frame.textWidth(tag);
  int tx = SCR_W - tw - 10;
  int ty = TITLE_H + ASCII_ROWS*8 - 22;
  if(ty < TITLE_H+2) ty = TITLE_H+2;
  frame.setCursor(tx, ty);
  frame.print(tag);
}

// softkey helpers
static void drawSoftkeys(const char* l,const char* m,const char* r){
  frame.fillRect(0,SCR_H-SOFTKEY_H,SCR_W,SOFTKEY_H,TFT_BLACK);
  frame.drawLine(0,SCR_H-SOFTKEY_H,SCR_W,SCR_H-SOFTKEY_H, C(0,60,0));

  auto btn=[&](const UiRect& rc,const char* s, SoftBtn id){
    bool p = BTN_PRESSED(id);
    if(p){
      frame.fillRect(rc.x,rc.y,rc.w,rc.h, ACCENT);
      frame.drawRect(rc.x,rc.y,rc.w,rc.h, ACCENT);
      frame.setTextColor(TFT_BLACK, ACCENT);
    }else{
      frame.fillRect(rc.x,rc.y,rc.w,rc.h, TFT_BLACK);
      frame.drawRect(rc.x,rc.y,rc.w,rc.h, ACCENT);
      frame.setTextColor(ACCENT, TFT_BLACK);
    }
    frame.setTextFont(1); frame.setTextSize(1);
    int ty=rc.y+(SOFTKEY_H-8)/2, tw=frame.textWidth(s), tx=rc.x+(rc.w-tw)/2;
    frame.setCursor(tx,ty); frame.print(s);
  };
  btn(BTN_LEFT, l, SoftBtn::LEFT);
  btn(BTN_MID , m, SoftBtn::MID );
  btn(BTN_RIGHT,r, SoftBtn::RIGHT);
}

// center card
static void drawCenterCard(const char* label, uint16_t labelColor){
  computeCardRect();
  frame.fillRoundRect(CARD.x,CARD.y,CARD.w,CARD.h,12, C(10,10,10));
  frame.drawRoundRect(CARD.x,CARD.y,CARD.w,CARD.h,12, ACCENT);
  frame.setTextFont(1); frame.setTextSize(2);
  frame.setTextColor(labelColor?labelColor:ACCENT, C(10,10,10));
  int tw=frame.textWidth(label), tx=CARD.x+(CARD.w-tw)/2, ty=CARD.y + CARD.h/2 - 8;
  frame.setCursor(tx,ty); frame.print(label);

  if(state==ScreenState::USB && strcmp(label,"Deploy payload")==0 && selectedPayload.length()){
    frame.setTextSize(1); frame.setTextColor(C(160,255,160), C(10,10,10));
    String s=String("Payload: ")+selectedPayload; while(frame.textWidth(s)>CARD.w-20 && s.length()>5) s.remove(s.length()-2);
    frame.setCursor(CARD.x+10, CARD.y + CARD.h - 14); frame.print(s);
  }
}
static inline void drawCenterCard(const char* label){ drawCenterCard(label, 0); }

// ---------------------- About page (static, centered) ----------------------
static void drawAbout(){
  computeCardRect();
  frame.fillRoundRect(CARD.x,CARD.y,CARD.w,CARD.h,12, C(10,10,10));
  frame.drawRoundRect(CARD.x,CARD.y,CARD.w,CARD.h,12, ACCENT);

  const char* lines[] = {
    "Hack3r T-Deck v1.0",
    "Created by: out0fstep",
    "github.com/out0fstep?tab=repositories"
  };
  const int nLines = 3;

  int innerX = CARD.x + 8;
  int innerY = CARD.y + 8;
  int innerW = CARD.w - 16;
  int innerH = CARD.h - 16;

  frame.fillRect(innerX, innerY, innerW, innerH, C(10,10,10));
  frame.setTextFont(1);
  frame.setTextSize(1);
  frame.setTextColor(ACCENT, C(10,10,10));

  const int lh = 14;
  int blockH = nLines * lh;
  int y = innerY + (innerH - blockH)/2;

  for(int i=0;i<nLines;i++){
    int tw = frame.textWidth(lines[i]);
    int x = innerX + (innerW - tw)/2;
    frame.setCursor(x, y);
    frame.print(lines[i]);
    y += lh;
  }
}

// ---------------------- Render UI ----------------------
static void renderListCentered(const char* line){ drawCenterCard(line); }
static void drawSoftkeysForState(){
  switch(state){
    case ScreenState::ABOUT:
      // Center shows Back for About page
      drawSoftkeys("","Back",""); break;

    case ScreenState::SETTINGS_COLOR:
      // show << Select >> in color picker
      drawSoftkeys("<<","Select",">>"); break;

    case ScreenState::SETTINGS_TIME:
      drawSoftkeys("HH-","12/24","MM+"); break;  // mid toggles format + saves

    case ScreenState::SETTINGS_WIFI:
      drawSoftkeys("SSID","Connect","PASS"); break;

    case ScreenState::RUN_DUCKY:
      drawSoftkeys("","Cancel",""); break;

    case ScreenState::MAIN:
    case ScreenState::USB:
    case ScreenState::OUI:
    case ScreenState::SETTINGS:
    case ScreenState::FILE_PICKER:
      drawSoftkeys("<<","Select",">>"); break;

    default:
      drawSoftkeys("","",""); break;
  }
}
static void renderUI(){
  drawTitleBar();
  switch(state){
    case ScreenState::MAIN:         renderListCentered(mainItems[mainIndex]); break;
    case ScreenState::USB:          renderListCentered(usbItems[usbIndex]); break;
    case ScreenState::WIFI:         renderListCentered("WiFi (UI coming)"); break;
    case ScreenState::BTE:          renderListCentered("BLE (UI coming)"); break;
    case ScreenState::OUI:          renderListCentered(ouiItems[ouiIndex]); break;
    case ScreenState::SETTINGS:     renderListCentered(setItems[setIndex]); break;
    case ScreenState::FILE_PICKER:  { String n=currentDuckyName(); renderListCentered(n.c_str()); } break;
    case ScreenState::OUI_PICKER:   renderListCentered("Pick OUI (later)"); break;
    case ScreenState::SETTINGS_COLOR: drawCenterCard(kColorNames[colorIdx], kColors[colorIdx]); break;
    case ScreenState::SETTINGS_TIME: {
      char line[32];
      snprintf(line,sizeof(line),"Clock %s %02d:%02d", clockIs12h?"12h":"24h", clk_h, clk_m);
      renderListCentered(line);
    } break;
    case ScreenState::SETTINGS_WIFI: {
      computeCardRect();
      frame.fillRoundRect(CARD.x,CARD.y,CARD.w,CARD.h,12, C(10,10,10));
      frame.drawRoundRect(CARD.x,CARD.y,CARD.w,CARD.h,12, ACCENT);
      frame.setTextFont(1); frame.setTextSize(1);
      frame.setTextColor(ACCENT, C(10,10,10));
      frame.setCursor(CARD.x+10, CARD.y+10); frame.print("Wi-Fi Settings");
      frame.setTextColor(C(180,255,180), C(10,10,10));
      frame.setCursor(CARD.x+10, CARD.y+30); frame.print(String("SSID: ")+ (wifiSSID.length()?wifiSSID:"(not set)"));
      String masked = wifiPASS; for(size_t i=0;i<masked.length();++i) masked.setCharAt(i,'*');
      frame.setCursor(CARD.x+10, CARD.y+46); frame.print(String("PASS: ")+ (wifiPASS.length()?masked:"(not set)"));
      wl_status_t st = WiFi.status();
      const char* s = (st==WL_CONNECTED) ? "Connected" :
                      (st==WL_IDLE_STATUS) ? "Idle" :
                      (st==WL_CONNECT_FAILED) ? "Failed" :
                      (st==WL_DISCONNECTED) ? "Disconnected" : "Status...";
      frame.setCursor(CARD.x+10, CARD.y+66); frame.print(String("State: ")+s);
    } break;
    case ScreenState::ABOUT:        drawAbout(); break;
    case ScreenState::RUN_DUCKY:    /* overlay draws itself */ break;
    default: break;
  }
  drawSoftkeysForState();
}

// ---------------------- Navigation / Input ----------------------
static void uiLeft(){
  switch(state){
    case ScreenState::MAIN: mainIndex=(mainIndex-1+6)%6; break;
    case ScreenState::USB:  usbIndex =(usbIndex -1+3)%3; break;
    case ScreenState::OUI:  ouiIndex =(ouiIndex -1+3)%3; break;
    case ScreenState::SETTINGS: setIndex=(setIndex-1+4)%4; break; // 4 items now
    case ScreenState::FILE_PICKER: if(duckyCount) duckyIdx=(duckyIdx-1+duckyCount)%duckyCount; break;
    case ScreenState::SETTINGS_COLOR: colorIdx=(colorIdx-1+kColorCount)%kColorCount; setAccent(kColors[colorIdx]); break;
    case ScreenState::SETTINGS_TIME: clk_h=(clk_h+23)%24; break;
    default: break;
  }
}
static void uiMid(){
  switch(state){
    case ScreenState::MAIN:
      switch(mainIndex){
        case 0: state=ScreenState::USB; break;
        case 1: state=ScreenState::WIFI; break;
        case 2: state=ScreenState::BTE; break;
        case 3: state=ScreenState::OUI; break;
        case 4: state=ScreenState::SETTINGS; break;
        case 5:
          g_aboutBackTo = ScreenState::SETTINGS;   // About should go "Back" to Settings
          state=ScreenState::ABOUT;
          break;
      } break;

    case ScreenState::USB:
      if(usbIndex==0){ if(initSD_ondemand()) sdScanDucky(); state=ScreenState::FILE_PICKER; }
      else if(usbIndex==1){
        if(selectedPayload.length()){
          ScreenState prev=state; state=ScreenState::RUN_DUCKY;
          runDuckyFile(selectedPayload);
          state=prev;
        }
      }
      else state=ScreenState::MAIN; // Back
      break;

    case ScreenState::FILE_PICKER:
      if(duckyCount) selectedPayload=duckyFiles[duckyIdx];
      state=ScreenState::USB;
      break;

    case ScreenState::OUI:
      if(ouiIndex==0) state=ScreenState::OUI_PICKER;
      else if(ouiIndex==1){ /* TODO hunt */ }
      else state=ScreenState::MAIN;
      break;

    case ScreenState::SETTINGS:
      switch(setIndex){
        case 0: colorIdx=findAccentIndex(); state=ScreenState::SETTINGS_COLOR; break;
        case 1: state=ScreenState::SETTINGS_TIME; break;
        case 2: state=ScreenState::SETTINGS_WIFI; break;
        default: savePrefs(); state=ScreenState::MAIN; break; // Back
      } break;

    case ScreenState::SETTINGS_COLOR:
      // Save chosen color and return to Settings list
      savePrefs();
      state = ScreenState::SETTINGS;
      break;

    case ScreenState::SETTINGS_TIME:
      // Mid toggles 12/24h and saves, remain on the page
      clockIs12h = !clockIs12h;
      savePrefs();
      break;

    case ScreenState::SETTINGS_WIFI:
      if(wifiSSID.length()==0 || wifiPASS.length()==0) break;
      WiFi.mode(WIFI_STA);
      WiFi.disconnect(true,true); delay(50);
      WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
      break;

    case ScreenState::ABOUT:
      // Back → go where we set earlier (Settings)
      state = g_aboutBackTo;
      break;

    default: break;
  }
}
static void uiRight(){
  switch(state){
    case ScreenState::MAIN: mainIndex=(mainIndex+1)%6; break;
    case ScreenState::USB:  usbIndex =(usbIndex +1)%3; break;
    case ScreenState::OUI:  ouiIndex =(ouiIndex +1)%3; break;
    case ScreenState::SETTINGS: setIndex=(setIndex+1)%4; break; // 4 items now
    case ScreenState::FILE_PICKER: if(duckyCount) duckyIdx=(duckyIdx+1)%duckyCount; break;
    case ScreenState::SETTINGS_COLOR: colorIdx=(colorIdx+1)%kColorCount; setAccent(kColors[colorIdx]); break;
    case ScreenState::SETTINGS_TIME: clk_m=(clk_m+1)%60; break;
    default: break;
  }
}

// ---------------------- Touch (tap + repeat) ----------------------
static void handleTap(uint16_t x,uint16_t y){
  if(UI_POINT_IN(BTN_LEFT,x,y))  { FLASH(SoftBtn::LEFT);  uiLeft();  return; }
  if(UI_POINT_IN(BTN_MID,x,y))   { FLASH(SoftBtn::MID);   uiMid();   return; }
  if(UI_POINT_IN(BTN_RIGHT,x,y)) { FLASH(SoftBtn::RIGHT); uiRight(); return; }
  if(UI_POINT_IN(CARD,x,y))      { FLASH(SoftBtn::MID);   uiMid();   return; }
}
static void pollTouch(){
  uint16_t x=0,y=0;
  bool touching=tft.getTouch(&x,&y);
  if(touching){ if(touchFlipX) x = SCR_W-1 - x; if(touchFlipY) y = SCR_H-1 - y; }
  static bool down=false; static uint32_t tDown=0, tLast=0; uint32_t now=millis();
  if(touching && !down){ down=true; tDown=now; tLast=now; handleTap(x,y); }
  else if(touching && down){ if(now - tDown > 400 && now - tLast > 220){ tLast=now; handleTap(x,y); } }
  else if(!touching && down){ down=false; }
}

// ---------------------- Arduino lifecycle ----------------------
void setup(){
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK); tft.setBrightness(255);
  frame.setColorDepth(16); frame.createSprite(SCR_W,SCR_H); frame.setTextFont(1); frame.setTextWrap(false);
  randomSeed(esp_random()); compute_image_len_once(); loadPrefs();

  // Touch / I2C bring-up
  { uint16_t dx,dy; (void)tft.getTouch(&dx,&dy); }
  tb_autodetect_once();

  if(wifiAuto && wifiSSID.length()){
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  }

  applyTZ();
#if CHARGE_SENSE_PIN >= 0
  pinMode(CHARGE_SENSE_PIN, INPUT);
#endif
}

void loop(){
  if(WiFi.status()==WL_CONNECTED && !clockUseNTP){
    static uint32_t tTry=0;
    if(millis()-tTry>500){ tTry=millis(); startNTP(); }
    struct tm tinfo;
    if(getLocalTime(&tinfo, 2000)){ clockUseNTP=true; }
  }
  tickClock();

  if(state!=ScreenState::RUN_DUCKY){
    drawBackground();
    renderUI();
    frame.pushSprite(0,0);
  }

  pollTrackball();
  if(g_tbSelectRequested){
    FLASH(SoftBtn::MID);
    uiMid();
    g_tbSelectRequested=false;
  }

  pollTouch();
  delay(60);
}
