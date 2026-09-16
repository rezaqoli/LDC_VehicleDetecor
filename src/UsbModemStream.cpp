#include "Config.h"

#if defined(ESP32s3) && MODEM_TRANSPORT == MODEM_TRANSPORT_USB

#include "UsbModemStream.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"

namespace
{
constexpr size_t USB_MODEM_RX_BUFFER_SIZE = 16 * 1024;
constexpr size_t USB_MODEM_TX_BUFFER_SIZE = 1024;
}

UsbModemStream *UsbModemStream::activeInstance_ = nullptr;

UsbModemStream::UsbModemStream()
  : rxBuffer_(nullptr), device_(nullptr), connected_(false), enumerated_(false),
    detectedVid_(0), detectedPid_(0), bulkInterfaceCount_(0), peeked_(-1)
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
  activeInstance_ = this;
  driverConfig.new_dev_cb = newDeviceCallback;
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

  const uint32_t started = millis();
  while (!enumerated_ && millis() - started < timeoutMs)
    delay(20);
  if (!enumerated_)
  {
    Serial.println("[USB MODEM] No USB device enumerated (check VBUS, D+/D-, cable and host wiring)");
    return false;
  }

  cdc_acm_host_device_config_t config = {};
  config.connection_timeout_ms = 1000;
  config.out_buffer_size = USB_MODEM_TX_BUFFER_SIZE;
  config.event_cb = eventCallback;
  config.data_cb = rxCallback;
  config.user_arg = this;

  uint8_t candidates[17] = {EC200U_USB_AT_INTERFACE};
  uint8_t candidateCount = 1;
  for (uint8_t i = 0; i < bulkInterfaceCount_ && candidateCount < sizeof(candidates); ++i)
  {
    if (bulkInterfaces_[i] != EC200U_USB_AT_INTERFACE)
      candidates[candidateCount++] = bulkInterfaces_[i];
  }

  esp_err_t err = ESP_ERR_NOT_FOUND;
  uint8_t openedInterface = 0xFF;
  for (uint8_t i = 0; i < candidateCount; ++i)
  {
    Serial.printf("[USB MODEM] Trying %04X:%04X interface %u\n",
                  detectedVid_, detectedPid_, candidates[i]);
    err = cdc_acm_host_open_vendor_specific(
        detectedVid_, detectedPid_, candidates[i], &config, &device_);
    if (err == ESP_OK)
    {
      openedInterface = candidates[i];
      break;
    }
  }
  if (err != ESP_OK)
  {
    Serial.printf("[USB MODEM] No usable bulk interface: %s\n", esp_err_to_name(err));
    device_ = nullptr;
    connected_ = false;
    return false;
  }

  connected_ = true;
  Serial.printf("[USB MODEM] Connected %04X:%04X interface %u\n",
                detectedVid_, detectedPid_, openedInterface);
  cdc_acm_host_desc_print(device_);
  return true;
}

void UsbModemStream::newDeviceCallback(usb_device_handle_t usbDevice)
{
  UsbModemStream *self = activeInstance_;
  if (!self || !usbDevice)
    return;

  const usb_device_desc_t *deviceDesc = nullptr;
  const usb_config_desc_t *configDesc = nullptr;
  if (usb_host_get_device_descriptor(usbDevice, &deviceDesc) != ESP_OK ||
      usb_host_get_active_config_descriptor(usbDevice, &configDesc) != ESP_OK)
    return;

  self->detectedVid_ = deviceDesc->idVendor;
  self->detectedPid_ = deviceDesc->idProduct;
  self->bulkInterfaceCount_ = 0;
  Serial.printf("[USB MODEM] Enumerated %04X:%04X, %u interfaces\n",
                deviceDesc->idVendor, deviceDesc->idProduct, configDesc->bNumInterfaces);

  // Walk the descriptors because USB interface numbers need not be contiguous.
  const uint8_t *cursor = reinterpret_cast<const uint8_t *>(configDesc);
  const uint8_t *end = cursor + configDesc->wTotalLength;
  while (cursor + 2 <= end)
  {
    const usb_standard_desc_t *standard = reinterpret_cast<const usb_standard_desc_t *>(cursor);
    if (standard->bLength < 2 || cursor + standard->bLength > end)
      break;
    if (standard->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE &&
        standard->bLength >= sizeof(usb_intf_desc_t))
    {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(cursor);
      bool bulkIn = false;
      bool bulkOut = false;
      const uint8_t *next = cursor + standard->bLength;
      while (next + 2 <= end)
      {
        const usb_standard_desc_t *child = reinterpret_cast<const usb_standard_desc_t *>(next);
        if (child->bLength < 2 || next + child->bLength > end ||
            child->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE)
          break;
        if (child->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
            child->bLength >= sizeof(usb_ep_desc_t))
        {
          const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(next);
          Serial.printf("[USB MODEM] IF %u class %02X EP %02X attr %02X MPS %u\n",
                        intf->bInterfaceNumber, intf->bInterfaceClass,
                        ep->bEndpointAddress, ep->bmAttributes, ep->wMaxPacketSize);
          if (USB_EP_DESC_GET_XFERTYPE(ep) == USB_TRANSFER_TYPE_BULK)
          {
            if (USB_EP_DESC_GET_EP_DIR(ep))
              bulkIn = true;
            else
              bulkOut = true;
          }
        }
        next += child->bLength;
      }
      if (bulkIn && bulkOut && self->bulkInterfaceCount_ < sizeof(self->bulkInterfaces_))
        self->bulkInterfaces_[self->bulkInterfaceCount_++] = intf->bInterfaceNumber;
    }
    cursor += standard->bLength;
  }
  self->enumerated_ = true;
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
