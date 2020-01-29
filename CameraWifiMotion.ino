/*******************************************************************************************************************
 *
 *             ESP32Camera with motion detection and web server -  using Arduino IDE 
 *             
 *             Serves a web page whilst detecting motion on a camera (uses ESP32Cam module)
 *             
 *             Included files: gmail-esp32.h, standard.h and wifi.h, motion.h
 *             Bult using Arduino IDE 1.8.10, esp32 boards v1.0.4
 *             
 *             Note: The flash can not be used if using an SD Card as they both use pin 4
 *             
 *             IMPORTANT! - If you are getting weird problems (motion detection retriggering all the time, slow wifi
 *                          response times.....chances are there is a problem with the power to the board.
 *                          It needs a good 500ma supply and probably a good smoothing capacitor.
 *                          This catches me out time and time again ;-)
 *             
 *      First time the ESP starts it will create an access point "ESPConfig" which you need to connect to in order to enter your wifi details.  
 *             default password = "12345678"   (note-it may not work if anything other than 8 characters long for some reason?)
 *             see: https://randomnerdtutorials.com/wifimanager-with-esp8266-autoconnect-custom-parameter-and-manage-your-ssid-and-password
 *
 *      Motion detection based on: https://eloquentarduino.github.io/2020/01/motion-detection-with-esp32-cam-only-arduino-version/
 *      
 *      camera troubleshooting: https://randomnerdtutorials.com/esp32-cam-troubleshooting-guide/
 *                  
 *                                                                                              www.alanesq.eu5.net
 *      
 ********************************************************************************************************************/



// ---------------------------------------------------------------
//                          -SETTINGS
// ---------------------------------------------------------------


  const String stitle = "CameraWifiMotion";              // title of this sketch

  const String sversion = "29Jan20";                     // version of this sketch
  
  const String HomeLink = "/";                           // Where home button on web pages links to (usually "/")

  const int datarefresh = 4000;                          // Refresh rate of the updating data on web page (1000 = 1 second)

  const int LogNumber = 40;                              // number of entries to store in the system log

  const int ServerPort = 80;                             // ip port to serve web pages on

  const int led = 4;                                     // illumination LED pin

  const int SystemCheckRate = 5000;                      // how often to do routine system checks (milliseconds)
  
  const boolean ledON = HIGH;                            // Status LED control 
  const boolean ledOFF = LOW;
  
  const int FrameRefreshTime = 200;                      // how often to check camera for motion (milliseconds)
  
  int TriggerLimitTime = 2;                              // min time between motion detection triggers (seconds)

  int EmailLimitTime = 60;                               // min time between email sends (seconds)

  bool UseFlash = 1;                                     // use flash when taking a picture
  

// ---------------------------------------------------------------


#include "soc/soc.h"                    // Disable brownout problems
#include "soc/rtc_cntl_reg.h"           // Disable brownout problems

// spiffs used to store images and settings
  #include <SPIFFS.h>
  #include <FS.h>                       // gives file access on spiffs
  int SpiffsFileCounter = 0;            // counter of last image stored
  int MaxSpiffsImages = 10;              // number of images to store

// sd card - see https://randomnerdtutorials.com/esp32-cam-take-photo-save-microsd-card/
  #include "SD_MMC.h"
  // #include <SPI.h>                   // (already loaded)
  // #include <FS.h>                    // gives file access on spiffs (already loaded)
  #define SD_CS 5                       // sd chip select pin
  #define FLASH_PIN 4                   // shared pin (flash and sd card)
  bool SD_Present;                      // flag if sd card is found (0 = no)

#include "wifi.h"                       // Load the Wifi / NTP stuff

#include "standard.h"                   // Standard procedures

#include "gmail_esp32.h"                // send email via smtp

#include "motion.h"                     // motion detection / camera

// misc
  unsigned long CAMERAtimer = millis();      // used for timing camera motion refresh timing
  unsigned long TRIGGERtimer = millis();     // used for limiting camera motion trigger rate
  unsigned long EMAILtimer = millis();       // used for limiting rate emails can be sent
  byte DetectionEnabled = 1;                 // flag if capturing motion is enabled (0=stopped, 1=enabled, 2=paused)
  String TriggerTime = "Not yet triggered";  // Time of last motion trigger
  unsigned long MaintTiming = millis();      // used for timing maintenance tasks
  bool emailWhenTriggered = 0;               // flag if to send emails when motion detection triggers
  
  
  
