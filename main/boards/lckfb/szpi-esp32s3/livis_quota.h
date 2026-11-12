#ifndef LIVIS_QUOTA_H
#define LIVIS_QUOTA_H

// Payload of the Mac agent's quota push (docs/codex-usage-sync.md §5), parsed
// independently of the display and BLE stacks so it can be host-tested.

#include <cJSON.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

struct LivisQuotaMessage {
    time_t sent_at = 0;
    int five_hour_percent = -1;
    int weekly_percent = -1;
    time_t five_hour_reset = 0;
    time_t weekly_reset = 0;
};

class LivisQuota {
public:
    // {"k":"quota","v":1,"t":<epoch>,"f":<0-100>,"w":<0-100>,"fr":<epoch>,"wr":<epoch>}
    // Unknown kinds, other versions, out-of-range values and implausible
    // timestamps are rejected; unknown extra fields are ignored.
    static bool Parse(const char* json, LivisQuotaMessage& message) {
        message = {};
        cJSON* root = cJSON_Parse(json);
        if (!cJSON_IsObject(root)) {
            cJSON_Delete(root);
            return false;
        }
        bool ok = Text(Field(root, "k"), "quota") &&
                  cJSON_IsNumber(Field(root, "v")) && Field(root, "v")->valuedouble == 1 &&
                  Epoch(Field(root, "t"), message.sent_at) &&
                  Percent(Field(root, "f"), message.five_hour_percent) &&
                  Percent(Field(root, "w"), message.weekly_percent) &&
                  Epoch(Field(root, "fr"), message.five_hour_reset) &&
                  Epoch(Field(root, "wr"), message.weekly_reset);
        cJSON_Delete(root);
        return ok;
    }

    // "重置 14:35" for the 5-hour window, "重置 周一 08:00" for weekly. Empty
    // (the page shows 重置时间未知) for implausible epochs or unsynced time.
    static std::string FormatReset(time_t epoch, bool weekly) {
        struct tm local = {};
        if (!Plausible(epoch) || !localtime_r(&epoch, &local) || local.tm_year < 125) return "";
        char text[24];
        if (weekly) {
            static const char* kWeekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
            std::snprintf(text, sizeof(text), "重置 周%s %02d:%02d",
                          kWeekdays[local.tm_wday], local.tm_hour, local.tm_min);
        } else {
            std::snprintf(text, sizeof(text), "重置 %02d:%02d", local.tm_hour, local.tm_min);
        }
        return text;
    }

    // "MM-DD HH:MM" matching LivisFeeds::TimeNow; placeholder before time sync.
    static std::string FormatStamp(time_t epoch) {
        struct tm local = {};
        if (!Plausible(epoch) || !localtime_r(&epoch, &local) || local.tm_year < 125) {
            return "时间未同步";
        }
        char text[20];
        std::strftime(text, sizeof(text), "%m-%d %H:%M", &local);
        return text;
    }

private:
    static cJSON* Field(cJSON* obj, const char* key) {
        return cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
    }
    static bool Text(cJSON* item, const char* expected) {
        return cJSON_IsString(item) && item->valuestring != nullptr &&
               std::strcmp(item->valuestring, expected) == 0;
    }
    static bool Epoch(cJSON* item, time_t& value) {
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) return false;
        value = static_cast<time_t>(item->valuedouble);
        return Plausible(value);
    }
    static bool Percent(cJSON* item, int& value) {
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) return false;
        value = static_cast<int>(std::lround(item->valuedouble));
        return value >= 0 && value <= 100;
    }
    // 2020-01-01 .. 2100-01-01; keeps stale marking sane if the payload is garbage.
    static bool Plausible(time_t value) {
        return value >= 1577836800 && value <= 4102444800;
    }
};

#endif  // LIVIS_QUOTA_H
