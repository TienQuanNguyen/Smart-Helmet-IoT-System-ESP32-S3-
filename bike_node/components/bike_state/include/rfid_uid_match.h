#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline int rfid_hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* Exact, full-length UID only. Accept AABBCCDD or AA:BB:CC:DD. */
static inline bool rfid_uid_matches(const uint8_t *uid, size_t length,
                                    const char *configured) {
  if (!uid || !configured || (length != 4 && length != 7 && length != 10))
    return false;
  size_t n = strlen(configured);
  bool colon = n == length * 3 - 1;
  if (!colon && n != length * 2) return false;
  for (size_t i = 0; i < length; ++i) {
    size_t offset = i * (colon ? 3 : 2);
    int high = rfid_hex_digit(configured[offset]);
    int low = rfid_hex_digit(configured[offset + 1]);
    if (high < 0 || low < 0 || uid[i] != (uint8_t)((high << 4) | low))
      return false;
    if (colon && i + 1 < length && configured[offset + 2] != ':')
      return false;
  }
  return true;
}