// ---------------------------------------------------------------
//    -SETUP     SETUP     SETUP     SETUP     SETUP     SETUP
// ---------------------------------------------------------------
//
// setup section (runs once at startup)

void setup(void) {
    
  Serial.begin(115200);
  // Serial.setTimeout(2000);
  // while(!Serial) { }        // Wait for serial to initialize.

  Serial.println(F("\n\n\n---------------------------------------"));
  Serial.println("Starting - " + stitle + " - " + sversion);
  Serial.println(F("---------------------------------------"));
  Serial.println( "ESP type: " + ESPType );
  
  // Serial.setDebugOutput(true);                                // enable extra diagnostic info  
   
  // configure the onboard illumination LED
    pinMode(led, OUTPUT); 
    digitalWrite(led, ledOFF);                                    // led on until wifi has connected

  startWifiManager();                                            // Connect to wifi (procedure is in wifi.h)
  
  WiFi.mode(WIFI_STA);     // turn off access point - options are WIFI_AP, WIFI_STA, WIFI_AP_STA or WIFI_OFF

  // set up web page request handling
    server.on(HomeLink, handleRoot);         // root page
    server.on("/data", handleData);          // This displays information which updates every few seconds (used by root web page)
    server.on("/ping", handlePing);          // ping requested
    server.on("/log", handleLogpage);        // system log
    server.on("/test", handleTest);          // testing page
    server.on("/reboot", handleReboot);      // reboot the esp
    server.on("/default", handleDefault);    // All settings to defaults
    server.on("/live", handleLive);          // capture and display live image
    server.on("/images", handleImages);      // display images
    server.on("/img", handleImg);            // latest captured image
    server.onNotFound(handleNotFound);       // invalid page requested
    
  // start web server
    Serial.println(F("Starting web server"));
    server.begin();

  // set up camera
    Serial.print(F("Initialising camera: "));
    Serial.println(setup_camera() ? "OK" : "ERR INIT");

  // Spiffs - see: https://circuits4you.com/2018/01/31/example-of-esp8266-flash-file-system-spiffs/
    if (!SPIFFS.begin(true)) {
      Serial.println("An Error has occurred while mounting SPIFFS");
      delay(5000);
      ESP.restart();
      delay(5000);
    } else {
      Serial.print("SPIFFS mounted successfully. ");
      Serial.print("total bytes: " + String(SPIFFS.totalBytes()));
      Serial.println(", used bytes: " + String(SPIFFS.usedBytes()));
      LoadSettingsSpiffs();     // Load settings from text file in Spiffs
    }

  // start sd card
      if(!SD_MMC.begin()){            // if loading sd card fails     ("/sdcard", true = 1 wire?)
          log_system_message("SD Card not found"); 
          pinMode(FLASH_PIN, OUTPUT);        // sd card failed so enable onboard flash LED
          SD_Present = 0;      // flag no working sd card found
      } else {
      uint8_t cardType = SD_MMC.cardType();
        if(cardType == CARD_NONE){           // if no sd card found
            log_system_message("SD Card type detect failed"); 
            pinMode(FLASH_PIN, OUTPUT);        // sd card failed so enable onboard flash LED
            SD_Present = 0;      // flag no working sd card found
        } else {
            log_system_message("SD Card found"); 
            SD_Present = 1;      // flag sd card found
        }
      }

  // Turn-off the 'brownout detector'
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Finished connecting to network
    for (int i = 0; i < 3; i++) {             // flash led
      digitalWrite(led, ledON);
      delay(100);
      digitalWrite(led, ledOFF);
      delay(200);
    }
    log_system_message(stitle + " Started");   
    TRIGGERtimer = millis();                            // reset retrigger timer to stop instant movement trigger
    
}


// ----------------------------------------------------------------
//   -LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP
// ----------------------------------------------------------------

