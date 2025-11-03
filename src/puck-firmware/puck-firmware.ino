#include <SPI.h>
#include <LoRa.h>
#include <driver/i2s.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// I2S MEMS mic from Adafruit: SPH0645
#define I2S_WS        25  // LRCL
#define I2S_CLK       26  // BCLK
#define I2S_SD        27  // DOUT

#define SAMPLE_RATE   16000
#define BUFFER_SIZE   127
#define RECORD_TIME   3000

// LoRa Module from Adafruit: RFM95W
#define LORA_G0_IRQ   4   // 17
#define LORA_SCK      18  // 18
#define LORA_MISO     19  // 19
#define LORA_MOSI     23	// 23
#define LORA_CS       14  // 4
#define LORA_RST      12  // 2
#define LORA_POW_EN		17  // 

#define ASIA          433E6
#define EUROPE        868E6
#define NORTH_AMERICA 915E6

// ADC from Adafruit: ADS1115
#define ADS_SDA       21
#define ADS_SCL       22
#define ADS_ALRT      00

#define BAUD          115200

char* node_id = "000007";

// ADC Configuration
// 0x023E = 0b 0000 0010 0011 1110
int16_t config = 0x023E; 
int16_t threshHi = 60;
int16_t threshLo = -1 * threshHi;

Adafruit_ADS1115 ads;
volatile bool adsTriggered = false;

// Audio buffers
int16_t audioBuffer[BUFFER_SIZE];
uint8_t payload[2 * BUFFER_SIZE];

char** parse_command(const char* command) {
  char** parsed = (char**)malloc(3 * sizeof(char*));

  for (int i = 0; i < 3; i++) {
    parsed[i] = (char*)malloc(50);
    parsed[i][0] = '\0';
  }
  
  int i = 0, j = 0;
  char c;
  while ((c = command[i++]) != '\0') {
    if (c == ':') {
      j++;
      if (j >= 3) break; // prevent overflow
    } else {
      char temp[2] = {c, '\0'};
      strcat(parsed[j], temp);
    }
  }
  
  return parsed;
}

uint8_t *buildPayload() {
  for(int i = 0; i < BUFFER_SIZE; i++) {
    payload[2 * i] = audioBuffer[i] & 0xFF;
    payload[2 * i + 1] = (audioBuffer[i] >> 8) & 0xFF;
  }

  return payload;
}

void loRaSendAudio() {
  LoRa.beginPacket();
  LoRa.write(buildPayload(), 2 * BUFFER_SIZE);
  LoRa.endPacket();
}

void eventISR() { adsTriggered = true; }

void setupI2S() {
  Serial.println("Setting up I2S microphone...");

  const i2s_config_t i2s_config = {
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_CLK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  
  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.print("I2S driver install failed: ");
    Serial.println(err);
  } else {
    i2s_set_pin(I2S_NUM_0, &pin_config);
    Serial.println("... I2S microphone initialization successful!");
  }
}

void configureADSComparatorDifferential(int16_t config, int16_t high, int16_t low) {
  Serial.println("Configuring ADS1115...");
  Serial.println("Setting thresh low to " + String(low) + ", and thresh hi to " + String(high) + "...");

  // CONFIG register
  Wire.beginTransmission(ADS1X15_ADDRESS);
  Wire.write(ADS1X15_REG_POINTER_CONFIG);
  Wire.write(highByte(config));
  Wire.write(lowByte(config));
  Wire.endTransmission();

  delay(10); // Give time for config to settle

  // HIGH THRESH register
  Wire.beginTransmission(ADS1X15_ADDRESS);
  Wire.write(ADS1X15_REG_POINTER_HITHRESH);
  Wire.write(highByte(high));
  Wire.write(lowByte(high));
  Wire.endTransmission();

  delay(10);

  // LOW THRESH register
  Wire.beginTransmission(ADS1X15_ADDRESS);
  Wire.write(ADS1X15_REG_POINTER_LOWTHRESH);
  Wire.write(highByte(low));
  Wire.write(lowByte(low));
  Wire.endTransmission();

  Serial.println("...ADS1115 successfully configured!");
}

