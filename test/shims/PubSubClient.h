#pragma once
class PubSubClient {
 public:
  PubSubClient(void *) {}
  void setServer(const char *, uint16_t) {}
  void setServer(uint32_t, uint16_t) {}
  void setCallback(void (*)(char *, uint8_t *, unsigned int)) {}
  void setSocketTimeout(int) {}
  void setBufferSize(int) {}
  bool connect(const char *, const char * = nullptr, const char * = nullptr) { return false; }
  bool connected() { return false; }
  bool publish(const char *, const char *) { return false; }
  void subscribe(const char *) {}
  void unsubscribe(const char *) {}
  void disconnect() {}
  void loop() {}
  int  state() { return -1; }
};
