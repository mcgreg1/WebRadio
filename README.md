# Simple webradio with bluetooth

![image](https://github.com/user-attachments/assets/48263a5e-00ec-4a49-b2bf-0f535401e218)
![Bildschirmfoto vom 2024-12-03 10-18-46](https://github.com/user-attachments/assets/12c998a2-23ef-4c59-8eae-7b665a20369e)



  
  
  ## Hardware: rotary encoder with button, audio amplifier (max98357a), ESP32 and speaker (4 or 8 Watt, e.g. Visaton FR7)

  ## What is working:
  1. Wifi for Webradio (Hardcoded Stream URLs)
  2. Bluetooth (Long Press switches between Wifi and Bluetooth)
  3. Volume control (Rotary Encoder)
  4. Auto DeepSleep when no activity (Volume 0 or no stream)
  5. Save preferences (Bluetooth or Wifi, Volume, Radiostation)

  ## TODO: 
  1. Wifi credentials are hardcoded
  2. Webserver with it's beauty: Add remove stream URLs, etc. 
  3. Wifi Credentials via WPS
  4. Add menu on lcd (e.g. conn_mode 2)
   
