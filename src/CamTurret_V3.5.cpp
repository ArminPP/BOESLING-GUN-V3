/*
###################################################
## Boesling *) Gun 3.0 - a cat protection system ##
###################################################
*) boesling means 'bad boy' in the Austrian dialect


First of all: I really like cats and pets.
But most of all I love my cat :-)

My cat, Fellina, is about 15 years old (around 80 years in human age) and suffers from atrosis.
We live in the country and there are lots of other cats nearby.
Every now and then a new young player appears and tries to bully Fellina.
She is a tiny cat weighing 3.5kg and sometimes finds it difficult to protect her territory.
I can watch it with some "CatCams" I've installed in the past, but I can't help her if a bully shows up.

So I decided to build a repellent that would scare away other cats but not hurt them.
After some internet research I came across 2 remarkable sites:

https://randomnerdtutorials.com/esp32-cam-pan-and-tilt-2-axis/
https://github.com/jonathanrandall/esp32_cam_electric_watergun/tree/main


I took both program ideas as a basis and built my own modifications around them.

*/

/*********
  Rui Santos
  Complete instructions at https://RandomNerdTutorials.com/esp32-cam-projects-ebook/

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiMulti.h>

#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
// #include "soc/soc.h"          // disable brownout problems
// #include "soc/rtc_cntl_reg.h" // disable brownout problems
#include "esp_http_server.h"
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "Version.h"
#include "Credentials.h"

WiFiMulti wifiMulti;
const uint16_t connectionTimeOut = 5000; // timeout for wifiMulti.run()

const char *ESP_HOSTNAME = "BoeslingGun3";

#define PART_BOUNDARY "123456789000000000000987654321"

// CAMERA_MODEL_ESP32S3_EYE
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y2_GPIO_NUM 11
#define Y3_GPIO_NUM 9
#define Y4_GPIO_NUM 8
#define Y5_GPIO_NUM 10
#define Y6_GPIO_NUM 12
#define Y7_GPIO_NUM 18
#define Y8_GPIO_NUM 17
#define Y9_GPIO_NUM 16

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

// ---------------------------
#define LED_GPIO_NUM 48     // internal LED
#define SERVO_TOP_PIN 39    // 14 is not working!
#define SERVO_BOTTOM_PIN 47 //
#define FIRE_PIN 38         // watergun (relay)
#define LIGHT_PIN 40        // external LED lamp (relay)
#define SPARE_PIN 41        // spare output

const u_int8_t SERVO_STEP = 5;
const u_int8_t SERVO_FAST_STEP = 15;

#ifndef NPN
// PNP relay shield Values (Low Level Trigger)  - better with ESP32-S3, during (re)boot GPIOs going high!
const u_int8_t ON = LOW;
const u_int8_t OFF = HIGH;
#else
// NPN relay shield Values (High Level Trigger)
const u_int8_t ON = HIGH;
const u_int8_t OFF = LOW;
#endif

// Light is driven with a MOS-Fet
const u_int8_t L_ON = HIGH;
const u_int8_t L_OFF = LOW;

unsigned long FIRE_TIME_HELPER;   // helper for duration of fireing
const uint32_t FIRE_TIME = 2000;  //
boolean RELAY_ON = false;         // to make sure switching relays off only once in the loop()
unsigned long LIGHT_TIME_HELPER;  // helper for duration of light on
const uint32_t LIGHT_TIME = 5000; //
boolean LIGHT_ON = false;         // if Aim and FIre is active, fire must be switched off later
const uint16_t FIRE_DELAY = 300;  // delay for fireing after aiming

Servo servoTop;    // PWM range 500µs (0°) to 2500 µs (180°)
Servo servoBottom; // PWM range unknown

const uint8_t servoTopHomePos = 105;         // home top position
const uint8_t servoBottomHomePos = 85;       // home bottom position
uint8_t servoTopPos = servoTopHomePos;       // actual top position
uint8_t servoBottomPos = servoBottomHomePos; // actual bottom position
const uint8_t servoTopTargetPos_1 = 90;      // User position #1
const uint8_t servoBottomTargetPos_1 = 180;  //
const uint8_t servoTopTargetPos_2 = 85;      // User position #2
const uint8_t servoBottomTargetPos_2 = 150;  //
const uint8_t servoTopTargetPos_3 = 95;      // User position #3
const uint8_t servoBottomTargetPos_3 = 100;  //
const uint8_t servoTopTargetPos_4 = 90;      // User position #4
const uint8_t servoBottomTargetPos_4 = 65;   //

const uint8_t servoTopMaxUpPos = 135;  // maximum allowed position to avoid damage
const uint8_t servoTopMaxDownPos = 65; // minimum allowed position to avoid damage

const int8_t servoShakeTopPos = 1;    // shake position Top
const int8_t servoShakeBottomPos = 2; // shake position Bottom
bool Shake = false;                   // shake mode

boolean heartBeat = false;

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">

    <title>Boesling Gun</title>
    
    <style>
        /* center all */
        body {
            font-family: Arial;
            display: flex;
            flex-direction: column;
          /*  justify-content: center;*/
            align-items: center;
            margin: 0;
            padding: 0;
            background-color: #f4f4f4;
        }

        h3 {
            margin-top: 5px;
            margin-bottom: 2px;
            text-align: center;
        }

        /* center span objects */
        .spans-container {
            display: flex;
            flex-wrap: wrap;
            justify-content: center;
            gap: 5px;
            margin-top: 5px;
        }

        /* center single span cell */
        .span-cell {
            box-sizing: border-box;
            max-width: 145;
            display: flex;
            white-space: nowrap;
            text-align: center;
        }

        /* center the cam image */
        .image-container {
            display: flex;
            justify-content: center;
            width: 100%;
            margin-top: 5px;
        }

        /* the image itself */
        img {
            max-width: 600px;
            width: 100%;
        }

        /* table for the navigation buttons */
        .content-container {
            display: flex;
            justify-content: center;
            gap: 80px;
            align-items: center;
            margin-top: 5px;
        }

        /* grid for the navigation buttons */
        .navigation-container {
            display: flex;
            flex-wrap: wrap;
            justify-content: center;
            align-items: center;
            max-width: 400px;
            margin: auto;
            grid-gap: 10px; /* distance between cells */
            row-gap: 5px;   /* special: distance between lines */
        }
        /* the navigation buttons */
        .navigation-item {
            width: 80px;
            height: 40px;  
            display: flex;
            justify-content: center;
            align-items: center;
            background-color: #2f4468;
            color: white;
            font-weight: bold;
            border-radius: 10px;
            cursor: pointer;
            margin: 5px;  
            padding: 0;  
            line-height: 3;
            user-select: none; /* not selectable, otherwise the browser will suggest some inside serach... */  
        }

        .navigation-item:hover {
            background-color: #16499f;
            
        }

        /* layout for large screens */
        @media (min-width: 768px) {
            .navigation-container {
                max-width: 480px; /* Breite anpassen */
                display: grid;
                grid-template-columns: repeat(4, 80px); /* 4 fixed rows */
                grid-template-rows: auto;
                grid-gap: 10px; /* distance between cells */
                row-gap: 1px;   /* special: distance between lines */
                justify-content: center;
            }

            /* Positionierung der Items */
            .navigation-item:nth-child(1) {
                grid-column: 1;
                grid-row: 1;
            }
            .navigation-item:nth-child(2) {
                grid-column: 2;
                grid-row: 1;
            }
            .navigation-item:nth-child(3) {
                grid-column: 3;
                grid-row: 1;
            }
            .navigation-item:nth-child(4) {
                grid-column: 4;
                grid-row: 1;
            }
            .navigation-item:nth-child(5) { /* U */
                grid-column: 1 / span 4;
                grid-row: 2;
                justify-self: center;
            }
            .navigation-item:nth-child(6) { /* LL */
                grid-column: 1;
                grid-row: 3;
            }
            .navigation-item:nth-child(7) { /* L */
                grid-column: 2;
                grid-row: 3;
            }
            .navigation-item:nth-child(8) { /* R */
                grid-column: 3;
                grid-row: 3;
            }
            .navigation-item:nth-child(9) { /* RR */
                grid-column: 4;
                grid-row: 3;
            }
            .navigation-item:nth-child(10) { /* D */
                grid-column: 1 / span 4;
                grid-row: 4;
                justify-self: center;
            }
        }

        /* layout for small screens */
        @media (max-width: 767px) {
            .navigation-container {
                display: grid;
                grid-template-columns: repeat(2, 80px); /* 2 fixed rows */
                grid-template-rows: auto;
                grid-gap: 10px; /* distance between cells */
                row-gap: 1px;   /* special: distance between lines */
                justify-content: center;
            }

            /* Positionierung der Items */
            .navigation-item:nth-child(1) {
                grid-column: 1;
                grid-row: 1;
            }
            .navigation-item:nth-child(2) {
                grid-column: 2;
                grid-row: 1;
            }
            .navigation-item:nth-child(3) {
                grid-column: 1;
                grid-row: 2;
            }
            .navigation-item:nth-child(4) {
                grid-column: 2;
                grid-row: 2;
            }
            .navigation-item:nth-child(6) { /* LL */
                grid-column: 1;
                grid-row: 4;
            }
            .navigation-item:nth-child(9) { /* RR */
                grid-column: 2;
                grid-row: 4;
            }
            .navigation-item:nth-child(5) { /* U */
                grid-column: 1 / span 2;
                grid-row: 3;
                justify-self: center;
            }
            .navigation-item:nth-child(7) { /* L */
                grid-column: 1;
                grid-row: 5;
            }
            .navigation-item:nth-child(8) { /* R */
                grid-column: 2;
                grid-row: 5;
            }
            .navigation-item:nth-child(10) { /* D */
                grid-column: 1 / span 2;
                grid-row: 6;
                justify-self: center;
            }
        }

        /* action buttons */
        .button {
            background-color: #2f4468;
            border: none;
            color: white;
            padding: 10px 20px;
            text-align: center;
            display: inline-block;
            font-size: 18px;
            cursor: pointer;
            margin: 6px 3px;
            user-select: none;
            border-radius: 10px;
            min-width: 80px;   
        }

        button:hover {
            background-color: #16499a;
        }

        /* style for reboot dialog container */
        #dialog {
            display: none;
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            padding: 20px;
            background-color: white;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            border-radius: 5px;
            z-index: 1000;
            text-align: center;
        }

        /* if dialog is popping up dim background */
        #overlay {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.7);
            z-index: 999;
        }

        /* dialog */
        .dialog-button {
            margin: 5px;
            padding: 10px 20px;
            cursor: pointer;
        }

        /* dialog */
        .yes-button {
            background-color: #850000;
            color: white;
            border: none;
        }

        /* dialog */
        .no-button {
            background-color: #ccc;
            color: black;
            border: none;
        }


    </style>
