#include <Arduino.h>
#include "Audio.h"
#include "SD.h"
#include "FS.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

/* ================= I2S ================= */
#define I2S_DOUT 27
#define I2S_BCLK 26
#define I2S_LRC  25

/* ================= SD ================= */
#define SD_CS 5

/* ================= TTP223 ================= */
#define BTN_PLAY 4
#define BTN_NEXT 15
#define BTN_PREV 33

#define LONG_PRESS_TIME   700
#define VERY_LONG_PRESS   3000
#define LOCK_TIME         250
#define VOL_REPEAT_TIME   220   // slower, smoother volume ramp

/* ================= OLED ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

/* ================= OBJECTS ================= */
Audio audio;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Preferences prefs;

/* ================= MUSIC ================= */
#define MAX_TRACKS 30
String tracks[MAX_TRACKS];
int totalTracks = 0;
int currentTrack = 0;

/* ================= STATE ================= */
bool playLast=false, nextLast=false, prevLast=false;
bool isPaused = true;
bool sleeping = false;
bool wasRunning = false;

unsigned long playPressTime = 0;
unsigned long nextPressTime = 0;
unsigned long prevPressTime = 0;
unsigned long lastAction = 0;
unsigned long lastVolStep = 0;

int volume = 14;

/* ================= UI ================= */
unsigned long lastUI = 0;
int waveShift = 0;
bool showVolumeUI = false;

/* album-art bar smoothing */
#define ART_BARS 5
float artBars[ART_BARS] = {0};

/* scroll */
int scrollX = 0;
unsigned long lastScroll = 0;

/* ================= FUNCTION DECLARATIONS ================= */
void scanMusic();
void playCurrent(bool restart = false);
void handleButtons();
void drawIdle();
void drawAlbumArtBars();
void drawVolumeUI();

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  pinMode(BTN_PLAY, INPUT);
  pinMode(BTN_NEXT, INPUT);
  pinMode(BTN_PREV, INPUT);

  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.setTextColor(SSD1306_WHITE);

  if (!SD.begin(SD_CS)) while (1);

  scanMusic();
  if (totalTracks == 0) while (1);

  prefs.begin("player", false);
  currentTrack = prefs.getInt("track", 0);
  volume       = prefs.getInt("vol", 14);
  if (currentTrack >= totalTracks) currentTrack = 0;

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(volume);

  drawIdle();
}

/* ================= LOOP ================= */
void loop() {
  if (!sleeping) audio.loop();
  handleButtons();

  bool running = audio.isRunning();

  /* ---- AUTO NEXT ---- */
  if (wasRunning && !running && !isPaused && !sleeping) {
    currentTrack = (currentTrack + 1) % totalTracks;
    playCurrent(true);
  }
  wasRunning = running;

  if (millis() - lastUI > 120 && !sleeping) {
    lastUI = millis();

    if (showVolumeUI)
      drawVolumeUI();
    else if (running && !isPaused)
      drawAlbumArtBars();
    else
      drawIdle();
  }
}

/* ================= MUSIC ================= */
void scanMusic() {
  File root = SD.open("/music");
  while (true) {
    File f = root.openNextFile();
    if (!f) break;
    if (!f.isDirectory()) {
      String n = f.name();
      n.toLowerCase();
      if (n.endsWith(".mp3"))
        tracks[totalTracks++] = "/music/" + String(f.name());
    }
    f.close();
  }
}

void playCurrent(bool restart) {
  if (restart) {
    audio.stopSong();
    delay(40);
    audio.connecttoFS(SD, tracks[currentTrack].c_str());
  } else {
    audio.pauseResume();
  }
  isPaused = false;
  prefs.putInt("track", currentTrack);
}