void setupADS1115() {
  Serial.println("Setting up ADC...");

  Wire.begin(ADS_SDA, ADS_SCL);
  pinMode(ADS_ALRT, INPUT);
  attachInterrupt(digitalPinToInterrupt(ADS_ALRT), eventISR, RISING);
  
  
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS1115.");
  }
  
  Serial.println("...ADC initialization successful!");

  configureADSComparatorDifferential(config, threshHi, threshLo);
}

void handleThresholdAdjust(const char* cmd) {
  if (cmd[0] == 'U') {
    Serial.println("adjusting threshold up...");
    threshHi += 10; threshLo = -1 * threshHi;
    configureADSComparatorDifferential(config, threshHi, threshLo);
    LoRa.beginPacket();
    LoRa.print(node_id);
    LoRa.print(" threshold set to ");
    LoRa.print(threshHi);
    LoRa.endPacket();
  }
  else if (cmd[0] == 'D') {
    if (threshHi <= 10) {
      LoRa.beginPacket();
      LoRa.print("Threshold already at lowest value!");
      LoRa.endPacket();  
    } else {
      Serial.println("adjusting threshold down...");
      threshHi -= 10; threshLo = -1 * threshHi;
      configureADSComparatorDifferential(config, threshHi, threshLo);
      LoRa.print(node_id);
      LoRa.print(" threshold set to ");
      LoRa.print(threshHi);
      LoRa.endPacket();
    }
  }
}

void handleSetNode(char message[]) {
  if (node_id != 0) {
    LoRa.beginPacket();
    LoRa.print("Node ID already set!");
    LoRa.endPacket();  
  }
}

void setupLoRa() {
  LoRa.setPins(LORA_CS, LORA_RST, LORA_G0_IRQ);
  LoRa.begin(EUROPE);
}

void loraOn() { digitalWrite(LORA_POW_EN, HIGH); }
void loraOff() { digitalWrite(LORA_POW_EN, LOW); }

void setup() {
  Serial.begin(BAUD);
  while (!Serial);
  
  // Turn LoRa module on
  pinMode(LORA_POW_EN, OUTPUT);
  loraOn();

  setupADS1115();
  //setupI2S();
  setupLoRa();
}

void loop() {
  if (adsTriggered) {
    adsTriggered = false;
    LoRa.beginPacket();
    LoRa.print("SEISMC:");
    LoRa.print(node_id);
    LoRa.endPacket();
    //Serial.println("GEOPHONE trigger!");
  }
  
  int packetSize = LoRa.parsePacket();
  char message[packetSize];
  int index = 0;

  if (packetSize > 0) {
    while (LoRa.available()) {
      char c = (char)LoRa.read();
      message[index] = c;
      index++;
    }
    
    char** parsed_cmd = parse_command(message);

    String cmd = (String)parsed_cmd[0];
    String id = (String)parsed_cmd[1];
    String dir = (String)parsed_cmd[2];

    cmd.trim(); id.trim(); dir.trim();

    Serial.println(cmd);
    Serial.println(id);
    Serial.println(dir);

    // Check if command was recieved
    if (cmd == "THRESH" && id == id) {
      Serial.println("...threhold adjust requested...");
      handleThresholdAdjust(dir.c_str());
    }
  }
  

  
  // Set pointer to CONFIG register (0x01)
  Wire.beginTransmission(ADS1X15_ADDRESS);
  Wire.write(ADS1X15_REG_POINTER_CONFIG);  // This is 0x01
  Wire.endTransmission();


  // Now request 2 bytes from CONFIG register
  Wire.requestFrom(ADS1X15_ADDRESS, 2);
  if (Wire.available() == 2) {
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    uint16_t config = (msb << 8) | lsb;

    Serial.print("ADS1115 Config Register: 0x");
    Serial.println(config, HEX);
  } else {
    Serial.println("Failed to read config register");
  }


  Wire.beginTransmission(ADS1X15_ADDRESS);
  Wire.write(ADS1X15_REG_POINTER_CONVERT);  // 0x00
  Wire.endTransmission();

  Wire.requestFrom(ADS1X15_ADDRESS, 2);
  if (Wire.available() == 2) {
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    int16_t result = (msb << 8) | lsb;

    float voltage = result * 0.125; // mV per bit at GAIN_ONE
    Serial.print("Manual ADC read: ");
    Serial.print(result);
    Serial.print(" (");
    Serial.print(voltage);
    Serial.println(" mV)");
  }
}
