#include <LoRa.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <driver/i2s.h>

// === PINS ===
#define LORA_NSS    5
#define LORA_SCK   18
#define LORA_MOSI  23
#define LORA_MISO  19
#define LORA_DIO0  26
#define LORA_RST   14

#define I2S_BCLK    4
#define I2S_WS     25
#define I2S_DIN    33   // Speaker OUT
#define I2S_DOUT   32   // Mic IN

#define TFT_CS     13
#define TFT_DC     12
#define TFT_RST    27

#define PTT_PIN     1   // TX pin

// === TEXT MODE BUTTONS ===
#define BTN_TEXT    0   // GPIO0 (BOOT button) - Enter text mode
#define BTN_UP     35
#define BTN_DOWN   36
#define BTN_LEFT   39
#define BTN_RIGHT  34

// === OBJECTS ===
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// === I2S ===
#define I2S_NUM     I2S_NUM_0
#define SAMPLE_RATE 16000
#define BUFFER_LEN  512
int16_t audioBuffer[BUFFER_LEN];

// === STATE ===
bool transmitting = false;
bool receiving = false;
int rssi = 0;
unsigned long lastMicCheck = 0;
const int MIC_THRESHOLD = 900;

// === TEXT MODE ===
bool textMode = false;
bool typing = false;
String currentMessage = "";
String lastReceivedText = "";

const char keyboard[4][11] = {
  "abcdeABCDE",
  "fghijFGHIJ",
  "klmnoKLMNO",
  "pqrstPQRSTuvwxy z01234 56789.!?,"
};
int cursorX = 0, cursorY = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PTT_PIN, INPUT_PULLUP);
  pinMode(BTN_TEXT, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  // --- TFT ---
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(10, 20);
  tft.println("SMART");
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.println("Walkie-Talkie + TEXT v2.0");
  delay(2000);

  // --- LoRa ---
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(915E6)) {
    tft.fillScreen(ST7735_RED);
    tft.setCursor(10, 50);
    tft.println("LoRa FAILED");
    while (1);
  }
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setTxPower(20);
  LoRa.receive(); // Always listening

  // --- I2S ---
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_DOUT
  };
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);

  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_GREEN);
  tft.println("LoRa + TEXT READY");
  delay(1000);
  tft.fillScreen(ST7735_BLACK);
}

void loop() {
  handleButtons();
  handlePTT();
  handleAutoSquelch();
  handleReceive();
  updateDisplay();
  delay(50);
}

void handleButtons() {
  if (digitalRead(BTN_TEXT) == LOW) {
    delay(200);
    if (!textMode) enterTextMode();
    else exitTextMode();
  }

  if (typing) {
    if (digitalRead(BTN_UP) == LOW)    { cursorY = max(0, cursorY - 1); delay(200); }
    if (digitalRead(BTN_DOWN) == LOW)  { cursorY = min(3, cursorY + 1); delay(200); }
    if (digitalRead(BTN_LEFT) == LOW)  { cursorX = max(0, cursorX - 1); delay(200); }
    if (digitalRead(BTN_RIGHT) == LOW) { cursorX = min(9, cursorX + 1); delay(200); }

    // Select character (hold TEXT + press direction to send)
    static unsigned long selectPress = 0;
    if (digitalRead(BTN_TEXT) == LOW && typing) {
      if (millis() - selectPress > 500) {
        char c = keyboard[cursorY][cursorX];
        if (c != ' ') {
          if (c >= 'a' && c <= 'z' && bitRead(cursorX, 0)) c -= 32; // crude shift
          currentMessage += c;
        } else if (currentMessage.length() > 0) {
          currentMessage.remove(currentMessage.length() - 1); // backspace
        }
        selectPress = millis();
      }
    }

    // Send message: double-tap TEXT button
    static bool lastTextState = HIGH;
    bool curr = digitalRead(BTN_TEXT);
    if (lastTextState == LOW && curr == HIGH && currentMessage.length() > 0 && typing) {
      static unsigned long lastTap = 0;
      if (millis() - lastTap < 400) {
        sendTextMessage(currentMessage);
        currentMessage = "";
        beep(1000, 100);
      }
      lastTap = millis();
    }
    lastTextState = curr;
  }
}

void enterTextMode() {
  textMode = true;
  typing = true;
  currentMessage = "";
  cursorX = cursorY = 0;
  beep(800, 80);
}

void exitTextMode() {
  textMode = false;
  typing = false;
  currentMessage = "";
  beep(600, 80);
}

void sendTextMessage(String msg) {
  LoRa.beginPacket();
  LoRa.write(0x02); // Text packet
  LoRa.print(msg);
  LoRa.endPacket();
  lastReceivedText = "You: " + msg; // show your own message
}

