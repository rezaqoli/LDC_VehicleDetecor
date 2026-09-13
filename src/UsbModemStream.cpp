#include "Config.h"

#if defined(ESP32s3) && MODEM_TRANSPORT == MODEM_TRANSPORT_USB

#include "UsbModemStream.h"
#include "esp_err.h"
#include "usb/usb_host.h"

namespace
{
constexpr size_t USB_MODEM_RX_BUFFER_SIZE = 16 * 1024;
constexpr size_t USB_MODEM_TX_BUFFER_SIZE = 1024;
}

UsbModemStream::UsbModemStream()
  : rxBuffer_(nullptr), device_(nullptr), connected_(false), peeked_(-1)
{
}

bool UsbModemStream::begin()
{
  if (!rxBuffer_)
    rxBuffer_ = xRingbufferCreate(USB_MODEM_RX_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
  if (!rxBuffer_)
    return false;

  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    Serial.printf("[USB MODEM] usb_host_install failed: %s\n", esp_err_to_name(err));
    return false;
  }

  if (xTaskCreatePinnedToCore(usbHostTask, "usb-host", 4096, this, 5, nullptr, 0) != pdPASS)
    return false;

  cdc_acm_host_driver_config_t driverConfig = {};
  driverConfig.driver_task_stack_size = 4096;
  driverConfig.driver_task_priority = 6;
  driverConfig.xCoreID = 0;
  driverConfig.new_dev_cb = nullptr;
  err = cdc_acm_host_install(&driverConfig);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    Serial.printf("[USB MODEM] cdc_acm_host_install failed: %s\n", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool UsbModemStream::waitForDevice(uint32_t timeoutMs)
{
  if (connected_ && device_)
    return true;

  cdc_acm_host_device_config_t config = {};
  config.connection_timeout_ms = timeoutMs;
  config.out_buffer_size = USB_MODEM_TX_BUFFER_SIZE;
  config.event_cb = eventCallback;
  config.data_cb = rxCallback;
  config.user_arg = this;

  esp_err_t err = cdc_acm_host_open_vendor_specific(
      EC200U_USB_VID, EC200U_USB_PID, EC200U_USB_AT_INTERFACE,
      &config, &device_);
  if (err != ESP_OK)
  {
    Serial.printf("[USB MODEM] EC200 open failed: %s\n", esp_err_to_name(err));
    device_ = nullptr;
    connected_ = false;
    return false;
  }

  connected_ = true;
  Serial.printf("[USB MODEM] Connected %04X:%04X interface %u\n",
                EC200U_USB_VID, EC200U_USB_PID, EC200U_USB_AT_INTERFACE);
  cdc_acm_host_desc_print(device_);
  return true;
}

bool UsbModemStream::connected() const
{
  return connected_ && device_ != nullptr;
}

int UsbModemStream::available()
{
  if (!rxBuffer_)
    return peeked_ >= 0 ? 1 : 0;
  size_t waiting = USB_MODEM_RX_BUFFER_SIZE - xRingbufferGetCurFreeSize(rxBuffer_);
  return (int)waiting + (peeked_ >= 0 ? 1 : 0);
}

int UsbModemStream::read()
{
  if (peeked_ >= 0)
  {
    int value = peeked_;
    peeked_ = -1;
    return value;
  }
  if (!rxBuffer_)
    return -1;
  size_t received = 0;
  uint8_t *item = static_cast<uint8_t *>(xRingbufferReceiveUpTo(rxBuffer_, &received, 0, 1));
  if (!item || received == 0)
    return -1;
  int value = item[0];
  vRingbufferReturnItem(rxBuffer_, item);
  return value;
}

int UsbModemStream::peek()
{
  if (peeked_ < 0)
    peeked_ = read();
  return peeked_;
}

size_t UsbModemStream::write(uint8_t value)
{
  return write(&value, 1);
}

size_t UsbModemStream::write(const uint8_t *buffer, size_t size)
{
  if (!connected() || !buffer || size == 0)
    return 0;
  return cdc_acm_host_data_tx_blocking(device_, buffer, size, 5000) == ESP_OK ? size : 0;
}

void UsbModemStream::flush()
{
  // cdc_acm_host_data_tx_blocking() completes each write before returning.
}

void UsbModemStream::rxCallback(uint8_t *data, size_t length, void *arg)
{
  UsbModemStream *self = static_cast<UsbModemStream *>(arg);
  if (!self || !self->rxBuffer_ || !data || length == 0)
    return;
  if (xRingbufferSend(self->rxBuffer_, data, length, 0) != pdTRUE)
    Serial.printf("[USB MODEM] RX overflow: dropped %u bytes\n", (unsigned)length);
}

void UsbModemStream::eventCallback(const cdc_acm_host_dev_event_data_t *event, void *arg)
{
  UsbModemStream *self = static_cast<UsbModemStream *>(arg);
  if (!self || !event)
    return;
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED)
  {
    self->connected_ = false;
    self->device_ = nullptr;
    Serial.println("[USB MODEM] Disconnected");
  }
}

void UsbModemStream::usbHostTask(void *)
{
  while (true)
  {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
  }
}

#endif
