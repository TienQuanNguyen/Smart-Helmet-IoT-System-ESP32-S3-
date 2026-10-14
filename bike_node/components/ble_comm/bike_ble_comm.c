#include "bike_ble_comm.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "helmet_bike_codec.h"
#include "sdkconfig.h"

static bool s_enabled;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_connected, s_valid;
static int64_t s_received_us;
static ble_helmet_status_packet_t s_status;

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"

static ble_uuid_any_t s_service_uuid, s_status_uuid;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_start, s_end, s_value, s_descriptor_end, s_cccd;
static uint8_t s_address_type;
static uint32_t s_backoff = 1000;
static bool s_have_sequence, s_subscribed;
static int64_t s_connected_us;
static ble_helmet_status_packet_t s_previous;
static struct ble_npl_callout s_retry, s_watchdog;
static int gap_event(struct ble_gap_event *event, void *arg);

static void invalidate(bool connected) {
  portENTER_CRITICAL(&s_lock);
  s_connected = connected;
  s_valid = false;
  portEXIT_CRITICAL(&s_lock);
}
static void retry_scan(void) {
  if (s_conn != BLE_HS_CONN_HANDLE_NONE) return;
  ble_npl_callout_reset(&s_retry, ble_npl_time_ms_to_ticks32(s_backoff));
  if (s_backoff < 30000) s_backoff = s_backoff > 15000 ? 30000 : s_backoff * 2;
}
static void disconnect_bad_peer(void) {
  invalidate(s_conn != BLE_HS_CONN_HANDLE_NONE);
  if (s_conn != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
}
static void scan(struct ble_npl_event *event) {
  (void)event;
  if (s_conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_active()) return;
  struct ble_gap_disc_params params = {.passive = 1, .filter_duplicates = 1};
  int rc = ble_gap_disc(s_address_type, 10000, &params, gap_event, NULL);
  if (rc && rc != BLE_HS_EALREADY) retry_scan();
}
static int subscribed(uint16_t conn, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg) {
  (void)attr;
  (void)arg;
  if (conn != s_conn) return 0;
  if (error->status)
    disconnect_bad_peer();
  else {
    s_subscribed = true;
    ESP_LOGI("bike_ble", "Helmet notifications subscribed");
  }
  return 0;
}
static int descriptor(uint16_t conn, const struct ble_gatt_error *error,
                      uint16_t chr_handle, const struct ble_gatt_dsc *dsc,
                      void *arg) {
  (void)chr_handle;
  (void)arg;
  if (conn != s_conn) return 0;
  if (!error->status) {
    if (ble_uuid_u16(&dsc->uuid.u) == 0x2902) s_cccd = dsc->handle;
  } else if (error->status == BLE_HS_EDONE && s_cccd) {
    const uint8_t enable[] = {1, 0};
    if (ble_gattc_write_flat(conn, s_cccd, enable, sizeof(enable), subscribed,
                             NULL))
      disconnect_bad_peer();
  } else
    disconnect_bad_peer();
  return 0;
}
static int characteristic(uint16_t conn, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg) {
  (void)arg;
  if (conn != s_conn) return 0;
  if (!error->status) {
    if (ble_uuid_cmp(&chr->uuid.u, &s_status_uuid.u) == 0) {
      if (!(chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
        disconnect_bad_peer();
        return 0;
      }
      s_value = chr->val_handle;
    } else if (s_value && chr->def_handle > s_value &&
               chr->def_handle <= s_descriptor_end) {
      s_descriptor_end = chr->def_handle - 1;
    }
  } else if (error->status == BLE_HS_EDONE && s_value &&
             s_descriptor_end > s_value) {
    if (ble_gattc_disc_all_dscs(conn, s_value, s_descriptor_end, descriptor,
                                NULL))
      disconnect_bad_peer();
  } else
    disconnect_bad_peer();
  return 0;
}
static int service(uint16_t conn, const struct ble_gatt_error *error,
                   const struct ble_gatt_svc *svc, void *arg) {
  (void)arg;
  if (conn != s_conn) return 0;
  if (!error->status) {
    s_start = svc->start_handle;
    s_end = svc->end_handle;
  } else if (error->status == BLE_HS_EDONE && s_start) {
    s_descriptor_end = s_end;
    if (ble_gattc_disc_all_chrs(conn, s_start, s_end, characteristic, NULL))
      disconnect_bad_peer();
  } else
    disconnect_bad_peer();
  return 0;
}
static void accept_notification(struct ble_gap_event *event) {
  if (event->notify_rx.conn_handle != s_conn ||
      event->notify_rx.attr_handle != s_value || !s_subscribed)
    return;
  uint8_t data[HELMET_STATUS_WIRE_SIZE];
  ble_helmet_status_packet_t packet;
  if (OS_MBUF_PKTLEN(event->notify_rx.om) != sizeof(data) ||
      os_mbuf_copydata(event->notify_rx.om, 0, sizeof(data), data) ||
      !helmet_status_decode(data, sizeof(data), &packet)) {
    disconnect_bad_peer();
    return;
  }
  if (s_have_sequence && !helmet_status_is_newer(&packet, &s_previous)) {
    /* Duplicates never refresh the freshness deadline; backwards data closes
     * link. */
    if (packet.sequence != s_previous.sequence ||
        packet.uptime_ms != s_previous.uptime_ms ||
        packet.state != s_previous.state || packet.flags != s_previous.flags)
      disconnect_bad_peer();
    return;
  }
  s_previous = packet;
  s_have_sequence = true;
  s_backoff = 1000;
  portENTER_CRITICAL(&s_lock);
  s_status = packet;
  s_received_us = esp_timer_get_time();
  s_valid = true;
  portEXIT_CRITICAL(&s_lock);
}
static int gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
      if (s_conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_active()) break;
      struct ble_hs_adv_fields fields;
      if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                  event->disc.length_data))
        break;
      for (unsigned i = 0; i < fields.num_uuids128; ++i) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &s_service_uuid.u) != 0)
          continue;
        if (ble_gap_disc_cancel()) break;
        if (ble_gap_connect(s_address_type, &event->disc.addr, 10000, NULL,
                            gap_event, NULL))
          retry_scan();
        break;
      }
      break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
      retry_scan();
      break;
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status) {
        invalidate(false);
        retry_scan();
        break;
      }
      s_conn = event->connect.conn_handle;
      s_connected_us = esp_timer_get_time();
      s_have_sequence = s_subscribed = false;
      s_start = s_end = s_value = s_cccd = 0;
      invalidate(true);
      ble_npl_callout_stop(&s_retry);
      ESP_LOGI("bike_ble", "Helmet connected; discovering service");
      if (ble_gattc_disc_svc_by_uuid(s_conn, &s_service_uuid.u, service, NULL))
        disconnect_bad_peer();
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      s_conn = BLE_HS_CONN_HANDLE_NONE;
      s_have_sequence = s_subscribed = false;
      invalidate(false);
      ESP_LOGW("bike_ble", "Helmet disconnected; authorization revoked");
      retry_scan();
      break;
    case BLE_GAP_EVENT_NOTIFY_RX:
      accept_notification(event);
      break;
    default:
      break;
  }
  return 0;
}
static void watchdog_tick(struct ble_npl_event *event) {
  (void)event;
  portENTER_CRITICAL(&s_lock);
  bool valid = s_valid;
  int64_t received = s_received_us;
  portEXIT_CRITICAL(&s_lock);
  if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
    int64_t age = esp_timer_get_time() - (valid ? received : s_connected_us);
    if (age > (valid ? 3000000LL : 10000000LL)) disconnect_bad_peer();
  }
  ble_npl_callout_reset(&s_watchdog, ble_npl_time_ms_to_ticks32(100));
}
static void on_reset(int reason) {
  (void)reason;
  s_conn = BLE_HS_CONN_HANDLE_NONE;
  invalidate(false);
}
static void on_sync(void) {
  if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &s_address_type))
    return;
  scan(NULL);
  ble_npl_callout_reset(&s_watchdog, ble_npl_time_ms_to_ticks32(100));
}
static void host_task(void *arg) {
  (void)arg;
  nimble_port_run();
  invalidate(false);
  vTaskDelete(NULL);
}
#endif

