////Pins Definition////
#define relayPin 1
#define waterSensorPin 35
#define scalePin0 16     // 16 - yellow wire
#define scalePin1 17     // 17 - orange wire 

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
const String userpass = "CREDENTIALS"; // format user:pass

////Variables for cloud////
String controlFood_e = "done";
int food_weight = 0;
int waterLevel = 0;
int selectedFood = 0;
int selectedWater = 0;

// Scale library and variables
#include "HX711.h"
HX711 scale;
float calibration_factor = 1899; 
float units;

#include "WiFi.h"
const char* ssid = "Net slab 2g";
const char* password = "prahova6net";

SemaphoreHandle_t xScaleSemaphore;  // semaphore to synchronize ElasticTask and ScaleTask
SemaphoreHandle_t xTaskSemaphore;   // semaphore to synchronize ElasticTask and WaterControlTask

void setup() {
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    vTaskDelay(1000);
  }
  Serial.println("CONNECTED!");

  //////Sync real time//////
  configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println();Serial.print("Sync real time. This will take few seconds.");
  while (time(nullptr) <= 100000) 
  {
    Serial.print("*");
    vTaskDelay(100);
  }
  time_t now = time(nullptr);
  setTime(now);
  Serial.println();Serial.print("Date and time set!!!");
  Serial.println(); Serial.print("IP address: "); Serial.println(WiFi.localIP());
  Serial.print("TimeStamp: "); Serial.println(getDate());
  
  scale.begin(scalePin0,scalePin1);   
  scale.set_scale();
  scale.tare();  //Reset the scale to 0
  long zero_factor = scale.read_average(); //Get a baseline reading
  Serial.print("Zero factor: "); 
  Serial.println(zero_factor);

    xScaleSemaphore = xSemaphoreCreateBinary(); 
    xTaskSemaphore = xSemaphoreCreateBinary();
    
    //////Create aditional Tasks for Core 0//////
    xTaskCreatePinnedToCore(
        TaskElastic, /* Task function.                            */
        "Task1",     /* name of task.                             */
        1024 * 7,    /* Stack size of task                        */
        NULL,        /* parameter of the task                     */
        1,           /* priority of the task                      */
        NULL,        /* Task handle to keep track of created task */
        0            /* Core                                      */
    );
    xTaskCreatePinnedToCore(
        TaskReadWaterLevel, /* Task function.                            */
        "Task3",            /* name of task.                             */
        1024 * 7,           /* Stack size of task                        */
        NULL,               /* parameter of the task                     */
        1,                  /* priority of the task                      */
        NULL,               /* Task handle to keep track of created task */
        0                   /* Core                                      */
    );
    xTaskCreatePinnedToCore(
        TaskReadScale, /* Task function.                            */
        "Task2",       /* name of task.                             */
        1024 * 7,      /* Stack size of task                        */
        NULL,          /* parameter of the task                     */
        1,             /* priority of the task                      */
        NULL,          /* Task handle to keep track of created task */
        0              /* Core                                      */
    );  

   xSemaphoreGive(xTaskSemaphore);
}
void loop()
{
  // Empty. Things are done in Tasks.
}
/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

void TaskElastic(void *pvParameters)  // This is a task for CLOUD processing data
{
  (void) pvParameters;
  for (;;) // A Task shall never return or exit.
  {
    vTaskDelay(1);
    xSemaphoreTake(xTaskSemaphore, portMAX_DELAY);
    String request = getRequest();  
    JsonArray response = parseResponse(request);
    handleEvents(response);
    xSemaphoreGive(xTaskSemaphore);
  }
}

//////Function that make a GET request to server//////
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
                        "User-Agent: esp32pet\r\n" +
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

//////Function that parse the response from the server//////
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

//////Function that handle all the events available//////
void handleEvents(JsonArray events){
  Serial.printf("handleEvents events size: %d\n", events.size());
  for (JsonVariant oneEvent : events) {
       if( isCurrentTime(oneEvent) ){
          Serial.println("CurrentTime match eventTime");
          setConfiguration(oneEvent);
          xSemaphoreGive(xScaleSemaphore);   // give semaphore to scale task
          const char* Once = "Once";
          const char* repeat = oneEvent["_source"]["repeat"].as<const char*>();
          
          if(strcmp (repeat, Once) == 0){
            postDelete(oneEvent["_id"].as<const char*>());
          }
          xSemaphoreTake(xScaleSemaphore,portMAX_DELAY);   // wait for scale task to finish measures
          postDone();
          break;
        }
    }
}

//////Function that check if the event is ready to execute//////
bool isCurrentTime(JsonVariant event) {
  time_t nowTime = now() + 2 * 60 * 60;
  time_t eventTime = getTime(event);

  String date = String(year(nowTime)) + '-' + printDigits(month(nowTime)) + '-' + printDigits(day(nowTime)) + 'T' + printDigits(hour(nowTime)) + ':' + printDigits(minute(nowTime)) + ':' + printDigits(second(nowTime)) + 'Z';
  Serial.println("isCurrentTime now timestampString: " + date);
  
  String dateEvent = String(year(eventTime)) + '-' + printDigits(month(eventTime)) + '-' + printDigits(day(eventTime)) + 'T' + printDigits(hour(eventTime)) + ':' + printDigits(minute(eventTime)) + ':' + printDigits(second(eventTime)) + 'Z';
  Serial.println("isCurrentTime event timestampString: " + dateEvent);

  return (getHour(nowTime) == getHour(eventTime) && ( (getMinute(eventTime) >= getMinute(nowTime) - 2) && (getMinute(eventTime) <= getMinute(nowTime) ) ) );
}

