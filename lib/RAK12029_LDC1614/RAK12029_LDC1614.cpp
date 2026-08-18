/**
   @file RAK12029_LDC1614_Inductive.cpp
   @author rakwireless.com (modified by Grok for dual I2C bus support)
   @brief LDC1614 Inductive Sensor Library - Dual Bus Support
   @version 2.0
   @date 2025-11-15
   @copyright Copyright (c) 2021 RAKwireless, modified under MIT license
**/

#include "RAK12029_LDC1614.h"
#include <math.h>

// === آرایه‌های رشته برای چاپ وضعیت ===
const char *status_str[] = {
  "conversion under range error", "conversion over range error",
  "watch dog timeout error", "Amplitude High Error",
  "Amplitude Low Error", "Zero Count Error",
  "Data Ready", "unread conversion is present for channel 0",
  "unread conversion is present for Channel 1.",
  "unread conversion is present for Channel 2.",
  "unread conversion is present for Channel 3."
};

// === سازنده ===
RAK12029_LDC1614_Inductive::RAK12029_LDC1614_Inductive(u8 IIC_ADDR) {
  set_iic_addr(IIC_ADDR);
}

// === توابع I2C با آدرس پویا ===
s32 RAK12029_LDC1614_Inductive::IIC_write_byte(TwoWire &bus, u8 reg, u8 byte) {
  bus.beginTransmission(_IIC_ADDR);
  bus.write(reg);
  bus.write(byte);
  return bus.endTransmission();
}

s32 RAK12029_LDC1614_Inductive::IIC_write_16bit(TwoWire &bus, u8 reg, u16 value) {
  bus.beginTransmission(_IIC_ADDR);
  bus.write(reg);
  bus.write(highByte(value));
  bus.write(lowByte(value));
  return bus.endTransmission();
}

void RAK12029_LDC1614_Inductive::IIC_read_byte(TwoWire &bus, u8 reg, u8 *byte) {
  bus.beginTransmission(_IIC_ADDR);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) {
    *byte = 0x00;
    return;
  }
  if (bus.requestFrom(_IIC_ADDR, (u8)1) == 1) {
    *byte = bus.read();
  } else {
    *byte = 0x00;
  }
}

s32 RAK12029_LDC1614_Inductive::IIC_read_16bit(TwoWire &bus, u8 start_reg, u16 *value) {
  *value = 0;
  bus.beginTransmission(_IIC_ADDR);
  bus.write(start_reg);
  if (bus.endTransmission(false) != 0) return -1;
  if (bus.requestFrom(_IIC_ADDR, (u8)2) != 2) return -1;
  *value = (bus.read() << 8) | bus.read();
  return 0;  // موفقیت
}

