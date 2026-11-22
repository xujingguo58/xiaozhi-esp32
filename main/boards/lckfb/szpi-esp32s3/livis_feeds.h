#ifndef LIVIS_FEEDS_H
#define LIVIS_FEEDS_H

#include "board.h"
#include "livis_display.h"
#include "settings.h"
#include "application.h"

#include <sdkconfig.h>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <http.h>
#include <network_interface.h>
#include <wifi_manager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// Runs separately from audio, the main loop and LVGL. Network errors retain timestamped data.
class LivisFeeds {
public:
    explicit LivisFeeds(LivisDisplay* display) : display_(display) {}

    void Start() {
        if (xTaskCreate(TaskEntry, "livis_feeds", 8192, this, 2, nullptr) != pdPASS) {
            ESP_LOGE("LivisFeeds", "Unable to start feed task");
        }
    }

private:
    LivisDisplay* display_;
    LivisPageData::Weather weather_;
    LivisPageData::Indoor indoor_;
    std::string indoor_read_at_;
    int64_t next_indoor_ = 0, indoor_success_ = 0;
    LivisPageData::Quote hk_, us_;
    std::string stocks_updated_;
    double latitude_ = 0, longitude_ = 0;
    std::string city_;
    bool has_location_ = false;
    int64_t next_location_ = 0, next_weather_ = 0, next_stocks_ = 0;
    int64_t weather_success_ = 0, stocks_success_ = 0;
    unsigned location_failures_ = 0, weather_failures_ = 0;
    static constexpr int64_t kWeatherIntervalUs = 30LL * 60 * 1000000;
    static constexpr int64_t kStocksIntervalUs = 5LL * 60 * 1000000;
    static constexpr int64_t kRetryUs = 2LL * 60 * 1000000;
    static constexpr int64_t kInitialRetryUs = 15LL * 1000000;

    static int64_t RetryDelay(unsigned& failures) {
        if (failures < 4) ++failures;
        return failures <= 3 ? kInitialRetryUs : kRetryUs;
    }

    static void TaskEntry(void* arg) { static_cast<LivisFeeds*>(arg)->Run(); }

    static bool Get(const std::string& url, std::string& body, size_t limit,
                    const char* referer = nullptr, const char* bearer = nullptr) {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
        if (!http) return false;
        http->SetTimeout(bearer ? 25000 : 8000);
        http->SetHeader("Accept-Encoding", "identity");
        http->SetHeader("User-Agent", "LivisDesktop/0.1");
        if (referer) http->SetHeader("Referer", referer);
        if (bearer && *bearer) http->SetHeader("Authorization", std::string("Bearer ") + bearer);
        if (!http->Open("GET", url)) return false;
        const auto status = http->GetStatusCode();
        if (!status || *status != 200) { http->Close(); return false; }
        body.clear();
        char buf[512];
        while (true) {
            auto count = http->Read(buf, sizeof(buf));
            if (!count || *count < 0) { http->Close(); return false; }
            if (*count == 0) break;
            if (body.size() + *count > limit) { http->Close(); return false; }
            body.append(buf, *count);
        }
        http->Close();
        return !body.empty();
    }

