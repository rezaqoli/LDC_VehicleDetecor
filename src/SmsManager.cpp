// ============================================================
// SmsManager.cpp
//   - Whitelist of phone numbers persisted in NVS.
//   - Compile-time defaults from -DSMS_DEFAULT_CONTACTS="+98...,+98...".
//   - Compile-time PIN from -DSMS_PIN="1234".  Every incoming SMS
//     must start with that PIN (and a space) to be accepted.
//   - taskSmsService polls the inbox every SMS_POLL_INTERVAL ms.
// ============================================================
#include "SmsManager.h"
#include "LteModem.h"
#include "DetectionControl.h"
#include "MqttHandler.h"
#include "PowerMonitor.h"
#include "TimeManager.h"
#include "PersistentConfig.h"
#include "WsUtils.h"
#include <cstring>
#include <cstdlib>

#ifndef SMS_PIN
  #define SMS_PIN "1234"
#endif

static char              s_pin[SMS_PIN_LEN] = SMS_PIN;
static SmsContact        s_contacts[SMS_MAX_CONTACTS];
static char              s_lastSender[SMS_CONTACT_LEN] = "";
static char              s_lastBody[SMS_MAX_LEN + 1]  = "";
static volatile uint32_t s_sentCount  = 0;
static volatile uint32_t s_rcvdCount  = 0;
static volatile uint32_t s_unauthCount = 0;