</head>
<body>
    <!-- the title span -->
    <!-------------------->    
    <div id="header" style="display: flex; justify-content: space-between; align-items: center; margin: 0px;">
        <h3>
            Boesling Gun
            <span id="Version">V0.0</span>
        </h3>
        <span id="heartBeat" style="font-family: monospace; font-size: 20px; margin-left: 5px;">❤️</span>
    </div>

    <!-- the info span -->
    <!------------------->
    <div class="spans-container">
        <div class="span-cell"; id="servTop">Top: 0</div>
        <div class="span-cell"; id="servBottom">Bot: 0</div>
        <div class="span-cell"; id="coreTemp">T: 0</div>
        <div class="span-cell"; id="wifiRssi">RSSI: 0</div>
        <div class="span-cell"; id="signalStrength">Q: 0|0</div>
    </div>

    <!-- live stream of the cam -->
    <!---------------------------->
    <div class="image-container">
        <img src="" id="photo" >
    </div>

    <div class="content-container">

        <!-- navigation buttons -->
        <!--       ontouchstart="actionToESP('fire');" IS NOT NEEDED ON MOBILE DEVICES  -->
        <!--       because it triggers the event twice! ?!?!?!  -->
        <!------------------------------------------------------------------------------------->


        <div class="navigation-container">
            <p class="navigation-item"
                style="background-color:#850000"
                onmousedown="actionToESP('POS1')"
                onmouseover="this.style.backgroundColor='#660000'"
                onmouseout="this.style.backgroundColor='#850000'"
            >POS1</p>
            <p class="navigation-item"
                style="background-color:#850000"
                onmousedown="actionToESP('POS2')"
                onmouseover="this.style.backgroundColor='#660000'"
                onmouseout="this.style.backgroundColor='#850000'"
            >POS2</p>
            <p class="navigation-item"
                style="background-color:#850000"
                onmousedown="actionToESP('POS3')"
                onmouseover="this.style.backgroundColor='#660000'"
                onmouseout="this.style.backgroundColor='#850000'"
            >POS3</p>
            <p class="navigation-item"
                style="background-color:#850000"
                onmousedown="actionToESP('POS4')"
                onmouseover="this.style.backgroundColor='#660000'"
                onmouseout="this.style.backgroundColor='#850000'"
            >POS4</p>
            <p class="navigation-item"
                onmousedown="actionToESP('up')"
            >U</p>
            <p class="navigation-item"
                onmousedown="actionToESP('f_left')"
            >&lt;&lt;</p>
            <p class="navigation-item"
                onmousedown="actionToESP('left')"
            >L</p>
            <p class="navigation-item"
               onmousedown="actionToESP('right')"
            >R</p>
            <p class="navigation-item"
                onmousedown="actionToESP('f_right')"
            >&gt;&gt;</p>
            <p class="navigation-item"
                onmousedown="actionToESP('down')"
            >D</p>
        </div>

        <!-- action buttons -->                         
        <!-------------------->
        <div class="button-container">
            <div style="margin-bottom:15px">
                <input id="AimAndFire" type="checkbox" checked/>
                <label for="AimAndFire">Aim & Fire!</label>
                <input id="shake" type="checkbox" checked/>
                <label for="shake">Shake it!</label>
            </div> 
            <div>
                <button class="button" onmousedown="actionToESP('fire');">Fire  !</button>
                <button class="button" onmousedown="actionToESP('LightOn');">Light On</button>
                <button class="button" onmousedown="actionToESP('LightOff');">Light Off</button>
            </div>
            <div>
                <button class="button" 
                    style="background-color:green" 
                    onmouseover="this.style.backgroundColor='#00ff40'"
                    onmouseout="this.style.backgroundColor='green'"                    
                    onmousedown="actionToESP('Home');">Home
                </button>
                <button disabled class="button" style="background-color:grey" onmousedown="actionToESP('LaserOn');">Laser on</button>
                <button class="button" 
                    style="background-color:yellow; color:red" 
                    onmouseover="this.style.backgroundColor='#ffc500'"
                    onmouseout="this.style.backgroundColor='yellow'"
                    onclick="openDialog()";">Restart
                </button>
                
            </div>
        </div>
    </div> 


    <!-- Reboot confirmation dialog -->                         
    <!-------------------------------->
    <div id="overlay"></div>
    <div id="dialog">
        <p>Reboot - Are you sure?</p>
        <button class="dialog-button yes-button" onclick="confirmReboot()">Yes</button>
        <button class="dialog-button no-button" onclick="closeDialog()">No</button>
    </div>


   <script>

   // Functions for dialog to confirming reboot of ESP32
   // --------------------------------------------------
    function openDialog() {
        document.getElementById("dialog").style.display = "block";
        document.getElementById("overlay").style.display = "block";
    }
    function closeDialog() {
        document.getElementById("dialog").style.display = "none";
        document.getElementById("overlay").style.display = "none";
    }
    function confirmReboot() {
        actionToESP('Restart');
        closeDialog();
    }

   // send actions to ESP32
   // ---------------------
   function actionToESP(x) {
    var aimAndFireCheckbox = document.getElementById("AimAndFire"); // get checkbox element according to its ID
    var isAimAndFireChecked = aimAndFireCheckbox.checked;           // checking if box is checked

    var shakeCheckbox = document.getElementById("shake");           // get shake checkbox element
    var isShakeChecked = shakeCheckbox.checked;                     // checking if shake box is checked

    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/action?go=" + x + "&aimAndFire=" + isAimAndFireChecked + "&shake=" + isShakeChecked, true);
    xhr.send();
   }
  
   // update variables from ESP32
   // ---------------------------
   function updateVariables() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/getVariables", true);
      xhr.onload = function() {
         if (xhr.status === 200) {
            // Answer is a JSON object, e.g. { "variable1": "123", "coreTemp": "456", "wifiRssi": "789" }
            var response = JSON.parse(xhr.responseText);
            document.getElementById("servTop").innerText = "Top: " + response.servTop +"° | ";
            document.getElementById("servBottom").innerText = "Bot: " + response.servBottom +"° | ";
            document.getElementById("coreTemp").innerText = "T: " + response.coreTemp +" °C | ";
            document.getElementById("wifiRssi").innerText = "RSSI: " + response.wifiRssi;
            document.getElementById("signalStrength").innerText = " Q: " + response.signalStrength;

            // check the heartBeat var and animate the ASCII heart
            var heartbeatElement = document.getElementById("heartBeat");
            if (response.heartBeat === true) {
                heartbeatElement.innerText = "❤️"; // high
            } else {
                heartbeatElement.innerText = "💔"; // low
            }
         }
      };
      xhr.send();
   }
   setInterval(updateVariables, 500);

   // update variables from ESP32 only once at onLoad
   // -----------------------------------------------
   function updateVariablesOnce() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/getVariablesOnce", true);
      xhr.onload = function() {
         if (xhr.status === 200) {
            // Answer is a JSON object, e.g. { "variable1": "123", "coreTemp": "456", "wifiRssi": "789" }
            var response = JSON.parse(xhr.responseText);
            document.getElementById("Version").innerText =  response.Version + "." + response.Build;
         }
      };
      xhr.send();
   }

   window.onload = function() {
     document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";
     updateVariables();
     updateVariablesOnce();
   };
  </script>
  </body>
