#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "helmet_bike_protocol.h"

#define HELMET_STATUS_WIRE_SIZE 8
static inline void helmet_status_encode(const ble_helmet_status_packet_t *p,
                                        uint8_t out[8]) {
  out[0] = p->version;
  out[1] = p->state;
  out[2] = p->flags;
  out[3] = p->sequence;
  for (unsigned i = 0; i < 4; ++i)
    out[4 + i] = (uint8_t)(p->uptime_ms >> (8 * i));
}
static inline bool helmet_status_decode(const uint8_t *data, size_t size,
                                        ble_helmet_status_packet_t *p) {
  if (!data || !p || size != HELMET_STATUS_WIRE_SIZE ||
      data[0] != BLE_PROTOCOL_VERSION || data[1] > BLE_HELMET_STATE_LOW_POWER ||
      (data[2] & ~7U))
    return false;
  p->version = data[0];
  p->state = data[1];
  p->flags = data[2];
  p->sequence = data[3];
  p->uptime_ms = 0;
  for (unsigned i = 0; i < 4; ++i)
    p->uptime_ms |= (uint32_t)data[4 + i] << (8 * i);
  return true;
}
static inline bool helmet_status_is_newer(
    const ble_helmet_status_packet_t *p,
    const ble_helmet_status_packet_t *last) {
  uint8_t delta = (uint8_t)(p->sequence - last->sequence);
  return delta != 0 && delta < 128 &&
         (int32_t)(p->uptime_ms - last->uptime_ms) >= 0;
}