void loop(void){

  unsigned long currentMillis = millis();        // get current time
  
  server.handleClient();                         // service any web page requests (may not be needed for esp32?)

  // camera motion detection 
  //        explanation of timing here: https://www.baldengineer.com/arduino-millis-plus-addition-does-not-add-up.html
  if (DetectionEnabled == 1) {    
    if ((unsigned long)(currentMillis - CAMERAtimer) >= FrameRefreshTime ) {                // limit camera motion detection rate
        CAMERAtimer = currentMillis;                                                        // reset timer
        if (!capture_still()) {                                                             // capture an image from camera
          log_system_message("Camera failed to capture image - resetting camera"); 
          // turn camera off then back on      
            digitalWrite(PWDN_GPIO_NUM, HIGH);
            delay(500);
            digitalWrite(PWDN_GPIO_NUM, LOW); 
            delay(100);
            RestartCamera(FRAME_SIZE_MOTION, PIXFORMAT_GRAYSCALE);    
         }
         float changes = motion_detect();                                                // find amount of change in current video frame
         if (changes >= (float)(image_threshold / 100.0)) {                              // if motion detected 
             if ((unsigned long)(currentMillis - TRIGGERtimer) >= (TriggerLimitTime * 1000) ) {   // limit time between detection triggers
                  TRIGGERtimer = currentMillis;                                           // reset last motion trigger time
                  MotionDetected(changes);                                                // run motion detected procedure (passing change level)
             } 
         }
         update_frame();                                                                 // Copy current frame to previous
    }
  }

  // periodically check Wifi is connected and refresh NTP time
    if ((unsigned long)(currentMillis - MaintTiming) >= SystemCheckRate ) {   
      WIFIcheck();                                 // check if wifi connection is ok
      MaintTiming = millis();                      // reset timer
      time_t t=now();                              // read current time to ensure NTP auto refresh keeps triggering (otherwise only triggers when time is required causing a delay in response)
      Serial.flush();                              // Make sure any serial data backlog is cleared
    }

  delay(20);

} 


// ----------------------------------------------------------------
//              Load settings from text file in Spiffs
// ----------------------------------------------------------------

void LoadSettingsSpiffs() {
    String TFileName = "/settings.txt";
    if (!SPIFFS.exists(TFileName)) {
      log_system_message("Settings file not found on Spiffs");
      return;
    }
    File file = SPIFFS.open(TFileName, "r");
    if (!file) {
      log_system_message("Unable to open settings file from Spiffs");
      return;
    } 

    // read contents of file
    String line;
    int tnum;

    // line 1 - emailWhenTriggered
      line = file.readStringUntil('\n');
      tnum = line.toInt();
      if (tnum == 0) emailWhenTriggered = 0;
      else if (tnum == 1) emailWhenTriggered = 1;
      else {
        log_system_message("Invalid emailWhenTriggered in settings: " + line);
        return;
      }
      
    // line 2 - block_threshold
      line = file.readStringUntil('\n');
      tnum = line.toInt();
      if (tnum < 1 || tnum > 100) {
        log_system_message("invalid block_threshold in settings");
        return;
      }
      block_threshold = tnum;
      
    // line 3 - image_threshold
      line = file.readStringUntil('\n');
      tnum = line.toInt();
      if (tnum < 1 || tnum > 100) {
        log_system_message("invalid image_threshold in settings");
        return;
      }
      image_threshold = tnum;

    // line 4 - TriggerLimitTime
      line = file.readStringUntil('\n');
      tnum = line.toInt();
      if (tnum < 1 || tnum > 3600) {
        log_system_message("invalid TriggerLimitTime in settings");
        return;
      }
      TriggerLimitTime = tnum;

    // line 5 - DetectionEnabled
      line = file.readStringUntil('\n');
      tnum = line.toInt();
      if (tnum == 0) DetectionEnabled = 0;
      else if (tnum == 1) DetectionEnabled = 1;
      else {
        log_system_message("Invalid DetectionEnabled in settings: " + line);
        return;
      }

    // line 6 - EmailLimitTime
      line = file.readStringUntil('\n');
      tnum = line.toInt();
      if (tnum < 60 || tnum > 10000) {
        log_system_message("invalid EmailLimitTime in settings");
        return;
      }
      EmailLimitTime = tnum;

    // line 7 - UseFlash
      line = file.readStringUntil('\n');
      tnum = line.toInt();
      if (tnum == 0) UseFlash = 0;
      else if (tnum == 1) UseFlash = 1;
      else {
        log_system_message("Invalid UseFlash in settings: " + line);
        return;
      }
      
      file.close();
      log_system_message("Settings loaded from Spiffs");
      
}


