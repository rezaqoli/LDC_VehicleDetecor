// ============================================================
// SmsManager.h  —  Outgoing + incoming SMS control
// ============================================================
#pragma once
#include <Arduino.h>
#include <stdint.h>

#define SMS_MAX_LEN         160
#define SMS_POLL_INTERVAL   5000
#define SMS_MAX_CONTACTS    4
#define SMS_CONTACT_LEN     20
#define SMS_PIN_LEN         8

struct SmsContact
{
  char  number[SMS_CONTACT_LEN];
  bool  enabled;
};

void   smsInit();
bool   smsSend(const char *number, const char *text);
bool   smsAddContact(const char *number);
bool   smsDelContact(const char *number);
void   smsClearContacts();
size_t smsContactCount();
bool   smsIsAllowed(const char *number);
bool   smsCheckPin(const char *body);  // returns true if body starts with the configured PIN

// diagnostics
uint32_t smsSentCount();
uint32_t smsRcvdCount();
uint32_t smsUnauthorizedCount();
const char *smsLastSender();
const char *smsLastBody();

// Iterate whitelist; returns the i-th enabled contact's number, or nullptr.
const char *smsGetContact(size_t index);

void   taskSmsService(void *);
