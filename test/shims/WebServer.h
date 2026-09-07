// Minimal WebServer.h stub
#pragma once
#include <functional>
class WebServer {
 public:
  void on(const char *, std::function<void()>) {}
  void send(int, const char *, const char *) {}
  void send(int, const char *, const char *, size_t) {}
  void begin() {}
  void handleClient() {}
};
