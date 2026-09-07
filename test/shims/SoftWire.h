#pragma once
#include "Wire.h"  // SoftWire mimics TwoWire in the production code
class SoftWire : public TwoWire {
 public:
  void setTxBuffer(void *, size_t) {}
  void setRxBuffer(void *, size_t) {}
  void setTimeout_ms(uint32_t) {}
  void setDelay_us(uint32_t) {}
};
