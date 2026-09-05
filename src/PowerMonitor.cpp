// ============================================================
// PowerMonitor.cpp
//   - Reads battery + solar voltages every POWER_PERIOD_MS.
//   - Publishes a per-minute MQTT event (POWER|...).
//   - Emits BATTERY_LOW / BATTERY_OK transition events + WS broadcast.
// ============================================================
#include "PowerMonitor.h"
#include "Config.h"
#include "MqttHandler.h"
#include "WsUtils.h"

static PowerReadings s_power = {0.0f, 0.0f, false, 0, false};

const PowerReadings &powerMonitorGet()
{
  return s_power;
}

void powerMonitorInit()
{
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  s_power.battery_v     = 0.0f;
  s_power.solar_v       = 0.0f;
  s_power.battery_low   = false;
  s_power.sampled_at_ms = 0;
  s_power.valid         = false;
}

static float readVoltage(int pin, float r1, float r2)
{
  uint32_t sum = 0;
  for (int i = 0; i < POWER_SAMPLE_AVG; i++)
  {
    sum += analogReadMilliVolts(pin);
    delayMicroseconds(200);
  }
  float v_adc  = (sum / (float)POWER_SAMPLE_AVG) / 1000.0f;
  float v_real = v_adc * ((r1 + r2) / r2);
  return v_real;
}

static void publishPowerEvent(bool low)
{
  char msg[96];
  snprintf(msg, sizeof(msg),
           "POWER|bat:%.2f|sol:%.2f|low:%d",
           s_power.battery_v, s_power.solar_v, low ? 1 : 0);
  mqttPublishEvent(msg);
}

static void announceLowTransition(bool nowLow)
{
  char msg[96];
  if (nowLow)
  {
    snprintf(msg, sizeof(msg),
             "BATTERY_LOW|bat:%.2f|min:%.2f",
             s_power.battery_v, BATTERY_LOW_V);
  }
  else
  {
    snprintf(msg, sizeof(msg),
             "BATTERY_OK|bat:%.2f",
             s_power.battery_v);
  }
  // Serial + WS broadcast (so dashboards light up) + MQTT event.
  Serial.printf("[POWER] %s\n", msg);
  wsBroadcast(msg);
  mqttPublishEvent(msg);
}

void taskPowerMonitor(void *)
{
  Serial.println("[POWER] Monitor task started");
  vTaskDelay(pdMS_TO_TICKS(5000));  // let ADC reference stabilise after boot

  for (;;)
  {
    float battery = readVoltage(BATTERY_PIN, VOLTAGE_DIVIDER_R1, VOLTAGE_DIVIDER_R2);
    float solar   = readVoltage(SOLAR_PIN,   VOLTAGE_DIVIDER_R1, VOLTAGE_DIVIDER_R2);

    s_power.battery_v     = battery;
    s_power.solar_v       = solar;
    s_power.battery_low   = (battery < BATTERY_LOW_V);
    s_power.sampled_at_ms = millis();
    s_power.valid         = true;

    Serial.printf("[POWER] Battery: %.2f V   Solar: %.2f V   %s\n",
                  battery, solar, s_power.battery_low ? "LOW!" : "ok");

    publishPowerEvent(s_power.battery_low);

    // Hysteresis for state-transition events.
    static bool wasLow = false;
    if (s_power.battery_low && !wasLow)
    {
      announceLowTransition(true);
      wasLow = true;
    }
    else if (!s_power.battery_low && wasLow && battery > BATTERY_OK_V)
    {
      announceLowTransition(false);
      wasLow = false;
    }

    vTaskDelay(pdMS_TO_TICKS(POWER_PERIOD_MS));
  }
}
