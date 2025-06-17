#include <WiFi.h>
#include <MicroOcpp.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>

#define DHTPIN 33
//L247N278
#define DHTTYPE DHT11

#define SS_PIN 21    // SDA
#define RST_PIN 22   // RST
#define LED_PIN 2    // Optional: LED toggled when charging

#define RXD2 16
#define TXD2 17

MFRC522 rfid(SS_PIN, RST_PIN);
DHT dht(DHTPIN, DHTTYPE);

float Power;

// Replace with your card's UID
byte validUID[4] = {0x21, 0xF5, 0xA2, 0x26};

const char* ssid = "Nothing";
const char* password = "123456789";
const char* ocppServer = "wss://ocppserver.myekigai.com"; //MAIN ONE


// float myVoltagePin=25;
// float myCurrentPin=35;
// float readVoltage;
// float readCurrent;
// float Voltage;
// float Current;
// float Power;

// String inputData = "";
String idTag = "0123456789ABCD"; // This will be used for OCPP transaction
unsigned long long  currentTransactionId = 0;

bool transactionActive = false;


unsigned long lastDHTRead = 0;
const unsigned long dhtInterval = 10000; // 10 seconds

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  dht.begin();

  SPI.begin(18, 19, 23);  // SCK, MISO, MOSI for ESP32
  rfid.PCD_Init();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  // pinMode(myVoltagePin, INPUT);
  // pinMode(myCurrentPin, INPUT);

  // analogSetWidth(12);               // 12-bit resolution
  //analogSetClockDiv(5);             // 80 MHz / 5 = 16 MHz ADC clock

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  // Initialize OCPP
  mocpp_initialize(ocppServer, "CP_1");
  Serial.println("MicroOcpp initialized");
  Serial.println("Scan your card...");
}

void loop() {


  mocpp_loop(); // MicroOcpp background tasks

  //setMeterValueSampleInterval(10);


  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  Serial.print("Card UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

    calculateCurrentVoltage();

    delay(2000);

  // Energy reading in Wh
  setEnergyMeterInput([]() {
   
    return calculateCurrentVoltage () ; 
  }, 1);





  if (isValidCard(rfid.uid.uidByte)) {
    Serial.println("Authorized card detected");

    auto tx = getTransaction();

    if (!tx || !tx->isActive()) {

      // No active transaction — start a new one
      Serial.printf("[main] Begin Transaction with idTag %s\n", idTag.c_str());



      auto ret = beginTransaction(idTag.c_str());

      if (ret) {
        Serial.println(F("[main] Transaction initiated. Waiting for plug + auth"));



        digitalWrite(LED_PIN, HIGH); // EV plug energized
        DHTRead();

      } else {
        Serial.println(F("[main] No transaction initiated"));
      }

    } else {
      // Transaction already running — check if same card to stop
      Serial.println(F("[main] Transaction already active"));

      const char* activeIdTag = getTransactionIdTag();
      Serial.print(F("[debug] Active IdTag: "));
      Serial.println(activeIdTag ? activeIdTag : "null");

      if (activeIdTag && idTag.equals(activeIdTag)) {
        Serial.println(F("[main] End transaction by RFID card"));
        endTransaction(idTag.c_str());
        digitalWrite(LED_PIN, LOW);
        currentTransactionId = 0;
      } else {
        Serial.println(F("[main] Cannot end transaction by RFID card (different card?)"));
      }
    }

  } else {
    Serial.println("Unauthorized card.");
  }

  delay(1500); // Debounce delay
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();


  
}
 

  // // Read serial input and send to server if charging
  // while (Serial.available()) {
  //   char c = Serial.read();
  //   if (c == '\n') {
  //     if (ocppPermitsCharge()) {
  //       sendSerialDataToServer(inputData);
  //     }
  //     inputData = "";
  //   } else {
  //     inputData += c;
  //   }
  // }


  // Optional: Power reading in W
  // setPowerMeterInput([]() {
  //   // Replace with real-time power reading
  //   return 567.89;
  // }, 1);



// Validate the scanned UID
bool isValidCard(byte *uid) {
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != validUID[i]) {
      return false;
    }
  }
  return true;
}

// Read DHT and send to OCPP server
void DHTRead() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.print(" °C, Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  sendDataToServer(temperature, humidity);
}

// Send DHT data to OCPP server
void sendDataToServer(float temperature, float humidity) {
  sendRequest("DataTransfer",
    [temperature, humidity]() {
      auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(256));
      JsonObject json = doc->to<JsonObject>();
      json["vendorId"] = "CustomClient";
      json["Temperature"] = temperature;
      json["Humidity"] = humidity;
      return doc;
    },
    [](JsonObject response) {
      Serial.println("Response from OCPP server:");
      serializeJson(response, Serial);
      Serial.println();
    }
  );
}

// Send custom serial data to OCPP server
void sendSerialDataToServer(String energy) {
  sendRequest("DataTransfer",
    [energy]() {
      auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(256));
      JsonObject json = doc->to<JsonObject>();
      json["vendorId"] = "CustomClient";
      json["data"] = energy;
      return doc;
    },
    [](JsonObject response) {
      Serial.println("Response from OCPP server:");
      serializeJson(response, Serial);
      Serial.println();
    }
  );

}

void sendMetervalues() {
  if (!isValidCard(rfid.uid.uidByte)) {
    return;
  }

  float energy = calculateCurrentVoltage();
  sendSerialDataToServer(String(energy));
}


// float calculateCurrentVoltage (){
  
//   readVoltage=analogRead(myVoltagePin);
//   Voltage = (readVoltage * 1000 ) / 4095;

//   readCurrent=analogRead(myCurrentPin);
//   Current = (readCurrent * 200 ) / 4095;

//   Power = Voltage * Current ;

//   Serial.println(readVoltage);
//   Serial.println(readCurrent);
//  Serial.println(Power);

//   return Power ;

// }

float calculateCurrentVoltage() {
  String buffer = "";

  while (Serial2.available()) {
    char incomingByte = Serial2.read();
  //  Serial.print(incomingByte); // Optional: Print to Serial Monitor
    buffer += incomingByte;
    delay(2); // Small delay to ensure the complete message is received
  }

 Serial.println(buffer);
 delay(1000);
  Power = buffer.toFloat(); // Convert the full string to float and return

  return Power;
}


