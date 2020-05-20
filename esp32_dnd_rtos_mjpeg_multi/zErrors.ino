void reportError(String aError) {
  Dictionary& pd = *pd_ptr;

  _PP(millis()); _PP(": "); _PL("entered reportError");
  pd("msg", aError);
  MDCur.scrollEffect = PA_SCROLL_LEFT;
  P_ptr->setSpeed(20);
  P_ptr->setIntensity(7);
  doDisplayText();
  tBlink.restart();
  tBlynk.disable();
  vTaskSuspend(tMjpeg);
  vTaskSuspend(tCam);
  vTaskSuspend(tStream);
}

void halt() {
  for (;;) taskYIELD();
}
