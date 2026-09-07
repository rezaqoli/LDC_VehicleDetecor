#pragma once
class TwoWire {
 public:
  void   begin() {}
  void   begin(int, int, uint32_t) {}
  int    available() { return 0; }
  int    read() { return -1; }
  uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
  void   beginTransmission(uint8_t) {}
  uint8_t endTransmission(bool = true) { return 0; }
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t *, size_t) { return 0; }
};
static TwoWire Wire;