</html>
)rawliteral";

/*
  debug print
*/
void printInfos(const char *text)
// #################################################################;
// #################################################################;
{
    Serial.printf("%s   |    WiFi RSSSI %i | CoreTemp: %3.2f \r\n", text, WiFi.RSSI(), temperatureRead());
}

/*
  returns '+1' or '-1' randomly
*/
int randomSign()
{
    return random(0, 2) * 2 - 1; // random() uses the ESP32’s RNG module !
}

/*
  start fireing the gun
*/
void Fire_On()
// #################################################################;
// #################################################################;
{
    digitalWrite(FIRE_PIN, ON);
    RELAY_ON = true;

    FIRE_TIME_HELPER = millis();

    printInfos("Fire on");
}

/*
  switches the light on and off
*/
void Light_On(bool OnOff)
// #################################################################;
// #################################################################;
{
    if (OnOff)
    {
        rgbLedWrite(RGB_BUILTIN, 255, 255, 255);
        digitalWrite(LIGHT_PIN, L_ON);

        printInfos("Light on");
    }
    else
    {
        rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
        digitalWrite(LIGHT_PIN, L_OFF);

        printInfos("Light off");
    }
}

/*
  helper for debug print (RSSI quality)
*/
String getSignalStrengthDescription(int rssi)
// #################################################################;
// #################################################################;
{
    if (rssi > -30)
        return "6|6";
    else if (rssi > -55)
        return "5|6";
    else if (rssi > -67)
        return "4|6";
    else if (rssi > -70)
        return "3|6";
    else if (rssi > -80)
        return "2|6";
    else
        return "1|6";
    // if (rssi > -30)
    //     return "Amazing";
    // else if (rssi > -55)
    //     return "Very good";
    // else if (rssi > -67)
    //     return "Fairly good";
    // else if (rssi > -70)
    //     return "Okay";
    // else if (rssi > -80)
    //     return "Not good";
    // else
    //     return "Extremely weak signal (unusable)";
}

