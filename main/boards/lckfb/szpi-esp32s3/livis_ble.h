#ifndef LIVIS_BLE_H
#define LIVIS_BLE_H

#include "application.h"
#include "livis_display.h"
#include "livis_quota.h"
#include "settings.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

// GATT server receiving the Mac agent's pushes (docs/codex-usage-sync.md §5-6).
// The device advertises as a peripheral; quota writes require a bonded,
// encrypted link. Callbacks run on the NimBLE host task; display updates take
// the LVGL lock inside LivisDisplay, so no extra queueing is needed.
class LivisBle {
public:
    explicit LivisBle(LivisDisplay* display) : display_(display) {}

    void Start() {
        // Standby widgets are created by Application::Initialize after the board
        // constructor returns; restore cached values once that has happened.
        Application::GetInstance().Schedule([this]() { RestoreCached(); });
        instance_ = this;
        // Defer the NimBLE stack past the boot allocation storm: starting it at
        // construction time starved the MQTT TLS read buffer (errno=12) while
        // the wake-word models and audio pipeline were still loading.
        const esp_timer_create_args_t init_args = {
            .callback = InitTick,
            .arg = nullptr,
            .name = "livis_ble_init",
        };
        if (esp_timer_create(&init_args, &init_timer_) != ESP_OK) {
            ESP_LOGE(kTag, "unable to create init timer");
            return;
        }
        esp_timer_start_once(init_timer_, kInitDelayUs);
    }

    static void InitTick(void*) {
        if (instance_ == nullptr) return;
        instance_->InitStack();
    }

    void InitStack() {
        if (nimble_port_init() != ESP_OK) {
            ESP_LOGE(kTag, "NimBLE init failed, quota sync disabled");
            return;
        }
        ble_hs_cfg.reset_cb = OnHostReset;
        ble_hs_cfg.sync_cb = OnHostSync;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        // LE Secure Connections bonding; writes are rejected until encrypted.
        // ID keys must be exchanged too: macOS centrals reconnect from rotating
        // private addresses, and without the peer IRK the bond cannot be
        // resolved after the first connection drops.
        ble_hs_cfg.sm_bonding = 1;
        ble_hs_cfg.sm_sc = 1;
        ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_BT);
        char name[16];
        std::snprintf(name, sizeof(name), "Livis-%02X%02X", mac[4], mac[5]);
        ble_svc_gap_device_name_set(name);

        ble_svc_gap_init();
        ble_svc_gatt_init();
        ble_gatts_count_cfg(kServices);
        ble_gatts_add_svcs(kServices);

        const esp_timer_create_args_t stale_args = {
            .callback = StaleTick,
            .arg = nullptr,
            .name = "livis_ble_stale",
        };
        if (esp_timer_create(&stale_args, &stale_timer_) == ESP_OK) {
            esp_timer_start_periodic(stale_timer_, kStaleCheckUs);
        }
        nimble_port_freertos_init(HostTask);
    }

