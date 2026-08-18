/**
   @file RAK12029_LDC1614.h
   @author rakwireless.com (modified for dual I2C bus)
   @brief LDC1614 Inductive Sensor Library - Dual Bus Support
   @version 2.0
   @date 2025-11-15
**/

#ifndef _RAK12029_LDC1614_H
#define _RAK12029_LDC1614_H

#include <Wire.h>
#include <Arduino.h>
#include <cstdint>

// === نوع داده‌ها ===
using s32 = int32_t;
using u32 = uint32_t;
using s16 = int16_t;
using u16 = uint16_t;
using s8  = int8_t;
using u8  = uint8_t;

// === آدرس پیش‌فرض ===
#define DEFAULT_IIC_ADDRESS 0x2A

// === رجیسترها ===
#define LDC1614_CONVERTION_RESULT_REG_START     0x00
#define LDC1614_SET_CONVERSION_TIME_REG_START   0x08
#define LDC1614_SET_CONVERSION_OFFSET_REG_START 0x0C
#define LDC1614_SET_LC_STABILIZE_REG_START      0x10
#define LDC1614_SET_FREQ_REG_START              0x14
#define LDC1614_SENSOR_STATUS_REG               0x18
#define LDC1614_ERROR_CONFIG_REG                0x19
#define LDC1614_SENSOR_CONFIG_REG               0x1A
#define LDC1614_MUL_CONFIG_REG                  0x1B
#define LDC1614_SENSOR_RESET_REG                0x1C
#define LDC1614_SET_DRIVER_CURRENT_REG          0x1E
#define LDC1614_READ_MANUFACTURER_ID            0x7E
#define LDC1614_READ_DEVICE_ID                  0x7F

// === بیت‌های خطا ===
#define LDC1614_UR_ERR2OUT  ((u16)1 << 15)
#define LDC1614_OR_ERR2OUT  ((u16)1 << 14)
#define LDC1614_WD_ERR2OUT  ((u16)1 << 13)
#define LDC1614_AH_ERR2OUT  ((u16)1 << 12)
#define LDC1614_AL_ERR2OUT  ((u16)1 << 11)
#define LDC1614_UR_ERR2INT  ((u16)1 << 7)
#define LDC1614_OR_ERR2INT  ((u16)1 << 6)
#define LDC1614_WD_ERR2INT  ((u16)1 << 5)
#define LDC1614_AH_ERR2INT  ((u16)1 << 4)
#define LDC1614_AL_ERR2INT  ((u16)1 << 3)
#define LDC1614_ZC_ERR2INT  ((u16)1 << 2)
#define LDC1614_DRDY_2INT   ((u16)1 << 0)

// === تنظیمات سنسور ===
#define LDC1614_ACTIVE_CHANNEL      (((u16)1 << 15) | ((u16)1 << 14))
#define LDC1614_SLEEP_MODE_EN       ((u16)1 << 13)
#define LDC1614_RP_OVERRIDE_EN      ((u16)1 << 12)
#define LDC1614_SENSOR_ACTIVATE_SEL ((u16)1 << 11)
#define LDC1614_AUTO_AMP_DIS        ((u16)1 << 10)
#define LDC1614_REF_CLK_SRC         ((u16)1 << 9)
#define LDC1614_INTB_DIS            ((u16)1 << 7)
#define LDC1614_HIGH_CURRENT_DRV    ((u16)1 << 6)

// === کانال‌ها ===
#define LDC1614_CHANNEL_0  3
#define LDC1614_CHANNEL_1  2
#define LDC1614_CHANNEL_2  1
#define LDC1614_CHANNEL_3  0
#define LDC1614_CHANNEL_NUM 4

// === کلاس اصلی ===
class RAK12029_LDC1614_Inductive {
public:
    RAK12029_LDC1614_Inductive(u8 IIC_ADDR = DEFAULT_IIC_ADDRESS);
    ~RAK12029_LDC1614_Inductive() = default;

    // === توابع I2C (با باس دلخواه) ===
    s32  IIC_write_byte(TwoWire &bus, u8 reg, u8 byte);
    void IIC_read_byte(TwoWire &bus, u8 reg, u8 *byte);
    s32  IIC_read_16bit(TwoWire &bus, u8 reg, u16 *value);
    s32  IIC_write_16bit(TwoWire &bus, u8 reg, u16 value);

    // === توابع عمومی (با باس دلخواه) ===
    void LDC1614_read_sensor_infomation(TwoWire &bus);
    s32  LDC1614_init(TwoWire &bus);
    s32  LDC1614_get_channel_result(TwoWire &bus, u8 channel, u32 *result);
    s32  LDC1614_set_conversion_time(TwoWire &bus, u8 channel, u16 value);
    s32  LDC1614_set_LC_stabilize_time(TwoWire &bus, u8 channel);
    s32  LDC1614_set_conversion_offset(TwoWire &bus, u8 channel, u16 value);
    u32  LDC1614_get_sensor_status(TwoWire &bus);  // ویرگول حذف شد
    s32  LDC1614_set_ERROR_CONFIG(TwoWire &bus, u16 value);
    s32  LDC1614_set_sensor_config(TwoWire &bus, u16 value);
    s32  LDC1614_set_mux_config(TwoWire &bus, u16 value);
    s32  LDC1614_reset_sensor(TwoWire &bus);
    s32  LDC1614_set_driver_current(TwoWire &bus, u8 channel, u16 value);
    s32  LDC1614_set_FIN_LDC1614_Fref_DIV(TwoWire &bus, u8 channel);
    s32  LDC1614_single_channel_config(TwoWire &bus, u8 channel, float inductance, float capacitance);
    s32  LDC1614_mutiple_channel_config(TwoWire &bus, float inductance, float capacitance);

    // === توابع تنظیم داخلی (بدون باس) ===
    void LDC1614_select_channel_to_convert(u8 channel, u16 *value);  // بدون bus
    void LDC1614_set_Rp(u8 channel, float n_kom);                   // بدون bus
    void LDC1614_set_L(u8 channel, float n_uh);                     // بدون bus
    void LDC1614_set_C(u8 channel, float n_pf);                     // بدون bus
    void LDC1614_set_Q_factor(u8 channel, float q);                 // بدون bus

    void set_iic_addr(u8 addr) { _IIC_ADDR = addr; }

private:
    u8 _IIC_ADDR;

    // توابع داخلی
    s32 LDC1614_parse_result_data(u8 channel, u32 raw_result, u32 *result);
    s32 LDC1614_sensor_status_parse(u16 value);

    // مقادیر داخلی
    float LDC1614_resistance[LDC1614_CHANNEL_NUM];
    float LDC1614_inductance[LDC1614_CHANNEL_NUM];
    float LDC1614_capacitance[LDC1614_CHANNEL_NUM];
    float LDC1614_Fref[LDC1614_CHANNEL_NUM];
    float LDC1614_Fsensor[LDC1614_CHANNEL_NUM];
    float LDC1614_Q_factor[LDC1614_CHANNEL_NUM];
};

#endif // _RAK12029_LDC1614_H