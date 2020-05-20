
// ==== BLYNK ===================


BLYNK_CONNECTED() {
  Blynk.virtualWrite(VFLASH, 0);
  Blynk.virtualWrite(VTURNOFF, 0);
  Blynk.virtualWrite(VCLOCK, 0);
  Blynk.syncAll();
  rtc.begin();
}

BLYNK_APP_CONNECTED() {
  tBlynk.setInterval(100 * TASK_MILLISECOND);
  Blynk.virtualWrite(VFLASH, 0);
  Blynk.virtualWrite(VTURNOFF, 0);
}


BLYNK_APP_DISCONNECTED() {
  tBlynk.setInterval(1000 * TASK_MILLISECOND);
}


void blynkCB() {
  Blynk.run();
}


BLYNK_WRITE(VTEXT) {
  Dictionary& pd = *pd_ptr;
  //  Parameters& pp = *pp_ptr;
  _PP("VTEXT: "); _PL(param.asStr());
  pd("msg", param.asStr());
  doDisplayText();

  tClock.disable();
  Blynk.virtualWrite(VCLOCK, 0);
}

BLYNK_WRITE(VSCROLL) {
  MD_Parola& P = *P_ptr;

  if (tClock.isEnabled()) return;

  int p = param.asInt();
  MDCur.scrollPause = 0;
  switch (p) {
    case 1:
      MDCur.scrollEffect = PA_SCROLL_UP;
      P.setSpeed(MDCur.scrollSpeed);
      break;

    case 2:
      MDCur.scrollEffect = PA_SCROLL_LEFT;
      P.setSpeed(MDCur.scrollSpeed);
      break;

    case 3:
      MDCur.scrollEffect = PA_PRINT;
      MDCur.scrollPause = 30000;
      P.setSpeed(SS_STEADY);
      doDisplayText();
      return;
      break;

    case 4:
      MDCur.scrollEffect = PA_SCROLL_RIGHT;
      P.setSpeed(MDCur.scrollSpeed);
      break;

    case 5:
      MDCur.scrollEffect = PA_SCROLL_DOWN;
      P.setSpeed(MDCur.scrollSpeed);
      break;
  }
  doDisplayText();
}

BLYNK_WRITE(VSPEED) {
  MD_Parola& P = *P_ptr;

  if (tClock.isEnabled()) return;

  int p = constrain(param.asInt(), 0, 100);  // expect 0-100
  MDCur.scrollSpeed = map(p, 0, 100, 150, 5); // map slider value to text scroll speed

  if (!(MDCur.scrollEffect == PA_PRINT || MDCur.scrollEffect == PA_NO_EFFECT))
    P.setSpeed(MDCur.scrollSpeed);  // Set new text scroll speed
}

BLYNK_WRITE(VBLINK) {
  if (tClock.isEnabled()) return;

  int p = param.asInt();
  if (p) tBlink.restart();
  else tBlink.disable();
}


BLYNK_WRITE(VFLASH) {
  if (tClock.isEnabled()) return;

  int p = param.asInt();
  digitalWrite(FLASHPIN, p == 1 ? HIGH : LOW);
}


BLYNK_WRITE(VBRIGHT) {
  MD_Parola& P = *P_ptr;

  int p = constrain(param.asInt(), 1, 15);
  P.setIntensity(p);
  MDCur.scrollIntensity = p;
}


BLYNK_WRITE(VTURNOFF) {
  MD_Parola& P = *P_ptr;

  _PP("VTURNOFF: "); _PL(param.asStr());

  int p = param.asInt();

  if (p == 10) {
    vTaskSuspend(tMjpeg);
    vTaskSuspend(tCam);
    vTaskSuspend(tStream);
    P.displayClear();
    P.displayShutdown(true);
    rtc_gpio_isolate((gpio_num_t) FLASHPIN);
    esp_wifi_stop();
    esp_deep_sleep_start();
  }
  Blynk.virtualWrite(VTURNOFF, 0);
}

BLYNK_WRITE(VCLOCK) {
  int p = param.asInt();
  if (p == 1) {
    tClock.restart();
  }
  else {
    tClock.disable();
  }
}
