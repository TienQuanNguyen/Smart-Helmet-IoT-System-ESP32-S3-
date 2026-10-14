#include "ble_comm.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "helmet_bike_codec.h"
#include "sdkconfig.h"

static bool s_initialized, s_enabled;
static uint8_t s_sequence;
static ble_helmet_status_packet_t s_status;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

#if CONFIG_SMART_HELMET_BLE_ENABLE && CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static ble_uuid_any_t s_service_uuid, s_status_uuid, s_command_uuid;
static uint16_t s_connection = BLE_HS_CONN_HANDLE_NONE, s_value_handle;
static uint8_t s_address_type;
static bool s_subscribed;
static struct ble_npl_event s_notify_event;
static struct ble_npl_callout s_advertise_retry;
static int gap_event(struct ble_gap_event *event, void *arg);

static void status_bytes(uint8_t bytes[8]) {
  portENTER_CRITICAL(&s_lock);
  ble_helmet_status_packet_t status = s_status;
  portEXIT_CRITICAL(&s_lock);
  helmet_status_encode(&status, bytes);
}

static int access_status(uint16_t connection, uint16_t handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)connection;
  (void)handle;
  (void)arg;
  uint8_t bytes[8];
  status_bytes(bytes);
  return os_mbuf_append(ctxt->om, bytes, sizeof(bytes)) == 0
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_command(uint16_t connection, uint16_t handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)connection;
  (void)handle;
  (void)arg;
  uint8_t bytes[4];
  if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(bytes) ||
      os_mbuf_copydata(ctxt->om, 0, sizeof(bytes), bytes) != 0 ||
      bytes[0] != BLE_PROTOCOL_VERSION ||
      bytes[1] > BLE_BIKE_COMMAND_STATUS_ACK || bytes[3])
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  /* Commands never override Helmet sensor authorization or FSM. */
  return 0;
}

static struct ble_gatt_chr_def s_characteristics[3];
static struct ble_gatt_svc_def s_services[2];

static void advertise(struct ble_npl_event *event) {
  (void)event;
  if (s_connection != BLE_HS_CONN_HANDLE_NONE) return;
  struct ble_hs_adv_fields fields = {
      .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
      .uuids128 = &s_service_uuid.u128,
      .num_uuids128 = 1,
      .uuids128_is_complete = 1};
  struct ble_gap_adv_params params = {.conn_mode = BLE_GAP_CONN_MODE_UND,
                                      .disc_mode = BLE_GAP_DISC_MODE_GEN};
  int rc = ble_gap_adv_set_fields(&fields);
  if (!rc)
    rc = ble_gap_adv_start(s_address_type, NULL, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
  if (rc && rc != BLE_HS_EALREADY) {
    ESP_LOGW("ble_comm", "Advertising retry rc=%d", rc);
    ble_npl_callout_reset(&s_advertise_retry, ble_npl_time_ms_to_ticks32(1000));
  }
}

static void notify_host(struct ble_npl_event *event) {
  (void)event;
  if (s_connection == BLE_HS_CONN_HANDLE_NONE || !s_subscribed) return;
  uint8_t bytes[8];
  status_bytes(bytes);
  struct os_mbuf *om = ble_hs_mbuf_from_flat(bytes, sizeof(bytes));
  if (!om) return;
  int rc = ble_gatts_notify_custom(s_connection, s_value_handle, om);
  if (rc) ESP_LOGD("ble_comm", "Notify rc=%d", rc);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (!event->connect.status) {
        s_connection = event->connect.conn_handle;
        ESP_LOGI("ble_comm", "Bike connected");
      } else
        advertise(NULL);
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      s_connection = BLE_HS_CONN_HANDLE_NONE;
      s_subscribed = false;
      ESP_LOGI("ble_comm", "Bike disconnected");
      advertise(NULL);
      break;
    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.attr_handle == s_value_handle) {
        s_subscribed = event->subscribe.cur_notify;
        notify_host(NULL);
      }
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      advertise(NULL);
      break;
    default:
      break;
  }
  return 0;
}

static void on_reset(int reason) {
  s_connection = BLE_HS_CONN_HANDLE_NONE;
  s_subscribed = false;
  ESP_LOGW("ble_comm", "Host reset=%d", reason);
}
static void on_sync(void) {
  if (ble_hs_util_ensure_addr(0) == 0 &&
      ble_hs_id_infer_auto(0, &s_address_type) == 0)
    advertise(NULL);
}
static void host_task(void *arg) {
  (void)arg;
  nimble_port_run();
  vTaskDelete(NULL);
}
#endif

