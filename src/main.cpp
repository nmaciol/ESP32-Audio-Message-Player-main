/*  *
 * The MP3 player is based on the following examples:
 * https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/examples-audiokit/streams-sd_mp3-audiokit/streams-sd_mp3-audiokit.ino
 *              and
 * https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/examples-tts/streams-google-audiokit/streams-google-audiokit.ino
 *
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <FS.h>
#include <SD_MMC.h>
#include <AudioTools.h>
#include <AudioTools/Disk/AudioSourceSDMMC.h>
#include <AudioTools/AudioLibs/AudioBoardStream.h>
#include <AudioTools/AudioCodecs/CodecMP3Helix.h>
#include <AudioTools/Communication/AudioHttp.h>
#include <SimpleFTPServer.h>
#include <esp_sleep.h>
#include <ESPmDNS.h>
#include <ESP32Time.h>
#include "sam_arduino.h"
#include <ESP32Ping.h>

//  Fix SD card access issue in FTP server
//  ----------------------------------------
// !!!!! Overwrite value in  .pio\libdeps\esp32dev\SimpleFTPServer\FtpServerKey.h
/// Line 63 #define DEFAULT_STORAGE_TYPE_ESP32 					STORAGE_SD_MMC

#undef DEFAULT_STORAGE_TYPE_ESP32
#define DEFAULT_STORAGE_TYPE_ESP32 STORAGE_SD_MMC
#include <PubSubClient.h>
#include <QueueList.h>
#include <Regexp.h>


/*
  0.2.0 Add Live Stream  code optimization
  0.2.3 Add  MQTT command to set sleep mode and deep sleep time, and publish sleep mode and WiFi RSSI in ping response
  0.2.4 Add RTC time to ping response and mDNS
  0.2.5 Add MQTT command to get last action 
  0.2.6 Add MQTT connect watchdog and reboot after 20 MQTT connection failures
  0.3.0 Replace SD Lib with SD_MMC, IniFile Lib with ArduinoJson, code optimization, bug fixes, and more MQTT commands
  0.3.1 Remove credentials.h and add error handling for missing or wrong entries in app.json
  0.3.2 Add Audio error mesages via TTS Comodore C64 voice for Config file not exist, json error, network connection error
  0.4.0 Add Simple Web Server functionality
*/

#define FIRMWARE_VERSION "0.4.0"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;        // GMT offset in seconds / Europe/Berlin / CET (Europe)
const int   daylightOffset_sec = 3600; // DST offset in seconds  / Europe/Berlin / CET (Europe)

ESP32Time rtc;
//ESP32Time rtc(3600);  // offset in seconds GMT+1
int chipSelect = PIN_AUDIO_KIT_SD_CARD_CS;
int MqttConnectErrorCounter = 0;
const int errorLED = 22;

// helper.h
void getTtmFileName(String text, char *buf);
bool setGlobalVar();
//void webHandler(WiFiClient client, AudioBoardStream i2s, String header);

// Declare variables for setting up WLAN, FTP, MQQTT, TTS, etc.
String S_HOST_NAME;
String S_AUDIO_VOLUME;
String S_START_SOUND;

String S_WIFI_SSID;
String S_WIFI_PASSWORD;

String S_FTP_SVR_USER;
String S_FTP_SVR_PASSWORD;

String S_MQTT_SERVER;
String S_MQTT_PORT;
String S_MQTT_USER;
String S_MQTT_PASSWORD;
String S_MQTT_HOUSE;

String S_TTS_QRY_GOOGLE;
String S_TTS_LANG;
String S_TTS_SPEED;
String S_TTS_MAX_LEN_TTM;

// create a queue of strings messages.
QueueList<String> queueOrder;

// WiFi + MQTT + FTP
WiFiClient wifiClientMqtt;
WiFiClient wifiClient;
WiFiServer  server(80);

URLStream url;
PubSubClient mqttClient(wifiClientMqtt);
FtpServer ftpServer;
File file; // final output stream

AudioBoardStream i2s(AudioKitEs8388V1);
EncodedAudioStream decoder(&i2s, new MP3DecoderHelix()); // Decoding stream