private:
    static constexpr const char* kTag = "LivisBle";
    static constexpr size_t kMaxPayload = 200;
    // Marks the page as old after six hours without a push (Mac asleep or gone).
    static constexpr double kStaleSeconds = 6.0 * 60 * 60;
    static constexpr int64_t kStaleCheckUs = 10LL * 60 * 1000000;
    // Boot grace period before the NimBLE stack takes its memory.
    static constexpr int64_t kInitDelayUs = 15LL * 1000000;
    static constexpr const char* kSettingsNamespace = "livis_codex";
    static constexpr const char* kSettingsKey = "quota";

    // docs/codex-usage-sync.md §5: service e5a10000-c6d2-4b3a-9f0e-5a1ec0ffee01.
    static const ble_uuid128_t kServiceUuid;
    static const ble_uuid128_t kQuotaUuid;
    static const ble_uuid128_t kStatusUuid;
    static const struct ble_gatt_chr_def kCharacteristics[];
    static const struct ble_gatt_svc_def kServices[];
    static LivisBle* instance_;
    static uint8_t own_addr_type_;

    LivisDisplay* display_;
    esp_timer_handle_t init_timer_ = nullptr;
    esp_timer_handle_t stale_timer_ = nullptr;
    LivisPageData::Quota five_hour_;
    LivisPageData::Quota weekly_;
    std::string updated_;
    time_t sent_at_ = 0;

    static void OnHostReset(int reason) {
        ESP_LOGW(kTag, "NimBLE host reset, reason=%d", reason);
    }
    static void OnHostSync() {
        if (ble_hs_util_ensure_addr(0) == 0) {
            ble_hs_id_infer_auto(0, &own_addr_type_);
            Advertise();
        }
    }
    static void HostTask(void*) {
        nimble_port_run();
        nimble_port_freertos_deinit();
    }
    static void StaleTick(void*) {
        if (instance_ != nullptr) instance_->CheckStale();
    }

    static void Advertise() {
        struct ble_hs_adv_fields fields = {};
        fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
        fields.uuids128 = &kServiceUuid;
        fields.num_uuids128 = 1;
        fields.uuids128_is_complete = 1;
        int rc = ble_gap_adv_set_fields(&fields);
        if (rc == 0) {
            // The name goes in the scan response; UUID plus name do not fit in
            // the 31-byte legacy advertisement.
            struct ble_hs_adv_fields response = {};
            const char* name = ble_svc_gap_device_name();
            response.name = reinterpret_cast<const uint8_t*>(name);
            response.name_len = static_cast<uint8_t>(std::strlen(name));
            response.name_is_complete = 1;
            rc = ble_gap_adv_rsp_set_fields(&response);
        }
        if (rc == 0) {
            struct ble_gap_adv_params params = {};
            params.conn_mode = BLE_GAP_CONN_MODE_UND;
            params.disc_mode = BLE_GAP_DISC_MODE_GEN;
            params.itvl_min = 800;  // 500 ms; the device is desk powered
            params.itvl_max = 800;
            rc = ble_gap_adv_start(own_addr_type_, nullptr, BLE_HS_FOREVER, &params,
                                   GapEvent, nullptr);
        }
        if (rc != 0) ESP_LOGE(kTag, "advertising failed, rc=%d", rc);
    }

    static int GapEvent(struct ble_gap_event* event, void*) {
        switch (event->type) {
            case BLE_GAP_EVENT_CONNECT:
                if (event->connect.status != 0) Advertise();  // failed; resume
                return 0;
            case BLE_GAP_EVENT_DISCONNECT:
                ESP_LOGI(kTag, "peer disconnected, reason=%d", event->disconnect.reason);
                Advertise();
                return 0;
            default:
                return 0;
        }
    }

    static int QuotaAccess(uint16_t, uint16_t, struct ble_gatt_access_ctxt* ctxt, void*) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        if (instance_ == nullptr) return BLE_ATT_ERR_UNLIKELY;
        char payload[kMaxPayload + 1] = {};
        uint16_t length = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, payload, kMaxPayload, &length) != 0) {
            ESP_LOGW(kTag, "quota write longer than %u bytes",
                     static_cast<unsigned>(kMaxPayload));
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
        return instance_->Apply(payload, true) ? 0 : BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }

    static int StatusAccess(uint16_t, uint16_t, struct ble_gatt_access_ctxt* ctxt, void*) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
        const auto* app = esp_app_get_description();
        char text[96];
        std::snprintf(text, sizeof(text), "{\"updated\":%lld,\"fw\":\"%s\"}",
                      static_cast<long long>(instance_ ? instance_->sent_at_ : 0),
                      app->version);
        return os_mbuf_append(ctxt->om, text, std::strlen(text)) == 0 ? 0 : BLE_ATT_ERR_UNLIKELY;
    }

    bool Apply(const char* payload, bool persist) {
        LivisQuotaMessage message;
        if (!LivisQuota::Parse(payload, message)) return false;
        // The board stores local wall time as system time with no TZ set
        // (ota.cc adds the server's timezone offset to the UTC timestamp), so
        // localtime_r renders true UTC epochs eight hours off. Shift the
        // payload epochs into the device clock domain using each message's
        // send time; this also keeps the staleness diff meaningful.
        const int64_t offset = static_cast<int64_t>(time(nullptr)) - message.sent_at;
        sent_at_ = message.sent_at + offset;
        five_hour_ = MakeQuota(message.five_hour_percent, message.five_hour_reset + offset, false);
        weekly_ = MakeQuota(message.weekly_percent, message.weekly_reset + offset, true);
        updated_ = LivisQuota::FormatStamp(sent_at_);
        if (persist) {
            // Cache the raw payload so the page survives a reboot.
            Settings settings(kSettingsNamespace, true);
            settings.SetString(kSettingsKey, payload);
        }
        display_->SetQuota(five_hour_, weekly_, updated_);
        ESP_LOGI(kTag, "quota: 5h %d%%, weekly %d%% (tz offset %llds)",
                 five_hour_.remaining_percent, weekly_.remaining_percent,
                 static_cast<long long>(offset));
        return true;
    }

    void RestoreCached() {
        LivisQuotaMessage message;
        const std::string payload = Settings(kSettingsNamespace).GetString(kSettingsKey);
        if (LivisQuota::Parse(payload.c_str(), message)) Apply(payload.c_str(), false);
    }

    static LivisPageData::Quota MakeQuota(int percent, time_t reset_epoch, bool weekly) {
        LivisPageData::Quota quota;
        quota.available = true;
        quota.remaining_percent = percent;
        quota.reset = LivisQuota::FormatReset(reset_epoch, weekly);
        return quota;
    }

    void CheckStale() {
        if (sent_at_ == 0 || updated_.find(" 旧") != std::string::npos) return;
        const double age = difftime(time(nullptr), sent_at_);
        if (age <= kStaleSeconds) return;
        updated_ += " 旧";
        display_->SetQuota(five_hour_, weekly_, updated_);
        ESP_LOGI(kTag, "no push for %.0f s, quota marked stale", age);
    }
};

