#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define TIME 1000
#define TTL_BAUD 115200

// FAN SETTINGS
#define FAN_POWER 14
#define FAN_PWM 33
#define FAN_PWM_CHANNEL 0
#define FAN_FREQ 25000
#define FAN_RESOLUTION 8
#define FAN_MIN_RPM 300
uint8_t FAN_START_SPEED = 3;
int FAN_SENSE_CURRENT = 0;
const int FAN_SENSE_COUNT = 2;
const int FAN_SENSE [FAN_SENSE_COUNT] = {32, 27};
int FAN_SENSE_RESULTS[2];
int FAN_CALC_RPM[2];
volatile int interruptCounter[2];
unsigned long FAN_PREV_MILLIS;


// BLE SETTINGS
#define BLE_LED 2
#define DEVINFO_UUID              (uint16_t)0x180a
#define DEVINFO_MANUFACTURER_UUID (uint16_t)0x2a29
#define DEVINFO_NAME_UUID         (uint16_t)0x2a24
#define DEVINFO_SERIAL_UUID       (uint16_t)0x2a25

#define DEVICE_MANUFACTURER "MY_MANUFACTURER_NAME"
#define DEVICE_NAME         "My_Fan_Project"
#define SERVICE_UUID        "9a8ca9ef-e43f-4157-9fee-c37a3d7dc12d"
#define SPEED_UUID          "e94f85c8-7f57-4dbd-b8d3-2b56e107ed60"

BLEServer* pServer = NULL;
BLECharacteristic *pCharSpeed;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

class BLEServerCallback: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      connectedLight();
      BLEDevice::startAdvertising();
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      disconnectedLight();
    }
    
    void connectedLight() {
      digitalWrite(BLE_LED, HIGH);
    }
    
    void disconnectedLight() {
      digitalWrite(BLE_LED, LOW);
    }
};

class SpeedCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() == 1 && value[0] == 0) {
        uint8_t v = value[0];
        Serial.print("Got speed value: ");
        Serial.println(v);
        digitalWrite(FAN_POWER, HIGH);
        ledcWrite(FAN_PWM_CHANNEL, 0);
      }
      else if (value.length() == 1) {
        uint8_t v = value[0];
        Serial.print("Got speed value: ");
        Serial.println(v);
        digitalWrite(FAN_POWER, LOW);
        ledcWrite(FAN_PWM_CHANNEL, v * 51);
      } else {
        Serial.println("Invalid data received");
      }
    }
};

void handleFanSenseInterrupt(int interruptID) {
  interruptCounter[interruptID]++;
}

void setup() {
  Serial.begin(TTL_BAUD);
  Serial.println("");
  Serial.println("");
  Serial.println("-------------------------");
  Serial.println("Starting UP!");

  // Pin Setup
  pinMode(BLE_LED, OUTPUT);
  pinMode(FAN_POWER, OUTPUT);
  pinMode(FAN_PWM, OUTPUT);
  pinMode(FAN_SENSE[0], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_SENSE[0]), []{ handleFanSenseInterrupt(0); }, FALLING);
  pinMode(FAN_SENSE[1], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_SENSE[1]), []{ handleFanSenseInterrupt(1); }, FALLING);
  ledcSetup(FAN_PWM_CHANNEL, FAN_FREQ, FAN_RESOLUTION);
  ledcAttachPin(FAN_PWM, FAN_PWM_CHANNEL);

  // Create the BLE Device
  String devName = DEVICE_NAME;
  String chipId = String((uint32_t)(ESP.getEfuseMac() >> 24), HEX);
  devName += '_';
  devName += chipId;
  BLEDevice::init(devName.c_str());
  Serial.println("BLE Device created");

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BLEServerCallback());
  Serial.println("BLE created Server");

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharSpeed = pService->createCharacteristic(SPEED_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  pCharSpeed->setCallbacks(new SpeedCallbacks());
  pCharSpeed->setValue(&FAN_START_SPEED, 1);
  ledcWrite(FAN_PWM_CHANNEL, FAN_START_SPEED * 51);

  pService->start();

  pService = pServer->createService(DEVINFO_UUID);

  BLECharacteristic *pChar = pService->createCharacteristic(DEVINFO_MANUFACTURER_UUID, BLECharacteristic::PROPERTY_READ);
  pChar->setValue(DEVICE_MANUFACTURER);

  pChar = pService->createCharacteristic(DEVINFO_NAME_UUID, BLECharacteristic::PROPERTY_READ);
  pChar->setValue(DEVICE_NAME);

  pChar = pService->createCharacteristic(DEVINFO_SERIAL_UUID, BLECharacteristic::PROPERTY_READ);
  pChar->setValue(chipId.c_str());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  
  Serial.println("Ready");
  Serial.print("Device name: ");
  Serial.println(devName);
  Serial.println("-------------------------");
  Serial.println("Waiting a client connection to notify...");
  clearStats();
}

void loop() {
  if (deviceConnected) {
    delay(10); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
  }
  // ble client disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // ble client connecting
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  if ((millis() - FAN_PREV_MILLIS) > TIME) { // Process counters once every second
    FAN_PREV_MILLIS = millis();
    for (int i = 0; i <= FAN_SENSE_COUNT-1; i++)
    {
      int count = interruptCounter[i];
      FAN_SENSE_RESULTS[i] = computeFanSpeed(count);
      interruptCounter[i] = 0;
      Serial.print("Fan # ");
      Serial.print(i);
      Serial.print(":");
      Serial.print(FAN_SENSE_RESULTS[i], DEC);        //Prints the computed fan speed to the serial monitor
      Serial.print(" RPM\r\n");      //Prints " RPM" and a new line to the serial monitor
    }
  }
}

int computeFanSpeed(int count) {
  return count / 2 * 60 ;   //interruptCounter counts 2 pulses per revolution of the fan over a one second period
}

void clearStats() {
  FAN_PREV_MILLIS = millis();
  interruptCounter[1] = 0;
  interruptCounter[2] = 0;
  FAN_CALC_RPM[1] = 0;
  FAN_CALC_RPM[2] = 0;
  FAN_SENSE_RESULTS[1] = 0;
  FAN_SENSE_RESULTS[2] = 0;
}