/* ================= BUTTONS ================= */
void handleButtons() {
  unsigned long now = millis();

  bool p = digitalRead(BTN_PLAY);
  bool n = digitalRead(BTN_NEXT);
  bool r = digitalRead(BTN_PREV);

  /* PLAY */
  if (p && !playLast) playPressTime = now;
  if (!p && playLast && now-lastAction>LOCK_TIME) {
    if (now-playPressTime > VERY_LONG_PRESS && isPaused) {
      sleeping = true;
      display.clearDisplay();
      display.display();
      audio.stopSong();
    } else if (now-playPressTime > LONG_PRESS_TIME) {
      playCurrent(true);
    } else {
      audio.pauseResume();
      isPaused = !isPaused;
    }
    lastAction = now;
  }
  playLast = p;

  /* NEXT (VOL DOWN) */
  if (n && !nextLast) nextPressTime = now;
  if (n && now-nextPressTime>LONG_PRESS_TIME && now-lastVolStep>VOL_REPEAT_TIME) {
    volume = max(0, volume-1);
    audio.setVolume(volume);
    prefs.putInt("vol", volume);
    lastVolStep = now;
    showVolumeUI = true;
  }
  if (!n && nextLast && now-lastAction>LOCK_TIME) {
    if (now-nextPressTime<=LONG_PRESS_TIME) {
      currentTrack = (currentTrack+1)%totalTracks;
      playCurrent(true);
    }
    showVolumeUI = false;
    lastAction = now;
  }
  nextLast = n;

  /* PREV (VOL UP) */
  if (r && !prevLast) prevPressTime = now;
  if (r && now-prevPressTime>LONG_PRESS_TIME && now-lastVolStep>VOL_REPEAT_TIME) {
    volume = min(21, volume+1);
    audio.setVolume(volume);
    prefs.putInt("vol", volume);
    lastVolStep = now;
    showVolumeUI = true;
  }
  if (!r && prevLast && now-lastAction>LOCK_TIME) {
    if (now-prevPressTime<=LONG_PRESS_TIME) {
      currentTrack--;
      if (currentTrack<0) currentTrack=totalTracks-1;
      playCurrent(true);
    }
    showVolumeUI = false;
    lastAction = now;
  }
  prevLast = r;
}

/* ================= UI ================= */
void drawIdle() {
  display.clearDisplay();
  for (int x=0;x<128;x+=8) {
    int y=32+sin((x+waveShift)*0.1)*10;
    display.fillCircle(x,y,2,SSD1306_WHITE);
  }
  display.setCursor(28,52);
  display.print("Press Play");
  display.display();
  waveShift+=4;
}

/* ===== ALBUM-ART STYLE BARS ===== */
void drawAlbumArtBars() {
  display.clearDisplay();

  int centerX = SCREEN_WIDTH / 2;
  int baseY   = 42;
  int barW    = 12;
  int gap     = 6;

  for (int i = 0; i < ART_BARS; i++) {
    float target = random(10, 34);
    artBars[i] = artBars[i]*0.75 + target*0.25;

    int h = artBars[i];
    int xL = centerX - (i+1)*(barW+gap);
    int xR = centerX + i*(barW+gap);

    display.fillRoundRect(xL, baseY-h, barW, h, 4, SSD1306_WHITE);
    display.fillRoundRect(xR, baseY-h, barW, h, 4, SSD1306_WHITE);
  }

  /* ---- Song name ---- */
  String name = tracks[currentTrack];
  name.remove(0, name.lastIndexOf("/") + 1);

  int textWidth = name.length() * 6;
  int y = 52;

  if (textWidth > SCREEN_WIDTH) {
    if (millis() - lastScroll > 200) {
      scrollX++;
      if (scrollX > textWidth) scrollX = 0;
      lastScroll = millis();
    }
    display.setCursor(-scrollX, y);
  } else {
    scrollX = 0;
    display.setCursor((SCREEN_WIDTH-textWidth)/2, y);
  }

  display.print(name);
  display.display();
}

void drawVolumeUI() {
  display.clearDisplay();
  display.drawRect(10,24,108,10,SSD1306_WHITE);
  int w=map(volume,0,21,0,106);
  display.fillRect(11,25,w,8,SSD1306_WHITE);
  display.setCursor(50,40);
  display.print(volume);
  display.display();
}
