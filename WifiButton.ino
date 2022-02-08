#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>

#include "ConfigForm.h"

#define APSSID "Config_WifiButton"
#define DOMAIN "wifibutton"

#define EEPROM_VALUE_SIZE 32
#define SSID_VALUE_INDEX 0
#define PASSWORD_VALUE_INDEX 1
#define REMOTE_IP_VALUE_INDEX 2
#define REMOTE_PORT_VALUE_INDEX 3
#define DEVICE_ID_VALUE_INDEX 4

#define DEBOUNCE_DELAY 50


ESP8266WebServer httpServer(80);

#define NUMBER_OF_BUTTONS 3
int pins[] = {D2, D1, D0};
int states[] = {LOW, LOW, LOW};
int lastStates[] = {LOW, LOW, LOW};
unsigned long debounceTimes[] = {0, 0, 0};

WiFiUDP Udp;
IPAddress remoteIP;
int remotePort;
String deviceId;

// Guarda un valor en la EEPROM (maximo 32 caracteres)
void saveConfigValue(int index, const char* value, int len) {
  int addr = index * EEPROM_VALUE_SIZE;
  // Serial.printf("Saving %s in &d\n", value, index);
  for (int i = 0; i < EEPROM_VALUE_SIZE; i++) {
    if (i < len) {
      EEPROM.write(addr + i, value[i]);
    } else {
      EEPROM.write(addr + i, 0);
    }
  }
  EEPROM.commit();
}

// Lee un valor de de EEPROM
void readConfigValue(int index, char* value) {
  int addr = index * EEPROM_VALUE_SIZE;
  for (int i = 0; i < EEPROM_VALUE_SIZE; i++) {
    value[i] = EEPROM.read(addr + i);
  }
  value[EEPROM_VALUE_SIZE] = '\0';
}

// Guarda un valor recibido desde el formulario de configuracion 
void savePostedValue(int index, char* name) {
  if (httpServer.hasArg(name)) {
      String value = httpServer.arg(name);
      saveConfigValue(index, value.c_str(), value.length());
    } 
}

// Enviamos el formulario de configuracion, y recibimos los valores nuevos
void handleRoot() {
  if (httpServer.method() == HTTP_POST) {
    savePostedValue(SSID_VALUE_INDEX, "ssid");
    savePostedValue(PASSWORD_VALUE_INDEX, "password");
    savePostedValue(REMOTE_IP_VALUE_INDEX, "remoteIP");
    savePostedValue(REMOTE_PORT_VALUE_INDEX, "port");
    savePostedValue(DEVICE_ID_VALUE_INDEX, "deviceId");
  }
  httpServer.send(200, "text/html", configForm);
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println("\n\n\n");

  // Configuramos los pines como entradas
  for (int i = 0; i < NUMBER_OF_BUTTONS; i++) {
    pinMode(pins[i], INPUT);
  }

  // Iniciamos la EEPROM, el tamaño es = 32 * numero de valores a guardar 
  EEPROM.begin(160);

  // Si al encender, mantenemos pulsado los butones 1 y 3 durante 10 segundos, borramos la configuración
  unsigned long resetTime = millis();
  while ((digitalRead(pins[0]) == HIGH) && (digitalRead(pins[1]) == LOW) && (digitalRead(pins[2]) == HIGH)) {

    unsigned long remaining = 10 - ((millis() - resetTime) / 1000);

    if (remaining > 0) {
      Serial.printf("%d second(s) to restore settings\n", remaining);
    } else {
      saveConfigValue(SSID_VALUE_INDEX, "", 0);
      saveConfigValue(PASSWORD_VALUE_INDEX, "", 0);
      saveConfigValue(REMOTE_IP_VALUE_INDEX, "", 0);
      saveConfigValue(REMOTE_PORT_VALUE_INDEX, "", 0);
      saveConfigValue(DEVICE_ID_VALUE_INDEX, "", 0);
      Serial.println("Restored factory settings!");
      break;
    }
    
    delay(500);
  }

  char ssid[EEPROM_VALUE_SIZE + 1];
  char password[EEPROM_VALUE_SIZE + 1];
  char remoteIPStr[EEPROM_VALUE_SIZE + 1];
  char remotePortStr[EEPROM_VALUE_SIZE + 1];
  char deviceIdStr[EEPROM_VALUE_SIZE + 1];

  // Leemos la configuracion guardada en la EEPROM
  readConfigValue(SSID_VALUE_INDEX, ssid);
  readConfigValue(PASSWORD_VALUE_INDEX, password);
  readConfigValue(REMOTE_IP_VALUE_INDEX, remoteIPStr);
  readConfigValue(REMOTE_PORT_VALUE_INDEX, remotePortStr);
  readConfigValue(DEVICE_ID_VALUE_INDEX, deviceIdStr);  

  int devId = atoi(deviceIdStr);
  if ((devId > 0) && (devId < 100)) {
    deviceId = String(devId);
  } else {
    Serial.println("Invalid device ID, using default value: 0");
    deviceId = "0";
  }

  // Intentamos conectarnos a la red Wifi
  Serial.println("Connecting to Wi-Fi network ...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);


  // Esperamos hasta que el dispositivo este conectado, o que pasen 30 segundos
  unsigned long startTime = millis();
  while ((WiFi.status() != WL_CONNECTED) && ((millis() - startTime) < 30000) ) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  // Comprobamos si estamos conectados a la Wifi
  if (WiFi.status() != WL_CONNECTED) {
    // Arracamos un AP para poder hacer la configuracion
    Serial.println("Start the AP so that the configuration page can be loaded ...");
    WiFi.softAP(APSSID);
    Serial.printf("IP Address: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("Connected to Wi-Fi network");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    if (!MDNS.begin(DOMAIN + deviceId)) {
      Serial.println("Can't start mDns");
    } else {
      // Usamos mDns para no tener que conocer la ip del dispositivo
      Serial.printf("mDns started: http://%s%s.local\n", DOMAIN, deviceId);
      // Add service to MDNS-SD
      MDNS.addService("http", "tcp", 80);
    }
  }

  // Iniciamos el servidor http
  httpServer.on("/", handleRoot);
  httpServer.begin();
  Serial.println("HTTP server started");

  if (!remoteIP.fromString(remoteIPStr)) {
    Serial.printf("Remote IP is not valid: %s\n", remoteIPStr);
  } else {
    Serial.printf("Remote IP: %s\n", remoteIP.toString().c_str());
  }

  if (!atoi(remotePortStr)) {
    remotePort = 8888;
    Serial.printf("Remote port is not valid, using default value: %d\n", remotePort);
  } else {
    remotePort = atoi(remotePortStr);
    Serial.printf("Remote port: %d\n", remotePort);
  }

  Serial.printf("Device id: %s", deviceId);

}

void loop() {
  MDNS.update();
  httpServer.handleClient();

  if (remoteIP.isSet()) {
    int buttonState;

    int i;
    for (i = 0; i < NUMBER_OF_BUTTONS; i++) {
      buttonState = digitalRead(pins[i]);
      if (buttonState != lastStates[i]) {
        debounceTimes[i] = millis();
      }

      // Filtramos los rebotes 
      if ((millis() - debounceTimes[i]) > DEBOUNCE_DELAY) {
        if (buttonState != states[i]) {
          states[i] = buttonState;
          if (buttonState == HIGH) {
            Udp.beginPacket(remoteIP, remotePort);
            Udp.write(("{\"deviceId\": " + deviceId + ", \"button\": " + i + "}").c_str());
            Udp.endPacket();
          }
        }
      }

      lastStates[i] = buttonState;
    }
  }
}