void smsInit()
{
  memset(s_contacts, 0, sizeof(s_contacts));

  // Load whitelist from NVS.
  char buf[256] = "";
  if (PersistentConfig::loadSmsContacts(buf, sizeof(buf)) && buf[0])
  {
    int n = 0;
    char *tok = strtok(buf, "|");
    while (tok && n < SMS_MAX_CONTACTS)
    {
      strncpy(s_contacts[n].number, tok, SMS_CONTACT_LEN - 1);
      s_contacts[n].number[SMS_CONTACT_LEN - 1] = '\0';
      s_contacts[n].enabled = true;
      n++;
      tok = strtok(NULL, "|");
    }
    Serial.printf("[SMS] Loaded %d contact(s) from NVS\n", n);
  }
#ifdef SMS_DEFAULT_CONTACTS
  else
  {
    // Fall back to compile-time default list.
    const char *def = SMS_DEFAULT_CONTACTS;
    int n = 0;
    char tmp[256];
    strncpy(tmp, def, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
    char *tok = strtok(tmp, ",");
    while (tok && n < SMS_MAX_CONTACTS)
    {
      while (*tok == ' ') tok++;
      strncpy(s_contacts[n].number, tok, SMS_CONTACT_LEN - 1);
      s_contacts[n].number[SMS_CONTACT_LEN - 1] = '\0';
      s_contacts[n].enabled = true;
      n++;
      tok = strtok(NULL, ",");
    }
    Serial.printf("[SMS] Loaded %d default contact(s) from build flag\n", n);
    // Persist for next boot.
    char joined[256] = "";
    for (int i = 0; i < n; i++)
    {
      if (i) strncat(joined, "|", sizeof(joined) - strlen(joined) - 1);
      strncat(joined, s_contacts[i].number, sizeof(joined) - strlen(joined) - 1);
    }
    PersistentConfig::saveSmsContacts(joined);
  }
#endif
  Serial.printf("[SMS] Init done. PIN length=%u, contacts=%u\n",
                (unsigned)strlen(s_pin), (unsigned)smsContactCount());
}

size_t smsContactCount()
{
  size_t n = 0;
  for (size_t i = 0; i < SMS_MAX_CONTACTS; i++) if (s_contacts[i].enabled) n++;
  return n;
}

bool smsIsAllowed(const char *number)
{
  if (!number) return false;
  for (size_t i = 0; i < SMS_MAX_CONTACTS; i++)
  {
    if (s_contacts[i].enabled && strcmp(s_contacts[i].number, number) == 0)
      return true;
  }
  return false;
}

bool smsCheckPin(const char *body)
{
  if (!body) return false;
  size_t pl = strlen(s_pin);
  if (strncmp(body, s_pin, pl) == 0 && (body[pl] == ' ' || body[pl] == '\0'))
    return true;
  return false;
}

static void persistContacts()
{
  char joined[256] = "";
  for (int i = 0; i < SMS_MAX_CONTACTS; i++)
  {
    if (!s_contacts[i].enabled) continue;
    if (joined[0]) strncat(joined, "|", sizeof(joined) - strlen(joined) - 1);
    strncat(joined, s_contacts[i].number, sizeof(joined) - strlen(joined) - 1);
  }
  PersistentConfig::saveSmsContacts(joined);
}

bool smsAddContact(const char *number)
{
  if (!number || !*number) return false;
  for (int i = 0; i < SMS_MAX_CONTACTS; i++)
  {
    if (s_contacts[i].enabled && strcmp(s_contacts[i].number, number) == 0)
      return true;  // already there
  }
  for (int i = 0; i < SMS_MAX_CONTACTS; i++)
  {
    if (!s_contacts[i].enabled)
    {
      strncpy(s_contacts[i].number, number, SMS_CONTACT_LEN - 1);
      s_contacts[i].number[SMS_CONTACT_LEN - 1] = '\0';
      s_contacts[i].enabled = true;
      persistContacts();
      return true;
    }
  }
  return false;  // full
}

bool smsDelContact(const char *number)
{
  if (!number) return false;
  for (int i = 0; i < SMS_MAX_CONTACTS; i++)
  {
    if (s_contacts[i].enabled && strcmp(s_contacts[i].number, number) == 0)
    {
      s_contacts[i].enabled = false;
      s_contacts[i].number[0] = '\0';
      persistContacts();
      return true;
    }
  }
  return false;
}

void smsClearContacts()
{
  for (int i = 0; i < SMS_MAX_CONTACTS; i++) s_contacts[i].enabled = false;
  persistContacts();
}

uint32_t smsSentCount()        { return s_sentCount; }
uint32_t smsRcvdCount()        { return s_rcvdCount; }
uint32_t smsUnauthorizedCount(){ return s_unauthCount; }
const char *smsLastSender()    { return s_lastSender; }
const char *smsLastBody()      { return s_lastBody; }

const char *smsGetContact(size_t index)
{
  size_t seen = 0;
  for (size_t i = 0; i < SMS_MAX_CONTACTS; i++)
  {
    if (!s_contacts[i].enabled) continue;
    if (seen == index) return s_contacts[i].number;
    seen++;
  }
  return nullptr;
}

bool smsSend(const char *number, const char *text)
{
  if (!number || !text) return false;
  if (!takeModem(30000))
  {
    Serial.println("[SMS] Modem busy for send");
    return false;
  }
  bool ok = modem.sendSMS(number, text);
  giveModem();
  if (ok)
  {
    s_sentCount++;
    Serial.printf("[SMS] Sent to %s (%u bytes)\n", number, (unsigned)strlen(text));
  }
  else
  {
    Serial.printf("[SMS] sendSMS failed for %s\n", number);
  }
  return ok;
}

// ---------------------------------------------------------
// Inbox polling
// ---------------------------------------------------------
// Read +CMGR response and extract the sender phone number and the message body.
// Returns true on a valid reply.
static bool readSmsAt(uint8_t index, char *outSender, size_t senderLen,
                                       char *outBody, size_t bodyLen)
{
  if (!takeModem(8000)) return false;

  modem.sendAT(GF("+CMGR="), String(index));
  if (modem.waitResponse(8000L, GF(AT_NL "+CMGR: ")) != 1)
  {
    giveModem();
    return false;
  }

  // Format: +CMGR: "<status>","<sender>",<length>\r\n<body>\r\nOK\r\n
  // After the "+CMGR: " prefix, the rest of the header is on the same line.
  String header = modem.stream.readStringUntil('\n');
  header.trim();
  // Drop the leading "+CMGR: " if waitResponse left it.
  int colon = header.indexOf("+CMGR:");
  if (colon >= 0) header = header.substring(colon + 6);
  header.trim();

  // Pull sender between the 1st and 2nd pair of quotes.
  int q1 = header.indexOf('"');
  int q2 = (q1 >= 0) ? header.indexOf('"', q1 + 1) : -1;
  int q3 = (q2 >= 0) ? header.indexOf('"', q2 + 1) : -1;
  int q4 = (q3 >= 0) ? header.indexOf('"', q3 + 1) : -1;
  if (q1 < 0 || q4 < 0 || q4 <= q3)
  {
    giveModem();
    return false;
  }
  String sender = header.substring(q3 + 1, q4);
  strncpy(outSender, sender.c_str(), senderLen - 1);
  outSender[senderLen - 1] = '\0';

  // Body is the next line.
  String body = modem.stream.readStringUntil('\n');
  body.trim();
  modem.waitResponse(2000L);
  giveModem();

  strncpy(outBody, body.c_str(), bodyLen - 1);
  outBody[bodyLen - 1] = '\0';
  return true;
}

static void deleteSmsAt(uint8_t index)
{
  if (!takeModem(5000)) return;
  modem.sendAT(GF("+CMGD="), String(index));
  modem.waitResponse(3000L);
  giveModem();
}

static uint8_t countInbox()
{
  // Returns number of messages currently in storage.
  if (!takeModem(5000)) return 0;
  modem.sendAT(GF("+CPMS?"));
  int8_t rsp = modem.waitResponse(3000L, GF("+CPMS: "));
  if (rsp != 1) { giveModem(); return 0; }
  // Format: +CPMS: "MT",<u1>,<t1>,"SM",<u2>,<t2>,"ME",<u3>,<t3>\r\n
  String line = modem.stream.readStringUntil('\n');
  modem.waitResponse(2000L);
  giveModem();

  // Count occurrences of "<n>,"<storage>" then take the next integer.
  // Simplest: find the third used-count by walking commas.
  int commas = 0;
  int lastComma = -1;
  for (int i = 0; i < (int)line.length(); i++)
  {
    if (line.charAt(i) == ',')
    {
      commas++;
      lastComma = i;
    }
  }
  if (commas < 8) return 0;  // malformed

  // Find the comma before the third storage used-count.
  // Walk the string and split by commas, looking for the third quoted storage.
  int fields[16] = {0};
  int fieldCount = 0;
  int start = 0;
  for (int i = 0; i <= (int)line.length() && fieldCount < 16; i++)
  {
    if (i == (int)line.length() || line.charAt(i) == ',')
    {
      fields[fieldCount++] = start;
      start = i + 1;
    }
  }
  // Fields: 0="MT", 1=u1, 2=t1, 3="SM", 4=u2, 5=t2, 6="ME", 7=u3, 8=t3
  if (fieldCount < 8) return 0;
  int used = atoi(line.c_str() + fields[7]);
  return (used > 0) ? (uint8_t)used : 0;
  (void)lastComma;  // keep compiler happy
}

// ---------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------
static void handleSmsCommand(const char *sender, const char *body)
{
  // body is the message *without* the PIN prefix.
  String cmd = String(body);
  cmd.trim();
  cmd.toUpperCase();
  Serial.printf("[SMS] cmd from %s: %s\n", sender, cmd.c_str());

  if (cmd == "STOP")
  {
    detectionSetPaused(true);
    mqttPublishEvent("DETECTION|paused:1|by:sms");
    smsSend(sender, "Detection PAUSED");
  }
  else if (cmd == "START" || cmd == "RESUME")
  {
    detectionSetPaused(false);
    mqttPublishEvent("DETECTION|paused:0|by:sms");
    smsSend(sender, "Detection RESUMED");
  }
  else if (cmd == "STATUS")
  {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "%s|LTE:%s|MQTT:%s|PAUSED:%s",
             detectionStateName(),
             lteGprsConnected ? "UP" : "DOWN",
             "?",   // (PubSubClient state isn't exposed cheaply; we keep it short)
             detectionIsPaused() ? "1" : "0");
    smsSend(sender, msg);
  }
  else if (cmd == "GET_POWER")
  {
    const PowerReadings &p = powerMonitorGet();
    char msg[96];
    snprintf(msg, sizeof(msg), "BAT:%.2fV SOL:%.2fV %s",
             p.battery_v, p.solar_v, p.battery_low ? "LOW" : "OK");
    smsSend(sender, msg);
  }
  else if (cmd == "GET_GPS")
  {
    GnssFix fix;
    if (gnssGetFix(fix))
    {
      char msg[160];
      snprintf(msg, sizeof(msg),
               "LAT:%.5f LON:%.5f ALT:%.0fm SATS:%u ACC:%.0fm %s",
               fix.latitude, fix.longitude, fix.altitude_m,
               fix.satellites, fix.accuracy_m, fix.timestamp);
      smsSend(sender, msg);
    }
    else
    {
      smsSend(sender, "No GPS fix yet");
    }
  }
  else if (cmd == "GET_TIME")
  {
    char iso[40] = "";
    if (isTimeSynced()) formatIsoTime(iso, sizeof(iso), 0);
    smsSend(sender, iso[0] ? iso : "Time not synced");
  }
  else if (cmd == "SAVE_ALL")
  {
    PersistentConfig::saveAllConfigs();
    smsSend(sender, "SAVED");
  }
  else
  {
    // Unknown — silently ignored (per design).
    Serial.printf("[SMS] unknown command ignored: %s\n", cmd.c_str());
  }
}

