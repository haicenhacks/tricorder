#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SCD30.h>
#include "Adafruit_PM25AQI.h"
#include "Adafruit_SGP30.h"
#include "wifi-details.h"


#define NEOPIXEL_I2C_POWER 2
#define NEOPIXEL_PIN 0
#define BUTTON_A 15
#define BUTTON_B 32
#define BUTTON_C 14


#include <WiFi.h>
#include <WiFiMulti.h>

#include <HTTPClient.h>

#include <WiFiClientSecure.h>

const char* ssid     = "your-ssid";
const char* password = "your-password";


Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
Adafruit_SCD30  scd30;
Adafruit_PM25AQI aqi = Adafruit_PM25AQI();
Adafruit_SGP30 sgp;
#include <HTTPClient.h>
unsigned long lastDisplay=0;
unsigned long lastRead=0;

float co2=0;
float temperature=0;
float humidity=0;
int pm10=0;
int pm25=0;
int pm100=0;
int tvoc = 0;
int displayMode = 0;
bool hasData = false;
int counter = 0;
uint16_t TVOC_base, eCO2_base;
WiFiClientSecure *client = new WiFiClientSecure;
HTTPClient ask;
  
void setup() {
  // put your setup code here, to run once:
  // turn on the QT port and NeoPixel
  pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
  Serial.begin(115200);
  pixel.begin();

  
  delay(1000);
  // Try to initialize!
  if (!scd30.begin()) {
    Serial.println("Failed to find SCD30 chip");
    while (1) { delay(10); }
  }
  
  display.begin(0x3C, true); // Address 0x3C default

  Serial.println("OLED begun");

  display.display();
  delay(1000);

  // Clear the buffer.
  display.clearDisplay();
  display.display();

  display.setRotation(1);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  // text display tests
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0,0);

  display.println("Connecting to wifi");
  display.display();
  WiFi.begin(STASSID, STAPSK);

  while (WiFi.status() != WL_CONNECTED) 
  {
      delay(500);
      Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(WiFi.localIP());
  display.display();
  

  Serial.println("SCD30 Found!");
  Serial.print("Measurement Interval: "); 
  Serial.print(scd30.getMeasurementInterval()); 
  Serial.println(" seconds");
  
 
  if (! aqi.begin_I2C())
  {
    Serial.println("Could not find PM 2.5 sensor!");
    while (1) delay(10);
  }
  else
  {
    Serial.println("PM25 found!");
  }

  if (! sgp.begin()){
    Serial.println("Sensor not found :(");
    while (1);
  }
  Serial.print("Found SGP30 serial #");
  Serial.print(sgp.serialnumber[0], HEX);
  Serial.print(sgp.serialnumber[1], HEX);
  Serial.println(sgp.serialnumber[2], HEX);
  sgp.setIAQBaseline(0x8EE3, 0x9F57);
  //0x8EE3 & TVOC: 0x9F57

  
}


uint32_t getAbsoluteHumidity(float temperature, float humidity)
{
    // approximation formula from Sensirion SGP30 Driver Integration chapter 3.15
    const float absoluteHumidity = 216.7f * ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) / (273.15f + temperature)); // [g/m^3]
    const uint32_t absoluteHumidityScaled = static_cast<uint32_t>(1000.0f * absoluteHumidity); // [mg/m^3]
    return absoluteHumidityScaled;
}

void readSensors()
{
  delay(1000);
  Serial.println("reading scd30");
  if (scd30.dataReady())
  {
    Serial.println("Data available!");

    if (!scd30.read())
    {
      Serial.println("Error reading sensor data");
      return;
    }

    co2 = scd30.CO2;
    temperature = scd30.temperature;
    humidity = scd30.relative_humidity;
  }

  Serial.println("Reading pm sensor");
  PM25_AQI_Data data;
  
  if (! aqi.read(&data))
  {
    Serial.println("Could not read from AQI");
    delay(500);  // try again in a bit!
    return;
  }

  Serial.println("AQI reading success");
  pm10 = data.pm10_standard;
  pm25 = data.pm25_standard;
  pm100 = data.pm100_standard;

  sgp.setHumidity(getAbsoluteHumidity(temperature, humidity));
  if (! sgp.IAQmeasure())
  {
    Serial.println("tvoc measurement failed");
    return;
  }
  tvoc = sgp.TVOC;
  
  if (! sgp.IAQmeasureRaw())
  {
    Serial.println("tvoc raw Measurement failed");
    return;
  }

  delay(1000);

  counter++;
  if (counter == 30)
  {
    counter = 0;

    if (! sgp.getIAQBaseline(&eCO2_base, &TVOC_base))
    {
      Serial.println("Failed to get baseline readings");
      return;
    }
  }
  
  Serial.println("read all sensors");
  send_data();
}