    static cJSON* Field(cJSON* obj, const char* key) {
        return cJSON_IsObject(obj) ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
    }
    static bool Number(cJSON* obj, const char* key, double& value) {
        auto item = Field(obj, key);
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) return false;
        value = item->valuedouble;
        return true;
    }
    static std::string TimeNow() {
        time_t now = time(nullptr);
        struct tm local = {};
        localtime_r(&now, &local);
        if (local.tm_year < 125) return "时间未同步";
        char text[20];
        std::strftime(text, sizeof(text), "%m-%d %H:%M", &local);
        return text;
    }
    static std::string Temperature(double value) {
        return livis::FormatTemperature(value);
    }

    static bool IndoorRange(cJSON* root, const char* key, double low, double high,
                            const char* unit, std::string& result) {
        auto object = Field(root, key);
        double min, max, count;
        if (!Number(object, "min", min) || !Number(object, "max", max) ||
            !Number(object, "count", count) || count < 1 || min < low || max > high || min > max) return false;
        char text[48];
        if (std::lround(min) == std::lround(max))
            std::snprintf(text, sizeof(text), "%.0f%s", min, unit);
        else
            std::snprintf(text, sizeof(text), "%.0f~%.0f%s", min, max, unit);
        result = text;
        return true;
    }

    bool UpdateIndoor(int64_t now) {
        if (std::strncmp(CONFIG_LIVIS_INDOOR_URL, "https://", 8) != 0) {
            ESP_LOGE("LivisFeeds", "Indoor API requires HTTPS");
            return false;
        }
        std::string body;
        if (!Get(CONFIG_LIVIS_INDOOR_URL, body, 4096, nullptr, CONFIG_LIVIS_INDOOR_TOKEN)) return false;
        cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
        auto status = Field(root, "status");
        auto read_at = Field(root, "read_at");
        LivisPageData::Indoor value;
        bool ok = cJSON_IsString(status) && status->valuestring &&
                  (!std::strcmp(status->valuestring, "ok") || !std::strcmp(status->valuestring, "partial")) &&
                  cJSON_IsString(read_at) && read_at->valuestring && *read_at->valuestring;
        if (ok) {
            value.temperature_available = IndoorRange(root, "temperature_c", -40, 85, "°", value.temperature);
            value.humidity_available = IndoorRange(root, "humidity_percent", 0, 100, "%", value.humidity);
            value.partial = !std::strcmp(status->valuestring, "partial");
            ok = value.temperature_available || value.humidity_available;
            if (ok && indoor_read_at_ != read_at->valuestring) {
                indoor_read_at_ = read_at->valuestring;
                indoor_success_ = now;
                indoor_ = value;
                display_->SetIndoor(indoor_);
            }
        }
        cJSON_Delete(root);
        return ok;
    }
    static const char* WeatherName(int code) {
        if (code == 0) return "晴";
        if (code <= 3) return "多云";
        if (code == 45 || code == 48) return "雾";
        if (code >= 51 && code <= 67) return "雨";
        if (code >= 71 && code <= 77) return "雪";
        if (code >= 80 && code <= 82) return "阵雨";
        if (code >= 85 && code <= 86) return "阵雪";
        if (code >= 95 && code <= 99) return "雷雨";
        return "天气";
    }
    static const char* WttrName(int code) {
        if (code == 113) return "晴";
        if (code == 116 || code == 119 || code == 122) return "多云";
        if (code == 143 || code == 248 || code == 260) return "雾";
        if (code >= 386) return "雷雨";
        if ((code >= 179 && code <= 185) || (code >= 227 && code <= 230) ||
            (code >= 320 && code <= 338) ||
            (code >= 350 && code <= 377)) return "雪";
        if (code >= 176) return "雨";
        return "天气";
    }

    // Persisted IP location (rewritten after every successful lookup) and the
    // manual override, stored in NVS as decimal-degree strings; erase lat/lon
    // to force a fresh IP lookup.
    bool LoadCachedLocation() {
        Settings settings("livis_loc", false);
        std::string lat = settings.GetString("lat", ""), lon = settings.GetString("lon", "");
        if (lat.empty() || lon.empty()) return false;
        char *lat_end = nullptr, *lon_end = nullptr;
        double a = std::strtod(lat.c_str(), &lat_end), b = std::strtod(lon.c_str(), &lon_end);
        if (lat_end == lat.c_str() || *lat_end != '\0' || lon_end == lon.c_str() ||
            *lon_end != '\0' || !std::isfinite(a) || !std::isfinite(b) ||
            a < -90 || a > 90 || b < -180 || b > 180) return false;
        latitude_ = a; longitude_ = b;
        city_ = settings.GetString("city", "已设位置");
        return has_location_ = true;
    }

    bool FinishLocation(double lat, double lon, const char* city) {
        if (!std::isfinite(lat) || !std::isfinite(lon) ||
            lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
        latitude_ = lat;
        longitude_ = lon;
        city_ = city && city[0] ? city : "IP定位";
        has_location_ = true;
        Settings writer("livis_loc", true);
        char text[24];
        std::snprintf(text, sizeof(text), "%.4f", latitude_);
        writer.SetString("lat", text);
        std::snprintf(text, sizeof(text), "%.4f", longitude_);
        writer.SetString("lon", text);
        writer.SetString("city", city_);
        return true;
    }

    bool Ip9Location() {
        // Domestic geolocation (https://ip9.com.cn, 60 req/min free tier):
        // locates the requester without a parameter, UTF-8, lat/lng as strings.
        std::string body;
        if (!Get("https://ip9.com.cn/get", body, 4096)) return false;
        cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
        auto data = Field(root, "data");
        auto ret = Field(root, "ret");
        auto lat = Field(data, "lat");
        auto lng = Field(data, "lng");
        auto city = Field(data, "city");
        char *lat_end = nullptr, *lng_end = nullptr;
        double a = 0, b = 0;
        bool ok = cJSON_IsNumber(ret) && ret->valuedouble == 200 &&
                  cJSON_IsString(lat) && lat->valuestring != nullptr &&
                  cJSON_IsString(lng) && lng->valuestring != nullptr;
        if (ok) {
            a = std::strtod(lat->valuestring, &lat_end);
            b = std::strtod(lng->valuestring, &lng_end);
            ok = lat_end != lat->valuestring && *lat_end == '\0' &&
                 lng_end != lng->valuestring && *lng_end == '\0';
        }
        if (ok) ok = FinishLocation(a, b, cJSON_IsString(city) ? city->valuestring : nullptr);
        cJSON_Delete(root);
        return ok;
    }

    bool IpWhoLocation() {
        std::string body;
        if (!Get("https://ipwho.is/?lang=zh-CN", body, 8192)) return false;
        cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
        double a = 0, b = 0;
        auto success = Field(root, "success");
        auto city = Field(root, "city");
        bool ok = cJSON_IsTrue(success) && Number(root, "latitude", a) && Number(root, "longitude", b) &&
                  FinishLocation(a, b, cJSON_IsString(city) ? city->valuestring : nullptr);
        cJSON_Delete(root);
        return ok;
    }

    // Fresh lookups first (ip9 is domestic and fast, ipwho.is is the backup);
    // the caller applies the persisted cache when both fail, so a dead
    // provider never blocks the weather fetch.
    bool ResolveLocation() {
        return Ip9Location() || IpWhoLocation();
    }

    bool OpenMeteo(LivisPageData::Weather& out) {
        char url[320];
        std::snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1", latitude_, longitude_);
        std::string body;
        if (!Get(url, body, 16384)) return false;
        cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
        auto current = Field(root, "current"); auto daily = Field(root, "daily");
        double temp, code;
        auto max = cJSON_GetArrayItem(Field(daily, "temperature_2m_max"), 0);
        auto min = cJSON_GetArrayItem(Field(daily, "temperature_2m_min"), 0);
        bool ok = Number(current, "temperature_2m", temp) && Number(current, "weather_code", code) &&
                  cJSON_IsNumber(max) && cJSON_IsNumber(min) && std::isfinite(max->valuedouble) && std::isfinite(min->valuedouble);
        if (ok) {
            out.available = true; out.city = city_; out.summary = WeatherName(static_cast<int>(code));
            out.temperature = Temperature(temp);
            out.minimum = Temperature(min->valuedouble);
            out.maximum = Temperature(max->valuedouble);
            out.updated = TimeNow();
        }
        cJSON_Delete(root);
        return ok;
    }

    static bool NumericString(cJSON* object, const char* key, double& result) {
        auto item = Field(object, key);
        if (!cJSON_IsString(item) || !item->valuestring) return false;
        char* end = nullptr; result = std::strtod(item->valuestring, &end);
        return end != item->valuestring && *end == '\0' && std::isfinite(result);
    }
    bool Wttr(LivisPageData::Weather& out) {
        char url[150];
        std::snprintf(url, sizeof(url), "https://wttr.in/%.4f,%.4f?format=j1", latitude_, longitude_);
        std::string body;
        if (!Get(url, body, 65536)) return false;
        cJSON* root = cJSON_ParseWithLength(body.c_str(), body.size());
        auto current = cJSON_GetArrayItem(Field(root, "current_condition"), 0);
        auto day = cJSON_GetArrayItem(Field(root, "weather"), 0);
        double temp, code, high, low;
        bool ok = NumericString(current, "temp_C", temp) && NumericString(current, "weatherCode", code) &&
                  NumericString(day, "maxtempC", high) && NumericString(day, "mintempC", low);
        if (ok) {
            out.available = true; out.city = city_; out.summary = WttrName(static_cast<int>(code));
            out.temperature = Temperature(temp); out.minimum = Temperature(low); out.maximum = Temperature(high);
            out.updated = TimeNow();
        }
        cJSON_Delete(root);
        return ok;
    }

    static std::vector<std::string> Split(const std::string& input, char delimiter) {
        std::vector<std::string> result;
        size_t pos = 0;
        while (pos <= input.size()) {
            size_t end = input.find(delimiter, pos);
            result.push_back(input.substr(pos, end == std::string::npos ? end : end - pos));
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        return result;
    }
    static bool ValidNumber(const std::string& value, bool positive) {
        char* end = nullptr;
        double parsed = std::strtod(value.c_str(), &end);
        return end != value.c_str() && *end == '\0' && std::isfinite(parsed) && (!positive || parsed > 0);
    }
    static std::string CompactTime(const std::string& value) {
        // Keep the exchange-provided date and time, not just the fetch time.
        if (value.size() >= 16 && value[4] == '-' && value[7] == '-') return value.substr(5, 11);
        if (value.size() >= 16 && value[4] == '/' && value[7] == '/') return value.substr(5, 11);
        return value;
    }
    static bool ParseLine(const std::string& body, const char* symbol, char delimiter,
                          size_t price_index, size_t change_index, size_t time_index,
                          LivisPageData::Quote& out) {
        auto start = body.find(symbol);
        if (start == std::string::npos) return false;
        start = body.find('"', start);
        if (start == std::string::npos) return false;
        auto end = body.find('"', ++start);
        if (end == std::string::npos) return false;
        auto fields = Split(body.substr(start, end - start), delimiter);
        if (fields.size() <= std::max(price_index, std::max(change_index, time_index)) ||
            !ValidNumber(fields[price_index], true) || !ValidNumber(fields[change_index], false) ||
            fields[time_index].empty()) return false;
        out.available = true; out.price = fields[price_index];
        out.change = (fields[change_index][0] != '-' && fields[change_index][0] != '+' ? "+" : "") + fields[change_index] + "%";
        out.status = CompactTime(fields[time_index]);
        return true;
    }
    bool Tencent(LivisPageData::Quote& hk, LivisPageData::Quote& us) {
        std::string body;
        if (!Get("https://qt.gtimg.cn/q=hk02015,usLI", body, 8192)) return false;
        return ParseLine(body, "v_hk02015=", '~', 3, 32, 30, hk) &&
               ParseLine(body, "v_usLI=", '~', 3, 32, 30, us);
    }
    bool Sina(LivisPageData::Quote& hk, LivisPageData::Quote& us) {
        std::string body;
        if (!Get("https://hq.sinajs.cn/list=hk02015,gb_li", body, 8192, "https://finance.sina.com.cn/")) return false;
        if (!ParseLine(body, "hq_str_hk02015=", ',', 6, 8, 17, hk) ||
            !ParseLine(body, "hq_str_gb_li=", ',', 1, 2, 3, us)) return false;
        // Hong Kong date and time are separate fields in this endpoint.
        auto start = body.find("hq_str_hk02015=");
        auto quote = body.find('"', start);
        auto end = body.find('"', quote + 1);
        auto fields = Split(body.substr(quote + 1, end - quote - 1), ',');
        if (fields.size() > 18 && !fields[18].empty()) hk.status = CompactTime(fields[17] + " " + fields[18]);
        return true;
    }

    void Run() {
        vTaskDelay(pdMS_TO_TICKS(3000));
        while (true) {
            const int64_t now = esp_timer_get_time();
            if (WifiManager::GetInstance().IsConnected()) {
                if (*CONFIG_LIVIS_INDOOR_TOKEN && *CONFIG_LIVIS_INDOOR_URL && now >= next_indoor_ &&
                    Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
                    bool ok = UpdateIndoor(now);
                    next_indoor_ = now + (ok ? static_cast<int64_t>(CONFIG_LIVIS_INDOOR_INTERVAL_SECONDS) * 1000000 : kRetryUs);
                    ESP_LOGI("LivisFeeds", "Indoor update %s", ok ? "completed" : "unavailable");
                }
                if (now >= next_location_) {
                    if (ResolveLocation()) {
                        location_failures_ = 0;
                        next_location_ = now + 24LL * 60 * 60 * 1000000;
                    } else {
                        next_location_ = now + RetryDelay(location_failures_);
                        ESP_LOGW("LivisFeeds", "Weather location unavailable; retry scheduled");
                        // Geolocation endpoints are the flakiest link on this
                        // network; fall back to persisted coordinates so the
                        // weather fetch itself is not held hostage.
                        if (LoadCachedLocation()) {
                            ESP_LOGI("LivisFeeds", "Using persisted location");
                        }
                    }
                }
                if (has_location_ && now >= next_weather_) {
                    LivisPageData::Weather value;
                    if (OpenMeteo(value) || Wttr(value)) {
                        weather_ = value; weather_success_ = now; weather_failures_ = 0;
                        display_->SetWeather(weather_);
                        next_weather_ = now + kWeatherIntervalUs;
                        ESP_LOGI("LivisFeeds", "Weather updated");
                    } else {
                        if (weather_.available) {
                            if (weather_.updated.find(" 旧") == std::string::npos) weather_.updated += " 旧";
                            display_->SetWeather(weather_);
                        }
                        next_weather_ = now + (weather_.available ? kRetryUs : RetryDelay(weather_failures_));
                        ESP_LOGW("LivisFeeds", "Weather sources unavailable");
                    }
                }
                if (now >= next_stocks_) {
                    LivisPageData::Quote hk, us;
                    if (Tencent(hk, us) || Sina(hk, us)) {
                        hk_ = hk; us_ = us; stocks_updated_ = TimeNow(); stocks_success_ = now;
                        display_->SetStocks(hk_, us_, stocks_updated_);
                        next_stocks_ = now + kStocksIntervalUs;
                        ESP_LOGI("LivisFeeds", "Stock quotes updated");
                    } else {
                        if (hk_.available || us_.available) {
                            if (stocks_updated_.find(" 旧") == std::string::npos) stocks_updated_ += " 旧";
                            display_->SetStocks(hk_, us_, stocks_updated_);
                        }
                        next_stocks_ = now + kRetryUs;
                        ESP_LOGW("LivisFeeds", "Stock sources unavailable");
                    }
                }
            }
            // Mark retained data stale even if the connection disappears before a scheduled fetch.
            const int64_t indoor_stale_us = std::max(5LL * 60 * 1000000,
                (static_cast<long long>(CONFIG_LIVIS_INDOOR_INTERVAL_SECONDS) + 2 * 60) * 1000000);
            if (indoor_success_ && now - indoor_success_ > indoor_stale_us && !indoor_.stale) {
                indoor_.stale = true; display_->SetIndoor(indoor_);
            }
            if (weather_success_ && now - weather_success_ > kWeatherIntervalUs + kRetryUs &&
                weather_.updated.find(" 旧") == std::string::npos) {
                weather_.updated += " 旧"; display_->SetWeather(weather_);
            }
            if (stocks_success_ && now - stocks_success_ > kStocksIntervalUs + kRetryUs &&
                stocks_updated_.find(" 旧") == std::string::npos) {
                stocks_updated_ += " 旧"; display_->SetStocks(hk_, us_, stocks_updated_);
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
};

#endif  // LIVIS_FEEDS_H
