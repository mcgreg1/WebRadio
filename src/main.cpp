#include "Arduino.h"
#include <Preferences.h> //write to flash
#include "WiFi.h"
#include "Audio.h"
#include <SimpleRotary.h>
//#include <WebServer.h>
#include "BluetoothA2DPSink.h"
#include "Ticker.h"

#define SHUTDOWN_TIME 10 //deepsleep after 10 min. 

BluetoothA2DPSink a2dp_sink;
//WebServer server(80); // Server der App
Preferences prefs; 
//store in flash: 
//conn_mode, volume, sender, active
ushort conn_mode=0; // 0 = Webradio, 1 = Bluetooth-Audio
ushort audioVolume=10;
ushort currentStation=0;



//OLED
#include <Wire.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display(128, 32, &Wire, -1);

// Digital I/O used

#define I2S_DOUT      14
#define I2S_BCLK      27
#define I2S_LRC       26


Audio audio;

String ssid =     "Pandora2";
String password = "LetMeIn#123";
#define WIFI_TIMEOUT_MS 20000 // So lange wird versucht, neu mit Wifi zu verbinden, wenn die Verbindung mal verloren geht (in keepWiFiAlive())



SimpleRotary rotary(18,19,15);


//structure for station list
typedef struct {
  String url;  //stream url
  String name; //stations name
} Station;

#define STATIONS 4
Station stationlist[STATIONS] PROGMEM = {
{"http://streams.egofm.de/egoFM-hq/","EGO.FM"},
{"http://streams.br.de/bayern3_2.m3u","BAYERN3"},
{"http://stream.antenne.de/antenne/stream/mp3","ANTENNE"},
{"http://streams.br.de/br24_2.m3u","BR24"}};

String currentSong;
bool wifi_conn_success=false;
unsigned long changeTime; 
unsigned long dataPacketReceivedTime;
bool audioChanged;
bool isPlaying; //for Bluetooth only

Ticker minuteTicker;



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

void drawMainMenu()
{
  //if (audioVolume)
  {
    display.clearDisplay();
    display.setCursor(0,0);             // Start at top-left corner
    display.setTextSize(2);
    if (conn_mode==0)
    {
      display.println(stationlist[currentStation].name);
      //display.setCursor(0,80);
      //display.println("22:55");
      //display.setCursor(16,0);
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


    display.display();
  }
}

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
    conn_mode= prefs.getUShort("ConnMode", 0);
    audioVolume= prefs.getUShort("volume", audioVolume);
    currentStation= prefs.getUShort("station", 0);
    Serial.printf("--------------------------------------------------\nFrom flash: mode: %d, volume: %d, station: %d\n", conn_mode, audioVolume, currentStation);
}

void writeToFlash()
{
    prefs.putUShort("ConnMode", conn_mode);
    prefs.putUShort("volume", audioVolume);
    prefs.putUShort("station", currentStation);
    Serial.printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\nTo flash: mode: %d, volume: %d, station: %d\n", conn_mode, audioVolume, currentStation);
}

void data_received_callback() {
  //Serial.println("Data packet received");

  if (audioVolume>0)
      dataPacketReceivedTime=millis();
  
}

void minuteCheck()
{

   Serial.printf("Minute check, uptime %d minutes\n", (millis()/60000));

  int inactiveMinutes = (millis()-dataPacketReceivedTime)/60000;
  if (inactiveMinutes)
  {
    Serial.printf("Inactive for %d minutes, shutdown in %d min. \n", inactiveMinutes, SHUTDOWN_TIME-inactiveMinutes);
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,0);
    display.println("Shutdown in");
    display.printf("%d minutes", SHUTDOWN_TIME-inactiveMinutes);
    display.display();
  }
  if (inactiveMinutes>SHUTDOWN_TIME)
  {
    Serial.println("Going to deepsleep");
    conn_mode=0; //start always in webradio 
    writeToFlash();
    prefs.end();
    display.clearDisplay();
    display.display();
    delay(300);
    Serial.flush();
    esp_deep_sleep_start();
  }

}



