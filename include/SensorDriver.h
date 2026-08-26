// ============================================================
// SensorDriver.h  —  LDC1614 sensor configuration & reading
// ============================================================
#pragma once
#ifndef SENSOR_DRIVER_H
#define SENSOR_DRIVER_H
#include "Globals.h"

// Configure all 4 channels of a sensor (conversion time, driver current, LC)
template<typename I2C>
void configureSensor(I2C &bus, RAK12029_LDC1614_Inductive &ldc, ChannelLC &lc);

// Read all 4 channels from one sensor (thread-safe with mutex)
template<typename I2C>
void readSensorChannels(I2C &bus, SemaphoreHandle_t mtx,
                        RAK12029_LDC1614_Inductive &ldc, RawFrame &frame, uint8_t sensor);

#endif // SENSOR_DRIVER_H