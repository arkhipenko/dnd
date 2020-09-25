/*
   Minecraft Interactive Do Not Enter Sword/Sign (ESP32-CAM)
   Supports streaming up to 10 clients

   Based on AI-Thinker ESP32-CAM
   Compile with:
   ESP32 Dev Module
   CPU Freq: 240
   Flash Freq: 80
   Flash mode: QIO
   Flash Size: 4Mb
   Patrition: Minimal SPIFFS
   PSRAM: Enabled


  Changelog:

  2020-04-XX:
    v1.0.0 - Initial release

  2020-05-07:
    v2.0.0 - Support for multiple streams

  2020-05-08:
    v2.1.0 - Switch between RAM and PSRAM based on the buffer size.
             Suspend camera and streaming tasks when no clients connected
             Allow IDLE_SLEEP for TaskScheduler when no clients connected

  2020-05-11:
    v2.2.0 - Add ability to be just a clock

*/


#define APPVER    "2.2.0"
#define APPTOKEN  "MSG01"
#define EEPROM_MAX 4096

//===== debugging setting =======================
//#define _DEBUG_
//#define _LIBDEBUG_
//#define _TEST_

//===== debugging macros =======================
#ifdef _DEBUG_
#define _PP(a) Serial.print(a);
#define _PL(a) Serial.println(a);
#else
#define _PP(a)
#define _PL(a)
#endif

//#define _TASK_TIMECRITICAL      // Enable monitoring scheduling overruns
#define _TASK_SLEEP_ON_IDLE_RUN // Enable 1 ms SLEEP_IDLE powerdowns between tasks if no callback methods were invoked during the pass
// #define _TASK_STATUS_REQUEST    // Compile with support for StatusRequest functionality - triggering tasks on status change events in addition to time only
// #define _TASK_WDT_IDS           // Compile with support for wdt control points and task ids
// #define _TASK_LTS_POINTER       // Compile with support for local task storage pointer
// #define _TASK_PRIORITY          // Support for layered scheduling priority
// #define _TASK_MICRO_RES         // Support for microsecond resolution
// #define _TASK_STD_FUNCTION      // Support for std::function (ESP8266 and ESP32 ONLY)
// #define _TASK_DEBUG             // Make all methods and variables public for debug purposes
// #define _TASK_INLINE            // Make all methods "inline" - needed to support some multi-tab, multi-file implementations
// #define _TASK_TIMEOUT           // Support for overall task timeout
// #define _TASK_OO_CALLBACKS      // Support for dynamic callback method binding
#include <TaskScheduler.h>

#include "src/OV2640.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <ParametersEEPROM.h>
#include <EspBootstrapDict.h>
#include <JsonConfigHttp.h>
#include <BlynkSimpleEsp32.h>
#include <WidgetRTC.h>
#include <TimeLib.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <HTTPUpdate.h>

#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

/*
  Next one is an include with wifi credentials.
  This is what you need to do:

  1. Create a file called "home_wifi_multi.h" in the same folder   OR   under a separate subfolder of the "libraries" folder of Arduino IDE. (You are creating a "fake" library really - I called it "MySettings").
  2. Place the following text in the file:
  #define SSID1 "replace with your wifi ssid"
  #define PWD1 "replace your wifi password"
  3. Save.

  Should work then
*/
#include "home_wifi_multi.h"

//==== HTTP OTA Update ==================
const String TOKEN(APPTOKEN);
const int NPARS = 9;
const int NPARS_WEB = 3;

#ifdef _DEBUG_
#define APPPREFIX "msgboardtest-"
#else
#define APPPREFIX "msgboard-"
#endif
#define APPHOST   "eta.home.lan"
#define APPPORT   80
#define APPURL    "/esp/esp32.php"
String appVersion;

// ===== parameters ============================
Dictionary* pd_ptr = NULL;
ParametersEEPROM* pp_ptr = NULL;

OV2640 cam;
const int LEDPIN = 33;
const int FLASHPIN = 4;

//==== CODE =========================

void setup() {
  setupPins();

#ifdef _DEBUG_
  Serial.begin(115200);
  _PP(millis()); _PP(": "); _PL("In setup");
  _PP("Tick time: "); _PL(portTICK_PERIOD_MS);
  _PP("Total PSRAM: "); _PL(ESP.getPsramSize());
  _PP("Free PSRAM: "); _PL(ESP.getFreePsram());
#endif
  setupOTA();
  setupParameters();
  setupBT();
  checkOTA();
  createCoreTasks();
}

void loop() {
  // this seems to be necessary to let IDLE task run and do GC
  vTaskDelay(1000);
}

void setupPins() {
  pinMode(LEDPIN, OUTPUT);
  digitalWrite(LEDPIN, HIGH);
  pinMode(FLASHPIN, OUTPUT);
  digitalWrite(FLASHPIN, LOW);
}