/*
  helper for reconnecting to WiFi after connection was lost (every 5 s)

  reconnection method without callbacks
  maybe the callback(s)
    WiFi.onEvent(WiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  causes a guru medidation?! I had this issue at a different project!

  only reconnect if mode is STA, in AP mode reconnection is not needed!
*/
void checkWIFIandReconnect()
// #################################################################;
// #################################################################;
{
    static unsigned long checkWiFiLoopPM = 0;
    unsigned long checkWiFiLoopCM = millis();
    if ((checkWiFiLoopCM - checkWiFiLoopPM >= 5000) && (WiFi.getMode() == WIFI_MODE_STA))
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("No WIFI connection found. Try to reconnect ...");

            if (wifiMulti.run(connectionTimeOut) == WL_CONNECTED)
            {
                Serial.println("");
                Serial.println("WiFi is connected");
                Serial.printf("IP address            : %s\r\n", WiFi.localIP().toString().c_str());
                Serial.printf("Connected to (SSID)   : %s\r\n", WiFi.SSID());
                Serial.printf("Signal strength (RSSI): %i\r\n", WiFi.RSSI());
            }
        } // -------- checkWiFiLoop end
        checkWiFiLoopPM = checkWiFiLoopCM;
    }
}

/*
  handler for local website
*/
static esp_err_t index_handler(httpd_req_t *req)
// #################################################################;
// #################################################################;
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