esp_err_t bike_ble_comm_init(void) {
  if (s_enabled) return ESP_OK;
#if CONFIG_BT_NIMBLE_ENABLED
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK) return err;
  err = nimble_port_init();
  if (err != ESP_OK) return err;
  if (ble_uuid_from_str(&s_service_uuid, BLE_HELMET_SERVICE_UUID) ||
      ble_uuid_from_str(&s_status_uuid, BLE_HELMET_STATUS_CHAR_UUID))
    goto failed;
  ble_npl_callout_init(&s_retry, nimble_port_get_dflt_eventq(), scan, NULL);
  ble_npl_callout_init(&s_watchdog, nimble_port_get_dflt_eventq(),
                       watchdog_tick, NULL);
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  if (xTaskCreate(host_task, "bike_ble", 4096, NULL, 5, NULL) != pdPASS)
    goto failed;
  s_enabled = true;
  return ESP_OK;
failed:
  nimble_port_deinit();
  return ESP_FAIL;
#else
  ESP_LOGW("bike_ble", "NimBLE disabled; relay remains fail-closed");
  return ESP_OK;
#endif
}
bool bike_ble_comm_is_connected(void) {
  portENTER_CRITICAL(&s_lock);
  bool connected = s_connected;
  portEXIT_CRITICAL(&s_lock);
  return connected;
}
bool bike_ble_comm_is_runtime_enabled(void) { return s_enabled; }
bool bike_ble_comm_get_status(ble_helmet_status_packet_t *status) {
  if (!status) return false;
  portENTER_CRITICAL(&s_lock);
  bool valid = s_connected && s_valid &&
               esp_timer_get_time() - s_received_us <= 3000000LL;
  *status = s_status;
  portEXIT_CRITICAL(&s_lock);
  return valid;
}
