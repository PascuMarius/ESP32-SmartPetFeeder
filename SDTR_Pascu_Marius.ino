#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

//// Real time clock library
#include <TimeLib.h>
#include <Time.h>

////ElasticSearch info////
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
const String node_name = "marius.index";
const String urlPost = "/marius.index/_doc/";
const String urlGet = "/marius.index/_search/";
const String urlDelete = "/marius.index/_delete_by_query/";
const char *host = "8f9677360fc34e2eb943d737b2597c7b.us-east-1.aws.found.io";
const int httpsPort = 9243;
const String userpass = "elastic:AWbtmGda2Q7BI2bYpdjyF4qd"; // format user:pass

////Variables for cloud////
String coltrolFood_e = "";
String coltrolWater_e = "";
int food_weight = 0;
int waterLevel = 0;
int selectedFood = 0;
int selectedWater = 0;

// Scale library and variables
#include "HX711.h"
HX711 scale;
float calibration_factor = 1899; 
float units;

// define two tasks for Elastic & AnalogRead
void TaskElastic( void *pvParameters );
void TaskAnalogReadA3( void *pvParameters );

#include "WiFi.h"
const char* ssid = "Net slab 2g";
const char* password = "prahova6net";

void setup() {
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("CONNECTED!");

  //////Sync real time//////
  configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println();Serial.print("Sync real time. This will take few seconds.");
  while (time(nullptr) <= 100000) 
  {
    Serial.print("*");
    delay(100);
  }
  time_t now = time(nullptr);
  setTime(now);
  Serial.println();Serial.print("Date and time set!!!");
  Serial.println(); Serial.print("IP address: "); Serial.println(WiFi.localIP());
  Serial.print("TimeStamp: "); Serial.println(getDate());
  
  scale.begin(16,17);   // 17 - portocaliu , 16 - galben
  scale.set_scale();
  scale.tare();  //Reset the scale to 0
  long zero_factor = scale.read_average(); //Get a baseline reading
  Serial.print("Zero factor: "); 
  Serial.println(zero_factor);

    // Now set up two tasks to run independently.
    xTaskCreatePinnedToCore(
    TaskElastic
    ,  "Elastic"   // A name 
    ,  5120  // This stack size can be checked & adjusted by reading the Stack Highwater
    ,  NULL
    ,  2  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,  NULL 
    ,  ARDUINO_RUNNING_CORE);

  xTaskCreatePinnedToCore(
    TaskReadScale
    ,  "ReadScale"
    ,  1024  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL 
    ,  ARDUINO_RUNNING_CORE);
}



void loop()
{
  // Empty. Things are done in Tasks.
}

/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

void TaskElastic(void *pvParameters)  // This is a task.
{
  (void) pvParameters;
  for (;;) // A Task shall never return or exit.
  {
    vTaskDelay(1000);
    String request = getRequest();  
    JsonArray response = parseResponse(request);
    handleEvents(response);
  }
}
String getRequest()
{
  String getResponse = "empty";
  String docGet = String("{\"query\":") + "{" + "\"match\":" + "{" + "\"event\":" + "\"start\"" + "}" + "}" + "}"; 
  WiFiClientSecure client;
  Serial.print("Connecting to Cloud at: "); Serial.println(host);
  client.setInsecure();
  int m = client.connect(host, httpsPort);
  if (!m) 
  {
    Serial.println("connection failed: " + m);
  }
  String httpGet = String("GET ") + urlGet + " HTTP/1.1\r\n" +
                       "Host: " + host + "\r\n" +
                        "User-Agent: esp8266pet\r\n" +
                        "Authorization: Basic " + "ZWxhc3RpYzpBV2J0bUdkYTJRN0JJMmJZcGRqeUY0cWQ=" + "\r\n" +
                        "Connection: close\r\n" +
                        "Content-Type: application/json\r\n" + 
                        "Content-Length: " + docGet.length() + "\r\n\r\n" + docGet; 
                        
  client.print(httpGet);
  while(getResponse == "empty"){
    String lineHeader = client.readStringUntil('\r\n');
    if (lineHeader == "\r") {
      getResponse = client.readStringUntil('\n');
    }
  }
 // lineGet = client.readStringUntil('\r\n\r\n');
  Serial.println("Reply was:");
  Serial.println(getResponse);
  client.stop();
  return getResponse;
  }