/*
  handler for variables send from ESP32 to local webserver
  this values will be updated every ~500 ms via javascript
*/
static esp_err_t variables_handler(httpd_req_t *req)
// #################################################################;
// #################################################################;
{
    JsonDocument jsonDoc;
    jsonDoc["servTop"] = servoTopPos;
    jsonDoc["servBottom"] = servoBottomPos;
    jsonDoc["coreTemp"] = temperatureRead(); //
    int rssi = WiFi.RSSI();
    jsonDoc["wifiRssi"] = rssi;
    jsonDoc["signalStrength"] = getSignalStrengthDescription(rssi);
    jsonDoc["heartBeat"] = heartBeat;
    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, jsonResponse.c_str());
    // Serial.println(jsonResponse);
    return ESP_OK;
}

/*
  handler for variables send from ESP32 to local webserver
  this values will be updated only once on loading of the website
*/
static esp_err_t variables_once_handler(httpd_req_t *req)
// #################################################################;
// #################################################################;
{
    JsonDocument jsonDoc;
    jsonDoc["Version"] = Version;
    jsonDoc["Build"] = BUILD_NUMBER;

    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, jsonResponse.c_str());

    return ESP_OK;
}

/*
  handles the videostream of the cam
*/
static esp_err_t stream_handler(httpd_req_t *req)
// #################################################################;
// #################################################################;
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->width > 400)
            {
                if (fb->format != PIXFORMAT_JPEG)
                {
                    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!jpeg_converted)
                    {
                        Serial.println("JPEG compression failed");
                        res = ESP_FAIL;
                    }
                }
                else
                {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
                }
            }
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            break;
        }
    }
    return res;
}