// BLE_UUID128_INIT takes the UUID least-significant byte first; these spell
// e5a1....-c6d2-4b3a-9f0e-5a1ec0ffee01 with ...0001/0002 for the characteristics.
const ble_uuid128_t LivisBle::kServiceUuid =
    BLE_UUID128_INIT(0x01, 0xee, 0xff, 0xc0, 0x1e, 0x5a, 0x0e, 0x9f,
                     0x3a, 0x4b, 0xd2, 0xc6, 0x00, 0x00, 0xa1, 0xe5);
const ble_uuid128_t LivisBle::kQuotaUuid =
    BLE_UUID128_INIT(0x01, 0xee, 0xff, 0xc0, 0x1e, 0x5a, 0x0e, 0x9f,
                     0x3a, 0x4b, 0xd2, 0xc6, 0x01, 0x00, 0xa1, 0xe5);
const ble_uuid128_t LivisBle::kStatusUuid =
    BLE_UUID128_INIT(0x01, 0xee, 0xff, 0xc0, 0x1e, 0x5a, 0x0e, 0x9f,
                     0x3a, 0x4b, 0xd2, 0xc6, 0x02, 0x00, 0xa1, 0xe5);
LivisBle* LivisBle::instance_ = nullptr;
uint8_t LivisBle::own_addr_type_ = 0;

const struct ble_gatt_chr_def LivisBle::kCharacteristics[] = {
    {
        .uuid = &LivisBle::kQuotaUuid.u,
        .access_cb = LivisBle::QuotaAccess,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                 BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid = &LivisBle::kStatusUuid.u,
        .access_cb = LivisBle::StatusAccess,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {},  // terminator
};

const struct ble_gatt_svc_def LivisBle::kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &LivisBle::kServiceUuid.u,
        .characteristics = LivisBle::kCharacteristics,
    },
    {},  // terminator
};

#endif  // LIVIS_BLE_H