void send_data()
{
  if(client)
  {
    client -> setCACert(rootCACertificate);
      // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
    HTTPClient https;
  
    Serial.println("sending data");
  
    String postData = "pass="+String(api_key) + "&device_id="+String(SEN_ID)
      +"&co2="+String(co2, 1)
      +"&temp1="+String(temperature,1)
      +"&humidity="+String(humidity,1)
      +"&pm10="+String(pm10)
      +"&pm25="+String(pm25)
      +"&pm100="+String(pm100)
      +"&tvoc="+String(tvoc)
      ; 
    Serial.println(postData);
    Serial.println(WiFi.status());

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    if (client->connect(host, 443))
    {
      client->println("POST /tricorder/insert HTTP/1.0");
      client->println("Host: " + (String)host);
      client->println(F("User-Agent: ESP"));
      client->println(F("Connection: close"));
      client->println(F("Content-Type: application/x-www-form-urlencoded;"));
      client->print(F("Content-Length: "));
      client->println(postData.length());
      client->println();
      client->println(postData);
      Serial.println(F("Data were sent successfully"));
      
      while (client->connected())
      {
        String line = client->readStringUntil('\n'); //HTTP headers
        if (line == "\n")
        {
          break;
        }
      }

      String line = client->readStringUntil('\n'); //payload first row
      Serial.println(line);
      client->stop();
      https.end();
    }
    else
    {
      Serial.println(F("Connection wasnt established"));
    }
    //Serial.println(httpCode); //Print HTTP return code 
  }
}
void updateDisplay()
{
  char buffer[11];
  display.clearDisplay();
  display.display();
  display.setTextSize(3);
  display.setCursor(0,0);
  switch (displayMode)
  {
    case 0:
      Serial.print("Temperature: ");
      Serial.print(temperature, 1);
      Serial.println(" degrees C");
      display.println("Temp.");
      display.println(temperature, 1);
      //sprintf(buffer, "Temp %4.1f", temperature);
      //display.println(buffer);
      break;

    case 1:
      Serial.print("Relative Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");
      display.println("Humid.");
      display.print(humidity, 1);
      display.println("%");
      
      //printf(buffer, "Hum. %4.1f%%", humidity);
      //display.println(buffer);
      break;
      
    case 2:
      Serial.print("CO2 ppm: ");
      Serial.print(co2);
      //sprintf(buffer, "CO2 %5.1f", co2);
      //display.println(buffer);
      display.println("CO2 ppm");
      display.println(co2, 1);
      break;
      
    case 3:
      Serial.print("PM 1.0: ");
      Serial.println(pm10);
      display.println("PM 1.0");
      display.println(pm10);
      break;     
      
    case 4:
      Serial.print("PM 2.5: ");
      Serial.println(pm25);
      display.println("PM 2.5");
      display.println(pm25);
      break;
      
    case 5:
      Serial.print("PM 10.0: ");
      Serial.println(pm100);
      display.println("PM 10.0");
      display.println(pm100);
      break;
    case 6:
      Serial.print("TVOC: ");
      Serial.println(tvoc);  
      display.println("TVOC");
      display.println(tvoc);
      break;
    case 7:
      Serial.print("TVOC base: ");
      Serial.println(TVOC_base);
      Serial.print("eCO2 base: ");
      Serial.println(eCO2_base);
      display.println(TVOC_base);
      display.println(eCO2_base);
      break;
    default:
      displayMode = 0;
      updateDisplay();
  }

  display.display();
  // Set the LED color  
  if (co2 < 600)
    {
      pixel.setBrightness(20); // not so bright
      pixel.setPixelColor(0, 0x00ff00);
      pixel.show();
    }
    else if (co2 < 1000)
    {
      pixel.setBrightness(20); // not so bright
      pixel.setPixelColor(0, 0xbfff00);
      pixel.show();
    }
    else if (co2 < 1500)
    {
      pixel.setBrightness(20); // not so bright
      pixel.setPixelColor(0, 0xffff00);
      pixel.show();
    }
    else if (co2 < 2000)
    {
      pixel.setBrightness(20); // not so bright
      pixel.setPixelColor(0, 0xff8000);
      pixel.show();
    }
    else if (co2 > 2100)
    {
      pixel.setBrightness(20); // not so bright
      pixel.setPixelColor(0, 0xcc0000);
      pixel.show();
    }
}


void loop() {
  unsigned long currentTime = millis();
  //Serial.println(currentTime);

  if ((currentTime - lastRead > 60*1000) || lastRead == 0)
  {
    readSensors();
    hasData = true;
    lastRead = currentTime;
  }

  if ((currentTime - lastDisplay > 2000) && hasData)
  {
    updateDisplay();
    lastDisplay = currentTime;
    displayMode ++;
  }
  
  delay(1000);
}

void LEDon() {
#if defined(PIN_NEOPIXEL)
  pixel.begin(); // INITIALIZE NeoPixel
  pixel.setBrightness(20); // not so bright
  pixel.setPixelColor(0, 0xff00ff);
  pixel.show();
#endif
}

void LEDoff() {
#if defined(PIN_NEOPIXEL)
  pixel.setPixelColor(0, 0x0);
  pixel.show();
#endif
}
