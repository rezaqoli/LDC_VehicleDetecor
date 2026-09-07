#pragma once
class WebSocketsServer {
 public:
  int  connectedClients() { return 0; }
  bool sendTXT(uint8_t, const char *) { return true; }
  bool broadcastTXT(const char *) { return true; }
  void begin() {}
  void loop() {}
  void onEvent(void (*)(uint8_t, uint8_t, uint8_t *, size_t)) {}
};