void setupParameters() {
  int rc;
  bool wifiTimeout;

  pd_ptr = new Dictionary(NPARS);
  Dictionary& pd = *pd_ptr;

  pp_ptr = new ParametersEEPROM(TOKEN, pd, 0, 512);
  ParametersEEPROM& pp = *pp_ptr;

  if (pp.begin() != PARAMS_OK) {
    _PL("Something is wrong with the EEPROM");
    delay(5000);
    ESP.restart(); // Something is wrong with the EEPROM
  }

  pd("Title", "DND Sword Setup");
  pd("ssid", SSID1);
  pd("password", PWD1);
  pd("cfg_url", "http://ota.home.lan/esp/config/");
  pd("msg", "Hello!");
  pd("devices", "8");

  rc = pp.load();

  _PL("Connecting to WiFi for 30 sec:");
  setupWifi(pd["ssid"].c_str(), pd["password"].c_str());
  wifiTimeout = waitForWifi(30 * BOOTSTRAP_SECOND);

  if (!wifiTimeout) {
    _PL(makeConfig(pd["cfg_url"]));
    rc = JSONConfig.parse(makeConfig(pd["cfg_url"]), pd);
    // If successful, the "pd" dictionary should have a refreshed set of parameters from the JSON file.
    _PP("JSONConfig finished. rc = "); _PL(rc);
    _PP("Current dictionary count = "); _PL(pd.count());
    _PP("Current dictionary size = "); _PL(pd.size());

    if (rc == JSON_OK) pd("saved", "ok");
  }
  if (wifiTimeout || !(rc == JSON_OK || pd("saved"))) {
    _PL("Device needs bootstrapping:");
    rc = ESPBootstrap.run(pd, NPARS_WEB, 10 * BOOTSTRAP_MINUTE);

    if (rc == BOOTSTRAP_OK) {
      pp.save();
      _PL("Bootstrapped OK. Rebooting.");
    }
    else {
      _PL("Bootstrap timed out. Rebooting.");
    }
    delay(2000);
    ESP.restart();
  }
}


// This method prepares for WiFi connection
void setupWifi(const char* ssid, const char* pwd) {
  _PP(millis()); _PL(": setup_wifi()");

  // We start by connecting to a WiFi network
  _PL("Connecting to WiFi...");
  // clear wifi config
  WiFi.disconnect();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pwd);
}


// This method waits for a WiFi connection for aTimeout milliseconds.
bool waitForWifi(unsigned long aTimeout) {
  _PP(millis()); _PL(": waitForWifi()");

  unsigned long timeNow = millis();

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    _PP(".");
    if (millis() - timeNow > aTimeout) {
      _PL(" WiFi connection timeout");
      return true;
    }
  }

  _PL(" WiFi connected");
  _PP("IP address: "); _PL(WiFi.localIP());
  _PP("SSID: "); _PL(WiFi.SSID());
  _PP("mac: "); _PL(WiFi.macAddress());
  delay(2000); // let things settle
  return false;
}

void setupBT() {
  esp_bt_controller_disable();
}

String makeConfig(String path) {
  String cfg(path);
  if (!cfg.endsWith("/")) cfg += "/";
  cfg += (appVersion + ".json");
  return cfg;
}

void setupOTA() {
  //  ESPhttpUpdate.onStart(update_started);
  //  ESPhttpUpdate.onEnd(update_finished);
  //  ESPhttpUpdate.onProgress(update_progress);
  //  ESPhttpUpdate.onError(update_error);
  appVersion = String(APPPREFIX) + WiFi.macAddress() + String("-") + String(APPVER);
  appVersion.replace(":", "");
  appVersion.toLowerCase();
}


void checkOTA() {
  Dictionary& pd = *pd_ptr;
  WiFiClient espClient;

  _PP(millis());
  _PL(": checkOTA()");

  _PL("Attempting OTA");
  _PP("host: "); _PL(pd["ota_host"]);
  _PP("port: "); _PL(pd["ota_port"]);
  _PP("url : "); _PL(pd["ota_url"]);
  _PP("ver : "); _PL(appVersion);

  httpUpdate.setLedPin(LEDPIN, LOW);
  t_httpUpdate_return ret = httpUpdate.update(espClient, pd["ota_host"], pd["ota_port"].toInt(), pd["ota_url"], appVersion);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      _PP("HTTP_UPDATE_FAILED: Error code=");
      _PP(httpUpdate.getLastError());
      _PP(" ");
      _PL(httpUpdate.getLastErrorString());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      _PL("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      _PL("HTTP_UPDATE_OK");
      break;
  }
}


#define APP_CPU 1
#define PRO_CPU 0

// ===== rtos task handles =========================
TaskHandle_t tMjpeg;
TaskHandle_t tTaskScheduler;
//TaskHandle_t tUdp;

void createCoreTasks() {
  _PP(millis()); _PL(F(": createTasks."));

  xTaskCreatePinnedToCore(
    mjpegCB,
    "mjpeg",
    4096,
    NULL,
    2,
    &tMjpeg,
    APP_CPU);

  xTaskCreatePinnedToCore(
    tsCB,   /* Task function. */
    "ts",     /* name of task. */
    6144,       /* Stack size of task */
    NULL,        /* parameter of the task */
    2,           /* priority of the task */
    &tTaskScheduler,  /* task handle */
    PRO_CPU);
}
