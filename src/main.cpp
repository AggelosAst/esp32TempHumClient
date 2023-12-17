#include <WiFi.h>
#include <WiFiGeneric.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <dht.h>

const char *ssid = "VODAFONE_H268Q-6419";
const char *password = "b4H4XXu3e9d2SyfS";

namespace programEnums {
    enum class options {
        AGGRESSIVE,
        NORMAL
    };
    enum class httpMethods {
        POST,
        GET
    };
    enum class counters {
        httpCounter
    };
    enum class counterAction {
        increase,
        decrease
    };
}

namespace programCounters {
    class ThresholdCounters {
    public:
        void editCounter(programEnums::counters counter, int value, programEnums::counterAction mode);
        int getCounter(programEnums::counters counter) const ;
    private:
        int requestFail = 0;
        int anotherFail = 0;
    };
    class controlVariables {
    public:
        bool canExecuteRequest = true;
    };
}

void programCounters::ThresholdCounters::editCounter(programEnums::counters counter, int value,
                                                    programEnums::counterAction mode) {
    switch (mode) {
        case programEnums::counterAction::increase:
            if (counter == programEnums::counters::httpCounter) {
                goto increaseHttpCount;
            }
        increaseHttpCount:
            this->requestFail += value;
            break;
        case programEnums::counterAction::decrease:
            if (counter == programEnums::counters::httpCounter) {
                goto decreaseHttpCount;
            }
            decreaseHttpCount:
            this->requestFail -= value;
            break;
    }
}
int programCounters::ThresholdCounters::getCounter(programEnums::counters counter) const  {
    if (counter == programEnums::counters::httpCounter) {
        return this->requestFail;
    } else {
        return 0;
    }
}

class pinManager {
public:
    constexpr const static int relayPin = 26;
    constexpr const static int ledIndicatorPin = 2;
    constexpr const static int tempSensorPin = 25;
    constexpr const static int speakerPin = 23;
};


HTTPClient http;
DHT dht(pinManager::tempSensorPin, DHT11);
programCounters::ThresholdCounters counters;


class httpClient {
public:
    static String serialiseRequestData(int temp, int hum, const char *sensorName);

    static DynamicJsonDocument DeserializeData(const char *input);

    void performRequest(const char *url, programEnums::httpMethods method, int temp, int hum, const char *sensorName,
                        const char *tempWarning, const char *humWarning);
};

String httpClient::serialiseRequestData(int temp, int hum, const char* sensorName) {
    DynamicJsonDocument doc(256);
    String theJSON;
    doc["temp"] = int(round(temp));
    doc["hum"] = hum;
    doc["sensorId"] = sensorName;
    serializeJson(doc, theJSON);

    return theJSON;
}


DynamicJsonDocument httpClient::DeserializeData(const char *input) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, input);
    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
    }
    return doc;
}

namespace indicatorHandlers {
    class ledIndicate {
    public:
        static void blinkLed(programEnums::options mode);
    };

    class relayIndicate {
    public:
        static void blinkRelay(size_t times, programEnums::options mode);
    };

    class speakerIndicate {
    public:
        static void playFrequency(int time, int frequency, int duty);
    };
};
namespace Messages {
    constexpr const char *LowTemp = "LowTemp";
    constexpr const char *HighTemp = "HighTemp";
    constexpr const char *LowHum = "LowHum";
    constexpr const char *HighHum = "HighHum";
    namespace Warnings {
        const char* humWarning{};
        const char* tempWarning{};
    }
}

namespace BackupVariables {
    class variables {
    public:
        int humidity = 0;
        int temperature = 0;
    };
}

void indicatorHandlers::speakerIndicate::playFrequency(int time, int frequency, int duty) {
    ledcWriteTone(0, frequency);
    ledcWrite(0, duty);
    delay(time);
    ledcWriteTone(0, 0);
    ledcWrite(0, 0);
}

void indicatorHandlers::ledIndicate::blinkLed(programEnums::options mode) {
    switch (mode) {
        case programEnums::options::AGGRESSIVE:
            for (int i = 0; i < 2; ++i) {
                delay(1000);
                digitalWrite(pinManager::ledIndicatorPin, HIGH);
                delay(1000);
                digitalWrite(pinManager::ledIndicatorPin, LOW);
            }

            
            break;
        case programEnums::options::NORMAL:
            delay(1000);
            digitalWrite(pinManager::ledIndicatorPin, HIGH);
            delay(1000);
            digitalWrite(pinManager::ledIndicatorPin, LOW);
            break;
    }
}

void indicatorHandlers::relayIndicate::blinkRelay(size_t times, programEnums::options mode) {
    switch (mode) {
        case programEnums::options::AGGRESSIVE:
            for (size_t i = 0; i < 2; i++) {
                delay(200);
                digitalWrite(pinManager::relayPin, LOW);
                delay(200);
                digitalWrite(pinManager::relayPin, HIGH);
            }
            break;
        case programEnums::options::NORMAL:
            for (size_t i = 0; i < times; i++) {
                delay(500);
                digitalWrite(pinManager::relayPin, LOW);
                delay(500);
                digitalWrite(pinManager::relayPin, HIGH);
            }
            break;
    }
}

indicatorHandlers::ledIndicate ledIndicator;
indicatorHandlers::relayIndicate relayIndicator;
indicatorHandlers::speakerIndicate speakerIndicator;
// Set up the namespaces
httpClient httpHandler;
programCounters::controlVariables variables;
BackupVariables::variables backupVars;


