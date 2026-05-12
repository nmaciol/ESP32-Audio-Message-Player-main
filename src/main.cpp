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
#include <RandomUtils.h>

//  Fix SD card access issue in FTP server
//  ----------------------------------------
// !!!!! Overwrite value in  .pio\libdeps\esp32dev\SimpleFTPServer\FtpServerKey.h
/// Line 63 #define DEFAULT_STORAGE_TYPE_ESP32 					STORAGE_SD  with
/// Line 63 #define DEFAULT_STORAGE_TYPE_ESP32 					STORAGE_SD_MMC

#undef DEFAULT_STORAGE_TYPE_ESP32
#define DEFAULT_STORAGE_TYPE_ESP32 STORAGE_SD_MMC
#include <PubSubClient.h>
#include <QueueList.h>
#include <Regexp.h>


/*
  2.0.0 Add Live Stream  code optimization
  2.3.0 Add  MQTT command to set sleep mode and deep sleep time, and publish sleep mode and WiFi RSSI in ping response
  2.4.0 Add RTC time to ping response and mDNS
  2.5.0 Add MQTT command to get last action 
  2.6.0 Add MQTT connect watchdog and reboot after 20 MQTT connection failures
  3.0.0 Replace SD Lib with SD_MMC, IniFile Lib with ArduinoJson, code optimization, bug fixes, and more MQTT commands
  3.1.0 Remove credentials.h and add error handling for missing or wrong entries in app.json
  3.2.0 Add Audio error mesages via TTS Comodore C64 voice for Config file not exist, json error, network connection error
  4.0.0 Add Simple Web Server functionality
  4.1.0 Add Web UI for Action Key 4 with selectable Sound/Audio Pools, and more MQTT commands to trigger Action Keys and select Sound/Audio Pools
  5.0.0 Chore: update espressif32 platform to 7.0.0
  5.0.1 Add MQTT commands to enable/disable for Stop4Press, ActionKeys, WebUI, FTP und MQTT
*/

#define FIRMWARE_VERSION "5.0.1"

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
boolean B_WEBUI_ENABLE = true;;
boolean B_ACTION_KEYS_ENABLE = true;
boolean B_FTP_SERVER_ENABLE = true;
boolean B_LOW_POWER_MODE_ENABLE = false;
boolean B_MQTT_CLIENT_ENABLE = true;
boolean B_STOP2PRESS_ENABLE = false;

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

AudioActions action;

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

// Variable to store the start time of Action Key 4 for cooldown management
time_t prevKey4PressTime;
time_t key4PressTime ;


// JSON document for app.ini file
JsonDocument docIniFile;

// Variable to store the HTTP request
String header;

// Define timeout time in milliseconds (example: 2000ms = 2s)
// This can be used to prevent the server from hanging while waiting for a client request
const long timeoutTime = 2000;



// Helper function to stop playing sound and live streams, and to clear the URL and decoder buffers
void stopPlaySound()
{
  // Serial.println("Stop Play Sound");
  currentLiveStream = "";
  decoder.end();
  url.flush();
  url.end();
  url.clear();
  copier.end();
}

void playKey4Sound()
{

  // If the interval since the last release time is exceeded,
  // > play the next random sound from the sound pool.

  // If the interval is not exceeded and Stop2Press is enabled,
  // > stop the currently playing sound.

    while (!queueOrder.isEmpty())
    {
      String a = queueOrder.pop();
    }

    
    // Serial.println("Sound started--Key");
    if (prevKey4PressTime == 0 || rtc.getEpoch() - prevKey4PressTime > docIniFile["ActionKeys"]["Key4"]["interval"].as<int>())
    {
      // After the cooldown period is over, play the next sound and reset the cooldown timer
      stopPlaySound();
      Serial.println("Button 4 pressed again after cooldown, stopping sound  ");
      String soundPoolName = docIniFile["ActionKeys"]["Key4"]["soundPool"].as<String>();
      JsonArray playlist = docIniFile["ActionKeys"]["Key4"]["soundPools"][soundPoolName].as<JsonArray>();
      int playlistSize = playlist.size();
      
      // Zufallsgenerator mit analogRead auf einem freien Pin initialisieren
      int randomIndex = randomRange(0, playlistSize - 1);

      Serial.println("Naechster Sound: "  + String(randomIndex) + " / " + String(playlistSize));
      const char* nextTrack = playlist[randomIndex].as<const char*>();

      queueOrder.push(nextTrack);
      mqttClient.publish((S_HOST_NAME + "/key4").c_str(), rtc.getTime().c_str(), 24);
      Serial.println ("Play -- " + String(nextTrack));

      prevKey4PressTime = rtc.getEpoch();

    } else if (B_STOP2PRESS_ENABLE) {
    
        // After the cooldown period is not over, stop play  and reset the cooldown timer
        prevKey4PressTime = 0; // Reset the cooldown timer to allow immediate replay on next press

        Serial.println("Button 4 pressed again during cooldown, stopping sound...");
        // If stop2Press is enabled, stop the current sound and reset the cooldown timer
        stopPlaySound();

    }
    

}



