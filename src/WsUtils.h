// ============================================================
// WsUtils.h  —  WebSocket utilities, speed pair config helpers
// ============================================================
#pragma once
#ifndef WS_UTILS_H
#define WS_UTILS_H
#include "Globals.h"

// ============================================================
// WebSocket TX (pooled, non-blocking)
// ============================================================
void wsSend(const char *msg);
void wsSendToClient(uint8_t num, const char *msg);
void wsBroadcast(const char *msg);
void wsLoop();
void sendCombinedWebSocketData(const SensorDataSnapshot &data);

// ============================================================
// Loop/Channel ID Formatting
// ============================================================
void formatLoopChannelId(uint8_t sensor, uint8_t ch, char *out, size_t outSize);

// ============================================================
// Speed Pair Configuration Management
// ============================================================
bool isChannelInEnabledSpeedPair(const char *channelId);
void refreshDetectorLoopModes();
void applySpeedPairConfig(uint8_t idx, bool enabled, float distance,
                          uint8_t sensor1, uint8_t ch1, uint8_t sensor2, uint8_t ch2);
void sendSpeedPairConfig(uint8_t num, uint8_t idx);
void sendAllSpeedPairConfigs(uint8_t num);
void sendAllSpeedResults(uint8_t num);
#endif // WS_UTILS_H
