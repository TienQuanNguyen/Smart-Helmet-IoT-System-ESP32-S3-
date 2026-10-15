#include <assert.h>
#include <stdio.h>

/* Test-only UIDs; these are never configured in firmware. */
#define CONFIG_BIKE_RFID_UID_1 "04:A1:B2:C3"
#define CONFIG_BIKE_RFID_UID_2 "01020304050607"
#include "../../../bike_node/components/bike_state/bike_state.c"

int main(void) {
  uint8_t allowed[] = {4, 0xA1, 0xB2, 0xC3};
  uint8_t other[] = {4, 0xA1, 0xB2, 0xC4};
  uint8_t long_uid[] = {1, 2, 3, 4, 5, 6, 7};
  assert(bike_state_init() == ESP_OK);
  assert(bike_state_is_hardcoded_uid_allowed(allowed, sizeof(allowed)));
  assert(bike_state_is_hardcoded_uid_allowed(long_uid, sizeof(long_uid)));
  assert(!bike_state_is_hardcoded_uid_allowed(other, sizeof(other)));
  assert(!bike_state_is_hardcoded_uid_allowed(long_uid, 4));
  assert(!bike_state_is_hardcoded_uid_allowed(NULL, 4));
  for (unsigned mask = 0; mask < 8; ++mask) {
    bike_state_update_helmet(mask & 1, mask & 2, mask & 4);
    bike_state_set_rfid_authorized(
        bike_state_is_hardcoded_uid_allowed(allowed, sizeof(allowed)));
    assert(bike_state_is_ignition_allowed() == (mask == 7));
    bike_state_set_rfid_authorized(
        bike_state_is_hardcoded_uid_allowed(other, sizeof(other)));
    assert(!bike_state_is_ignition_allowed());
  }
  bike_state_set_rfid_authorized(true);
  assert(bike_state_is_ignition_allowed());
  bike_state_set_rfid_authorized(false);
  assert(!bike_state_is_ignition_allowed());
  bike_state_reset();
  assert(!bike_state_request_card(allowed, sizeof(allowed)));
  bike_state_update_helmet(true, true, true);
  assert(!bike_state_is_ignition_allowed());
  assert(bike_state_request_card(allowed, sizeof(allowed)));
  assert(bike_state_is_ignition_allowed());
  assert(!bike_state_request_card(other, sizeof(other)));
  assert(bike_state_is_ignition_allowed());
  assert(bike_state_request_card(allowed, sizeof(allowed)));
  assert(!bike_state_is_ignition_allowed());
  assert(bike_state_request_card(long_uid, sizeof(long_uid)));
  bike_state_update_helmet(false, true, true);
  assert(!bike_state_is_ignition_allowed());
  bike_state_update_helmet(true, true, true);
  assert(!bike_state_is_ignition_allowed());
  assert(bike_state_request_card(allowed, sizeof(allowed)));
  bike_state_update_helmet(true, true, false);
  assert(!bike_state_is_ignition_allowed());
  bike_state_update_helmet(true, true, true);
  assert(!bike_state_is_ignition_allowed());
  for (unsigned mask = 0; mask < 8; ++mask) {
    bike_state_reset();
    bike_state_update_helmet(mask & 1, mask & 2, mask & 4);
    assert(bike_state_request_card(allowed, sizeof(allowed)) == (mask == 7));
    assert(bike_state_is_ignition_allowed() == (mask == 7));
  }
  puts("PASS: RFID allowlist + all Helmet authorization combinations");
  return 0;
}