// Text-To-Phoneme converter
SAM sam(i2s);
StreamCopy copier; //  Audio Stream Input
File audioFile;
bool bStartup = false;
bool bLiveStreamPause = false;
String acceptMime = "audio/mp3";
String order;
Str query;
String currentLiveStream;
float PreviousVolume = 0;
String PreviousLang = S_TTS_LANG;
String TempVolume = "";
String LastAction = "";
// JSON document for app.ini file
JsonDocument docIniFile;

// Variable to store the HTTP request
String header;

// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

void webHandler () {
    Serial.println("New Client.");                // print a message out in the serial port
    String currentLine = "";     
    int timerCounter = 0;                          // make a String to hold incoming data from the client
    while (wifiClient.connected() &&  timerCounter  < 2000 ) {  // loop while the client's connected
      timerCounter++;
      delay(1);
      if (wifiClient.available()) {                   // if there's bytes to read from the client,
        char c = wifiClient.read();                   // read a byte, then
        //Serial.write(c);                        // print it out the serial monitor
        header += c;
        if (c == '\n') {                          // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            String index = "";
            wifiClient.println("HTTP/1.1 200 OK");
            wifiClient.println("Content-type:text/html");
            wifiClient.println("Connection: close");
            wifiClient.println();
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            // Display the HTML web page  i2s.incrementVolume(-0.05);
            if (header.indexOf("GET /test") >= 0 || header.indexOf("GET /vol") >= 0) {
                index += "OK";
                Serial.println("Start Test-Sound.");
                // Serial.println("Sound started--Key");
                if (header.indexOf("GET /test1") >= 0) {        // Test Sounds
                  queueOrder.push("mp3" + docIniFile["WebUI"]["test"]["mp3a"].as<String>()); 
                } else if (header.indexOf("GET /test2") >= 0) {
                  queueOrder.push("mp3" +  docIniFile["WebUI"]["test"]["mp3b"].as<String>());
                } else if (header.indexOf("GET /test3") >= 0) {
                  queueOrder.push("tts" +  docIniFile["WebUI"]["test"]["tts"].as<String>());
                }else if (header.indexOf("GET /test4") >= 0) {
                  if (currentLiveStream.length() == 0) {
                    currentLiveStream = docIniFile["WebUI"]["test"]["radio"].as<String>();
                    acceptMime = "audio/mp3";
                    Serial.println("Start Radio Stream: " + currentLiveStream);
                  } else {
                    Serial.println("Stop Radio Stream: ");
                    currentLiveStream = "";
                    decoder.end();
                    delay(100);
                    url.flush();
                    url.end();
                    url.clear();
                    copier.end();
                    bLiveStreamPause = true;
                  }
                } else if (header.indexOf("GET /volUp") >= 0) {     // Volume
                  i2s.incrementVolume(0.05);
                } else if (header.indexOf("GET /volDown") >= 0) {
                  i2s.incrementVolume(-0.05);
                } else if (header.indexOf("GET /volRestore") >= 0) {
                  i2s.setVolume((float)S_AUDIO_VOLUME.toFloat());
                }else if (header.indexOf("GET /volSave") >= 0) {                  
                  docIniFile["main"]["audio-volume"] = String(i2s.getVolume());
                  // Saving new volume to app.json
                  SD_MMC.remove("/app.json");
                  File file = SD_MMC.open("/app.json", FILE_WRITE);
                  serializeJson(docIniFile, file);
                  file.close();
                }
            } else {
                index += "<!DOCTYPE html><html>";
                index += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>ESP32 Audio-Message-Player</title>";
                index += "<link rel=\"icon\" href=\"data:,\">";
                index += "<script>";
                index += "  function start(service){ ";
                index += "    document.getElementById(\"loader\").style.display = \"block\";";
                index += "    fetch(service, { method: 'GET' })";
                index += "      .then(res => res.text())";
                index += "      .then(() => { ";
                index += "        document.getElementById(\"loader\").style.display = \"none\";";
                index += "      })";
                index += "      .catch(() => {";
                index += "        document.getElementById(\"loader\").style.display = \"none\";";
                index += "        alert(\"Fehler\");";
                index += "      });";

                index += "    };";
                index += "</script>";
                // CSS to style the on/off buttons 
                // Feel free to change the background-color and font-size attributes to fit your preferences
                index += "<style>html { font-family: Helvetica; display: inline-block; margin: 5px auto; text-align: center;}";
                index += "table { display: table; border-collapse: separate; border-spacing: 2px; border-color: gray; margin-left: auto; margin-right: auto; text-align: left;}";
                index += "table tbody td {padding: 5px 6px; border-bottom: 1px solid #e9ecef;  vertical-align: middle; word-wrap:break-word;}";
                index += "button { display: block;  width: 100%; font-size: 32px; margin: 2px; min-width: 60px;}";
                index += "#loader{display:none;position:fixed;top:0;left:0;width:100\%;height:100%;background:#0008;color:white;font-size:20px;text-align:center;padding-top:50%;z-index:9999;}";
                index += "</style></head>";
                index += "<div id=\"loader\">Loading...</div>";
                index += "<table style=\"margin-top: 5px;\">";
                index += "<caption><b>ESP32 Audio-Message-Player</b></caption>";
                index += "<tr>";
                      index += "<td><b>Time</b></td>";
                      index += "<td>" + rtc.getTime() + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td style=\"width: 30%;\">Host-Name</td>";
                    index += "<td>" + S_HOST_NAME + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>MQTT House</td>";
                    index += "<td>" + S_MQTT_HOUSE + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>WiFi SSID</td>";
                    index += "<td>" + S_WIFI_SSID + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>IP</td>";
                    index += "<td>" + WiFi.localIP().toString() + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>RSSI</td>";
                    index += "<td>" + String(WiFi.RSSI()) + " dBm</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>Volume</td>";
                    index += "<td>" + String(i2s.getVolume()) + "  /  Init: " + String(S_AUDIO_VOLUME) + " /  Prev: " + String(PreviousVolume) + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>Last Action</td>";
                    index += "<td>" + String(LastAction.c_str()) + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>Free Heap</td>";
                    uint32_t number = ESP.getFreeHeap();
                    char buffer[12];
                    sprintf(buffer, "%u", number);
                    index += "<td>" +  String(buffer) + "</td>";
                  index += "</tr>";
                  index += "<tr>";
                    index += "<td>Version</td>";
                    index += "<td>" + String(FIRMWARE_VERSION) + "</td>";
                  index += "</tr>";
                index += "</table>";

                //Table with buttons to trigger actions Volume up/down, restore and save
                index += "<table style=\"margin-top: 5px;\">";
                index += "<caption><b>&#128266;&nbsp;Volumen</b></caption>";
                  index += "<tr>";
                    index += "<td><button onclick=\"start('/volUp');\" >&#128264;&nbsp;+</button></td>";
                    index += "<td><button onclick=\"start('/volDown');\" >&#128265;&nbsp;-</button></td>";
                    index += "<td><button onclick=\"start('/volRestore');\" >&#8634;</button></td>";
                    index += "<td><button onclick=\"start('/volSave');\" >&#128190;</button></td>";
                  index += "</tr>";
                index += "</table>";

                //Table with buttons to trigger actions Volume up/down, restore and save  alt=\"" + docIniFile["main"]["start-sound"].as<String>() + "\"
                index += "<table style=\"margin-top: 5px;\">";
                index += "<caption><b>&#12340;&nbsp;Test-sounds</b></caption>";
                  index += "<tr>";
                    index += "<td><button onclick=\"start('/test1');\" >&#128276;</button></td>";
                    index += "<td><button onclick=\"start('/test2');\" >&#128021;</button></td>";
                    index += "<td><button onclick=\"start('/test3');\" >&#129311;</button></td>";
                    index += "<td><button onclick=\"start('/test4');\" >&#128251;/&#9209;</button></td>";
                  index += "</tr>";
                index += "</table>";
                index += "</body></html>";
            }
            wifiClient.println(index);

            // The HTTP response ends with another blank line
            wifiClient.println();

          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    wifiClient.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
    return;
  }

// Helper function to play error messages via TTS Comodore C64 voice
void playError(const char *sText)
{
  auto cfg = i2s.defaultConfig();
  cfg.bits_per_sample = sam.bitsPerSample();
  cfg.channels = sam.channels();
  cfg.sample_rate = sam.sampleRate();
  cfg.sd_active = false;
  i2s.begin(cfg);
  i2s.setVolume(0.5);
  sam.say(sText);
  //
  delay(1000);
  
}

// TTS setup Google Query
const char *tts(const char *text, const char *lang = "de-DE", const char *speed = "0.7")
{
  query = S_TTS_QRY_GOOGLE.c_str();
  query.replace("%1", lang);
  query.replace("%2", speed);
  Str encoded(text);
  encoded.urlEncode();
  query.replace("%3", encoded.c_str());
  return query.c_str();
}

// Worker function for Text-To-MP3 conversion with Google TTS
void TTM_Worker_Google(String sTts)
{

  char buf[240];
  // Serial.println("TTM " + sTts);
  getTtmFileName(sTts, buf);
  // show results
  // Serial.print ("Converted string: ");
  // Serial.println (buf);
  SD_MMC.begin();
  if (!SD_MMC.open("/tts-mp3/" + String(buf) + ".mp3", FILE_READ))
  {

    // Serial.print("File  --- ");
    // Serial.print(buf);
    // Serial.println(" --- does not exist");
    //  url
    const char *url_str = tts(sTts.c_str(), S_TTS_LANG.c_str(), S_TTS_SPEED.c_str());
    // Serial.println("URL Query -- " + String(url_str));
    //  generate mp3 with the help of google translate
    url.begin(url_str, "audio/mp3");
    decoder.begin();
    // copy file
    SD_MMC.begin();
    file = SD_MMC.open("/tts-mp3/" + String(buf) + ".mp3", FILE_WRITE);
    file.seek(0); // overwrite from beginning
    copier.begin(file, url);
    copier.copyAll();
    file.close();
  }
  // Serial.println("TTM Push Queue MP3  " + String(PreviousVolume));
  if (PreviousVolume > 0.01)
  {
    queueOrder.push("mp3" + TempVolume + "!/tts-mp3//" + String(buf) + ".mp3");
  }
  else
  {
    queueOrder.push("mp3/tts-mp3//" + String(buf) + ".mp3");
    // Serial.println ("TTM -> TTS " +  String(buf));
  }
}

// Button 3
void stopPlaySound(bool, int, void *)
{
  // Serial.println("Stop Play Sound");
  currentLiveStream = "";
  decoder.end();
  url.flush();
  url.end();
  url.clear();
  copier.end();
}
// Button 4
void playTestSound(bool, int, void *)
{
  // Serial.println("Sound started--Key");
  queueOrder.push("mp3" + S_START_SOUND);
}
// Button 5
void audioVolumeDown(bool, int, void *)
{
  i2s.incrementVolume(-0.05);
};
// Button 6
void audioVolumeUp(bool, int, void *)
{
  i2s.incrementVolume(0.05);
};

// MQTT callback function
void mqttCallback(char *topic, byte *message, unsigned int length)
{
  String messageTemp;
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
  Serial.println(topic);
  // Execute MQTT-command
  if (String(topic) == S_HOST_NAME + "/mp3" or String(topic) == S_MQTT_HOUSE + "/mp3")
  {
    // Serial.print("MP3 starting  -- ");
    queueOrder.push("mp3" + messageTemp);
  }
  else if (String(topic) == S_HOST_NAME + "/incVol" or String(topic) == S_MQTT_HOUSE + "/incVol")
  {
    // Serial.println("Setting Volume");
    if (PreviousVolume > 0) {
      i2s.incrementVolume((float)messageTemp.toFloat());
    }
    else {
      i2s.incrementVolume((float)messageTemp.toFloat());
      PreviousVolume=i2s.getVolume();
    }
  }
  else if (String(topic) == S_HOST_NAME + "/setVol" or String(topic) == S_MQTT_HOUSE + "/setVol")
  {
    // Serial.println("Setting Volume");
    i2s.setVolume((float)messageTemp.toFloat());
    docIniFile["main"]["audio-volume"] = messageTemp;
    // Saving new volume to app.json
    SD_MMC.remove("/app.json");
    File file = SD_MMC.open("/app.json", FILE_WRITE);
    serializeJson(docIniFile, file);
    file.close();

    PreviousVolume = 0;
  }
  else if (String(topic) == S_HOST_NAME + "/resVol" or String(topic) == S_MQTT_HOUSE + "/resVol")
  {
    // Serial.println("Setting Volume");
    // Restore volume from app.json
    i2s.setVolume((float)S_AUDIO_VOLUME.toFloat());
    PreviousVolume = 0;
  }
  else if (String(topic) == S_HOST_NAME + "/stop" or String(topic) == S_MQTT_HOUSE + "/stop")
  {
    Serial.println("MQTT Stopping playing");
    currentLiveStream = "";
    i2s.setMute(true);
    decoder.end();
    url.flush();
    url.end();
    url.clear();
    // i2s.end();
    copier.end();
    delay(100);
    i2s.setMute(false);
    //queueOrder.push("mp3" + S_START_SOUND);
  }
  else if (String(topic) == S_HOST_NAME + "/tts" or String(topic) == S_MQTT_HOUSE + "/tts")
  {
    queueOrder.push("tts" + messageTemp);
  }
  else if (String(topic) == S_HOST_NAME + "/ls/mp3" or String(topic) == S_MQTT_HOUSE + "/ls/mp3")
  {
    Serial.println("ls/mp3");
    currentLiveStream = messageTemp;
    acceptMime = "audio/mp3";
  }
  else if (String(topic) == S_HOST_NAME + "/ls/aac" or String(topic) == S_MQTT_HOUSE + "/ls/aac")
  {
    Serial.println("ls/aac");
    currentLiveStream = messageTemp;
    acceptMime = "audio/aac";
  }
  else if (String(topic) == S_HOST_NAME + "/ttm" or String(topic) == S_MQTT_HOUSE + "/ttm")
  {
    // Serial.print("TTM starting  -- ");
    if (messageTemp.length() > S_TTS_MAX_LEN_TTM.toInt())
    {
      queueOrder.push("tts" + messageTemp);
    }
    else
    {
      queueOrder.push("ttm" + messageTemp);
    }
  }
  else if (String(topic) == S_HOST_NAME + "/delttm")
  {
    char buf[240];
    getTtmFileName(messageTemp, buf);
    // show results
    String filename = "/tts-mp3/" + String(buf) + ".mp3";
    SD_MMC.begin();
    if (SD_MMC.remove(filename))
    {
      // Serial.print("File ");
      // Serial.print(filename);
      // Serial.println(" deleted successfully.");
    }
    else
    {
      // Serial.print("Failed to delete file ");
      // Serial.println(filename);
    }
  }
  else if (String(topic) == S_HOST_NAME + "/speed" or String(topic) == S_MQTT_HOUSE + "/speed")
  {
    S_TTS_SPEED = messageTemp;

  }
  else if (String(topic) == S_HOST_NAME + "/ping")
  {
    uint32_t number = ESP.getFreeHeap();
    char buffer[12];
    sprintf(buffer, "%u", number);
    // Serial.println("Connection test Ping");
    mqttClient.publish((S_HOST_NAME + "/FreeHeap").c_str(), (String(buffer)).c_str(), 24);
    mqttClient.publish((S_HOST_NAME + "/version").c_str(), FIRMWARE_VERSION, 24);
  

    mqttClient.publish((S_HOST_NAME + "/currVol").c_str(), (String(i2s.getVolume())).c_str(), 24);
    mqttClient.publish((S_HOST_NAME + "/IP").c_str(),  (WiFi.localIP().toString()).c_str());
    mqttClient.publish((S_HOST_NAME + "/SSID").c_str(),  (WiFi.SSID()).c_str());
    mqttClient.publish((S_HOST_NAME + "/LastAction").c_str(),  LastAction.c_str());
    
    // Sleep Mode
    wifi_ps_type_t ps;
    esp_wifi_get_ps(&ps);
    if (ps == WIFI_PS_MIN_MODEM)
    {
      mqttClient.publish((S_HOST_NAME + "/sleepMode").c_str(), "MIN_MODEM");
    }
    else if (ps == WIFI_PS_MAX_MODEM)
    {
      mqttClient.publish((S_HOST_NAME + "/sleepMode").c_str(), "MAX_MODEM");
    }
    else
    {
      mqttClient.publish((S_HOST_NAME + "/sleepMode").c_str(), "NONE");
    }

    // WiFi RSSI
    mqttClient.publish((S_HOST_NAME + "/rssi").c_str(), (String(WiFi.RSSI())).c_str());
    mqttClient.publish((S_HOST_NAME + "/time").c_str(), (rtc.getTime()).c_str());
  }
  else if (String(topic) == S_HOST_NAME + "/reboot")
  {
    // Serial.println("Reboot");
    i2s.end();
    decoder.end();
    ESP.restart();
  }
  else if (String(topic) == S_HOST_NAME + "/lpm")  // Low Power Mode
  {
    if(messageTemp=="on"){
      Serial.print("WiFi set sleep mode ON");
      delay(100);
      //WiFi.setSleep(true); 
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      delay(100);
      //queueOrder.push("ttm20!Low Power Mode on");
    } else {
      WiFi.setSleep(false);
    }
  }
  else if (String(topic) == S_HOST_NAME + "/sleep")  // Deep Sleep
  {
    Serial.println("Deep Sleep");
    delay(100);
    esp_sleep_enable_timer_wakeup((uint64_t)messageTemp.toInt() * 1000000); // Convert seconds to microseconds
    esp_deep_sleep_start();
  } 
  else if (String(topic) == S_HOST_NAME + "/getLastAct")  // Last Action
  {
    mqttClient.publish(("esp32/LastAction/" + String(S_HOST_NAME)).c_str(), LastAction.c_str());
  } 
  //Serial.println(String(topic) + " - " + messageTemp);
} 

// MQTT reconnect
void mqttReconnect()
{
  // Loop until we're reconnected
  while (!mqttClient.connected())
  {
    MqttConnectErrorCounter++;
    if(MqttConnectErrorCounter >= 20){
      Serial.println("MQTT connection failed 20 times - Reboot");
      ESP.restart();
    }
    // Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect((S_HOST_NAME + "--mp3Player").c_str(), S_MQTT_USER.c_str(), S_MQTT_PASSWORD.c_str()))
    {
      // Serial.println("connected");
      //  Host
      mqttClient.subscribe((S_HOST_NAME + "/mp3").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/reboot").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/setVol").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/incVol").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/resVol").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/stop").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/tts").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ttm").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/delttm").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ping").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/speed").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ls/aac").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ls/mp3").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/lpm").c_str());  // Low Power Mode
      mqttClient.subscribe((S_HOST_NAME + "/sleep").c_str());  // Deep sleep
      mqttClient.subscribe((S_HOST_NAME + "/getLastAct").c_str());  // 

      // House
      mqttClient.subscribe((S_MQTT_HOUSE + "/tts").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/ttm").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/setVol").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/incVol").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/resVol").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/stop").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/mp3").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/speed").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/ls/mp3").c_str());      
      mqttClient.subscribe((S_MQTT_HOUSE + "/ls/aac").c_str());
      // Startup
      delay(100);
      if (bStartup)
      {
        mqttClient.publish((S_MQTT_HOUSE + "/IP").c_str(), (S_HOST_NAME + " - " + WiFi.localIP().toString()).c_str());
        //queueOrder.push("ttm" + S_HOST_NAME + " is online with IP " + WiFi.localIP().toString());
        queueOrder.push("ttm30!" + S_HOST_NAME );

        bStartup = false;
      }
      MqttConnectErrorCounter = 0;
    }
    else
    {
      Serial.println("MQTT connect failed -  try again in 5 seconds");
      //  Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// Setuo  i2s, Wifi, MQTT, FTP
void setup()
{
  Serial.begin(115200); // <--------------- do not comment out
  pinMode(errorLED, OUTPUT);

  //  For development
  //--------------------
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);
  // Statt Info geht; Debug, Info, Warning, Error
  // setup audiokit before SD!
  // setup output
  auto cfg = i2s.defaultConfig(TX_MODE);
  if (!i2s.begin(cfg))
  {
    // Serial.println("i2s failed");
    stop();
  }

  // Set globVar with app.json values
  if (!setGlobalVar())
  {
    // SD Card error
    digitalWrite(errorLED, LOW);
  }
  else
  {
    digitalWrite(errorLED, HIGH);
  }
  i2s.setVolume(S_AUDIO_VOLUME.toFloat());
  
  // setup additional buttons
  i2s.addDefaultActions();
  i2s.addAction(i2s.getKey(3), stopPlaySound);
  i2s.addAction(i2s.getKey(4), playTestSound);
  i2s.addAction(i2s.getKey(5), audioVolumeDown);
  i2s.addAction(i2s.getKey(6), audioVolumeUp);

  // Connecting to a WiFi network
  // Serial.println();
  // Serial.print("Connecting to SSID: ");
  // Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.begin(S_WIFI_SSID.c_str(), S_WIFI_PASSWORD.c_str());

  int wifiConnectCounter = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    wifiConnectCounter++;
    if (wifiConnectCounter > 20)
    {
      playError("ERROR 3  ");
      playError("NETWORK CONNECTION ERROR");
      Serial.print("Network connection failed");
      delay(5000000);
      ESP.restart();
    }
    Serial.print(".");
  }

  // Serial.println("WiFi connected");
  // Serial.println("IP address: ");
  // Serial.println(WiFi.localIP());

   delay(100);

  // Setup MDNS responder
  if (!MDNS.begin(S_HOST_NAME.c_str())) {
    Serial.println("Error setting up MDNS responder!");
    while(1) {
      delay(1000);
    }
  }
  delay(100);

  // Setup MQTT
  mqttClient.setServer(S_MQTT_SERVER.c_str(), S_MQTT_PORT.toInt());
  mqttClient.setCallback(mqttCallback);

  delay(10);

  // Initialize time via NTP
  /*---------set with NTP---------------*/
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)){
    rtc.setTimeStruct(timeinfo); 
  }


  delay(10);

  // Start FTP server with username and password
  ftpServer.begin(S_FTP_SVR_USER.c_str(), S_FTP_SVR_PASSWORD.c_str());
  //
  // Serial.println("FTP server started!");
  //
  delay(10);
  queueOrder.push("mp3" + S_START_SOUND);
  bStartup = true;

  // Start Web Server
  server.begin();

  Serial.println("+-------------------------------------------+");
  Serial.println(" Host-Name  : " + S_HOST_NAME);
  Serial.println(" MQTT House : " + S_MQTT_HOUSE);
  Serial.println(" WiFi SSID  : " + S_WIFI_SSID);
  Serial.println(" FTP  user  : " + S_FTP_SVR_USER);
  Serial.println(" MQTT SRV   : " + S_MQTT_SERVER);
  Serial.println(" Volume     : " + String(i2s.getVolume()) + " Sys: " + String(S_AUDIO_VOLUME) + " Prev: " + String(PreviousVolume) );
  Serial.println(" ");
  Serial.println(" IP         : " + WiFi.localIP().toString());
  Serial.println(" RSSI       : " + String(WiFi.RSSI()) + " dBm");
  Serial.println(" Time       : " + rtc.getTime());
  Serial.println(" Version    : " + String(FIRMWARE_VERSION));
  
  Serial.println(" ");
}