ble_helmet_status_packet_t ble_comm_make_status_packet(system_state_t state,
                                                       uint32_t uptime_ms) {
  uint8_t protocol_state = BLE_HELMET_STATE_LOW_POWER;
  switch (state) {
    case SYSTEM_STATE_BOOT:
      protocol_state = BLE_HELMET_STATE_BOOT;
      break;
    case SYSTEM_STATE_STARTUP_ALCOHOL_CHECK:
      protocol_state = BLE_HELMET_STATE_ALCOHOL_CHECK;
      break;
    case SYSTEM_STATE_READY_TO_RIDE:
      protocol_state = BLE_HELMET_STATE_READY;
      break;
    case SYSTEM_STATE_DRIVING_MONITORING:
      protocol_state = BLE_HELMET_STATE_DRIVING;
      break;
    case SYSTEM_STATE_ACCIDENT_DETECTED:
      protocol_state = BLE_HELMET_STATE_ACCIDENT;
      break;
    case SYSTEM_STATE_EMERGENCY_REPORTING:
      protocol_state = BLE_HELMET_STATE_EMERGENCY;
      break;
    default:
      break;
  }
  uint8_t flags = 0;
  if (protocol_state == BLE_HELMET_STATE_READY ||
      protocol_state == BLE_HELMET_STATE_DRIVING)
    flags |= BLE_HELMET_FLAG_ALCOHOL_PASS;
  if (protocol_state == BLE_HELMET_STATE_ACCIDENT ||
      protocol_state == BLE_HELMET_STATE_EMERGENCY)
    flags |= BLE_HELMET_FLAG_EMERGENCY;
  portENTER_CRITICAL(&s_lock);
  uint8_t sequence = ++s_sequence;
  portEXIT_CRITICAL(&s_lock);
  return (ble_helmet_status_packet_t){.version = BLE_PROTOCOL_VERSION,
                                      .state = protocol_state,
                                      .flags = flags,
                                      .sequence = sequence,
                                      .uptime_ms = uptime_ms};
}

bool ble_comm_init(void) {
  if (s_initialized) return true;
  s_status = ble_comm_make_status_packet(SYSTEM_STATE_BOOT, 0);
#if CONFIG_SMART_HELMET_BLE_ENABLE && CONFIG_BT_NIMBLE_ENABLED
  if (nvs_flash_init() != ESP_OK || nimble_port_init() != ESP_OK) return false;
  if (ble_uuid_from_str(&s_service_uuid, BLE_HELMET_SERVICE_UUID) ||
      ble_uuid_from_str(&s_status_uuid, BLE_HELMET_STATUS_CHAR_UUID) ||
      ble_uuid_from_str(&s_command_uuid, BLE_BIKE_COMMAND_CHAR_UUID))
    goto failed;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  s_characteristics[0] = (struct ble_gatt_chr_def){
      .uuid = &s_status_uuid.u,
      .access_cb = access_status,
      .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
      .val_handle = &s_value_handle};
  s_characteristics[1] =
      (struct ble_gatt_chr_def){.uuid = &s_command_uuid.u,
                                .access_cb = access_command,
                                .flags = BLE_GATT_CHR_F_WRITE};
  s_services[0] =
      (struct ble_gatt_svc_def){.type = BLE_GATT_SVC_TYPE_PRIMARY,
                                .uuid = &s_service_uuid.u,
                                .characteristics = s_characteristics};
  if (ble_gatts_count_cfg(s_services) || ble_gatts_add_svcs(s_services) ||
      ble_svc_gap_device_name_set("Helmet Node"))
    goto failed;
  ble_npl_event_init(&s_notify_event, notify_host, NULL);
  ble_npl_callout_init(&s_advertise_retry, nimble_port_get_dflt_eventq(),
                       advertise, NULL);
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  if (xTaskCreate(host_task, "helmet_ble", 4096, NULL, 5, NULL) != pdPASS)
    goto failed;
  s_enabled = true;
#endif
  s_initialized = true;
  return true;
#if CONFIG_SMART_HELMET_BLE_ENABLE && CONFIG_BT_NIMBLE_ENABLED
failed:
  nimble_port_deinit();
  return false;
#endif
}

bool ble_comm_is_enabled(void) { return s_enabled; }
bool ble_comm_notify_state(system_state_t state, uint32_t uptime_ms) {
  ble_helmet_status_packet_t packet =
      ble_comm_make_status_packet(state, uptime_ms);
  portENTER_CRITICAL(&s_lock);
  s_status = packet;
  portEXIT_CRITICAL(&s_lock);
#if CONFIG_SMART_HELMET_BLE_ENABLE && CONFIG_BT_NIMBLE_ENABLED
  if (s_enabled)
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_notify_event);
#endif
  return s_initialized;
}
bool ble_comm_send_emergency(const char *source_event, uint32_t uptime_ms) {
  (void)source_event;
  return ble_comm_notify_state(SYSTEM_STATE_EMERGENCY_REPORTING, uptime_ms);
}