void handleWebUI () {
    Serial.println("New Client.");                  // print a message out in the serial port
    String currentLine = "";     
    int timerCounter = 0;                           // make a String to hold incoming data from the client
    while (wifiClient.connected() &&  timerCounter  < timeoutTime ) {  // loop while the client's connected
      timerCounter++;
      delay(1);
      if (wifiClient.available()) {                   // if there's bytes to read from the client,
        char c = wifiClient.read();                   // read a byte, then
        //Serial.write(c);                            // print it out the serial monitor
        header += c;
        if (c == '\n') {                              // if the byte is a newline character
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
            if (header.indexOf("GET /test") >= 0 || header.indexOf("GET /vol") >= 0  || header.indexOf("GET /sound") >= 0  || header.indexOf("GET /key") >= 0 ){
                index += "OK";
                //Serial.println("Start Test-Sound.");
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
                    //Serial.println("Start Radio Stream: " + currentLiveStream);
                  } else {
                    //Serial.println("Stop Radio Stream: ");
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
                }else if (header.indexOf("GET /soundPoolSave") >= 0) { 
                  //Serial.println("Save SoundPool from WebUI");
                  int paramStart = header.indexOf("/soundPoolSave?") + String("/soundPoolSave?").length();
                  int paramEnd = header.indexOf("&", paramStart); 
                  //Serial.println("ParamStart: " + String(paramStart) + " ParamEnd: " + String(paramEnd));
                        
                  String soundPoolName = header.substring(paramStart, paramEnd);        
                  //Serial.println("SoundPool Name: " + soundPoolName);
                  docIniFile["ActionKeys"]["Key4"]["soundPool"] = soundPoolName;
                  // Saving new SoundPool to app.json
                  SD_MMC.remove("/app.json");
                  File file = SD_MMC.open("/app.json", FILE_WRITE);
                  serializeJson(docIniFile, file);
                  file.close();
                }   else if (header.indexOf("GET /key4") >= 0) {
                  playKey4Sound();
                }
            } else {
                index += "<!DOCTYPE html><html>";
                index += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>" + S_HOST_NAME+ "-AMPlayer</title>";
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
                    index += "<td>" + String(WiFi.RSSI()) + " dBm  </td>";
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
                    index += "</tr><tr>";
                      index += "<td style=\"text-align: center;\" colspan=\"4\">";
                      index += "<b>&#12340;&nbsp;Test-sounds</b>";
                      index += "</td>";
                    index += "<tr>";
                      index += "<td><button onclick=\"start('/test1');\" >&#128276;</button></td>";
                      index += "<td><button onclick=\"start('/test2');\" >&#128021;</button></td>";
                      index += "<td><button onclick=\"start('/test3');\" >&#129311;</button></td>";
                      index += "<td><button style=\"font-size: 16px;\" onclick=\"start('/test4');\" >&#128251;<br>/&#9209;</button></td>";
                    index += "</tr>";
                    index += "<tr>";
                      index += "<td><p>Key4<br>SoundPool</p></td>";
                      index += "<td colspan=\"2\"><select style=\"font-size: 22px;\" id=\"SoundPool\" >";

                      String soundPoolName = docIniFile["ActionKeys"]["Key4"]["soundPool"].as<String>();
                      JsonObject soundPools = docIniFile["ActionKeys"]["Key4"]["soundPools"].as<JsonObject>();

                      for (JsonPair kv : soundPools) {
                          String key = kv.key().c_str();
                          if( key == soundPoolName) {
                            index += "  <option value=\"" + key + "\" selected>" + key + "</option>";
                          } else {
                            index += "  <option value=\"" + key + "\">" + key + "</option>";
                          }
                        }

                        index += "</select></td>";
                        index += "<td><button style=\"font-size: 14px; \" onclick=\"start('/soundPoolSave?' + document.getElementById('SoundPool').value + '&');\" >&#128190;</button></td>";
                    index += "</tr>";
                    index += "<tr>";
                      index += "<td><button onclick=\"start('/key4');\" >&#127188;</button></td>";
                      index += "<td></button></td>";
                      index += "<td></button></td>";
                      index += "<td></button></td>";
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
    //Serial.println("Client disconnected.");
    //Serial.println("");
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


//  Button 3
void stopPlaySoundOn(bool, int, void *)
{
  stopPlaySound();
}

// Button 4
// This function will play a random sound from the playlist defined in app.json when Button 4 is pressed. 
// It also implements a cooldown mechanism to prevent multiple triggers within a short time frame.
// When PIR on Key4 is triggered, play a random sound from the playlist defined in app.json and prevent multiple triggers within a short time frame defined by "interval" in app.json



void playKey4SoundOn(bool, int, void *)
{
  
    // Use the RTC epoch time to manage cooldown for Action Key 4
    key4PressTime = rtc.getEpoch();
    return;
}


void playKey4SoundOff(bool, int, void *)
{
    // Serial.println("Sound stopped--Key");
    if ( rtc.getEpoch() - key4PressTime > 10) { // 
      if ( B_WEBUI_ENABLE){
        B_WEBUI_ENABLE = false;
        Serial.println("WebUI disabled by Key4");
        queueOrder.push("ttmen!Web UI deaktiviert");
      } else {
        B_WEBUI_ENABLE = true;  
        queueOrder.push("ttmen!Web UI aktiviert");
        Serial.println("WebUI enabled by Key4");
      }
    } else {
      playKey4Sound();
    }
    return;
}

// Button 5
void audioVolumeDownOn(bool, int, void *)
{
  i2s.incrementVolume(-0.05);
};
// Button 6
void audioVolumeUpOn(bool, int, void *)
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

  int  pos = String(topic).lastIndexOf('/');
  String topicSuffix = String(topic).substring(pos + 1);
  String topicPrefix = String(topic).substring(0, pos);

  Serial.println("Topic Prefix: " + topicPrefix);
  Serial.println("Topic Suffix: " + topicSuffix);
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
  else if (String(topic) == S_HOST_NAME + "/lpm")  //  Power Mode
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
  else if (topicPrefix == S_HOST_NAME + "/enable")  
  {

    if (topicSuffix == "webui" ) {
      B_WEBUI_ENABLE = (messageTemp == "on");
      Serial.println("Web UI Enable set to " + String(B_WEBUI_ENABLE));
    } else if (topicSuffix == "ftp") {
      B_FTP_SERVER_ENABLE = (messageTemp == "on");
      Serial.println("FTP Server Enable set to " + String(B_FTP_SERVER_ENABLE));
    } else if (topicSuffix == "mqtt") {
      B_MQTT_CLIENT_ENABLE = (messageTemp == "on");
      Serial.println("MQTT Client Enable set to " + String(B_MQTT_CLIENT_ENABLE));
    } else if (topicSuffix == "akey") {
      B_ACTION_KEYS_ENABLE = (messageTemp == "on");
      Serial.println("Action Keys Enable set to " + String(B_ACTION_KEYS_ENABLE));
    } else if (topicSuffix == "stop2press") {
      B_STOP2PRESS_ENABLE = (messageTemp == "on");
      Serial.println("Stop 2 Press Enable set to " + String(B_STOP2PRESS_ENABLE));
    }
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
      mqttClient.subscribe((S_HOST_NAME + "/enable/webui").c_str());  // 
      mqttClient.subscribe((S_HOST_NAME + "/enable/ftp").c_str());  // 
      mqttClient.subscribe((S_HOST_NAME + "/enable/mqtt").c_str());  // 
      mqttClient.subscribe((S_HOST_NAME + "/enable/akey").c_str());  // 
      mqttClient.subscribe((S_HOST_NAME + "/enable/stop2press").c_str());  // 

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

  // Initialize random seed with esp_random() for better randomness in sound selection
  randomInit();
  
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

  // Blocking Key4 call to prevent multiple triggers within a short time frame when the device is started and the button is pressed.
  prevKey4PressTime = rtc.getEpoch();

  // setup additional buttons
  action.add(i2s.getKey(3), stopPlaySoundOn);
  action.add(i2s.getKey(4), playKey4SoundOn, playKey4SoundOff  );
  action.add(i2s.getKey(5), audioVolumeDownOn);
  action.add(i2s.getKey(6), audioVolumeUpOn);

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
  // Web UI Handler - check if clients are available and handle requests
  if(B_WEBUI_ENABLE)  {
    wifiClient = server.available();
    if(wifiClient) {
      handleWebUI();   // handle the web request                        
      return;          // return to avoid executing the rest of the loop while handling the web request
    }
  } 
  
  
  if(true){
      if(B_MQTT_CLIENT_ENABLE){
          // MQTT Reconnect when the connection is lost
          if (!mqttClient.connected())
          {
            mqttReconnect();
          }
          // MQTT
          mqttClient.loop();
      }

      // Execute all actions if the corresponding button/ pin is low
      if (B_ACTION_KEYS_ENABLE) {
        action.processActions();
      }

      // Handle FTP server operations
      if(B_FTP_SERVER_ENABLE){
           ftpServer.handleFTP(); // Continuously process FTP requests
      }

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
                Serial.println("File open failed -- " + orderText );
                LastAction = rtc.getDateTime() + " - " + "not exist" + " - " + orderText;
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
            } else {
                Serial.println("Unknown order type -- " + orderTyp);
                mqttClient.publish( "esp32/error", (S_HOST_NAME + " / " + orderTyp + " - unknown order type").c_str()); 
                LastAction = rtc.getDateTime() + " - " + "unknown" + " - " + orderText; 

            }

              if(B_LOW_POWER_MODE_ENABLE) {
  
                esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                Serial.println("Set sleep mode after action -- " + String(S_HOST_NAME));
              }
          }
        }
      }
  }
}