void taskSmsService(void *)
{
  Serial.println("[SMS] Service task started");

  // Wait for LTE to be initialised.
  while (!lteInitialized)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  // Configure text mode + storage + new-message URC.
  if (takeModem(8000))
  {
    modem.sendAT(GF("+CMGF=1"));
    modem.waitResponse(2000L);
    modem.sendAT(GF("+CPMS=\"ME\",\"ME\",\"ME\""));
    modem.waitResponse(2000L);
    modem.sendAT(GF("+CNMI=2,1,0,0,0"));
    modem.waitResponse(2000L);
    giveModem();
  }

  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(SMS_POLL_INTERVAL));
    if (!lteInitialized || !lteGprsConnected) continue;

    uint8_t cnt = countInbox();
    if (cnt == 0) continue;

    // Read from index 1 up to cnt. (Storage is sparse; we keep going until we've
    // successfully read and deleted every entry.)
    for (uint8_t i = 1; i <= SMS_MAX_CONTACTS * 2 && i <= 30; i++)
    {
      char sender[SMS_CONTACT_LEN] = "";
      char body[SMS_MAX_LEN + 1]   = "";
      if (!readSmsAt(i, sender, sizeof(sender), body, sizeof(body)))
        continue;
      if (sender[0] == '\0') continue;  // slot empty

      deleteSmsAt(i);
      s_rcvdCount++;

      strncpy(s_lastSender, sender, sizeof(s_lastSender) - 1);
      s_lastSender[sizeof(s_lastSender) - 1] = '\0';
      strncpy(s_lastBody, body, sizeof(s_lastBody) - 1);
      s_lastBody[sizeof(s_lastBody) - 1] = '\0';

      // wsBroadcast so dashboards see the activity.
      char line[220];
      snprintf(line, sizeof(line), "SMS|from:%s|body:%s", sender, body);
      wsBroadcast(line);

      if (!smsIsAllowed(sender))
      {
        s_unauthCount++;
        Serial.printf("[SMS] Dropped (unauthorised): %s\n", sender);
        continue;
      }
      if (!smsCheckPin(body))
      {
        s_unauthCount++;
        Serial.printf("[SMS] Dropped (bad PIN): %s\n", sender);
        continue;
      }

      // Strip the PIN prefix and dispatch.
      size_t pl = strlen(s_pin);
      const char *p = body + pl;
      while (*p == ' ') p++;
      handleSmsCommand(sender, p);
    }
  }
}
