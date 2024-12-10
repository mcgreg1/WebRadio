#include "Arduino.h"
#include <Preferences.h> //write to flash
#include "WiFi.h"
#include "Audio.h"
#include <SimpleRotary.h> 
//#include <WebServer.h>
#include "BluetoothA2DPSink.h" //for Bluetooth mode
#include "Ticker.h" //for minute periodic timer

//OLED
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define SHUTDOWN_TIME 5 //deepsleep after 10 min. 
#define WIFI_TIMEOUT_MS 20000 //try wifi for x miliseconds before give up
#define MAX_AUDIO_VOLUME 40 //pay attention to the maxvolumesteps and Bluetooth scaling factor when changing this!
// Digital I/O used

//Audio Amplifier PINS
#define I2S_DOUT      14
#define I2S_BCLK      27
#define I2S_LRC       26
#define ROT_DT        19
#define ROT_CLK       18
#define ROT_SW        15

//Rotary Encoder PINS
SimpleRotary rotary(ROT_CLK, ROT_DT, ROT_SW);
//OLED PINS: default SDA (21), SCL(22) 
Adafruit_SSD1306 display(128, 32, &Wire, -1);



BluetoothA2DPSink a2dp_sink;
//WebServer server(80); // TODO: Webserver
Preferences prefs; 
Audio audio;

//TODO: credentials via Webserver
String ssid =     "Frieda";//<-- Add your credentials here
String password = "Frieda4all";//<-- Add your credentials here


//structure for station list
typedef struct {
  String url;  //stream url
  String name; //station name
} Station;

#define STATIONS 4
Station stationlist[STATIONS] PROGMEM = {
{"http://streams.egofm.de/egoFM-hq/","EGO.FM"},
{"http://streams.br.de/bayern3_2.m3u","BAYERN3"},
{"http://stream.antenne.de/antenne/stream/mp3","ANTENNE"},
{"http://streams.br.de/br24_2.m3u","BR24"}};


//Values stored in flash
ushort conn_mode=0; // 0 = Webradio, 1 = Bluetooth-Audio
ushort audioVolume=10;
ushort currentStation=0;


String currentSong;
bool wifi_conn_success=false;
unsigned long changeTime; 
unsigned long dataPacketReceivedTime;
bool audioChanged;
bool isPlaying; //for Bluetooth only

Ticker minuteTicker;
Ticker shutdownTicker;
uint8_t shutdownSeconds;

void keepWiFiAlive(void * parameters) {
  for (;;) {
    if (wifi_conn_success) {
      // Nur wenn die manuell gestartete, erste Verbindung geklappt hat, wieder neu verbinden
      // Denn wenn diese schon nicht geklappt hat, hat es auch keinen Wert, nochmal zu versuchen.
      if (WiFi.status() == WL_CONNECTED) {
        //Wifi ist immer noch verbunden
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        continue;
      }
      Serial.println("WiFi-Verbindungsversuch....!");
      WiFi.begin(ssid, password);
      unsigned long startAttemptTime=millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
      }
      
      if (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(300000 / portTICK_PERIOD_MS);
        continue;
      } else {
        continue;
      }
    } else {
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }
  }
}
void shutdownTimer()
{
  if (audioVolume)//interrupt Flag
  {
    Serial.println("Audio increased, shutdown canceled");
    shutdownSeconds=5;
    shutdownTicker.detach();
  }
  else
  {
    if (shutdownSeconds==0)
      esp_deep_sleep_start();
    shutdownSeconds--;
    Serial.printf("Shutdown in %d seconds\n", shutdownSeconds);
    display.clearDisplay();
    display.setCursor(0,0);             // Start at top-left corner
    display.setTextSize(2);
    display.println("Shutdown");
    display.printf("in %d sec.", shutdownSeconds);
    display.display();

  }
}

//OLED Main Menu - adjusted for 128x32 pixel display
void drawMainMenu()
{
  Serial.println("DrawMainMenu");
  if (audioVolume)
  {
    display.clearDisplay();
    display.setCursor(0,0);             // Start at top-left corner
    display.setTextSize(2);
    if (conn_mode==0)
    {
      display.println(stationlist[currentStation].name);
      display.setTextSize(1);
      //split the interpret and songname
      byte delim = currentSong.indexOf('-');
      String interpret = currentSong.substring(0, delim-1);
      String song = currentSong.substring(delim+1);
      
      display.println(interpret);
      display.println(song);
    }
    if (conn_mode==1)
    {
      display.println("Bluetooth");
      display.println("  mode");
    }
  }
  else 
  {
    shutdownTicker.attach(1, shutdownTimer);
  }
    display.display();

}
//draw OLED progressbar for volume visualisation
void drawProgressbar(int x,int y, int width,int height, int progress)
{
  Serial.printf("Progress: %d %%\n", progress);
   progress = progress > 100 ? 100 : progress; // set the progress value to 100
   progress = progress < 0 ? 0 :progress; // start the counting to 0-100
   float bar = ((float)(width-1) / 100) * progress;
   display.drawRect(x, y, width, height, SSD1306_WHITE);
   display.fillRect(x+2, y+2, bar , height-4, SSD1306_WHITE); // initailize the graphics fillRect(int x, int y, int width, int height)
}

