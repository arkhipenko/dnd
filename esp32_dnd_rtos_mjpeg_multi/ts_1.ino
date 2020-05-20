
//==== Task Scheduler section ==============

#define CLK_PIN   14   // or SCK
#define DATA_PIN  13   // or MOSI
#define CS_PIN    15   // or SS

#define MAX_STRLEN 129

// ==== Virtual PINS ====
#define VTEXT     V0  // 128 chars
#define VSCROLL   V1  // 1 - left, 2 - no, 3 - right
#define VSPEED    V3  // 0-100
#define VFLASH    V4  // 0/1
#define VBLINK    V5  // 0/1
#define VBRIGHT   V6  // 0-15
#define VCLOCK    V7  // 0-15
#define VTURNOFF  V100  // on/off

WidgetRTC rtc;

#define SS_STEADY 5

typedef struct {
  int             scrollSpeed;
  int             scrollPause;
  textEffect_t    scrollEffect;
  textPosition_t  scrollAlign;
  int             scrollIntensity;
} MDConfig;

// ==== Tasks =================

Scheduler ts;

void textCB();
void blinkCB();
bool blinkOE();
void blinkOD();
void blynkCB();
bool clockOE();
void clockCB();
void clockOD();

Task tText(TASK_MILLISECOND, TASK_FOREVER, &textCB, &ts);
Task tBlink(400 * TASK_MILLISECOND, TASK_FOREVER, &blinkCB, &ts, false, &blinkOE, &blinkOD);
Task tBlynk(100 * TASK_MILLISECOND, TASK_FOREVER, &blynkCB, &ts);
Task tClock(500 * TASK_MILLISECOND, TASK_FOREVER, &clockCB, &ts, false, &clockOE, &clockOD);

MD_Parola* P_ptr;

MDConfig MDCur;
MDConfig MDBackup;

char msg[129];
String blynk_auth, blynk_host;
String msg_backup;


void tsCB(void* pvParameters) {
  _PP(millis()); _PP(": "); _PL("entered tsCB");
  Dictionary& pd = *pd_ptr;

  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(1);

  //=== setup section for TS ==================
  P_ptr = new MD_Parola(MD_MAX72XX::ICSTATION_HW, DATA_PIN, CLK_PIN, CS_PIN, pd["devices"].toInt());

  setupScreen();
  setupBlynk();
  startTasks();
#ifdef _TASK_SLEEP_ON_IDLE_RUN
  ts.allowSleep(false);
#endif

  //=== loop() section for TS ===================
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    ts.execute();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    taskYIELD();
  }
}

void setupScreen() {
  MD_Parola& P = *P_ptr;

  P.begin();
  
  MDCur.scrollIntensity = 0; // start low to prevent brownouts... MAX_INTENSITY / 2;
  MDCur.scrollAlign = PA_CENTER;
  MDCur.scrollSpeed = SS_STEADY;
  MDCur.scrollEffect = PA_PRINT;
  MDCur.scrollPause = 30000;
  MDBackup = MDCur;
  
  doDisplayText();
}

void doDisplayText() {
  Dictionary& pd = *pd_ptr;
  MD_Parola& P = *P_ptr;

  pd["msg"].toCharArray(msg, MAX_STRLEN);
  P.displayReset();
  P.displayClear();
  P.displayText(msg, MDCur.scrollAlign, MDCur.scrollSpeed, MDCur.scrollPause, MDCur.scrollEffect, MDCur.scrollEffect);
}

void startTasks() {
  tText.enable();
  tBlynk.enable();
}

void setupBlynk() {
  Dictionary& pd = *pd_ptr;

  _PL("Blynk parameters:");
  _PP("blynk_auth : "); _PL(pd["blynk_auth"].c_str());
  _PP("blynk_host : "); _PL(pd["blynk_host"].c_str());
  _PP("blynk_port : "); _PL(pd["blynk_port"].toInt());

  blynk_auth = String(pd["blynk_auth"]);
  blynk_host = String(pd["blynk_host"]);
  Blynk.config(blynk_auth.c_str(), blynk_host.c_str(), pd["blynk_port"].toInt());
}

void textCB() {
  MD_Parola& P = *P_ptr;
  if (P.displayAnimate()) {
    P.displayReset();
  }
}

bool active = false;
bool blinkOE() {
  active = false;
  return true;
}

void blinkCB() {
  MD_Parola& P = *P_ptr;

  if (active) P.setIntensity(0);
  else P.setIntensity(MDCur.scrollIntensity);
  active = !active;
}

void blinkOD() {
  MD_Parola& P = *P_ptr;

  P.setIntensity(MDCur.scrollIntensity);
}


bool clockOE() {
  MD_Parola& P = *P_ptr;
  Dictionary& pd = *pd_ptr;

  msg_backup = pd["msg"];
  MDBackup = MDCur;
  MDCur.scrollEffect = PA_PRINT;
  MDCur.scrollPause = 30000;
  MDCur.scrollSpeed = SS_STEADY;

  tBlink.disable();
  Blynk.virtualWrite(VFLASH, 0);
  Blynk.virtualWrite(VBLINK, 0);
  clockCB();
  return true;
}

void clockOD() {
  Dictionary& pd = *pd_ptr;

  MDCur = MDBackup;
  pd("msg", msg_backup);
  doDisplayText();
}


void clockCB() {
  char buf[16];
  Dictionary& pd = *pd_ptr;
  MD_Parola& P = *P_ptr;

  if ( tClock.getRunCounter() & 1 ) {
    sprintf( buf, "%02d:%02d:%02d ", hour(), minute(), second() );
  }
  else {
    sprintf( buf, "%02d:%02d:%02d.", hour(), minute(), second() );
  }
  pd("msg", buf);
  doDisplayText();
}