//////Functios that process time//////
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

//////Function that set the configuration for the sensors//////
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

//////Function that delete the executed event from the server//////
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
  Serial.println(docDelete);
  String httpDelete = String("POST ") + urlDelete + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "User-Agent: esp32pet\r\n" +
                    "Authorization: Basic " + "ZWxhc3RpYzpBV2J0bUdkYTJRN0JJMmJZcGRqeUY0cWQ=" + "\r\n" +
                    "Connection: close\r\n" +
                    "Content-Type: application/json\r\n" +
                    "Content-Length: " + docDelete.length() + "\r\n\r\n" + docDelete;
  client.print(httpDelete);
  String line = client.readStringUntil('\n');
  Serial.println("Reply was:");
  Serial.println(line);
}

//////Function that makes a POST request with measured data//////
void postDone()
{
  String docDone = "{\"timestamp\":\"" + getDate() + "\"," +
                   "\"event\":\"" + "done" + "\"," +
                   "\"food_weight[g]\":" + food_weight + "," +
                   "\"water_level\":\"" + translateWaterLevel(waterLevel) + "\"" + "}";
  WiFiClientSecure client;
  Serial.print("Connecting to Cloud at: ");
  Serial.println(host);
  client.setInsecure();
  int y = client.connect(host, httpsPort);
  if (!y)
  {
    Serial.println("connection failed: " + y);
  }
  String httpDone = String("POST ") + urlPost + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "User-Agent: esp32pet\r\n" +
                    "Authorization: Basic " + "ZWxhc3RpYzpBV2J0bUdkYTJRN0JJMmJZcGRqeUY0cWQ=" + "\r\n" +
                    "Connection: close\r\n" +
                    "Content-Type: application/json\r\n" +
                    "Content-Length: " + docDone.length() + "\r\n\r\n" + docDone;
  client.print(httpDone);
  String line = client.readStringUntil('\n');
  Serial.println("Reply was:");
  Serial.println(line);
}

//////Function that return the date in the timestamp format//////
String getDate()
{
  String date = String(year()) + '-' + printDigits(month()) + '-' + printDigits(day()) + 'T' + printDigits(hour() + 2) + ':' + printDigits(minute()) + ':' + printDigits(second()) + 'Z';
  return date;
}
//////
String printDigits(int digits)
{
  String dig;
  // utility function for digital clock display: prints preceding colon and leading 0
  if (digits < 10)
    dig = "0";
  dig = dig + String(digits);
  return dig;
}

void TaskReadWaterLevel(void *pvParameters)  // This is the second task made for water level sensor 
{
  (void) pvParameters;

  while (1)
  {
   vTaskDelay(1);  // check if water needs control each minute
   if(xSemaphoreTake(xTaskSemaphore, 5000)) // control water every time elastic task is done or at every 5 secs.
   {
     controlWater(); 
     xSemaphoreGive(xTaskSemaphore);
   }
   else
   {
     controlWater(); 
   }
  }
}
//////Function that control the water level using pump and the water level sensor//////
void controlWater()
{
  if (selectedWater == 0)
    selectedWater = 1;
  measureWater();
  if (waterLevel < selectedWater)
  {
    digitalWrite(relayPin, HIGH);  // open pump
    measureWater();
  }else
  digitalWrite(relayPin, LOW); // close pump
}
void measureWater()
{
  int value = 0;
  //Take a 50 reads average value
  for (int i = 0; i < 50; i++)
  {
    value = value + analogRead(waterSensorPin);
  }
  int averageRead = value / 50;
  //Serial.print("Water level sensor measured value: ");
  //Serial.print(averageRead);Serial.println();
  if (averageRead <= 88)
  {
    waterLevel = 0;
  }
  if (averageRead > 88 && averageRead <= 190)
  {
    waterLevel = 1;
  }
  if (averageRead > 190 && averageRead <= 240)
  {
    waterLevel = 2;
  }
  if (averageRead > 240)
  {
    waterLevel = 3;
  }
  Serial.print("Current water level(0..3): ");
  Serial.print(waterLevel);Serial.println();
}

String translateWaterLevel(int level){
  String water_level = "";
  if(level == 0){
   water_level = "Empty";
  }else if(level == 1){
    water_level = "Low";
      }else if(level == 2){
      water_level = "Medium";
        }else if(level == 3){
        water_level = "High";
          }
          return water_level;
}

void TaskReadScale(void *pvParameters)  //This is the third task made for scale
{
  (void) pvParameters;
  while (1)
  {
    vTaskDelay(1);
    xSemaphoreTake(xScaleSemaphore,portMAX_DELAY);
    controlFood();
    xSemaphoreGive(xScaleSemaphore);
  }
}
//////Function used to control the food using the scale//////
void controlFood()
{
  float units = 0;
  while (units <= selectedFood) // units <= selectedFood
  {
    scale.set_scale(calibration_factor);  //Adjust scale to calibration factor value
    // Calculate the new value in grams
    Serial.print("Reading: ");
    units = scale.get_units();
    if (units < 0){
      units = 0.00;
    }
    food_weight = int(units);
    Serial.print(units);
    Serial.print(" grams");Serial.println();
    vTaskDelay(50);
  }
}