void readFromFlash()
{
    String mode[] = {"WiFi", "Bluetooth"};
    conn_mode= prefs.getUShort("ConnMode", 0);
    audioVolume= prefs.getUShort("volume", audioVolume);
    currentStation= prefs.getUShort("station", 0);
    Serial.printf("-----------------------------------------------------\n");
    Serial.printf("From flash: mode: %s, volume: %d/%d, station: %s\n", mode[conn_mode], audioVolume, MAX_AUDIO_VOLUME, stationlist[currentStation].name);
    Serial.printf("-----------------------------------------------------\n");
}

void writeToFlash()
{
    String mode[] = {"WiFi", "Bluetooth"};
    prefs.putUShort("ConnMode", conn_mode);
    if (audioVolume)
      prefs.putUShort("volume", audioVolume);
    prefs.putUShort("station", currentStation);
    Serial.printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    Serial.printf("To flash: mode: %s, volume: %d/%d, station: %s\n", mode[conn_mode], audioVolume, MAX_AUDIO_VOLUME, stationlist[currentStation].name);
    Serial.printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
}

//Callback for the Bluetooth mode, when data is received, Workaround to see if music is streaming or idle
void data_received_callback() {
  //Serial.println("Data packet received");
  if (audioVolume>0)
  {
      dataPacketReceivedTime=millis();
      //drawMainMenu();
  }
}
//Check every minute if we are in idle mode, and go to deepsleep 
void minuteCheck()
{
  int inactiveMinutes = (millis()-dataPacketReceivedTime)/60000;
  Serial.printf("Minute check, uptime %d minutes, idle %d minutes\n", (millis()/60000), inactiveMinutes);

  if (inactiveMinutes)
  {
    if (SHUTDOWN_TIME-inactiveMinutes < 3)
    {
      Serial.printf("Inactive for %d minutes, shutdown in %d min. \n", inactiveMinutes, SHUTDOWN_TIME-inactiveMinutes);
      display.clearDisplay();
      delay(500);
      display.setTextSize(2);
      display.setCursor(0,0);
      display.println("Shutdown");
      display.printf("in %d min.", SHUTDOWN_TIME-inactiveMinutes);
      display.display();
    }
  }
  if (inactiveMinutes>=SHUTDOWN_TIME)
  {
    Serial.println("Going to deepsleep");
    conn_mode=0; //start always in webradio 
    //writeToFlash(); no need to write, all data has been saved on change already
    prefs.end();
    display.clearDisplay();
    display.display();
    delay(500);
    Serial.flush();
    esp_deep_sleep_start();
  }

}