void handlePTT() {
  if (textMode) return; // disable voice when typing
  bool ptt = digitalRead(PTT_PIN) == LOW;
  if (ptt && !transmitting) startTransmit();
  else if (!ptt && transmitting) stopTransmit();
}

void handleAutoSquelch() {
  if (textMode || transmitting) return;
  if (millis() - lastMicCheck > 50) {
    if (micLevel() > MIC_THRESHOLD) {
      startTransmit();
    }
    lastMicCheck = millis();
  }
}

void startTransmit() {
  if (textMode) return;
  transmitting = true;
  LoRa.beginPacket();
  LoRa.write(0x01); // Audio packet header
}

void stopTransmit() {
  if (transmitting) {
    LoRa.endPacket();
    transmitting = false;
  }
}

void handleReceive() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    uint8_t type = LoRa.read();
    rssi = LoRa.packetRssi();

    if (type == 0x01) { // Audio
      receiving = true;
      while (LoRa.available() >= 2) {
        uint8_t high = LoRa.read();
        uint8_t low = LoRa.read();
        int16_t sample = (high << 8) | low;
        i2s_write_bytes(I2S_NUM, (const char*)&sample, 2, 0);
      }
      receiving = false;

    } else if (type == 0x02) { // Text
      String text = LoRa.readString();
      lastReceivedText = text;
      beep(1200, 150);
      beep(800, 100);
    }
  }
}

int micLevel() {
  size_t bytesRead = 0;
  i2s_read(I2S_NUM, audioBuffer, BUFFER_LEN * 2, &bytesRead, 0);
  if (bytesRead == 0) return 0;
  long sum = 0;
  for (int i = 0; i < BUFFER_LEN; i++) sum += abs(audioBuffer[i]);
  return sum / BUFFER_LEN;
}

void beep(int freq, int dur) {
  tone(33, freq, dur); // simple tone on speaker pin (works on most amps)
}

void drawKeyboard() {
  tft.fillRect(0, 40, 160, 80, ST7735_BLACK);
  tft.setTextSize(1);
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 10; x++) {
      uint16_t color = (x == cursorX && y == cursorY) ? ST7735_YELLOW : ST7735_WHITE;
      tft.setTextColor(color);
      tft.setCursor(5 + x*15, 45 + y*18);
      char c = keyboard[y][x];
      tft.print(c == ' ' ? '_' : c);
    }
  }
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(0, 120);
  tft.fillRect(0, 120, 160, 8, ST7735_BLACK);
  tft.print(currentMessage + "_");
}

void updateDisplay() {
  tft.fillScreen(ST7735_BLACK);

  // Title
  tft.setTextSize(2);
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(20, 5);
  tft.println("SMART");

  if (textMode) {
    tft.setTextSize(1);
    tft.setTextColor(ST7735_MAGENTA);
    tft.setCursor(5, 25);
    tft.print("TEXT MODE");
    drawKeyboard();

    if (lastReceivedText != "") {
      tft.setTextColor(ST7735_YELLOW);
      tft.setCursor(0, 30);
      tft.print(lastReceivedText);
    }
    return;
  }

  // Normal voice mode
  tft.setTextSize(1);
  tft.setTextColor(transmitting ? ST7735_RED : ST7735_GREEN);
  tft.setCursor(5, 30);
  tft.print(transmitting ? "TX VOICE" : "RX READY");
  if (receiving) {
    tft.setTextColor(ST7735_YELLOW);
    tft.print(" PLAYING");
  }

  // Show last text message
  if (lastReceivedText != "") {
    tft.setTextColor(ST7735_YELLOW);
    tft.setCursor(0, 45);
    tft.print("MSG: ");
    tft.print(lastReceivedText.substring(0, 20));
  }

  // RSSI
  tft.setCursor(5, 70);
  tft.setTextColor(ST7735_WHITE);
  tft.print("RSSI: "); tft.print(rssi); tft.print(" dBm");
  int bar = map(constrain(rssi, -120, -50), -120, -50, 0, 100);
  tft.fillRect(5, 85, bar, 8, ST7735_GREEN);

  // Mic + Batt
  int mic = micLevel();
  int micBar = map(constrain(mic, 0, 2000), 0, 2000, 0, 100);
  tft.setCursor(5, 100);
  tft.print("Mic: "); tft.print(mic);
  tft.fillRect(5, 115, micBar, 8, ST7735_BLUE);

  int batt = analogRead(34);
  int pct = map(constrain(batt, 1800, 2300), 1800, 2300, 0, 100);
  pct = constrain(pct, 0, 100);
  tft.setCursor(5, 130);
  tft.print("Batt: "); tft.print(pct); tft.print("%");
}