// ============================================================
// WsCommandHandler.h  —  WebSocket command parser & event handler
// ============================================================
#pragma once

#include "Globals.h"

// WebSocket event handler (passed to webSocket.onEvent)
void webSocketEvent(uint8_t num, uint8_t type, uint8_t *payload, size_t len);
void processSystemCommand(const String &cmd, void (*replyFunc)(uint8_t num, const char *msg), uint32_t num);