// ----------------------------------------------------------------
//              Save settings to text file in Spiffs
// ----------------------------------------------------------------

void SaveSettingsSpiffs() {
    String TFileName = "/settings.txt";
    SPIFFS.remove(TFileName);   // delete old file if present
    File file = SPIFFS.open(TFileName, "w");
    if (!file) {
      log_system_message("Unable to open settings file in Spiffs");
      return;
    } 

    // save settings in to file
    file.println(String(emailWhenTriggered));
    file.println(String(block_threshold));
    file.println(String(image_threshold));
    file.println(String(TriggerLimitTime));
    file.println(String(DetectionEnabled));
    file.println(String(EmailLimitTime));
    file.println(String(UseFlash));
      
    file.close();
}


// ----------------------------------------------------------------
//                reset back to default settings
// ----------------------------------------------------------------

void handleDefault() {

    // default settings
      emailWhenTriggered = 0;
      block_threshold = 15;
      image_threshold= 20;
      TriggerLimitTime = 3;
      DetectionEnabled = 1;
      EmailLimitTime = 60;
      UseFlash = 1;

    SaveSettingsSpiffs();     // save settings in Spiffs

    log_system_message("Defauls web page request");      
    String message = "reset to default";

    server.send(404, "text/plain", message);   // send reply as plain text
    message = "";      // clear variable
      
}

// ----------------------------------------------------------------
//       -root web page requested    i.e. http://x.x.x.x/
// ----------------------------------------------------------------

