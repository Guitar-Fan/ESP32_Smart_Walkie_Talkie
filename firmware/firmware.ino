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

#define PTT_PIN     1   // TX pin (safe input)

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
const int MIC_THRESHOLD = 900;  // Auto-squelch level

void setup() {
  Serial.begin(115200);
  pinMode(PTT_PIN, INPUT_PULLUP);

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
  tft.println("Walkie-Talkie v1.0");
  delay(1500);

  // --- LoRa ---
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(915E6)) {  // Change to 433E6 if needed
    tft.fillScreen(ST7735_RED);
    tft.setCursor(10, 50);
    tft.println("LoRa FAILED");
    while (1);
  }
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setTxPower(20);
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST7735_GREEN);
  tft.println("LoRa READY");

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

  tft.println("I2S READY");
  delay(1000);
  tft.fillScreen(ST7735_BLACK);
}

void loop() {
  handlePTT();
  handleAutoSquelch();
  handleReceive();
  updateDisplay();
  delay(10);
}

// === SMART FEATURES ===
void handlePTT() {
  bool ptt = digitalRead(PTT_PIN) == LOW;
  if (ptt && !transmitting) startTransmit();
  else if (!ptt && transmitting) stopTransmit();
}

void handleAutoSquelch() {
  if (!transmitting && digitalRead(PTT_PIN) == HIGH) {
    if (millis() - lastMicCheck > 50) {
      if (micLevel() > MIC_THRESHOLD) {
        startTransmit();
        tft.fillRect(100, 40, 60, 20, ST7735_YELLOW);
        tft.setCursor(105, 45);
        tft.print("AUTO");
      }
      lastMicCheck = millis();
    }
  }
}

void startTransmit() {
  transmitting = true;
  LoRa.beginPacket();
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
    receiving = true;
    rssi = LoRa.packetRssi();

    while (LoRa.available()) {
      uint8_t high = LoRa.read();
      uint8_t low = LoRa.read();
      int16_t sample = (high << 8) | low;
      i2s_write_bytes(I2S_NUM, (const char*)&sample, 2, portMAX_DELAY);
    }
    receiving = false;
  }
}

int micLevel() {
  size_t bytesRead = 0;
  i2s_read(I2S_NUM, audioBuffer, BUFFER_LEN * 2, &bytesRead, 0);
  if (bytesRead == 0) return 0;
  long sum = 0;
  for (int i = 0; i < BUFFER_LEN; i++) {
    sum += abs(audioBuffer[i]);
  }
  return sum / BUFFER_LEN;
}

void updateDisplay() {
  tft.fillScreen(ST7735_BLACK);

  // Title
  tft.setTextSize(2);
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(20, 5);
  tft.println("SMART");

  // Status
  tft.setTextSize(1);
  tft.setTextColor(transmitting ? ST7735_RED : ST7735_GREEN);
  tft.setCursor(5, 30);
  tft.print(transmitting ? "TX" : "RX");
  if (receiving) {
    tft.setTextColor(ST7735_YELLOW);
    tft.print(" *RECV*");
  }

  // RSSI Bar
  tft.setCursor(5, 50);
  tft.setTextColor(ST7735_WHITE);
  tft.print("RSSI: ");
  tft.print(rssi);
  tft.print(" dBm");
  int bar = map(constrain(rssi, -120, -50), -120, -50, 0, 100);
  tft.fillRect(5, 65, bar, 8, ST7735_GREEN);

  // Mic Level
  tft.setCursor(5, 80);
  tft.print("Mic: ");
  int mic = micLevel();
  tft.print(mic);
  int micBar = map(constrain(mic, 0, 2000), 0, 2000, 0, 100);
  tft.fillRect(5, 95, micBar, 8, ST7735_BLUE);

  // Battery (ADC on GPIO34 - add divider if needed)
  int batt = analogRead(34);
  int pct = map(constrain(batt, 1800, 2300), 1800, 2300, 0, 100);
  pct = constrain(pct, 0, 100);
  tft.setCursor(5, 110);
  tft.print("Batt: ");
  tft.print(pct);
  tft.print("%");
  if (pct < 20) {
    tft.setTextColor(ST7735_RED);
    tft.print(" LOW!");
  }
}