void
httpClient::performRequest(const char *url, programEnums::httpMethods method, int temp, int hum, const char *sensorName,
                           const char *tempWarning, const char *humWarning) {

    //Create a new session for the url
    http.begin(url);

    //Add the required headers
    http.setUserAgent("esp32/1.0");
    http.addHeader("X-Requested-With-X", "Arduino");
    http.addHeader("Content-Type", "application/json");

    //Set the timeout
    http.setTimeout(2000);


    switch (method) {
        case programEnums::httpMethods::POST: {
            const char *JsonToSend = this->serialiseRequestData(temp, hum, sensorName).c_str();

            //Perform Request and deserialize the JSON data.
            int requestStatus = http.POST(this->DeserializeData(JsonToSend).as<String>());
            Serial.println(this->DeserializeData(JsonToSend).as<String>());
            if (requestStatus != HTTP_CODE_OK) {
                Serial.println(requestStatus);
                Serial.println("Bad http response code, theres an error");
                if (variables.canExecuteRequest) {
                   int errorCounter = counters.getCounter(programEnums::counters::httpCounter);
                   if (errorCounter >= 4) {
                       Serial.println("Big amount of errors");
                       variables.canExecuteRequest = false;
                   } else {
                       counters.editCounter(programEnums::counters::httpCounter, 1, programEnums::counterAction::increase);
                   }
                }
                return;
            }
            //Fetch response and deserialize.
            DynamicJsonDocument httpResponseData = this->DeserializeData(http.getString().c_str());

            //Check if errors exist in the response.
            if (httpResponseData.containsKey("error")) {
                Serial.println("An error occurred while performing the request, its probably a server side error.");
                Serial.println(httpResponseData["error"].as<const char *>());
                return;
            } else {
                //They dont exist, continue.
                Serial.println("Success!");
                Serial.println(httpResponseData["data"].as<const char *>());

                if (strcmp(humWarning, "LowHum") == 0) {
                    Serial.println("Low Hum");
                } else if (strcmp(humWarning, "HighHum") == 0) {
                    Serial.println("High humidity");
                    //speakerIndicator.playFrequency(400, 500, 128); TODO: Enable this
                    int relayStatus = digitalRead(pinManager::relayPin);
                    if (relayStatus == LOW) {
                        Serial.println("Relay already enabled");
                    } else {
                        digitalWrite(pinManager::relayPin, LOW);
                    }
                } else {
                    int relayStatus = digitalRead(pinManager::relayPin);
                    if (relayStatus == LOW) {
                        digitalWrite(pinManager::relayPin, HIGH);
                    }
                }
                if (strcmp(tempWarning, "HighTemp") == 0) {
                    Serial.println("High temp");
                } else if (strcmp(tempWarning, "LowTemp") == 0) {
                    Serial.println("Low temp");
                }
            }
        }
            break;
        case programEnums::httpMethods::GET:
            break;
    }
}


IPAddress local_ip(192, 168, 0, 1);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

void setup() {
    Serial.begin(9600);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.enableLongRange(true);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    WiFi.softAP("ESP-32-Server", "12345678910");
    WiFi.begin(ssid, password);
    dht.begin();
    pinMode(pinManager::relayPin, OUTPUT);
    pinMode(pinManager::ledIndicatorPin, OUTPUT);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(pinManager::speakerPin, 0);
    digitalWrite(pinManager::relayPin, 0x1);
    while (WiFiClass::status() != WL_CONNECTED) {
        Serial.println("Connecting..");
        delay(500);
    }
    Serial.println("Connected");
    digitalWrite(pinManager::ledIndicatorPin, 0x1);
}

void loop() {
    if ((WiFiClass::status() == WL_CONNECTED)) {
    } else {
        Serial.println("Connection lost");
    }
    delay(1000);
    int temperature = dht.readTemperature();
    int humidity = dht.readHumidity();

    const char *deviceName = "ESP32";
    const char *url = "https://voidtools.lol/api/v1/data";

    if (humidity > 100) {
        Messages::Warnings::humWarning = "Normal";
        if (backupVars.humidity > 0) {
            humidity = backupVars.humidity;
            Serial.println("Adjusted hum due to error.");
        } else {
            Serial.println("Unexpected output, check this on line");
        }
    }
    if (temperature > 50) {
        Messages::Warnings::tempWarning = "Normal";
        if (backupVars.temperature > 0) {
            temperature = backupVars.temperature;
            Serial.println("Adjusted temp due to error.");
        } else {
            Serial.println("Unexpected output, check this on line");
        }
    }
    if (humidity >= 70) {
        Messages::Warnings::humWarning = Messages::HighHum;
    } else if (humidity < 30) {
        Messages::Warnings::humWarning = Messages::LowHum;
    } else {
        Messages::Warnings::humWarning = "Normal";
    }
    if (temperature > 30) {
        Messages::Warnings::tempWarning = Messages::HighTemp;
    } else if (temperature < 20) {
        Messages::Warnings::tempWarning = Messages::LowTemp;
    } else {
        Messages::Warnings::tempWarning = "Normal";
    }
    if (variables.canExecuteRequest){
        backupVars.humidity = humidity;
        backupVars.temperature = temperature;
        httpHandler.performRequest(url, programEnums::httpMethods::POST, temperature, humidity, deviceName,Messages::Warnings::tempWarning, Messages::Warnings::humWarning);
    } else {
        Serial.println("Couldn't send request, errored too much");
        ledIndicator.blinkLed(programEnums::options::AGGRESSIVE);
    }
}