////////
JsonArray parseResponse(String response){
  Serial.println("parseResponse: response = " + response);
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, response);
  JsonArray hitsArray;
  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return hitsArray;
  }
  hitsArray = doc["hits"]["hits"];
  return hitsArray;
}
////////
void handleEvents(JsonArray events){
  Serial.printf("handleEvents events size: %d\n", events.size());
  for (JsonVariant oneEvent : events) {
       if( isCurrentTime(oneEvent) ){
          Serial.println("CurrentTime match eventTime");
          setConfiguration(oneEvent);
          //event = "start";
          //controlFood(selectedFood);
          controlWater();
          const char* Once = "Once";
          const char* repeat = oneEvent["_source"]["repeat"].as<const char*>();
          delay(200);
          if(strcmp (repeat, Once) == 0){
            postDelete(oneEvent["_id"].as<const char*>());
          }
        //  postDone();
          break;
        }
    }
}
////////
bool isCurrentTime(JsonVariant event) {
  time_t nowTime = now() + 2 * 60 * 60;
  time_t eventTime = getTime(event);

  String date = String(year(nowTime)) + '-' + printDigits(month(nowTime)) + '-' + printDigits(day(nowTime)) + 'T' + printDigits(hour(nowTime)) + ':' + printDigits(minute(nowTime)) + ':' + printDigits(second(nowTime)) + 'Z';
  Serial.println("isCurrentTime now timestampString: " + date);
  
  String dateEvent = String(year(eventTime)) + '-' + printDigits(month(eventTime)) + '-' + printDigits(day(eventTime)) + 'T' + printDigits(hour(eventTime)) + ':' + printDigits(minute(eventTime)) + ':' + printDigits(second(eventTime)) + 'Z';
  Serial.println("isCurrentTime event timestampString: " + dateEvent);

  return (getHour(nowTime) == getHour(eventTime) && ( (getMinute(eventTime) >= getMinute(nowTime) - 2) && (getMinute(eventTime) <= getMinute(nowTime) ) ) );
}
////////
int getHour(time_t timest){
    struct tm *tmp = gmtime(&timest);
    int h = (timest / 3600) % 24;  
    return h;
}
////////
int getMinute(time_t timest){
    struct tm *tmp = gmtime(&timest); 
    int m = (timest / 60) % 60;
    return m;
}
////////
time_t getTime(JsonVariant event) {
  const char* timestampString = event["_source"]["timestamp"].as<const char*>();
  //Serial.printf("getTime timestampString: %s", timestampString);
  TimeElements tm;
  int yr, mnth, d, h, m, s;
  sscanf( timestampString, "%4d-%2d-%2dT%2d:%2d:%2dZ", &yr, &mnth, &d, &h, &m, &s);
  tm.Year = yr - 1970;
  tm.Month = mnth;
  tm.Day = d;
  tm.Hour = h;
  tm.Minute = m;
  tm.Second = s;

  return makeTime(tm);
}
////////
void setConfiguration(JsonVariant event) {
  int setFoodWeight = event["_source"]["setFoodWeight"].as<int>();
  selectedFood = setFoodWeight;
  const char* setWaterLevel = event["_source"]["setWaterLevel"].as<const char*>();
  if(setWaterLevel == "Empty"){
     selectedWater = 0;
    }else if(setWaterLevel == "Low"){
            selectedWater = 1;
      }else if(setWaterLevel == "Medium"){
              selectedWater = 2;
        }else if(setWaterLevel == "High"){
                selectedWater = 3;
          }
  const char* repeat = event["_source"]["repeat"].as<const char*>();   
  Serial.printf("setConfiguration repeat: %s\n", repeat);
}

void postDelete(const char* id){
  String docDelete = String("{\"query\":") + "{" + "\"match\":" + "{" + "\"_id\":\"" + id + "\"" + "}" + "}" + "}";
  WiFiClientSecure client;
  Serial.print("Connecting to Cloud at: ");
  Serial.println(host);
  client.setInsecure();
  int x = client.connect(host, httpsPort);
  if (!x)
  {
    Serial.println("connection failed: " + x);
  }
  String httpDelete = String("POST ") + urlDelete + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "User-Agent: esp8266pet\r\n" +
                    "Authorization: Basic " + "ZWxhc3RpYzpBV2J0bUdkYTJRN0JJMmJZcGRqeUY0cWQ=" + "\r\n" +
                    "Connection: close\r\n" +
                    "Content-Type: application/json\r\n" +
                    "Content-Length: " + docDelete.length() + "\r\n\r\n" + docDelete;
  client.print(httpDelete);
  String line = client.readStringUntil('\n');
  Serial.println("Reply was:");
  Serial.println(line);
}

void TaskReadScale(void *pvParameters)  // This is a task.
{
  (void) pvParameters;

  while (1)
  {
   /* if(controlFood_e == "start"){
      controlFood();
    }else if(controlWater_e == "start"){
      controlWater();
      }*/
  }
}

void controlWater()
{
  
}

String getDate()
{
  String date = String(year()) + '-' + printDigits(month()) + '-' + printDigits(day()) + 'T' + printDigits(hour() + 3) + ':' + printDigits(minute()) + ':' + printDigits(second()) + 'Z';
  return date;
}

String printDigits(int digits)
{
  String dig;
  // utility function for digital clock display: prints preceding colon and leading 0
  if (digits < 10)
    dig = "0";
  dig = dig + String(digits);
  return dig;
}