/*
  handles the user inputs from the website to the ESP32
  e.g. switches GPIOs, ...
*/
static esp_err_t cmd_handler(httpd_req_t *req)
// #################################################################;
// #################################################################;
{
    char *buf;
    size_t buf_len;
    char variable[32] = {0};
    bool AimAndFire = false;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK)
            {
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
            char status[16] = {0};
            if (httpd_query_key_value(buf, "aimAndFire", status, sizeof(status)) == ESP_OK)
            {
                AimAndFire = (strcmp(status, "true") == 0); // Konvertiere zu bool
                Serial.printf("AimAndFire Checkbox status: %s\r\n", AimAndFire ? "Checked" : "Unchecked");
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
            char shakeStatus[16] = {0};
            if (httpd_query_key_value(buf, "shake", shakeStatus, sizeof(shakeStatus)) == ESP_OK)
            {
                Shake = (strcmp(shakeStatus, "true") == 0); // Konvertiere zu bool
                Serial.printf("Shake Checkbox status: %s\r\n", Shake ? "Checked" : "Unchecked");
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    sensor_t *s = esp_camera_sensor_get();
    // flip the camera vertically
    // s->set_vflip(s, 1);          // 0 = disable , 1 = enable
    //  mirror effect
    // s->set_hmirror(s, 1);          // 0 = disable , 1 = enable
    // s->set_brightness(s, 1);//up the blightness just a bit
    // s->set_saturation(s, -2);//lower the saturation
    // s->set_framesize(s, FRAMESIZE_QVGA);

    int res = 0;
    if (!strcmp(variable, "up")) // moves the top servo up
    {
        if (servoTopPos <= servoTopMaxUpPos)
        {
            servoTopPos += SERVO_STEP;
            servoTop.write(servoTopPos);
        }
        Serial.printf("Up: %i ", servoTopPos);
        printInfos("");
    }
    else if (!strcmp(variable, "down")) // moves the top servo down
    {
        if (servoTopPos >= servoTopMaxDownPos)
        {
            servoTopPos -= SERVO_STEP;
            servoTop.write(servoTopPos);
        }
        Serial.printf("Down: %i", servoTopPos);
        printInfos("");
    }
    else if (!strcmp(variable, "left")) // moves the bottom servo to the left
    {
        if (servoBottomPos <= 180 - SERVO_STEP)
        {
            servoBottomPos += SERVO_STEP;
            servoBottom.write(servoBottomPos);
        }
        Serial.printf("Left: %i ", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "f_left")) // moves the bottom servo fast to the left
    {
        if (servoBottomPos <= 180 - SERVO_FAST_STEP)
        {
            servoBottomPos += SERVO_FAST_STEP;
            servoBottom.write(servoBottomPos);
        }
        Serial.printf("Fast Left: %i ", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "right")) // moves the bottom servo to the right
    {
        if (servoBottomPos >= 0 + SERVO_STEP)
        {
            servoBottomPos -= SERVO_STEP;
            servoBottom.write(servoBottomPos);
        }
        Serial.printf("Right: %i ", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "f_right")) // moves the bottom servo fast to the right
    {
        if (servoBottomPos >= 0 + SERVO_FAST_STEP)
        {
            servoBottomPos -= SERVO_FAST_STEP;
            servoBottom.write(servoBottomPos);
        }
        Serial.printf("Fast Right: %i", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "POS1")) // moves both servos to a user position (#1)
    {
        servoTopPos = servoTopTargetPos_1;
        servoTop.write(servoTopPos);

        servoBottomPos = servoBottomTargetPos_1;
        servoBottom.write(servoBottomPos);

        if (AimAndFire)
        {
            delay(FIRE_DELAY);
            Fire_On();
            Light_On(true);
            LIGHT_ON = true;
            LIGHT_TIME_HELPER = millis();
        }

        Serial.printf("POS1-Top: %i ", servoTopPos);
        Serial.printf("POS1-Bottom: %i", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "POS2")) // moves both servos to a user position (#2)
    {
        servoTopPos = servoTopTargetPos_2;
        servoTop.write(servoTopPos);

        servoBottomPos = servoBottomTargetPos_2;
        servoBottom.write(servoBottomPos);

        if (AimAndFire)
        {
            delay(FIRE_DELAY);
            Fire_On();
            Light_On(true);
            LIGHT_ON = true;
            LIGHT_TIME_HELPER = millis();
        }

        Serial.printf("POS2-Top: %i ", servoTopPos);
        Serial.printf("POS2-Bottom: %i", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "POS3")) // moves both servos to a user position (#3)
    {
        servoTopPos = servoTopTargetPos_3;
        servoTop.write(servoTopPos);

        servoBottomPos = servoBottomTargetPos_3;
        servoBottom.write(servoBottomPos);

        if (AimAndFire)
        {
            delay(FIRE_DELAY);
            Fire_On();
            Light_On(true);
            LIGHT_ON = true;
            LIGHT_TIME_HELPER = millis();
        }

        Serial.printf("POS3-Top: %i ", servoTopPos);
        Serial.printf("POS3-Bottom: %i", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "POS4")) // moves both servos to a user position (#4)
    {
        servoTopPos = servoTopTargetPos_4;
        servoTop.write(servoTopPos);

        servoBottomPos = servoBottomTargetPos_4;
        servoBottom.write(servoBottomPos);

        if (AimAndFire)
        {
            delay(FIRE_DELAY);
            Fire_On();
            Light_On(true);
            LIGHT_ON = true;
            LIGHT_TIME_HELPER = millis();
        }

        Serial.printf("POS4-Top: %i ", servoTopPos);
        Serial.printf("POS4-Bottom: %i", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "Home")) // moves both servos to the home position
    {
        servoTopPos = servoTopHomePos;
        servoTop.write(servoTopPos);

        servoBottomPos = servoBottomHomePos;
        servoBottom.write(servoBottomPos);

        Serial.printf("Home-Top: %i ", servoTopPos);
        Serial.printf("Home-Bottom: %i", servoBottomPos);
        printInfos("");
    }
    else if (!strcmp(variable, "Restart")) // moves both servos to a user position (#2)
    {
        Serial.println("RESTART ESP");
        printInfos("");
        ESP.restart();
    }

    else if (!strcmp(variable, "fire")) // starts the watergun (relay)
    {
        Fire_On();
    }
    else if (!strcmp(variable, "LightOn")) // switches the light on
    {
        Light_On(true);
    }
    else if (!strcmp(variable, "LightOff")) // switches the light off
    {
        Light_On(false);
    }
    else
    {
        res = -1;
    }
    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

/*

*/
void startCameraServer()
// #################################################################;
// #################################################################;
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};
    httpd_uri_t cmd_uri = {
        .uri = "/action",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};
    httpd_uri_t variables_uri = {
        .uri = "/getVariables",
        .method = HTTP_GET,
        .handler = variables_handler,
        .user_ctx = NULL};
    httpd_uri_t variables_once_uri = {
        .uri = "/getVariablesOnce",
        .method = HTTP_GET,
        .handler = variables_once_handler,
        .user_ctx = NULL};

    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &variables_uri);
        httpd_register_uri_handler(camera_httpd, &variables_once_uri);
    }
    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void setup()
