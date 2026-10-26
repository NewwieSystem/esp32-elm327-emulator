#include "link.h"
#include "config.h"
#include "elm327.h"

#include <WiFi.h>
#include "BluetoothSerial.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "freertos/queue.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled
#endif

static LinkType current = LINK_BT;
static String lineBuf;

// ---------------------------------------------------------------- Bluetooth Classic
static BluetoothSerial *serialBT = nullptr;

// ---------------------------------------------------------------- BLE
static BLEServer *bleServer = nullptr;
static BLECharacteristic *bleChars[3] = {nullptr, nullptr, nullptr};
static QueueHandle_t bleRx = nullptr;

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto v = c->getValue();                                // std::string or String depending on the core
    for (size_t i = 0; i < v.length(); i++) {
      char ch = v[i];
      xQueueSend(bleRx, &ch, 0);
    }
  }
};
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {}
  void onDisconnect(BLEServer *s) override { delay(50); s->getAdvertising()->start(); }
};

static BLECharacteristic *makeChar(BLEService *svc, const char *uuid, RxCallbacks *cb) {
  BLECharacteristic *c = svc->createCharacteristic(uuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  c->addDescriptor(new BLE2902());
  c->setCallbacks(cb);
  return c;
}

// ---------------------------------------------------------------- Wi-Fi
static WiFiServer *tcpServer = nullptr;
static WiFiClient tcpClient;

// ---------------------------------------------------------------- common
const char *linkName(LinkType t) {
  return t == LINK_BT ? "Bluetooth Classic" : t == LINK_BLE ? "Bluetooth LE" : "Wi-Fi";
}
const char *linkShortName(LinkType t) { return t == LINK_BT ? "BT" : t == LINK_BLE ? "BLE" : "WiFi"; }
LinkType linkType() { return current; }

static String btMac() {
  const uint8_t *m = esp_bt_dev_get_address();
  if (!m) return "--";
  char b[18];
  snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return b;
}

String linkAddress() {
  if (current == LINK_WIFI) return WiFi.softAPIP().toString() + ":" + String(WIFI_TCP_PORT);
  return btMac();
}

bool linkClientConnected() {
  switch (current) {
    case LINK_BT:   return serialBT && serialBT->hasClient();
    case LINK_BLE:  return bleServer && bleServer->getConnectedCount() > 0;
    case LINK_WIFI: return tcpClient && tcpClient.connected();
    default:        return false;
  }
}

void linkBegin(LinkType t) {
  current = t;
  if (t == LINK_BT) {
    serialBT = new BluetoothSerial();
    if (!serialBT->begin(BT_DEVICE_NAME)) Serial.println("Bluetooth start failed");
  } else if (t == LINK_BLE) {
    bleRx = xQueueCreate(512, 1);
    BLEDevice::init(BT_DEVICE_NAME);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());
    auto *cb = new RxCallbacks();
    BLEService *a = bleServer->createService(BLE_SVC_A);
    bleChars[0] = makeChar(a, BLE_CHR_A1, cb);
    bleChars[1] = makeChar(a, BLE_CHR_A2, cb);
    BLEService *b = bleServer->createService(BLE_SVC_B);
    bleChars[2] = makeChar(b, BLE_CHR_B1, cb);
    a->start();
    b->start();
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SVC_A);
    adv->addServiceUUID(BLE_SVC_B);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
  } else {
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);      // no Bluetooth at all: more RAM for Wi-Fi
    WiFi.mode(WIFI_AP);
    IPAddress ip(WIFI_AP_IP), mask(255, 255, 255, 0);
    WiFi.softAPConfig(ip, ip, mask);
    const char *pw = WIFI_AP_PASSWORD;
    WiFi.softAP(WIFI_AP_SSID, pw[0] ? pw : nullptr);
    WiFi.setSleep(false);                                 // power save adds 100+ ms to every answer
    tcpServer = new WiFiServer(WIFI_TCP_PORT);
    tcpServer->begin();
    tcpServer->setNoDelay(true);
  }
}

static void sendOut(const String &s) {
  switch (current) {
    case LINK_BT:
      serialBT->print(s);                                  // one write: fewer, larger Bluetooth packets
      break;
    case LINK_BLE: {
      const int CHUNK = 20;                                // fits the default BLE MTU (23)
      for (unsigned pos = 0; pos < s.length(); pos += CHUNK) {
        String part = s.substring(pos, min((unsigned)s.length(), pos + CHUNK));
        for (auto *c : bleChars) {
          c->setValue((uint8_t *)part.c_str(), part.length());
          c->notify();
        }
        if (pos + CHUNK < s.length()) delay(4);
      }
      break;
    }
    case LINK_WIFI:
      if (tcpClient && tcpClient.connected()) tcpClient.write((const uint8_t *)s.c_str(), s.length());
      break;
    default: break;
  }
}

static void feed(char c) {
  if (c == '\r' || c == '\n') {                            // ELM327 lines end with CR; ignore the LF of CR LF
    if (lineBuf.length() == 0) return;
    onLinkTraffic();
    String reply = elmHandleLine(lineBuf);
    lineBuf = "";
    sendOut(reply);
  } else if (c >= 0x20 && lineBuf.length() < 64) {
    lineBuf += c;
  } else if (lineBuf.length() >= 64) {
    lineBuf = "";
    sendOut("?\r\r>");
  }
}

void linkPoll() {
  switch (current) {
    case LINK_BT:
      while (serialBT && serialBT->available()) feed((char)serialBT->read());
      break;
    case LINK_BLE: {
      char c;
      while (bleRx && xQueueReceive(bleRx, &c, 0) == pdTRUE) feed(c);
      break;
    }
    case LINK_WIFI:
      if (tcpServer->hasClient()) {                        // newest connection wins, like most adapters
        if (tcpClient) tcpClient.stop();
        tcpClient = tcpServer->available();
        tcpClient.setNoDelay(true);
        lineBuf = "";
      }
      while (tcpClient && tcpClient.available()) feed((char)tcpClient.read());
      break;
    default: break;
  }
}
