#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

// ESP32 devkit v1 pinout
#define CS            4
#define RST           2
#define G0            17

#define ASIA          433E6
#define EUROPE        868E6
#define NORTH_AMERICA 915E6

#define BAUD          115200

Preferences puck_prefs;

int default_country = EUROPE;
const char* serverUrl = "https://api.staging.eas-defense.com/register";
const char* ssid = "";
const char* pass = "";
const char* access_token = "";

bool connect_to_wifi(const char* ssid, const char* password) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    Serial.println("Connecting to WiFi...");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(1000);
      Serial.print("Status: ");
      Serial.println(WiFi.status());
      attempts++;
    }
  }
  return WiFi.status() == WL_CONNECTED;
}

String construct_json_payload(String id, float snr, float rssi, float battery, String event_type) {
  String json = "{";
  json += "\"device_id\": " + id + "\",";
  json += "\"data\": {";
  json += "\"snr\": " + (String)snr + "\",";
  json += "\"rssi\": " + (String)rssi + "\",";
  json += "\"battery\": " + (String)battery +"\"}, ";
  json += "\"event_type\": " + event_type + "\"";
  json += "}";

  return json;
}

bool register_event(String jsonPayload) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    String access_token = puck_prefs.getString("api_key");

    http.begin(serverUrl); // HTTPS URL
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-access-token", access_token);

    int httpCode = http.POST(jsonPayload);

    if (httpCode > 0) {
      String response = http.getString();
      Serial.println(response);
      return true;
    } else {
      Serial.printf("POST failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
  http.end();
  } else {
    Serial.println("base failed to connect to WiFi.");
  }
  return false;
}

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

void setup() {
  int country = default_country;
  Serial.begin(BAUD);
  while (!Serial);

  puck_prefs.begin("user", false);

  LoRa.setPins(CS, RST, G0);
  if (puck_prefs.isKey("country")) {
    country = puck_prefs.getInt("country");
  }

  if (!LoRa.begin(country)) {
    Serial.println("Starting LoRa failed!");
  }
}

void loop() {
  bool event = false;
  String command = "";
  String jsonPayload = "";

  // Listen for incoming LoRa packets...
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    Serial.print("packet available : ");
    Serial.print(packetSize);
    Serial.println(" bytes.");
    while (LoRa.available()) {
      char next = (char)LoRa.read();
      Serial.print(next);
      incoming += next;
    }
    char** parsed_cmd = parse_command(incoming.c_str());

    String cmd = (String)parsed_cmd[0];
    String arg1 = (String)parsed_cmd[1];
    String arg2 = (String)parsed_cmd[2];

    cmd.trim(); arg1.trim(); arg2.trim();

    String node_id = arg1;
    float snr = LoRa.packetSnr();
    float rssi = LoRa.packetRssi();
    float battery = arg2.toInt() / 100.0;

    if (cmd == "SEISMC") {
      jsonPayload = construct_json_payload(node_id, snr, rssi, battery, "trigger");
      event = true;
    }
  }

  if (event) {
    Serial.println("...registering event to cloud...");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(ssid);
      while (!connect_to_wifi(ssid, pass));
    }
      
    register_event(jsonPayload);
    event = false;
  }

  // Listen for incominh serial communication...
  if (Serial.available()) {
    command = Serial.readStringUntil('\n');
    command.trim();

    char cmd_buffer[150]; // Make sure this is large enough
    strcpy(cmd_buffer, command.c_str());
    char** parsed_cmd = parse_command(cmd_buffer);

    String cmd = (String)parsed_cmd[0];
    String arg1 = (String)parsed_cmd[1];
    String arg2 = (String)parsed_cmd[2];

    cmd.trim(); arg1.trim(); arg2.trim();

    if (cmd == "CONNECT") {
      Serial.print("base station trying to connect to wifi: ");
      while (!connect_to_wifi(ssid, pass));
      Serial.print("base station is connected to wifi! ");
    } else if (cmd == "CMD") {
      if (arg1 == "api_key") {
        puck_prefs.putString("api_key", arg2);
      }
    } else if (cmd == "THRESH") {
        Serial.println(command);
        LoRa.beginPacket();
        LoRa.print(command);
        LoRa.endPacket();
      } else {
      Serial.println("Unknown command");
    }
  }
}