// #################################################################;
// #################################################################;
// #################################################################;
// #################################################################;
// #################################################################;
// #################################################################;
{
    // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector  // INFO Causes Errors: [V][esp32-hal-periman.c:235] perimanSetBusDeinit(): Deinit function for type UART_RTS (5) successfully set to 0x42008774
    Serial.begin(115200);
    // Serial.setDebugOutput(true); // doesn't work?

    rgbLedWrite(RGB_BUILTIN, 0, 0, 0); // switch off the internal light (after a reboot, the light is still on!)

    pinMode(FIRE_PIN, OUTPUT);
    digitalWrite(FIRE_PIN, OFF); // make sure relay is off
    RELAY_ON = false;

    pinMode(LIGHT_PIN, OUTPUT);
    digitalWrite(LIGHT_PIN, L_OFF); // make sure relay is off
    LIGHT_ON = false;

    pinMode(SPARE_PIN, OUTPUT);
    digitalWrite(SPARE_PIN, OFF);

    servoTop.setPeriodHertz(50);    // standard 50 hz servo
    servoBottom.setPeriodHertz(50); // standard 50 hz servo

    servoTop.attach(SERVO_TOP_PIN, 500, 2500);       //  standard is: 500, 2500       544, 2400
    servoBottom.attach(SERVO_BOTTOM_PIN, 500, 2500); //  unknown ..

    servoTop.write(servoTopPos);
    servoBottom.write(servoBottomPos);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 14;     // reducing image quality...
        config.fb_count = 1;          // ... and fb_count ...
        Serial.println("psramFound"); // ... because sometimes my VPN lacks of bandwidth
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        Serial.println("No psramFound");
    }

    // Camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    // Wi-Fi connection
    WiFi.persistent(false);       // reset WiFi and erase credentials
    WiFi.disconnect(false, true); // Wifi adapter off - not good! / delete ap values
    WiFi.eraseAP();               // new function because (disconnect(false, true) doesn't erase the credentials...)
    WiFi.mode(WIFI_OFF);          // mode is off (no AP, STA, ...)

    delay(500);

    Serial.println(F("after clearing WiFI credentials"));
    // setting hostname in ESP32 always before setting the mode!
    // https://github.com/matthias-bs/BresserWeatherSensorReceiver/issues/19
    WiFi.setHostname(ESP_HOSTNAME);

    WiFi.mode(WIFI_MODE_STA);
    // Connectmode Station: as client on accesspoint
    wifiMulti.addAP(WIFI_1_SSID, WIFI_1_PASSWORD); // network 1
    wifiMulti.addAP(WIFI_2_SSID, WIFI_2_PASSWORD); // network 2
    wifiMulti.addAP(WIFI_3_SSID, WIFI_3_PASSWORD); // network 3, and so on ...
    checkWIFIandReconnect();

    Serial.print("Camera Stream Ready! Go to: http://");
    Serial.print(WiFi.localIP());
    printInfos(" ");

    // OTA Configiration and Enable OTA
    Serial.println("\nEnabling OTA Feature");
    ArduinoOTA.setPassword(OTA_PASSWORD); // <---- change this!
    ArduinoOTA.begin();

    // Start streaming web server
    startCameraServer();
}

