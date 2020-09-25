
//==== MJPEG Code =============================================
TaskHandle_t tCam, tStream;
SemaphoreHandle_t frameSync = NULL;
QueueHandle_t streamingClients;

extern Scheduler ts;

WebServer server(80);

const int FPS = 25;
const int WSINTERVAL = 50;

void mjpegCB(void* pvParameters) {
  _PP(millis()); _PP(": "); _PL("entered mjpegCB");
  _PP("Heap at mjpegCB = "); _PL(ESP.getFreeHeap());

  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(WSINTERVAL);

  frameSync = xSemaphoreCreateBinary();
  if (frameSync == NULL) {
    reportError("E:fr sem"); halt();
  }

  streamingClients = xQueueCreate( 10, sizeof(WiFiClient*) );
  if (streamingClients == NULL) {
    reportError("E:stream Q"); halt();
  }

  xSemaphoreGive( frameSync );
  //=== setup section  ==================
  setupCam();

  xTaskCreatePinnedToCore(
    camCB,
    "cam",
    4096,
    NULL,
    2,
    &tCam,
    APP_CPU);

  xTaskCreatePinnedToCore(
    streamCB,
    "strmCB",
    4096,
    NULL, //(void*) handler,
    2,
    &tStream,
//    APP_CPU);
    PRO_CPU);

  server.on("/mjpeg/1", HTTP_GET, handleJPGSstream);
  server.on("/jpg", HTTP_GET, handleJPG);
  server.onNotFound(handleNotFound);
  server.begin();

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    server.handleClient();
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

volatile size_t camSize;
volatile char* camBuf;

void camCB(void* pvParameters) {
  _PP(millis()); _PP(": "); _PL("entered camCB");

  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(1000 / FPS);
  portMUX_TYPE xSemaphore = portMUX_INITIALIZER_UNLOCKED;

  char* fbs[2] = { NULL, NULL };
  size_t fSize[2] = { 0, 0 };
  int ifb = 0;

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {
    cam.run();
    size_t s = cam.getSize();
    if (s > fSize[ifb]) {
      fSize[ifb] = s * 4 / 3;
      fbs[ifb] = allocateMemory(fbs[ifb], fSize[ifb]);
    }
    char* b = (char*) cam.getfb();
    memcpy(fbs[ifb], b, s);

    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    xSemaphoreTake( frameSync, portMAX_DELAY );

    portENTER_CRITICAL(&xSemaphore);

    camBuf = fbs[ifb];
    camSize = s;
    ifb++;
    ifb &= 1;

    portEXIT_CRITICAL(&xSemaphore);

    xSemaphoreGive( frameSync );

    xTaskNotifyGive( tStream );
    taskYIELD();

    if ( eTaskGetState( tStream ) == eSuspended ) {
      _PL("Suspending camCB");
#ifdef _TASK_SLEEP_ON_IDLE_RUN
      ts.allowSleep(true);
#endif
      vTaskSuspend(NULL);
#ifdef _TASK_SLEEP_ON_IDLE_RUN
      ts.allowSleep(false);
#endif
      _PL("Woke up camCB");
    }
  }
}

char* allocateMemory(char* aPtr, size_t aSize) {
  _PP(millis()); _PP(": "); _PL("entered allocateMemory");

  if (aPtr != NULL) free(aPtr);

  size_t freeHeap = ESP.getFreeHeap();
  char* ptr = NULL;

  _PP("Asking for "); _PP(aSize); _PL(" bytes");
  _PP("Available heap "); _PP(freeHeap); _PL( " bytes");


  if ( aSize > freeHeap * 2 / 3 ) {
    _PL("Trying PSRAM");
    if ( psramFound() && ESP.getFreePsram() > aSize ) {
      ptr = (char*) ps_malloc(aSize);
    }
  }
  else {
    _PL("Trying Heap");
    ptr = (char*) malloc(aSize);
    if ( ptr == NULL && psramFound() && ESP.getFreePsram() > aSize) {
      _PL("Trying PSRAM after Heap failed");
      ptr = (char*) ps_malloc(aSize);
    }
  }
  if (ptr == NULL) {
    reportError("E:Out of mem"); halt();
  }
  return ptr;
}



// ==== STREAMING ======================================================
const char HEADER[] = "HTTP/1.1 200 OK\r\n" \
                      "Access-Control-Allow-Origin: *\r\n" \
                      "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";
const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
const int hdrLen = strlen(HEADER);
const int bdrLen = strlen(BOUNDARY);
const int cntLen = strlen(CTNTTYPE);


void handleJPGSstream(void)
{
  _PP(millis()); _PP(": "); _PL("entered handleJPGSstream");

  if ( !uxQueueSpacesAvailable(streamingClients) ) return;

  WiFiClient* client = new WiFiClient;
  *client = server.client();
  client->write(HEADER, hdrLen);
  client->write(BOUNDARY, bdrLen);

  xQueueSend(streamingClients, (void *) &client, 0);
  if ( eTaskGetState( tCam ) == eSuspended ) vTaskResume( tCam );
  if ( eTaskGetState( tStream ) == eSuspended ) vTaskResume( tStream );
}


void streamCB(void * pvParameters) {
  _PP(millis()); _PP(": "); _PL("entered streamCB");

  char buf[16];
  TickType_t xLastWakeTime;
  TickType_t xFrequency;

  ulTaskNotifyTake( pdTRUE,          /* Clear the notification value before exiting. */
                    portMAX_DELAY ); /* Block indefinitely. */
  _PP(millis()); _PP(": "); _PL("streamCB activated");

  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    xFrequency = pdMS_TO_TICKS(1000 / FPS);

    UBaseType_t activeClients = uxQueueMessagesWaiting(streamingClients);
    if ( activeClients ) {
      xFrequency /= activeClients;

      WiFiClient *client;
      xQueueReceive (streamingClients, (void*) &client, 0);

      if (!client->connected()) {
        _PP("Client "); _PP((uint32_t)client); _PL(" disconnected");
        delete client;
      }
      else {
        xSemaphoreTake( frameSync, portMAX_DELAY );

        client->write(CTNTTYPE, cntLen);
        sprintf(buf, "%d\r\n\r\n", camSize);
        client->write(buf, strlen(buf));
        client->write((char*) camBuf, (size_t)camSize);
        client->write(BOUNDARY, bdrLen);

        xQueueSend(streamingClients, (void *) &client, 0);

        xSemaphoreGive( frameSync );
        taskYIELD();
      }
    }
    else {
      _PL("Suspending streamCB");
      vTaskSuspend(NULL);
      _PL("Woke up streamCB");
    }
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}


const char JHEADER[] = "HTTP/1.1 200 OK\r\n" \
                       "Content-disposition: inline; filename=capture.jpg\r\n" \
                       "Content-type: image/jpeg\r\n\r\n";
const int jhdLen = strlen(JHEADER);

void handleJPG(void)
{
  WiFiClient client = server.client();

  if (!client.connected()) return;
  client.write(JHEADER, jhdLen);
  client.write((char*)cam.getfb(), cam.getSize());
}

void handleNotFound()
{
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text / plain", message);
}




void setupCam() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame parameters
  //  config.frame_size = FRAMESIZE_UXGA;
  //  config.frame_size = FRAMESIZE_XGA;
  //  config.frame_size = FRAMESIZE_SVGA;
  //  config.frame_size = FRAMESIZE_VGA;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;

  esp_err_t rc = cam.init(config);
  if (rc != ESP_OK) {
    reportError("E:Cam init"); halt();
  }
}