void loop()
{
  // Web UI Handler
  wifiClient = server.available();
  if (wifiClient) {                              // If a new client connects,
    webHandler();                            // handle the web request
  } else {

      // MQTT Reconnect when the connection is lost
      if (!mqttClient.connected())
      {
        mqttReconnect();
      }

      // MQTT
      mqttClient.loop();

      // Execute all actions if the corresponding button/ pin is low
      i2s.processActions();

      // Handle FTP server operations
      ftpServer.handleFTP(); // Continuously process FTP requests

      // Stop Live Strem
      if (!queueOrder.isEmpty() and currentLiveStream.length() > 0 and !bLiveStreamPause)
      {
        // Serial.println("Stop LS");
        decoder.end();
        delay(100);
        url.flush();
        url.end();
        url.clear();
        copier.end();
        bLiveStreamPause = true;
      }

      // Check out queues and work items
      if (!copier.copy()  or copier.copy() == 0)  // Is free to take new items
        {
          if ( true){
          // Start Live Stream
            if (currentLiveStream.length() > 0 and queueOrder.isEmpty())
            {
              // Serial.println("Start LS");
              //  Prefix Volume control
              if (currentLiveStream.substring(2, 3).compareTo("!") == 0)
              {
                if (currentLiveStream.substring(0, 2).toInt() > 3)
                {
                  if (PreviousVolume == 0)
                  {
                    PreviousVolume = i2s.getVolume();
                  }
                  TempVolume = currentLiveStream.substring(0, 2);
                  i2s.setVolume(("0." + TempVolume).toFloat());
                  // Serial.println("New volume -- " + String(i2s.getVolume()));
                }
              }
              // LV-Stream
              Serial.println("LS -- " + currentLiveStream );
              decoder.begin();
              if (!url.begin(currentLiveStream.c_str(), (acceptMime.c_str()))){
            
                Serial.println("LS  -- error " );
              
                currentLiveStream = "";
                mqttClient.publish("esp32/error", (S_HOST_NAME + " - Live stream address no response").c_str());
              } else {
                Serial.println("LS  -- start " );
                copier.begin(decoder, url);
                copier.copy();
                bLiveStreamPause = false;
              }
            }
          }

        // MP3 TTS TTM
        if (!copier.copy())
        {

          if (PreviousVolume > 0)
          {
            i2s.setVolume(PreviousVolume);
            PreviousVolume = 0;
            TempVolume = "";
          }
          if (PreviousLang.length() > 0)
          {
            S_TTS_LANG = PreviousLang;
            PreviousLang = "";
          }

          if (!queueOrder.isEmpty())
          {
            String order = queueOrder.pop();
            // Serial.println ("Loop - " + order);
            String orderTyp = order.substring(0, 3);
            String orderText = order.substring(3);

            LastAction = rtc.getDateTime() + " - " + orderTyp + " - " + orderText;  
            // Prefix Volume control
            if (orderText.substring(2, 3).compareTo("!") == 0)
            {
              if (orderText.substring(0, 2).toInt() > 3)
              {
                if (PreviousVolume == 0)
                {
                  PreviousVolume = i2s.getVolume();
                }
                TempVolume = orderText.substring(0, 2);
                i2s.setVolume(("0." + TempVolume).toFloat());
                // Serial.println("New volume -- " + String(i2s.getVolume()));
              }
              else if (orderText.substring(0, 2) == "en" or orderText.substring(0, 2) == "pl")
              {
                PreviousLang = S_TTS_LANG;
                S_TTS_LANG = orderText.substring(0, 2);
              }
              // Serial.println("Prefix -- " + orderText.substring(0, 2));
              orderText = orderText.substring(3);
            }

            if (orderTyp == "mp3")
            {
              // Serial.println("Loop Sound started -- " + orderText);
              SD_MMC.begin();
              audioFile = SD_MMC.open(orderText);
              if (audioFile)
              {
                decoder.begin();
                copier.begin(decoder, audioFile);
                copier.copy();
              }
              else
              {
                mqttClient.publish("esp32/error", (S_HOST_NAME + " / " + orderText + " - does not exist").c_str());
                // Serial.println("File open failed -- " + orderText );
              }
            }
            else if (orderTyp == "ttm")
            {

              TTM_Worker_Google(orderText);
            }
            else if (orderTyp == "tts")
            {
              // Serial.println("TTS " + orderText);
              const char *url_str = tts(orderText.c_str(), S_TTS_LANG.c_str(), S_TTS_SPEED.c_str());
              // generate mp3 with the help of google tts
              decoder.begin();
              if (!url.begin(url_str, "audio/mp3"))
              {
                mqttClient.publish( "esp32/error", (S_HOST_NAME + " / " + url_str + " - no response").c_str());
              }
              else
              {
                // Serial.println("TTS Url: " + String(url_str));
                copier.begin(decoder, url);
                copier.copyAll();
              }
            }
          }
        }
      }
  }
}