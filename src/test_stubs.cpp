// test_stubs.cpp — host-build stubs for symbols normally provided by .cpp
// files we exclude from the native test build (MqttHandler, LteModem, WsUtils).
#include "Globals.h"
#include "MqttHandler.h"
#include "WsUtils.h"
#include "LteModem.h"
#include <cstring>
#include <cstdio>

// From WsUtils
void wsSend(const char *)                              {}
void wsSendToClient(uint8_t, const char *)            {}
void wsBroadcast(const char *)                        {}
bool isChannelInEnabledSpeedPair(const char *)            { return false; }
void refreshDetectorLoopModes()                          {}
void applySpeedPairConfig(uint8_t, bool, float,
                          uint8_t, uint8_t, uint8_t, uint8_t) {}
void sendSpeedPairConfig(IReplyChannel &, uint8_t, uint8_t) {}
void sendAllSpeedPairConfigs(IReplyChannel &, uint8_t)    {}
void sendAllSpeedResults(IReplyChannel &, uint8_t)       {}

// From MqttHandler
char mqttClientId[32] = "ESP32_Vehicle_Detector";
char mqttServer[64]    = "iot.iolink.ir";
IPAddress mqttServerIp = IPAddress(0, 0, 0, 0);
uint16_t mqttPort      = 1883;
char mqttUser[32]      = "";
char mqttPass[32]      = "";
char mqttTopicEvents[64]            = "vehicles/events";
char mqttTopicCommands[64]          = "vehicles/commands";
char mqttTopicCommandResponses[64]  = "vehicles/command_responses";

volatile uint32_t mqttPubPublished    = 0;
volatile uint32_t mqttPubPublishedEvt = 0;
volatile uint32_t mqttPubPublishedRsp = 0;
volatile uint32_t mqttPubDropped      = 0;
volatile uint32_t mqttPubReconnect    = 0;
char             mqttPubLastErr[64]   = "none";

void  mqttInit()                                       {}
void  mqttPublishEvent(const char *)                  {}
void  mqttPublishResponse(const char *)               {}
void  mqttPublishCallback(char *, byte *, unsigned int){}
void  mqttCallback(char *, byte *, unsigned int)     {}
bool  mqttConnect()                                    { return false; }
void  taskMqttLoop(void *)                             {}
void  taskMqttPublisher(void *)                        {}
void  setLastErr(const char *s)
{
  if (!s) s = "none";
  strncpy(mqttPubLastErr, s, sizeof(mqttPubLastErr) - 1);
  mqttPubLastErr[sizeof(mqttPubLastErr) - 1] = '\0';
}

// From LteModem
char  lte_apn[32] = "shatelmobile";
bool  lteInitialized = false;
bool  lteGprsConnected = false;
bool  takeModem(uint32_t)         { return true; }
void  giveModem()                  {}
bool  modemMutexReady()            { return true; }
void  modemMutexInit()             {}
void  taskLTEInit(void *)          {}
void  taskLTEStatusMonitor(void *){}
void  taskLTECommandConsole(void *){}

// TimeManager stubs (header is in LteModem.h via TimeManager.h)
#include "TimeManager.h"
bool     isTimeSynced()      { return false; }
time_t   getEpochTime()      { return 0; }
time_t   getLastSyncTime()   { return 0; }
void     formatIsoTime(char *buf, size_t len, time_t t) { if (buf && len) buf[0] = '\0'; (void)t; }
void     getEventTimestamp(char *buf, size_t len)       { if (buf && len) buf[0] = '\0'; }
void     taskTimeSync(void *) {}

// GNSS stubs (declared in LteModem.h)
void          gnssInit()           {}
const GnssFix &gnssGetLastFix()    { static GnssFix f{}; return f; }
GnssState      gnssGetState()      { return GnssState::OFF; }
uint32_t       gnssGetLastRequestMs() { return 0; }
bool           gnssGetFix(GnssFix &) { return false; }
void           gnssShutdown()      {}
void           taskGnssIdleWatcher(void *) {}