void loop()
// #################################################################;
// #################################################################;
// #################################################################;
// #################################################################;
// #################################################################;
// #################################################################;
{
    // only if relay is on switch it off!
    if (((millis() - FIRE_TIME_HELPER) > FIRE_TIME) && RELAY_ON)
    {
        digitalWrite(FIRE_PIN, OFF);
        RELAY_ON = false;
        printInfos("Fire off");
    }

    // if aim and fire is active, flash the light every xx ms
    static unsigned long LightLoopPM = 0;
    unsigned long LightLoopCM = millis();
    if (LightLoopCM - LightLoopPM >= 100)
    {
        if (LIGHT_ON)
        {
            static boolean toggle = false;
            toggle = !toggle;
            Light_On(toggle);
            if (Shake) // if shake is active, move the servos randomly
            {
                servoTopPos = servoTopPos + randomSign() * servoShakeTopPos; // random(servoShakeTopPos);
                servoTop.write(servoTopPos);
                servoBottomPos = servoBottomPos + randomSign() * servoShakeBottomPos; // random(servoShakeBottomPos);
                servoBottom.write(servoBottomPos);
            }
        }
        // -------- LightLoop end
        LightLoopPM = LightLoopCM;
    }

    // if aim and fire is active, switch light off after 5s
    if (((millis() - LIGHT_TIME_HELPER) > LIGHT_TIME) && LIGHT_ON)
    {
        Light_On(false);
        LIGHT_ON = false;
        printInfos("Light off");
    }

    // delay(10);

    ArduinoOTA.handle();
    checkWIFIandReconnect();

    // heartBeat toogles, so the heart at the website
    // will be animated. (as a info for the enduser ;-)
    // If the ESP32 is frozen, the animation will stop.
    static unsigned long HeartBeatLoopPM = 0;
    unsigned long HeartBeatLoopCM = millis();
    if (HeartBeatLoopCM - HeartBeatLoopPM >= 1000)
    {
        heartBeat = !heartBeat;
        // -------- HeartBeatLoop end
        HeartBeatLoopPM = HeartBeatLoopCM;
    }
}
