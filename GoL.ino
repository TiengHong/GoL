#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 5
#define SCL_PIN 6
#define BOOT_PIN 9
#define NAV_PIN 2
#define LED_PIN 8
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

const int gridWidth = 120;
const int gridHeight = 60;
const int xOffset = 4;
const int yOffset = 2;

bool grid[60][120];
bool nextGrid[60][120];
uint8_t screenCap[1024];
uint8_t bmp[1086];

enum State { TITLE_ANIMATION, GAME_RUNNING, FUNCTION_MENU };
State currentState = TITLE_ANIMATION;

int menuIndex = 0;
int currentSpeed = 3;
bool inverted = false;
unsigned long lastUpdate = 0;
int updateInterval = 20;
unsigned long bootPressStart = 0;
bool bootWasPressed = false;
bool navWasPressed = false;

int gliderX, gliderY, gliderDX, gliderDY;

const char* menuItems[] = {"Screenshot", "Speed", "Burn-in PT", "Noise", "Quit"};
int speedDelays[] = {0, 90, 70, 50, 40, 30, 20, 10};

void resetToTitle();
void triggerNoiseStart();
void updateTitleAnimation();
void drawTitleAnimation();
void startFromTitle();
void drawGrid();
void computeNextGen();
int countNeighbors(int x, int y);
void spawnGlider(int x, int y);
void drawMenu();
void handleScreenshot();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nSystem Start");
  
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(NAV_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  Wire.begin(SDA_PIN, SCL_PIN);
  if(display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("connecting...");
    display.display();
  }

  Serial.print("WiFi: Connecting");
  WiFi.begin("G199_IoT_2.4G", "smart10_199");
  unsigned long startWait = millis();
  bool ledState = false;
  while (WiFi.status() != WL_CONNECTED && millis() - startWait < 15000) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    delay(500);
    Serial.print(".");
  }
  digitalWrite(LED_PIN, LOW);
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi: Connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi: Connected");
    display.println("IP:");
    display.println(WiFi.localIP());
    display.display();
    delay(2000);
  } else {
    Serial.println("\nWiFi: Failed (Timeout)");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi: Failed");
    display.display();
    delay(2000);
  }

  server.on("/", handleScreenshot);
  server.begin();

  randomSeed(esp_random());
  resetToTitle();
}

void loop() {
  server.handleClient();

  bool bootState = (digitalRead(BOOT_PIN) == LOW);
  bool navState = (digitalRead(NAV_PIN) == LOW);

  if (currentState != FUNCTION_MENU) {
    if (bootState && !bootWasPressed) {
      bootPressStart = millis();
      bootWasPressed = true;
    } else if (!bootState && bootWasPressed) {
      unsigned long duration = millis() - bootPressStart;
      if (duration > 500) {
        memcpy(screenCap, display.getBuffer(), 1024);
        currentState = FUNCTION_MENU;
      } else {
        spawnGlider(random(gridWidth - 2), random(gridHeight - 2));
      }
      bootWasPressed = false;
    }
  } else {
    if (navState && !navWasPressed) {
      menuIndex = (menuIndex + 1) % 5;
      navWasPressed = true;
      delay(200);
    } else if (!navState) {
      navWasPressed = false;
    }

    if (bootState && !bootWasPressed) {
      if (menuIndex == 0) {
      } else if (menuIndex == 1) {
        currentSpeed = (currentSpeed % 7) + 1;
      } else if (menuIndex == 2) {
        inverted = !inverted;
        display.invertDisplay(inverted);
      } else if (menuIndex == 3) {
        triggerNoiseStart();
      } else if (menuIndex == 4) {
        currentState = GAME_RUNNING;
      }
      bootWasPressed = true;
      delay(200);
    } else if (!bootState) {
      bootWasPressed = false;
    }
  }

  if (currentState == TITLE_ANIMATION) {
    updateTitleAnimation();
  } else if (currentState == GAME_RUNNING) {
    drawGrid();
    computeNextGen();
    delay(speedDelays[currentSpeed]);
  } else {
    drawMenu();
  }
}

void handleScreenshot() {
  memset(bmp, 0, 1086);
  uint8_t header[] = {
    0x42, 0x4D, 0x3E, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00,
    0x28, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00
  };
  memcpy(bmp, header, 62);
  
  for (int y = 0; y < 64; y++) {
    for (int x_byte = 0; x_byte < 16; x_byte++) {
      uint8_t outByte = 0;
      for (int b = 0; b < 8; b++) {
        int x = x_byte * 8 + b;
        int srcY = 63 - y;
        int byteIdx = x + (srcY / 8) * 128;
        if (screenCap[byteIdx] & (1 << (srcY % 8))) {
          outByte |= (1 << (7 - b));
        }
      }
      bmp[62 + y * 16 + x_byte] = outByte;
    }
  }
  server.setContentLength(1086);
  server.send(200, "image/bmp", "");
  server.sendContent((const char*)bmp, 1086);
  Serial.println("Capture Success");
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  int startIdx = (menuIndex / 2) * 2;
  for (int i = 0; i < 2; i++) {
    int idx = startIdx + i;
    if (idx >= 5) break;
    
    display.setCursor(10, 10 + i * 20);
    if (idx == 1) {
      display.print(menuItems[idx]);
      display.print(": ");
      display.print(currentSpeed);
    } else {
      display.print(menuItems[idx]);
    }
    
    if (idx == menuIndex) {
      display.drawFastHLine(10, 10 + i * 20 + 8, 80, SSD1306_WHITE);
    }
  }
  display.display();
}

