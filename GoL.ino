#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// I2C Pins for ESP32-C3
#define SDA_PIN 5
#define SCL_PIN 6

// OLED parameters
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Game of Life Grid dimensions (half resolution for better visibility)
const int gridWidth = 64;
const int gridHeight = 32;
const int cellSize = 2;

bool grid[gridHeight][gridWidth];
bool nextGrid[gridHeight][gridWidth];

void setup() {
  Serial.begin(115200);

  // Initialize I2C with specified pins
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 25);
  display.println(F("Game of Life"));
  display.display();
  delay(2000);

  // Randomly initialize the grid
  randomSeed(analogRead(0));
  initGrid();
}

void loop() {
  drawGrid();
  computeNextGen();
  
  // Optional: Check for stagnation or just let it run
  // For now, just a small delay to control speed
  delay(50);
}

void initGrid() {
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      grid[y][x] = random(2);
    }
  }
}

void drawGrid() {
  display.clearDisplay();
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      if (grid[y][x]) {
        display.fillRect(x * cellSize, y * cellSize, cellSize, cellSize, SSD1306_WHITE);
      }
    }
  }
  display.display();
}

int countNeighbors(int x, int y) {
  int count = 0;
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      if (i == 0 && j == 0) continue;
      
      // Wrap around logic
      int nx = (x + i + gridWidth) % gridWidth;
      int ny = (y + j + gridHeight) % gridHeight;
      
      if (grid[ny][nx]) {
        count++;
      }
    }
  }
  return count;
}

void computeNextGen() {
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      int neighbors = countNeighbors(x, y);
      
      if (grid[y][x]) {
        // Any live cell with two or three live neighbours survives
        nextGrid[y][x] = (neighbors == 2 || neighbors == 3);
      } else {
        // Any dead cell with exactly three live neighbours becomes a live cell
        nextGrid[y][x] = (neighbors == 3);
      }
    }
  }

  // Copy nextGrid to grid
  for (int y = 0; y < gridHeight; y++) {
    for (int x = 0; x < gridWidth; x++) {
      grid[y][x] = nextGrid[y][x];
    }
  }
}
