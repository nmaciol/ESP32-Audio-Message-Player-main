#ifndef HELPER_H
#define HELPER_H



#include <ArduinoJson.h>
#include <cstdint>
#include <Regexp.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <QueueList.h>
#include <AudioTools.h>




extern JsonDocument docIniFile;

extern String S_HOST_NAME;
extern String S_AUDIO_VOLUME;
extern String S_START_SOUND;

extern String S_WIFI_SSID ;
extern String S_WIFI_PASSWORD ;

extern String S_FTP_SVR_USER;
extern String S_FTP_SVR_PASSWORD;

extern String S_MQTT_SERVER;
extern String S_MQTT_PORT;
extern String S_MQTT_USER;
extern String S_MQTT_PASSWORD;
extern String S_MQTT_HOUSE;

extern String S_TTS_QRY_GOOGLE;
extern String S_TTS_LANG;
extern String S_TTS_SPEED;
extern String S_TTS_MAX_LEN_TTM;




extern void playError(const char *sText);



// Overwrite globVar with AppIni values
bool setGlobalVar(){
  
    Serial.println(" ------  Start set global params ------------");
    // --------------  app.json ------------------
    SD_MMC.begin();
    // App.json file must be in the root directory of the SD Card
    File file = SD_MMC.open("/app.json");
    if (!file) {
      Serial.println("App.json -- Datei nicht gefunden!");
      playError("ERROR 1  ");
      playError("CONFIG FILE NOT EXIST");
      return(false);
    }

    

    DeserializationError error = deserializeJson(docIniFile, file);
    if (error) {
      Serial.print("JSON Fehler: ");
      playError("ERROR 2  ");
      playError("CONFIG FILE ERROR");
      Serial.println(error.c_str());
      return(false);
    }

        if ( docIniFile["main"]["host-name"]) {
          S_HOST_NAME = docIniFile["main"]["host-name"].as<String>();
        } else {
          Serial.print("section 'main' has not an entry 'host-name' with value ");
        }

        if ( docIniFile["main"]["audio-volume"]) {
          S_AUDIO_VOLUME = docIniFile["main"]["audio-volume"].as<String>();
        } else {
          Serial.print("section 'main' has an not entry 'audio-volume' with value ");
        }

        if ( docIniFile["main"]["start-sound"]) {
          S_START_SOUND = docIniFile["main"]["start-sound"].as<String>();
        } else {
          Serial.print("section 'main' has an not entry 'start-sound' with value ");
        }

        if ( docIniFile["wifi"]["ssid"]) {
          S_WIFI_SSID = docIniFile["wifi"]["ssid"].as<String>();
        } else {
          Serial.print("section 'wifi' has an not entry 'ssid' with value ");
        }

        if ( docIniFile["wifi"]["password"]) {
          S_WIFI_PASSWORD = docIniFile["wifi"]["password"].as<String>();
        } else {
          Serial.print("section 'wifi' has an not entry 'password' with value ");
        }

        if ( docIniFile["mqtt"]["server"]) {
          S_MQTT_SERVER = docIniFile["mqtt"]["server"].as<String>();
        } else {
          Serial.print("section 'mqtt' has an not entry 'Server' with value ");
        }

        if ( docIniFile["mqtt"]["port"]) {
          S_MQTT_PORT = docIniFile["mqtt"]["port"].as<String>();
        } else {
          Serial.print("section 'mqtt' has an not entry 'Port' with value ");
        }

        if ( docIniFile["mqtt"]["user"]) {
          S_MQTT_USER = docIniFile["mqtt"]["user"].as<String>();
        } else {    
          Serial.print("section 'mqtt' has an not entry 'user' with value ");
        }

        if ( docIniFile["mqtt"]["password"]) {
          S_MQTT_PASSWORD = docIniFile["mqtt"]["password"].as<String>();
        } else {
          Serial.print("section 'mqtt' has an entry 'password' with value ");
        }

        if ( docIniFile["mqtt"]["house"]) {
          S_MQTT_HOUSE = docIniFile["mqtt"]["house"].as<String>();
        } else {
          Serial.print("section 'mqtt' has an not entry 'House' with value ");
        }

        if ( docIniFile["ftp"]["user"]) {
          S_FTP_SVR_USER = docIniFile["ftp"]["user"].as<String>();
        } else {
          Serial.print("section 'ftp' has an not entry 'user' with value ");
        }

        if ( docIniFile["ftp"]["password"]) {
          S_FTP_SVR_PASSWORD = docIniFile["ftp"]["password"].as<String>();
        } else {
           Serial.print("section 'ftp' has an not entry 'password' with value ");
          Serial.print("section 'ftp' has an entry 'password' with value ");
        }

        if ( docIniFile["tts"]["qry-google"]) {
          S_TTS_QRY_GOOGLE = docIniFile["tts"]["qry-google"].as<String>();
        } else {
          Serial.print("section 'tts' has an not entry 'Qry Google' with value ");
        }

        if ( docIniFile["tts"]["lang"]) {
          S_TTS_LANG = docIniFile["tts"]["lang"].as<String>();
        } else {
          Serial.print("section 'tts' has an not entry 'Lang' with value ");
        }

        if ( docIniFile["tts"]["speed"]) {
          S_TTS_SPEED = docIniFile["tts"]["speed"].as<String>();
        } else {
          Serial.print("section 'tts' has an not entry 'Speed' with value ");
        }

        if ( docIniFile["tts"]["max-len-ttm"]) {
          S_TTS_MAX_LEN_TTM = docIniFile["tts"]["max-len-ttm"].as<String>();
        } else {
          Serial.print("section 'tts' has an not entry 'Max-len-TTM' with value ");
        }
      file.close();
      return(true);
  }



void getTtmFileName(String text,  char* buf) {

  MatchState ms (buf);
  //char buf [240]; 

  strcpy (buf, text.c_str());
  ms.Target (buf);    // recompute length
  // replace vowels with *
  ms.GlobalReplace ("Heizungsraum", "Hzg");    
  ms.GlobalReplace ("Minuten", "M");    
  ms.GlobalReplace ("Waschmaschine", "Wm");    
  ms.GlobalReplace ("[aeiouöüßä .,?:]", "");     
  ms.GlobalReplace ("mm", "m");     
  ms.GlobalReplace ("nn", "n");     
  ms.GlobalReplace ("rr", "r");     
  ms.GlobalReplace ("ff", "f");      
  ms.GlobalReplace ("ll", "l");      

  // show results
  Serial.print ("Converted string: ");
  Serial.println (buf);
}


#endif // HELPER_H