void spawnGlider(int x, int y) {
  if (x < 0 || x >= gridWidth - 2 || y < 0 || y >= gridHeight - 2) return;
  grid[y][x+1] = true;
  grid[y+1][x+2] = true;
  grid[y+2][x] = true;
  grid[y+2][x+1] = true;
  grid[y+2][x+2] = true;
}

void resetToTitle() {
  currentState = TITLE_ANIMATION;
  int edge = random(4);
  if (edge == 0) { gliderX = random(SCREEN_WIDTH); gliderY = 0; gliderDX = (gliderX < 64) ? 1 : -1; gliderDY = 1; }
  else if (edge == 1) { gliderX = random(SCREEN_WIDTH); gliderY = 63; gliderDX = (gliderX < 64) ? 1 : -1; gliderDY = -1; }
  else if (edge == 2) { gliderX = 0; gliderY = random(SCREEN_HEIGHT); gliderDX = 1; gliderDY = (gliderY < 32) ? 1 : -1; }
  else { gliderX = 127; gliderY = random(SCREEN_HEIGHT); gliderDX = -1; gliderDY = (gliderY < 32) ? 1 : -1; }
}

void triggerNoiseStart() {
  randomSeed(analogRead(0));
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      grid[y][x] = random(2);
    }
  }
  currentState = GAME_RUNNING;
}

void updateTitleAnimation() {
  if (millis() - lastUpdate < updateInterval) return;
  lastUpdate = millis();
  gliderX += gliderDX * 3;
  gliderY += gliderDY * 3;
  if (gliderX >= 32 && gliderX <= 96 && gliderY >= 26 && gliderY <= 48) { startFromTitle(); return; }
  if (gliderX < -5 || gliderX > 133 || gliderY < -5 || gliderY > 69) { resetToTitle(); }
  drawTitleAnimation();
}

void drawTitleAnimation() {
  display.clearDisplay();
  display.drawRect(32, 26, 64, 22, SSD1306_WHITE);
  display.setTextSize(1, 2);
  display.setTextColor(SSD1306_WHITE);
  const char* t = "Game of Life";
  for(int i = 0; i < 12; i++) { display.setCursor(40 + i * 4, 30); display.write(t[i]); }
  display.setTextSize(1);
  display.setCursor(40, 50);
  display.print(F("v.1.2"));
  display.drawPixel(gliderX+1, gliderY, SSD1306_WHITE);
  display.drawPixel(gliderX+2, gliderY+1, SSD1306_WHITE);
  display.drawPixel(gliderX, gliderY+2, SSD1306_WHITE);
  display.drawPixel(gliderX+1, gliderY+2, SSD1306_WHITE);
  display.drawPixel(gliderX+2, gliderY+2, SSD1306_WHITE);
  display.display();
}

void startFromTitle() {
  for (int y = 0; y < gridHeight; y++) { for (int x = 0; x < gridWidth; x++) { grid[y][x] = false; } }
  for (int x = 28; x <= 92; x++) { grid[24][x] = true; grid[46][x] = true; }
  for (int y = 24; y <= 46; y++) { grid[y][28] = true; grid[y][92] = true; }
  for (int y = 28; y <= 44; y++) { for (int x = 36; x <= 84; x++) { if (random(10) < 4) grid[y][x] = true; } }
  for (int x = 36; x <= 62; x++) { grid[48][x] = (random(10) < 3); grid[49][x] = (random(10) < 3); }
  int gx = gliderX - xOffset; int gy = gliderY - yOffset;
  if (gx >= 0 && gx < gridWidth - 2 && gy >= 0 && gy < gridHeight - 2) { spawnGlider(gx, gy); }
  currentState = GAME_RUNNING;
}

void drawGrid() {
  display.clearDisplay();
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      if (grid[y][x]) { display.drawPixel(x + xOffset, y + yOffset, SSD1306_WHITE); }
    }
  }
  display.display();
}

int countNeighbors(int x, int y) {
  int count = 0;
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      if (i == 0 && j == 0) continue;
      int nx = (x + i + gridWidth) % gridWidth;
      int ny = (y + j + gridHeight) % gridHeight;
      if (grid[ny][nx]) count++;
    }
  }
  return count;
}

void computeNextGen() {
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      int n = countNeighbors(x, y);
      if (grid[y][x]) nextGrid[y][x] = (n == 2 || n == 3);
      else nextGrid[y][x] = (n == 3);
    }
  }
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      grid[y][x] = nextGrid[y][x];
    }
  }
}
