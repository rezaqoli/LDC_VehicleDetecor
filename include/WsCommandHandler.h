// ============================================================
// WsCommandHandler.h  —  WebSocket command parser & event handler
// ============================================================
#pragma once

#include "Globals.h"
#include "ReplyChannel.h"

// WebSocket event handler (passed to webSocket.onEvent)
void webSocketEvent(uint8_t num, uint8_t type, uint8_t *payload, size_t len);
void processSystemCommand(const String &cmd, IReplyChannel &reply, uint8_t num);