void setup() 
{
  //Rotary Button as wakeup 
 //pinMode(GPIO_NUM_15, INPUT_PULLUP);  // Declaring the pin with the push button as INPUT_PULLUP
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, 0); 

  //Use the PINs below as additional ground and 3.3V for the display
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);

  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  Serial.begin(9600);

  //load preferences
  prefs.begin("webradio");
  readFromFlash();
  //if (audioVolume<5)
  //  audioVolume=10;

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(2);
  display.clearDisplay();
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,0);             // Start at top-left corner
  display.println("ESP Startup");
  display.display();

  if (conn_mode==0)
  {
    display.println("connecting...");
    display.display();
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("connecting..");
    unsigned int wifiStartTime=millis();
    while (WiFi.status() != WL_CONNECTED) 
    {
      delay(1500);
      unsigned int duration= millis()-wifiStartTime;
      if (duration>WIFI_TIMEOUT_MS)
      {
        Serial.printf("Could not establisch Wifi Connection (%s) for %d seconds, going to bluetooth mode", ssid, duration/1000);
        //to to Bluetooth mode
        conn_mode=1;
        writeToFlash();
        ESP.restart();
        break;
      }
      
    }
    display.clearDisplay();
    Serial.println("connected!");
    display.println("connected!");

      // Keep Wifi alive
    xTaskCreatePinnedToCore
    (
      keepWiFiAlive,
      "Keep Wifi Alive",
      5000,
      NULL,
      2,
      NULL,
      0
    );
  }
  if (conn_mode==1)
  {
    Serial.println("Bluetooth Mode");
    i2s_pin_config_t my_pin_config = 
    {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
    };

    a2dp_sink.set_pin_config(my_pin_config);
    a2dp_sink.set_on_data_received(data_received_callback);
    a2dp_sink.start("KG_Radio");
    display.setTextSize(2);   
    if (conn_mode==0)
    {
      display.println("KG_Radio");
    }
    if (conn_mode==1)
    {
      display.println("Bluetooth");
      display.println("Mode");
    }
    display.display();
  }
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolumeSteps(64); // max 255
  audio.setVolume(audioVolume);
  audioChanged=true;
  changeTime=millis();

  if (conn_mode==0)
  {
      audio.connecttohost(stationlist[currentStation].url.c_str());
      Serial.printf(" Current station: %s\n, index=%d\n", stationlist[currentStation].name, currentStation);
  }
  minuteTicker.attach_ms(60000, minuteCheck);
  shutdownSeconds=5; //5 seconds countdown before deepsleep
}

void loop()
{
  //Rotary
  byte btn;
  // 0 = not pushed, 1 = pushed, 2 = long pushed
  btn = rotary.pushType(1000); // number of milliseconds button has to be pushed for it to be considered a long push.
  if ( btn == 1) 
  {
    if (conn_mode==0)
    {

      if (currentStation<STATIONS-1)
        currentStation++;
      else 
        currentStation=0;

      audio.connecttohost(stationlist[currentStation].url.c_str());
      Serial.printf(" Current station: %s\n, index=%d\n", stationlist[currentStation].name, currentStation);
      changeTime=millis();
    }
    if (conn_mode==1);
    {
      if (isPlaying)
        a2dp_sink.play();
      else
        a2dp_sink.pause();
      isPlaying= !isPlaying;
      Serial.printf("Setting is playing to %d\n", isPlaying);
    }
  }

  if ( btn == 2 ) 
  {
    int pushTime= rotary.pushTime();
    Serial.printf("Long Pushed for %d ms", pushTime);
    if (conn_mode==0)
      conn_mode=1;
    else 
      conn_mode=0;
    writeToFlash();
    prefs.end();
    delay(100);
    ESP.restart();
  }
  audio.loop();
  
  byte rot;
  rot = rotary.rotate();
  
  // CW
  if ( rot == 1 )
  {
    if (audioVolume<MAX_AUDIO_VOLUME)
      audio.setVolume(++audioVolume);
  }
  // CCW
  if ( rot == 2 ) 
  {
    if (audioVolume>0)
      audio.setVolume(--audioVolume);
  }
  //Display status on any rotation
  if (rot>0)
  {
      Serial.printf("Audio: %d\n", audioVolume);
      changeTime = millis();
      audioChanged=true;
      a2dp_sink.set_volume(audioVolume*2.5); //bluetooth scaling factor
      int progress = audioVolume*2.5; //convert to %
      display.clearDisplay();
      drawProgressbar(0, 0, 120, 15 ,progress);
      display.setCursor(5, 21);
      display.setTextSize(1);
      display.printf("   Volume: %d %%\n", progress);
      display.display();
  }
  //Keep the audio volume on display for 5 seconds
  int waitTime; //defines how long the audio bar should be shown. 
  if (audioVolume)
    waitTime=3000; //3 seconds
  else
    waitTime=1000;

  if (millis()-changeTime>waitTime && audioChanged)
  {
    Serial.println("Audio has changed");
    drawMainMenu();
    if (audioVolume)
    {
      Serial.printf("Write to flash because of audio change to %d\n", audioVolume);
      if (conn_mode==0)//save audio only in webradio mode
      {
        writeToFlash();
      }
    }
    audioChanged=false;
    
  }
}

// Web radio events
void audio_info(const char *info)
{
    Serial.print("info        "); Serial.println(info);
    if (audioVolume>0)
      dataPacketReceivedTime=millis();
}
void audio_showstreamtitle(const char *info)
{
  currentSong = String(info);
  drawMainMenu();
  Serial.print("streamtitle ");Serial.println(info);
  if (audioVolume>0)
    dataPacketReceivedTime=millis();
}

void audio_showstation(const char *info)
{
  drawMainMenu();
    Serial.print("station     ");Serial.println(info);
}