// === توابع عمومی ===
void RAK12029_LDC1614_Inductive::LDC1614_read_sensor_infomation(TwoWire &bus) {
  u16 value = 0;
  IIC_read_16bit(bus, LDC1614_READ_MANUFACTURER_ID, &value);
  Serial.printf("Manufacturer ID: 0x%04X\n", value);
  IIC_read_16bit(bus, LDC1614_READ_DEVICE_ID, &value);
  Serial.printf("Device ID: 0x%04X\n", value);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_init(TwoWire &bus) {
  return LDC1614_reset_sensor(bus);  // فقط ریست
}

s32 RAK12029_LDC1614_Inductive::LDC1614_single_channel_config(TwoWire &bus, u8 channel, float inductance, float capacitance) {
  if (channel >= 4) {
    Serial.println("ERROR: Invalid channel!");
    return -2;
  }

  LDC1614_set_L(channel, inductance);
  LDC1614_set_C(channel, capacitance);

  if (LDC1614_set_FIN_LDC1614_Fref_DIV(bus, channel)) return -1;
  LDC1614_set_LC_stabilize_time(bus, channel);
  LDC1614_set_conversion_time(bus, channel, 0x0546);
  LDC1614_set_driver_current(bus, channel, 0xA000);
  LDC1614_set_mux_config(bus, 0x020C);  // تک کانال
  u16 config = 0x1401;
  LDC1614_select_channel_to_convert(channel, &config);
  LDC1614_set_sensor_config(bus, config);
  return 0;
}

s32 RAK12029_LDC1614_Inductive::LDC1614_mutiple_channel_config(TwoWire &bus, float inductance, float capacitance) {
  for (u8 ch = 0; ch < 4; ch++) {
    LDC1614_set_L(ch, inductance);
    LDC1614_set_C(ch, capacitance);
    if (LDC1614_set_FIN_LDC1614_Fref_DIV(bus, ch)) return -1;
    LDC1614_set_LC_stabilize_time(bus, ch);
    LDC1614_set_conversion_time(bus, ch, 0x0546);
    LDC1614_set_driver_current(bus, ch, 0xA000);
  }
  LDC1614_set_mux_config(bus, 0xC20C);  // 4 کانال
  LDC1614_set_sensor_config(bus, 0x1401);
  return 0;
}

s32 RAK12029_LDC1614_Inductive::LDC1614_parse_result_data(u8 channel, u32 raw_result, u32 *result) {
  *result = raw_result & 0x0FFFFFFF;
  if (*result == 0x0FFFFFFF) {
    //Serial.printf("CH%d: No coil detected!\n", channel);
    *result = 0;
    return -1;
  }
  u8 err = raw_result >> 28;
  if (err & 0x08) Serial.printf("CH%d: Under range\n", channel);
  if (err & 0x04) Serial.printf("CH%d: Over range\n", channel);
  if (err & 0x02) Serial.printf("CH%d: Watchdog timeout\n", channel);
  if (err & 0x01) Serial.printf("CH%d: Amplitude error\n", channel);
  return 0;
}

s32 RAK12029_LDC1614_Inductive::LDC1614_get_channel_result(TwoWire &bus, u8 channel, u32 *result) {
  if (!result || channel >= 4) return -1;
  u32 raw = 0;
  u16 msb, lsb;
  IIC_read_16bit(bus, LDC1614_CONVERTION_RESULT_REG_START + channel * 2, &msb);
  IIC_read_16bit(bus, LDC1614_CONVERTION_RESULT_REG_START + channel * 2 + 1, &lsb);
  raw = ((u32)msb << 16) | lsb;
  return LDC1614_parse_result_data(channel, raw, result);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_conversion_time(TwoWire &bus, u8 channel, u16 value) {
  return IIC_write_16bit(bus, LDC1614_SET_CONVERSION_TIME_REG_START + channel, value);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_conversion_offset(TwoWire &bus, u8 channel, u16 value) {
  return IIC_write_16bit(bus, LDC1614_SET_CONVERSION_OFFSET_REG_START + channel, value);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_LC_stabilize_time(TwoWire &bus, u8 channel) {
  return IIC_write_16bit(bus, LDC1614_SET_LC_STABILIZE_REG_START + channel, 30);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_FIN_LDC1614_Fref_DIV(TwoWire &bus, u8 channel) {
  float L = LDC1614_inductance[channel] * 1e-6;
  float C = LDC1614_capacitance[channel] * 1e-12;
  if (L == 0 || C == 0) return -1;

  LDC1614_Fsensor[channel] = 1e6 / (2 * M_PI * sqrt(L * C));

  u16 FIN_DIV = (u16)(LDC1614_Fsensor[channel] / 8.75f + 1.0f);
  u16 Fref_DIV = (LDC1614_Fsensor[channel] * 4 < 40) ? 2 : 4;
  LDC1614_Fref[channel] = 40.0f / Fref_DIV;

  u16 value = (FIN_DIV << 12) | Fref_DIV;
  return IIC_write_16bit(bus, LDC1614_SET_FREQ_REG_START + channel, value);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_ERROR_CONFIG(TwoWire &bus, u16 value) {
  return IIC_write_16bit(bus, LDC1614_ERROR_CONFIG_REG, value);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_sensor_config(TwoWire &bus, u16 value) {
  return IIC_write_16bit(bus, LDC1614_SENSOR_CONFIG_REG, value);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_mux_config(TwoWire &bus, u16 value) {
  return IIC_write_16bit(bus, LDC1614_MUL_CONFIG_REG, value);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_reset_sensor(TwoWire &bus) {
  return IIC_write_16bit(bus, LDC1614_SENSOR_RESET_REG, 0x8000);
}

s32 RAK12029_LDC1614_Inductive::LDC1614_set_driver_current(TwoWire &bus, u8 channel, u16 value) {
  return IIC_write_16bit(bus, LDC1614_SET_DRIVER_CURRENT_REG + channel, value);
}

void RAK12029_LDC1614_Inductive::LDC1614_select_channel_to_convert(u8 channel, u16 *value) {
  *value &= 0x3FFF;
  if (channel == 1) *value |= 0x4000;
  if (channel == 2) *value |= 0x8000;
  if (channel == 3) *value |= 0xC000;
}

void RAK12029_LDC1614_Inductive::LDC1614_set_Rp(u8 channel, float n_kom) {
  LDC1614_resistance[channel] = n_kom;
}

void RAK12029_LDC1614_Inductive::LDC1614_set_L(u8 channel, float n_uh) {
  LDC1614_inductance[channel] = n_uh;
}

void RAK12029_LDC1614_Inductive::LDC1614_set_C(u8 channel, float n_pf) {
  LDC1614_capacitance[channel] = n_pf;
}

void RAK12029_LDC1614_Inductive::LDC1614_set_Q_factor(u8 channel, float q) {
  LDC1614_Q_factor[channel] = q;
}

s32 RAK12029_LDC1614_Inductive::LDC1614_sensor_status_parse(u16 value) {
  u8 src = value >> 14;
  Serial.printf("Status Source: CH%d\n", src);

  for (int i = 0; i < 6; i++) {
    if (value & (1 << (8 + i))) {
      Serial.println(status_str[5 - i]);
    }
  }
  if (value & (1 << 6)) Serial.println(status_str[6]);
  for (int i = 0; i < 4; i++) {
    if (value & (1 << i)) {
      Serial.println(status_str[10 - i]);
    }
  }
  return 0;
}

u32 RAK12029_LDC1614_Inductive::LDC1614_get_sensor_status(TwoWire &bus) {
  u16 value = 0;
  IIC_read_16bit(bus, LDC1614_SENSOR_STATUS_REG, &value);
  LDC1614_sensor_status_parse(value);
  return value;
}