#pragma once

#include <Arduino.h>

#if defined(ESP32s3) && MODEM_TRANSPORT == MODEM_TRANSPORT_USB

#include "freertos/ringbuf.h"
#include "usb/cdc_acm_host.h"

class UsbModemStream : public Stream
{
public:
  UsbModemStream();

  bool begin();
  bool waitForDevice(uint32_t timeoutMs);
  bool connected() const;

  int available() override;
  int read() override;
  int peek() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  void flush() override;
  using Print::write;

private:
  static void usbHostTask(void *arg);
  static void rxCallback(uint8_t *data, size_t length, void *arg);
  static void eventCallback(const cdc_acm_host_dev_event_data_t *event, void *arg);

  RingbufHandle_t rxBuffer_;
  cdc_acm_dev_hdl_t device_;
  volatile bool connected_;
  int peeked_;
};

#endif