void setup() {

  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, 0); //Rotary Button as wakeup 


  Serial.begin(9600);
  //load preferences
  prefs.begin("webradio");
  readFromFlash();
  //conn_mode=0; //start always in web radio mode

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setCursor(0,0);             // Start at top-left corner
  display.println("ESP Startup");
  display.display();
  //display.drawString(0, 16, "Hello world");

  display.display();


  if (conn_mode==0)
  {
    display.println("connecting...");
    display.display();
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("connecting..");
    while (WiFi.status() != WL_CONNECTED) delay(1500);
    display.clearDisplay();
    Serial.println("connected!");
    display.println("connected!");


      // WiFi-Verbindung aufrechterhalten
    xTaskCreatePinnedToCore(
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
    /*
#define I2S_DOUT      14 //25??
#define I2S_BCLK      27
#define I2S_LRC       26
*/

    Serial.println("Bluetooth Mode");
    i2s_pin_config_t my_pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
    };

    a2dp_sink.set_pin_config(my_pin_config);
    a2dp_sink.set_on_data_received(data_received_callback);
    a2dp_sink.start("KuechenRadio");
    display.setTextSize(2);   
    if (conn_mode==0)
    {
      display.println("Webradio");
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

  if (conn_mode==0)
  {
      audio.connecttohost(stationlist[currentStation].url.c_str());
      Serial.printf(" Current station: %s\n, index=%d\n", stationlist[currentStation].name, currentStation);
  }



  //delay(1000);

  minuteTicker.attach_ms(60000, minuteCheck);

}

void loop(){

  //Rotary
  byte btn;
  // 0 = not pushed, 1 = pushed, 2 = long pushed

  //button only in wifi mode fdse
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
    Serial.println("Long Pushed");
    if (conn_mode==0)
      conn_mode=1;
    else 
      conn_mode=0;
    writeToFlash();
    //delay(100);
    ESP.restart();
    /*
    if (isPlaying)
    {
      Serial.println("Is Playing, shutdown");
      audio.connecttohost(NULL);
    }
    else
    {
      Serial.println("Wakeup");
    }
    */
  }
  audio.loop();
  

  byte rot;
  rot = rotary.rotate();
  
  // CW
  if ( rot == 1 )
  {
    if (audioVolume<40)
    {
      audio.setVolume(++audioVolume);
      
    }
      
    Serial.printf("Audio: %d\n", audioVolume);
  }
  // CCW
  if ( rot == 2 ) 
  {
    if (audioVolume>0)
    {
      audio.setVolume(--audioVolume);
    }
      
    Serial.printf("Audio: %d\n", audioVolume);

  }

  if (rot>0)
  {
      display.display();
      changeTime = millis();
      audioChanged=true;
      a2dp_sink.set_volume(audioVolume*2.5); //bluetooth scaling factor
      //void drawProgressbar(int x,int y, int width,int height, int progress)
      int progress = audioVolume*2.5;
      if (conn_mode==1)
        progress = progress*2.5;
      display.clearDisplay();
      drawProgressbar(0, 0, 120, 15 ,progress);
      display.setCursor(5, 21);
      display.setTextSize(1);
      display.printf("   Volume: %d %%\n", progress);
      display.display();

  }

  if (millis()-changeTime>5*1000)
  {
    drawMainMenu();
    
  }
  //save audio after 10 seconds
  if ((millis()-changeTime)>10*1000 && audioChanged)
  {
    Serial.printf("Write to flash because of audio change to %d\n", audioVolume);
    if (conn_mode==0)//save audio only in webradio mode
    {
      writeToFlash();
      
    }
    audioChanged=false;

  }


   
    //vTaskDelay(1);
}

// optional
void audio_info(const char *info){
    Serial.print("info        "); Serial.println(info);
    if (audioVolume>0)
      dataPacketReceivedTime=millis();
}
void audio_showstreamtitle(const char *info){
  currentSong = String(info);
  drawMainMenu();
  Serial.print("streamtitle ");Serial.println(info);
  if (audioVolume>0)
    dataPacketReceivedTime=millis();
}
void audio_id3data(const char *info){  //id3 metadata
    Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info){  //end of file
    Serial.print("eof_mp3     ");Serial.println(info);
}
void audio_showstation(const char *info){
  drawMainMenu();
    Serial.print("station     ");Serial.println(info);


}

void audio_bitrate(const char *info){
    Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
    Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
    Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info){
    Serial.print("eof_speech  ");Serial.println(info);
}



