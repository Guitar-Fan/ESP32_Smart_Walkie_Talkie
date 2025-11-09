#include <SPI.h>
#include <LoRa.h>
#include <driver/i2s.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// Pin definitions from schematic
// LoRa Module (SX1272/RFM95W)
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 14
#define LORA_DIO0 2

// I2S Audio
#define I2S_BCLK 32
#define I2S_LRCLK 25
#define I2S_DOUT 26 // To MAX98357A DIN
#define I2S_DIN 33  // From INMP441 SD

// TFT Display (Adafruit 1.8" TFT)
#define TFT_CS 15
#define TFT_DC 4
#define TFT_RST 12

// Buttons
#define BUTTON1_PIN 34
#define BUTTON2_PIN 35

// Constants
#define SAMPLE_RATE 16000
#define I2S_PORT I2S_NUM_0
#define LORA_FREQUENCY 915E6

// Globals
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
bool transmitting = false;
enum State { IDLE, TRANSMITTING, RECEIVING };
State currentState = IDLE;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("ESP32 Walkie-Talkie");

  // Initialize display
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.println("ESP32 Walkie-Talkie");
  updateDisplay();

  // Initialize buttons
  pinMode(BUTTON1_PIN, INPUT);
  pinMode(BUTTON2_PIN, INPUT);

  // Initialize LoRa
  Serial.println("Initializing LoRa...");
  SPI.begin(LORA_SCK, LORA_MISO, Lora_MOSI);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Starting LoRa failed!");
    tft.println("LoRa Init Failed");
    while (1);
  }
  Serial.println("LoRa Initialized.");
  tft.println("LoRa Initialized");
  LoRa.onReceive(onReceive);
  LoRa.receive();

  // Initialize I2S
  Serial.println("Initializing I2S...");
  initI2S();
  Serial.println("I2S Initialized.");
  tft.println("I2S Initialized");
  
  tft.println("Ready.");
}

void loop() {
  // Button 1: Start transmitting
  if (digitalRead(BUTTON1_PIN) == HIGH && !transmitting) {
    transmitting = true;
    currentState = TRANSMITTING;
    updateDisplay();
    Serial.println("Started transmitting...");
  }

  // Button 2: Stop transmitting
  if (digitalRead(BUTTON2_PIN) == HIGH && transmitting) {
    transmitting = false;
    currentState = IDLE;
    LoRa.receive(); // Go back to receive mode
    updateDisplay();
    Serial.println("Stopped transmitting.");
  }

  if (transmitting) {
    // Read from I2S microphone and send via LoRa
    size_t bytes_read = 0;
    uint8_t i2s_buffer[1024];
    i2s_read(I2S_PORT, &i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);

    if (bytes_read > 0) {
      LoRa.beginPacket();
      LoRa.write(i2s_buffer, bytes_read);
      LoRa.endPacket();
    }
  }
}

void onReceive(int packetSize) {
  if (packetSize == 0 || transmitting) return;

  currentState = RECEIVING;
  updateDisplay(LoRa.packetRssi());

  uint8_t buffer[packetSize];
  LoRa.readBytes(buffer, packetSize);

  // Write to I2S speaker
  size_t bytes_written = 0;
  i2s_write(I2S_PORT, buffer, packetSize, &bytes_written, portMAX_DELAY);

  currentState = IDLE;
  updateDisplay();
}

void initI2S() {
  // I2S configuration
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  // I2S pin configuration
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_DIN
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

void updateDisplay(int rssi = 0) {
  tft.fillScreen(ST7735_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(2);

  switch (currentState) {
    case IDLE:
      tft.println("Idle");
      break;
    case TRANSMITTING:
      tft.setTextColor(ST7735_RED);
      tft.println("TX");
      break;
    case RECEIVING:
      tft.setTextColor(ST7735_GREEN);
      tft.println("RX");
      break;
  }

  if (rssi != 0) {
    tft.setTextSize(1);
    tft.setCursor(0, 20);
    tft.print("RSSI: ");
    tft.print(rssi);
    tft.println(" dBm");
  }
}
