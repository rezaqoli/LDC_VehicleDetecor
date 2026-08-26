#pragma once

#include "Config.h"

#include <TinyGsmClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern TinyGsm modem;
extern TinyGsmClient lteClient;
extern SemaphoreHandle_t modemMutex;
extern bool lteInitialized;
extern bool lteGprsConnected;

void taskLTEInit(void *);
void taskLTEStatusMonitor(void *);
void taskLTECommandConsole(void *);