void handleRoot() {

  log_system_message("root webpage requested");     


  // action any buttons presses etc.

    // email was clicked -  if an email is sent when triggered 
      if (server.hasArg("email")) {
        if (!emailWhenTriggered) {
              log_system_message("Email when motion detected enabled"); 
              emailWhenTriggered = 1;
        } else {
          log_system_message("Email when motion detected disabled"); 
          emailWhenTriggered = 0;
        }
        SaveSettingsSpiffs();     // save settings in Spiffs
      }
      
    // if wipeS was entered  - clear all stored images in Spiffs
      if (server.hasArg("wipeS")) {
        log_system_message("Clearing all stored images (Spiffs)"); 
        SPIFFS.format();
        SpiffsFileCounter = 0;
        TriggerTime = "Not since images wiped";
        SaveSettingsSpiffs();     // save settings in Spiffs
      }

//    // if wipeSD was entered  - clear all stored images on SD Card
//      if (server.hasArg("wipeSD")) {
//        log_system_message("Clearing all stored images (SD Card)"); 
//        fs::FS &fs = SD_MMC;
//        fs.format();     // not a valid command
//      }
      
    // if blockt was entered - block_threshold
      if (server.hasArg("blockt")) {
        String Tvalue = server.arg("blockt");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 100 && val != block_threshold) { 
          log_system_message("block_threshold changed to " + Tvalue ); 
          block_threshold = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
    // if imaget was entered - image_threshold
      if (server.hasArg("imaget")) {
        String Tvalue = server.arg("imaget");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 100 && val != image_threshold) { 
          log_system_message("image_threshold changed to " + Tvalue ); 
          image_threshold = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
    // if emailtime was entered - min time between email sends
      if (server.hasArg("emailtime")) {
        String Tvalue = server.arg("emailtime");   // read value
        int val = Tvalue.toInt();
        if (val > 59 && val < 10000 && val != EmailLimitTime) { 
          log_system_message("EmailLimitTime changed to " + Tvalue + " seconds"); 
          EmailLimitTime = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
      // if triggertime was entered - min time between triggers
      if (server.hasArg("triggertime")) {
        String Tvalue = server.arg("triggertime");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 3600 && val != TriggerLimitTime) { 
          log_system_message("Triggertime changed to " + Tvalue + " seconds"); 
          TriggerLimitTime = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
  
    // if button "toggle illuminator LED" was pressed  
      if (server.hasArg("illuminator")) {
        // button was pressed 
          TRIGGERtimer = millis();                            // reset retrigger timer to stop instant movement trigger
          if (digitalRead(led) == ledOFF) {
            digitalWrite(led, ledON);  
            log_system_message("Illuminator LED turned on");    
          } else {
            digitalWrite(led, ledOFF);  
            log_system_message("Illuminator LED turned off"); 
          }
          SaveSettingsSpiffs();     // save settings in Spiffs
      }

    // if button "flash" was pressed  - toggle flash enabled
      if (server.hasArg("flash")) {
        // button was pressed 
        if (UseFlash == 0) {
            UseFlash = 1;
            log_system_message("Flash enabled");    
          } else {
            UseFlash = 0;
            log_system_message("Flash disabled");    
          }
          SaveSettingsSpiffs();     // save settings in Spiffs
      }
      
    // if button "toggle movement detection" was pressed  
      if (server.hasArg("detection")) {
        // button was pressed 
          TRIGGERtimer = millis();                            // reset retrigger timer to stop instant movement trigger
          if (DetectionEnabled == 0) {
            DetectionEnabled = 1;
            log_system_message("Movement detection enabled");    
          } else {
            DetectionEnabled = 0;
            log_system_message("Movement detection disabled");    
          }
          SaveSettingsSpiffs();     // save settings in Spiffs
      }


  // build the HTML code 
  
    String message = webheader(0);                                      // add the standard html header
    message += "<FORM action='/' method='post'>\n";                     // used by the buttons (action = the page send it to)
    message += "<P>";                                                   // start of section
    

    // insert an iframe containing the changing data (updates every few seconds using java script)
       message += "<BR><iframe id='dataframe' height=150; width=600; frameborder='0'; src='/data'></iframe>\n"
      "<script type='text/javascript'>\n"
         "window.setInterval(function() {document.getElementById('dataframe').src='/data';}, " + String(datarefresh) + ");\n"
      "</script>\n"; 

    // minimum seconds between triggers
      message += "<BR>Minimum time between triggers:";
      message += "<input type='number' style='width: 60px' name='triggertime' min='1' max='3600' value='" + String(TriggerLimitTime) + "'>seconds \n";

    // minimum seconds between email sends
      message += "<BR>Minimum time between E-mails:";
      message += "<input type='number' style='width: 60px' name='emailtime' min='60' max='10000' value='" + String(EmailLimitTime) + "'>seconds \n";

    // detection parameters
      message += "<BR>Detection thresholds: ";
      message += "Block<input type='number' style='width: 35px' name='blockt' title='Variation in a block to count as changed'min='1' max='99' value='" + String(block_threshold) + "'>%, \n";
      message += "Image<input type='number' style='width: 35px' name='imaget' title='Percentage changed blocks in image to count as motion detected' min='1' max='99' value='" + String(image_threshold) + "'>% \n"; 

    // input submit button  
      message += "<BR><input type='submit'><BR><BR>\n";

    // Toggle illuminator LED button
      if (!SD_Present) message += "<input style='height: 30px;' name='illuminator' title='Toggle the Illumination LED On/Off' value='Light' type='submit'> \n";

    // Toggle 'use flash' button
      if (!SD_Present) message += "<input style='height: 30px;' name='flash' title='Toggle use of flash when capturing image On/Off' value='Flash' type='submit'> \n";

    // Toggle movement detection
      message += "<input style='height: 30px;' name='detection' title='Movement detection enable/disable' value='Detection' type='submit'> \n";

    // Toggle email when movement detection
      message += "<input style='height: 30px;' name='email' value='Email' title='Send email when motion detected enable/disable' type='submit'> \n";

    // Clear images in spiffs
      message += "<input style='height: 30px;' name='wipeS' value='Wipe Store' title='Delete all images stored in Spiffs' type='submit'> \n";
    
//    // Clear images on SD Card
//      if (SD_Present) message += "<input style='height: 30px;' name='wipeSD' value='Wipe SDCard' title='Delete all images on SD Card' type='submit'> \n";

    message += "</span></P>\n";    // end of section    
    message += webfooter();        // add the standard footer

    server.send(200, "text/html", message);      // send the web page
    message = "";      // clear variable

}

  
// ----------------------------------------------------------------
//     -data web page requested     i.e. http://x.x.x.x/data
// ----------------------------------------------------------------
//
//   This shows information on the root web page which refreshes every few seconds

void handleData(){

  String message = 
      "<!DOCTYPE HTML>\n"
      "<html><body>\n";
   
          
  // Movement detection
    message += "<BR>Movement detection is ";
    if (DetectionEnabled) message +=  "enabled, last triggered: " + TriggerTime + "\n";
    else message += red + "disabled" + endcolour + "\n";

  // email when motion detected
    message += "<BR>Send an Email when motion detected: "; 
    if (emailWhenTriggered) message += red + "enabled" + endcolour;  
    else message += "disabled";
    
  // Illumination
    message += "<BR>Illumination LED is ";    
    if (digitalRead(led) == ledON) message += red + "On" + endcolour;
    else message += "Off";
    if (!SD_Present) {    // if no sd card in use
      // show status of use flash 
      if (UseFlash) message += " - Flash enabled\n";
      else message += " - Flash disabled\n";
    }

  // show current time
    message += "<BR>Time: " + currentTime() + "\n";      // show current time

  // show if a sd card is present
    if (SD_Present) message += "<BR>SD-Card present (Flash disabled)";
  
  
  message += "</body></htlm>\n";
  
  server.send(200, "text/html", message);   // send reply as plain text
  message = "";      // clear variable
  
}


// ----------------------------------------------------------------
//           -Display live image     i.e. http://x.x.x.x/live
// ----------------------------------------------------------------

void handleLive(){

  log_system_message("Live image requested");      


  String message = webheader(0);                                      // add the standard html header

  message += "<BR><H1>Live Image - " + currentTime() +"</H1><BR>\n";

  capturePhotoSaveSpiffs(UseFlash);     // capture an image from camera

  // insert image in to html
    message += "<img src='/img' alt='Live Image' width='70%'>\n";
    
  message += webfooter();                                             // add the standard footer
  
  server.send(200, "text/html", message);      // send the web page
  message = "";      // clear variable  
}


// ----------------------------------------------------------------
//    -Display captured images     i.e. http://x.x.x.x/images
// ----------------------------------------------------------------

void handleImages(){

  log_system_message("Stored images page requested");   
  int ImageToShow = SpiffsFileCounter;     // set current image to display when /img called

  // action any buttons presses etc.

    // if a image select button was pressed
      if (server.hasArg("button")) {
        String Bvalue = server.arg("button");   // read value
        int val = Bvalue.toInt();
        Serial.println("Button " + Bvalue + " was pressed");
        ImageToShow = val;     // select which image to display when /img called
      }

      
  String message = webheader(0);                                      // add the standard html header
  message += "<FORM action='/images' method='post'>\n";               // used by the buttons (action = the page send it to)

  message += "<H1>Stored Images</H1>";
  
  // create image selection buttons
    String sins;   // style for button
    for(int i=1; i <= MaxSpiffsImages; i++) {
        if (i == ImageToShow) sins = "background-color: #0f8; height: 30px;";
        else sins = "height: 30px;";
        message += "<input style='" + sins + "' name='button' value='" + String(i) + "' type='submit'>\n";
    }

  // Insert time info. from text file
    String TFileName = "/" + String(ImageToShow) + ".txt";
    File file = SPIFFS.open(TFileName, "r");
    if (!file) {
      Serial.println("Error opening file - " + TFileName);
    } else {
      String line = file.readStringUntil('\n');
      message += "<BR>" + line +"\n";
    }
    file.close();
    
  // insert image in to html
    message += "<BR><img src='/img?pic=" + String(ImageToShow) + "' alt='Camera Image' width='70%'>\n";
  

  message += webfooter();                                             // add the standard footer

    
  server.send(200, "text/html", message);      // send the web page
  message = "";      // clear variable
  
}


// ----------------------------------------------------------------
//      -ping web page requested     i.e. http://x.x.x.x/ping
// ----------------------------------------------------------------

void handlePing(){

  log_system_message("ping web page requested");      
  String message = "ok";

  server.send(404, "text/plain", message);   // send reply as plain text
  message = "";      // clear variable
  
}


// ----------------------------------------------------------------
// -last stored image page requested     i.e. http://x.x.x.x/img
// ----------------------------------------------------------------
// pic parameter on url selects which file to display

void handleImg(){
    
    int ImageToShow = SpiffsFileCounter;     // set image to display as current image
        
    // if a image to show is specified in url
        if (server.hasArg("pic")) {
        String Bvalue = server.arg("pic");   // read value
        int val = Bvalue.toInt();
        Serial.println("Button " + Bvalue + " was pressed");
        ImageToShow = val;     // select which image to display when /img called
        }

    log_system_message("display stored image requested " + String(ImageToShow));

    if (ImageToShow < 1 || ImageToShow > MaxSpiffsImages) ImageToShow=SpiffsFileCounter;
    
    // send image file
        String TFileName = "/" + String(ImageToShow) + ".jpg";
        File f = SPIFFS.open(TFileName, "r");                         // read file from spiffs
            if (!f) Serial.println("Error reading " + TFileName);
            else {
                size_t sent = server.streamFile(f, "image/jpeg");     // send file as web page
                if (!sent) Serial.println("Error sending " + TFileName);
            }
        f.close();


    ImageToShow = 0;     // reset image to display to most recent
    
}


// ----------------------------------------------------------------
//                       -spiffs procedures
// ----------------------------------------------------------------
// capture live image and save in spiffs (also on sd-card if present)

void capturePhotoSaveSpiffs(bool UseFlash) {

  if (DetectionEnabled == 1) DetectionEnabled = 2;               // pause motion detecting while photo is captured (don't think this is required?)

  bool ok;

  // increment image count
    SpiffsFileCounter++;
    if (SpiffsFileCounter > MaxSpiffsImages) SpiffsFileCounter = 1;

  // use flash if required
  bool tFlash = digitalRead(led);                           // store current Illuminator LED status
  if (!SD_Present && UseFlash)  digitalWrite(led, ledON);   // turn Illuminator LED on if no sd card and it is required
  

  // ------------------- capture an image -------------------
    
  RestartCamera(FRAME_SIZE_PHOTO, PIXFORMAT_JPEG);      // restart camera in jpg mode to take a photo (uses greyscale mode for movement detection)

  camera_fb_t * fb = NULL; // pointer
  ok = 0; // Boolean to indicate if the picture has been taken correctly
  byte TryCount = 0;    // attempt counter to limit retries

  do {

    TryCount ++;
      
    Serial.println("Taking a photo... attempt #" + String(TryCount));
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }
       

    // ------------------- save image to Spiffs -------------------
    
    String IFileName = "/" + String(SpiffsFileCounter) + ".jpg";      // file names to store in Spiffs
    String TFileName = "/" + String(SpiffsFileCounter) + ".txt";
    // Serial.println("Picture file name: " + IFileName);

    File file = SPIFFS.open(IFileName, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file in writing mode");
      return;
    }
    else {
      file.write(fb->buf, fb->len);     // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(IFileName);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    file.close();
    
    // save text file to spiffs with time info. 
      SPIFFS.remove(TFileName);   // delete old file with same name if present
      file = SPIFFS.open(TFileName, "w");
      if (!file) {
        Serial.println("Failed to create text file");
        return;
      }
      else file.println(currentTime());
      file.close();


    // ------------------- save image to SD Card -------------------
    
    if (SD_Present) {

      fs::FS &fs = SD_MMC; 
      
      // read image number from counter text file
        int Inum = 0;  
        String CFileName = "/counter.txt";   
        file = fs.open(CFileName, FILE_READ);
        if (!file) Serial.println("Unable to read counter.txt from sd card"); 
        else {
          // read contents
          String line = file.readStringUntil('\n');    
          Inum = line.toInt();
          if (Inum > 0 && Inum < 2000) Serial.println("Last stored image on SD Card was #" + line);
          else Inum = 0;
        }
        file.close();
        Inum ++;
        
      // store new image number to counter text file
        if (fs.exists(CFileName)) fs.remove(CFileName);
        file = fs.open(CFileName, FILE_WRITE);
        if (!file) Serial.println("Unable to create counter file on sd card");
        else file.println(String(Inum));
        file.close();
        
      IFileName = "/" + String(Inum) + ".jpg";     // file names to store on sd card
      TFileName = "/" + String(Inum) + ".txt";
      
      // save image
        file = fs.open(IFileName, FILE_WRITE);
        if (!file) Serial.println("Failed to open sd-card image file in writing mode");
        else file.write(fb->buf, fb->len); // payload (image), payload length
        file.close();
        
      // save text (time and date info)
        file = fs.open(TFileName, FILE_WRITE);
        if (!file) Serial.println("Failed to create sd-card text file in writing mode");
        else file.println(currentTime());
        file.close();      
    }    // end of store on SD card
    
    
    // ------------------------------------------------------------

    
    esp_camera_fb_return(fb);    // return frame so memory can be released

    // check if file has been correctly saved in SPIFFS
      ok = checkPhoto(SPIFFS, IFileName);
    
  } while ( !ok && TryCount < 3);           // if there was a problem try again 

  if (!SD_Present) digitalWrite(led, tFlash);         // restore flash status

  if (TryCount == 3) log_system_message("Unable to capture/store image");
  
  RestartCamera(FRAME_SIZE_MOTION, PIXFORMAT_GRAYSCALE);    // restart camera back to greyscale mode for movement detection
  if (capture_still()) update_frame();                      // update stored frame
  TRIGGERtimer = millis();                                  // reset retrigger timer to stop instant movement trigger
  if (DetectionEnabled == 2) DetectionEnabled = 1;          // restart paused motion detecting
    
}


// check file saved to Spiffs ok
bool checkPhoto( fs::FS &fs, String IFileName ) {
  File f_pic = fs.open( IFileName );
  unsigned int pic_sz = f_pic.size();
  bool tres = ( pic_sz > 100 );
  if (!tres) log_system_message("Problem detected taking/storing image");
  return ( tres );
}


// ----------------------------------------------------------------
//                       -restart the camera
// ----------------------------------------------------------------
//  pixformats = PIXFORMAT_ + YUV422,GRAYSCALE,RGB565,JPEG
//  framesizes = FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA

void RestartCamera(framesize_t fsize, pixformat_t format) {

    bool ok;
    esp_camera_deinit();
      config.frame_size = fsize;
      config.pixel_format = format;
    ok = esp_camera_init(&config);
    if (ok == ESP_OK) {
      Serial.println("Camera mode switched");
    }
    else {
      // failed so try again
        delay(50);
        ok = esp_camera_init(&config);
        if (ok == ESP_OK) Serial.println("Camera restarted");
        else Serial.println("Camera failed to restart");
    }

}


// ----------------------------------------------------------------
//                       -motion has been detected
// ----------------------------------------------------------------

void MotionDetected(float changes) {

  if (DetectionEnabled == 1) DetectionEnabled = 2;               // pause motion detecting (prob. not required?)
  
    log_system_message("Camera detected motion: " + String(changes)); 
    TriggerTime = currentTime();                                 // store time of trigger
    
    capturePhotoSaveSpiffs(UseFlash);                            // capture an image

    if (emailWhenTriggered) {
      // check when last email was sent
        unsigned long currentMillis = millis();        // get current time  
        if ((unsigned long)(currentMillis - EMAILtimer) >= (EmailLimitTime * 1000) ) {
      
          // send an email
              String emessage = "Camera triggered at " + currentTime();
              byte q = sendEmail(emailReceiver,"Message from CameraWifiMotion", emessage);    
              if (q==0) log_system_message("email sent ok" );
              else log_system_message("Error sending email, error code=" + String(q) );

          EMAILtimer = currentMillis;    // reset timer   
         }
         else log_system_message("Too soon to send another email");
    }

  TRIGGERtimer = millis();                                       // reset retrigger timer to stop instant movement trigger
  if (DetectionEnabled == 2) DetectionEnabled = 1;               // restart paused motion detecting

}



// ----------------------------------------------------------------
//           -testing page     i.e. http://x.x.x.x/test
// ----------------------------------------------------------------

void handleTest(){

  log_system_message("Testing page requested");      

  String message = webheader(0);                                      // add the standard html header

  message += "<BR>Testing page<BR><BR>\n";

  // ---------------------------- test section here ------------------------------



//        capturePhotoSaveSpiffs(1);
//        
//        // send email
//          String emessage = "Test email";
//          byte q = sendEmail(emailReceiver,"Message from CameraWifiMotion sketch", emessage);    
//          if (q==0) log_system_message("email sent ok" );
//          else log_system_message("Error sending email code=" + String(q) );


       
  // -----------------------------------------------------------------------------

  message += webfooter();                                             // add the standard footer

    
  server.send(200, "text/html", message);      // send the web page
  message = "";      // clear variable
  
}


// --------------------------- E N D -----------------------------