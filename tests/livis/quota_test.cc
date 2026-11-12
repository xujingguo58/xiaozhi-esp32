// Host test for the quota payload parser. Run from the firmware root:
//   clang++ -std=c++20 -I main/boards/lckfb/szpi-esp32s3 \
//     -I managed_components/espressif__cjson/cJSON tests/livis/quota_test.cc \
//     managed_components/espressif__cjson/cJSON.c && ./a.out
// Reset/stamp strings depend on the runner's timezone, so only their stable
// parts are asserted; the numeric fields are timezone independent.
#include "../../main/boards/lckfb/szpi-esp32s3/livis_quota.h"
#include <cassert>
#include <cstring>

int main() {
    LivisQuotaMessage message;

    // Well-formed v1 payload, including unknown extra fields that must be ignored.
    assert(LivisQuota::Parse(
        R"({"k":"quota","v":1,"t":1794400000,"f":62,"w":83,"fr":1794410000,"wr":1795000000,"x":1})",
        message));
    assert(message.sent_at == 1794400000);
    assert(message.five_hour_percent == 62);
    assert(message.weekly_percent == 83);
    assert(message.five_hour_reset == 1794410000);
    assert(message.weekly_reset == 1795000000);

    // Rounded percentages from the agent are accepted.
    assert(LivisQuota::Parse(
        R"({"k":"quota","v":1,"t":1794400000,"f":62.4,"w":0,"fr":1794410000,"wr":1795000000})",
        message));
    assert(message.five_hour_percent == 62);
    assert(message.weekly_percent == 0);

    // Unknown kind (forward compatibility: other message types) is dropped.
    assert(!LivisQuota::Parse(
        R"({"k":"media","v":1,"t":1794400000,"f":62,"w":83,"fr":1794410000,"wr":1795000000})",
        message));
    // Wrong version, missing field, bad JSON, out-of-range and garbage epochs.
    assert(!LivisQuota::Parse(
        R"({"k":"quota","v":2,"t":1794400000,"f":62,"w":83,"fr":1794410000,"wr":1795000000})",
        message));
    assert(!LivisQuota::Parse(
        R"({"k":"quota","v":1,"t":1794400000,"f":62,"wr":1795000000})", message));
    assert(!LivisQuota::Parse("{\"k\":\"quota\",", message));
    assert(!LivisQuota::Parse(
        R"({"k":"quota","v":1,"t":1794400000,"f":101,"w":83,"fr":1794410000,"wr":1795000000})",
        message));
    assert(!LivisQuota::Parse(
        R"({"k":"quota","v":1,"t":1794400000,"f":-1,"w":83,"fr":1794410000,"wr":1795000000})",
        message));
    assert(!LivisQuota::Parse(
        R"({"k":"quota","v":1,"t":0,"f":62,"w":83,"fr":1794410000,"wr":1795000000})",
        message));
    assert(!LivisQuota::Parse(
        R"({"k":"quota","v":1,"t":99999999999,"f":62,"w":83,"fr":1794410000,"wr":1795000000})",
        message));

    // Reset stamps: stable parts only, timezone differences allowed.
    const std::string five = LivisQuota::FormatReset(1794410000, false);
    assert(five.rfind("重置 ", 0) == 0 && five.size() > 8 && five.find(':') != std::string::npos);
    const std::string weekly = LivisQuota::FormatReset(1795000000, true);
    assert(weekly.rfind("重置 周", 0) == 0 && weekly.find(':') != std::string::npos);
    // Implausible epochs render as empty; the page shows 重置时间未知.
    assert(LivisQuota::FormatReset(0, false).empty());
    assert(LivisQuota::FormatReset(99999999999, true).empty());

    // Update stamp shape "MM-DD HH:MM".
    const std::string stamp = LivisQuota::FormatStamp(1794400000);
    assert(stamp.size() == 11 && stamp[2] == '-' && stamp[5] == ' ' && stamp[8] == ':');
    assert(LivisQuota::FormatStamp(0) == "时间未同步");
    return 0;
}
