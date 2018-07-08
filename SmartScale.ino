#include "ESP8266WiFi.h"
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <HX711.h>  // HX711 library for the scale
#include <SSD1306Wire.h> // Library for the SSD1306 Screen
#include <OLEDDisplayFonts.h>
#include <SPI.h>

/************************* WiFi Access Point *********************************/
#define WLAN_SSID       "XXX"
#define WLAN_PASS       "XXX"

/************************* Adafruit.io Setup *********************************/
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883           // use 8883 for SSL
#define AIO_USERNAME    "XXX"
#define AIO_KEY         "XXX"

/************ Global State (you don't need to change this!) ******************/
// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

const char MQTT_SERVER[]     = AIO_SERVER;
const char MQTT_USERNAME[]   = AIO_USERNAME;
const char MQTT_PASSWORD[]   = AIO_KEY;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, AIO_SERVERPORT, MQTT_USERNAME, MQTT_PASSWORD);

/********************************* Screen ************************************/

const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = D6;
const int SDC_PIN = D7;

SSD1306Wire      display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);

/********************************* Scale ************************************/

#define calibration_factor (-45969.00 / 2.345) //-7050.0 //This value is obtained using the SparkFun_HX711_Calibration sketch

const int SCALE_DOUT_PIN = D2;
const int SCALE_SCK_PIN = D3;

#define NUM_MEASUREMENTS 20 // Number of measurements
#define THRESHOLD 20      // Measures only if the weight is greater than 2 kg. Convert this value to pounds if you're not using SI units.
#define THRESHOLD1 0.5  // Restart averaging if the weight changes more than 0.5 kg.

HX711 scale(SCALE_DOUT_PIN, SCALE_SCK_PIN);

float weight = 0.0;
float prev_weight = 0.0;


/********************************* Feed ************************************/

const char WEIGHT_FEED[]  = AIO_USERNAME "/feeds/weight";
Adafruit_MQTT_Publish weight_pub = Adafruit_MQTT_Publish(&mqtt, WEIGHT_FEED);


/*************************** Sketch Code ************************************/

void MQTT_connect();

void setup() {
  Serial.begin(115200);
  delay(10);

  initilizeTFTDisplay();

  connectToWifi();
  
  scale.set_scale(calibration_factor); //This value is obtained by using the SparkFun_HX711_Calibration sketch
  scale.tare(); //Assuming there is no weight on the scale at start up, reset the scale to 0
}

void loop() {
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTT_connect();

  // this is our 'wait for incoming subscription packets' busy subloop
  Adafruit_MQTT_Subscribe *subscription;
  Serial.print(scale.get_units());

if(scale.get_units() > THRESHOLD){
    printWeight(scale.get_units());
    if(stableWeightReached()) {
      float weight = readWeightFromHX711();
  
      if (! weight_pub.publish(weight)){
        Serial.println(F("Failed to publish weight\n"));
      }
      else{
        Serial.println(F("Weight published!\n"));
        printWeight(weight);
        delay(2000);
        display.setFont(ArialMT_Plain_16);
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        display.clear();
        display.drawString(0, 0, "SmartScale v0.1");
        display.setFont(ArialMT_Plain_24);
        display.drawString(30, 20, "Saved!");
        display.display();
        delay(4000);
        while(scale.get_units() > THRESHOLD) {
          delay(100);
        }
      }
  }
  else {printString("Please wait...");}
}
else {printString("Ready.");}

  
  delay(500);
}

float readWeightFromHX711(){
  weight = scale.get_units();

  float avgweight = 0;

  if (weight > THRESHOLD) { // Takes measures if the weight is greater than the threshold
    float weight0 = scale.get_units();
    for (int i = 0; i < NUM_MEASUREMENTS; i++) {  // Takes several measurements
      delay(200);
      weight = scale.get_units();
      avgweight += weight;
      if(i > 0){
        avgweight = avgweight / 2;
      }
      if ((weight - weight0) > THRESHOLD1) {
        avgweight = 0;
        i = 0;
      }
      weight0 = weight;
      printWeight(avgweight);
    }

    Serial.print("Measured weight: ");
    Serial.print(avgweight, 1);
    Serial.println(" kg");    
    }
    return avgweight;
}

bool stableWeightReached() {
  weight = scale.get_units();
  Serial.println(weight);
  if((prev_weight - weight) < 2 && weight > THRESHOLD){
    prev_weight = weight;
    return true;
  }
  else{
    prev_weight = weight;
    return false;
  }
}

void initilizeTFTDisplay(){
  display.init();
  display.flipScreenVertically();
}

void printString(String text) {
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.clear();
  display.drawString(0, 0, "SmartScale v0.1");
  display.setFont(ArialMT_Plain_24);
  display.drawString(0, 20, text);
  display.display();
  }

void printWeight(float wg) {
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.clear();
  display.drawString(0, 0, "SmartScale v0.1");
  display.setFont(ArialMT_Plain_24);
  display.drawString(30, 20, String(wg));
  display.display();
  }

void connectToWifi(){
  // Connect to WiFi access point.
  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);
  printString("WLAN");
  String stat = "WLAN";

  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    stat += ".";
    printString(stat);
  }
 
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: "); 
  Serial.println(WiFi.localIP());
  printString("IP: " + WiFi.localIP());
  delay(1000);
}

void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}
