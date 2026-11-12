#ifndef LIVIS_PAGES_H
#define LIVIS_PAGES_H

#include <lvgl.h>
#include <material_symbols.h>
#include <wifi_manager.h>
#include "livis_format.h"

#include <cstdio>
#include <ctime>
#include <string>

// Presentation-ready values supplied by future data providers. No network or credentials here.
struct LivisPageData {
    struct Weather {
        bool available = false;
        std::string city;
        std::string summary;
        std::string temperature;
        std::string minimum;
        std::string maximum;
        std::string updated;
    } weather;
    struct Quote {
        bool available = false;
        std::string price;
        std::string change;
        std::string status;
    } hk, us;
    std::string stocks_updated;
    struct Quota {
        bool available = false;
        int remaining_percent = 0;
        std::string reset;
    } five_hour, weekly;
    std::string codex_updated;
};

// All methods are called with the LVGL lock held, or from the LVGL task.
class LivisPages {
public:
    void Create(lv_obj_t* face, int diameter, const lv_font_t* icon_font) {
        diameter_ = diameter;
        for (int i = 0; i < 3; ++i) {
            pages_[i] = lv_obj_create(face);
            lv_obj_set_size(pages_[i], diameter, diameter);
            lv_obj_center(pages_[i]);
            lv_obj_set_style_bg_opa(pages_[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(pages_[i], 0, 0);
            lv_obj_set_style_pad_all(pages_[i], 0, 0);
            lv_obj_remove_flag(pages_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
        }
        auto date_row = Row(pages_[0], 43, 185, 26, LV_FLEX_ALIGN_CENTER);
        date_ = RowLabel(date_row, "--/--");
        weekday_ = RowLabel(date_row, "");
        wifi_ = RowLabel(date_row, "");
        lv_obj_set_style_text_font(wifi_, icon_font, 0);
        clock_ = Label(pages_[0], "--:--", 74, 190);
        lv_obj_set_style_text_font(clock_, &lv_font_montserrat_48, 0);
        auto weather_row = Row(pages_[0], 136, 180, 34, LV_FLEX_ALIGN_CENTER);
        weather_icon_ = WeatherIcon(weather_row);
        weather_ = RowLabel(weather_row, "待更新");
        temperature_ = RowLabel(weather_row, "--°");
        // Both labels inherit the current theme font, including after a theme refresh.
        weather_detail_ = Label(pages_[0], "", 170, 176);
        weather_stale_ = Label(pages_[0], "", 190, 130);

        Label(pages_[1], "理想汽车", 37, 150);
        auto hk_header = Row(pages_[1], 64, 180, 24, LV_FLEX_ALIGN_SPACE_BETWEEN);
        RowLabel(hk_header, "港股 2015");
        hk_change_ = RowLabel(hk_header, "--");
        auto hk_price_row = Row(pages_[1], 86, 180, 26, LV_FLEX_ALIGN_START);
        hk_ = RowLabel(hk_price_row, "--");
        lv_obj_set_style_text_font(hk_, &lv_font_montserrat_20, 0);
        RowLabel(hk_price_row, "HKD");
        auto divider = lv_obj_create(pages_[1]);
        lv_obj_set_size(divider, diameter * 140 / 240, 1);
        lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, diameter * 117 / 240);
        lv_obj_set_style_border_width(divider, 0, 0);
        lv_obj_set_style_bg_color(divider, lv_color_hex(0xEADACC), 0);
        lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
        auto us_header = Row(pages_[1], 127, 180, 24, LV_FLEX_ALIGN_SPACE_BETWEEN);
        RowLabel(us_header, "美股 LI");
        us_change_ = RowLabel(us_header, "--");
        auto us_price_row = Row(pages_[1], 149, 180, 26, LV_FLEX_ALIGN_START);
        us_ = RowLabel(us_price_row, "--");
        lv_obj_set_style_text_font(us_, &lv_font_montserrat_20, 0);
        RowLabel(us_price_row, "USD");
        stocks_updated_ = Label(pages_[1], "等待更新", 190, 150);

        Label(pages_[2], "Codex 用量", 43, 150);
        five_ = Label(pages_[2], "5 小时  --", 77, 170);
        five_bar_ = Progress(pages_[2], 98);
        five_reset_ = Label(pages_[2], "等待更新", 106, 170);
        weekly_ = Label(pages_[2], "每周  --", 132, 170);
        weekly_bar_ = Progress(pages_[2], 153);
        weekly_reset_ = Label(pages_[2], "等待更新", 161, 170);
        codex_updated_ = Label(pages_[2], "等待更新", 184, 170);

        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                auto dot = lv_obj_create(pages_[i]);
                lv_obj_set_size(dot, 5, 5);
                lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, (j - 1) * 12, -diameter * 9 / 100);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_border_width(dot, 0, 0);
                lv_obj_set_style_bg_color(dot, lv_color_hex(i == j ? 0xA8663E : 0xDFCBB8), 0);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            }
        }
        RenderData();
        UpdateClockAndWifi();
    }

    void SetVisible(bool visible) {
        visible_ = visible;
        for (int i = 0; i < 3; ++i) {
            if (pages_[i] == nullptr) continue;
            if (visible && i == page_) lv_obj_remove_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    void Next() { page_ = (page_ + 1) % 3; SetVisible(visible_); }
    void Home() { page_ = 0; SetVisible(visible_); }
    void SetData(const LivisPageData& data) { data_ = data; RenderData(); }
    void SetWeather(const LivisPageData::Weather& weather) { data_.weather = weather; RenderData(); }
    void SetStocks(const LivisPageData::Quote& hk, const LivisPageData::Quote& us,
                   const std::string& updated) {
        data_.hk = hk;
        data_.us = us;
        data_.stocks_updated = updated;
        RenderData();
    }

    void UpdateClockAndWifi() {
        if (clock_ == nullptr) return;
        time_t now = time(nullptr);
        struct tm local = {};
        localtime_r(&now, &local);
        const int minute_key = local.tm_year >= 125 ? local.tm_yday * 1440 + local.tm_hour * 60 + local.tm_min : -1;
        if (minute_key != last_minute_) {
            last_minute_ = minute_key;
            if (local.tm_year >= 125) {
            char text[24];
            std::snprintf(text, sizeof(text), "%02d:%02d", local.tm_hour, local.tm_min);
            lv_label_set_text(clock_, text);
            const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
            std::snprintf(text, sizeof(text), "%d月%d日", local.tm_mon + 1, local.tm_mday);
            lv_label_set_text(date_, text);
            std::snprintf(text, sizeof(text), "周%s", weekdays[local.tm_wday]);
            lv_label_set_text(weekday_, text);
            } else {
                lv_label_set_text(clock_, "--:--");
                lv_label_set_text(date_, "等待时间同步");
                lv_label_set_text(weekday_, "");
            }
        }
        const bool connected = WifiManager::GetInstance().IsConnected();
        if (last_wifi_ != static_cast<int>(connected)) {
            last_wifi_ = connected;
            lv_label_set_text(wifi_, connected ? MATERIAL_SYMBOLS_WIFI : MATERIAL_SYMBOLS_WIFI_OFF);
            lv_obj_set_style_text_color(wifi_, lv_color_hex(connected ? 0x66876B : 0x9C5B48), 0);
        }
    }

private:
    int diameter_ = 0;
    int page_ = 0;
    bool visible_ = false;
    int last_minute_ = -2;
    int last_wifi_ = -1;
    LivisPageData data_;
    lv_obj_t* pages_[3] = {};
    lv_obj_t *date_ = nullptr, *weekday_ = nullptr, *wifi_ = nullptr, *clock_ = nullptr, *weather_ = nullptr;
    lv_obj_t *weather_icon_ = nullptr, *sun_ = nullptr, *cloud_ = nullptr, *rain_ = nullptr;
    lv_obj_t *temperature_ = nullptr, *weather_detail_ = nullptr, *weather_stale_ = nullptr;
    lv_obj_t *hk_ = nullptr, *hk_change_ = nullptr, *us_ = nullptr;
    lv_obj_t *us_change_ = nullptr, *stocks_updated_ = nullptr, *five_ = nullptr;
    lv_obj_t *five_reset_ = nullptr, *weekly_ = nullptr, *weekly_reset_ = nullptr;
    lv_obj_t* codex_updated_ = nullptr;
    lv_obj_t *five_bar_ = nullptr, *weekly_bar_ = nullptr;

    lv_obj_t* Label(lv_obj_t* parent, const char* text, int y, int width) {
        auto label = lv_label_create(parent);
        lv_obj_set_width(label, diameter_ * width / 240);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, diameter_ * y / 240);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x60402F), 0);
        lv_label_set_text(label, text);
        return label;
    }
    lv_obj_t* Row(lv_obj_t* parent, int y, int width, int height, lv_flex_align_t alignment) {
        auto row = lv_obj_create(parent);
        lv_obj_set_size(row, diameter_ * width / 240, diameter_ * height / 240);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, diameter_ * y / 240);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, diameter_ * 6 / 240, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, alignment, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        return row;
    }
    static lv_obj_t* RowLabel(lv_obj_t* parent, const char* text) {
        auto label = lv_label_create(parent);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0x60402F), 0);
        return label;
    }
    lv_obj_t* Progress(lv_obj_t* parent, int y) {
        auto bar = lv_bar_create(parent);
        lv_obj_set_size(bar, diameter_ * 150 / 240, 4);
        lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, diameter_ * y / 240);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xEADACB), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xB8794D), LV_PART_INDICATOR);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        return bar;
    }
    static lv_obj_t* Shape(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color) {
        auto object = lv_obj_create(parent);
        lv_obj_set_pos(object, x, y);
        lv_obj_set_size(object, width, height);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(object, 0, 0);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
        return object;
    }
    lv_obj_t* WeatherIcon(lv_obj_t* parent) {
        auto icon = lv_obj_create(parent);
        lv_obj_set_size(icon, 32, 30);
        lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_set_style_pad_all(icon, 0, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        sun_ = lv_obj_create(icon);
        lv_obj_set_size(sun_, 24, 24);
        lv_obj_set_pos(sun_, 1, 1);
        lv_obj_remove_flag(sun_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(sun_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sun_, 0, 0);
        lv_obj_set_style_pad_all(sun_, 0, 0);
        Shape(sun_, 6, 6, 12, 12, 0xE5AD5C);
        Shape(sun_, 11, 1, 2, 4, 0xE5AD5C);
        Shape(sun_, 11, 19, 2, 4, 0xE5AD5C);
        Shape(sun_, 1, 11, 4, 2, 0xE5AD5C);
        Shape(sun_, 19, 11, 4, 2, 0xE5AD5C);
        Shape(sun_, 3, 3, 3, 3, 0xE5AD5C);
        Shape(sun_, 18, 3, 3, 3, 0xE5AD5C);
        Shape(sun_, 3, 18, 3, 3, 0xE5AD5C);
        Shape(sun_, 18, 18, 3, 3, 0xE5AD5C);
        cloud_ = lv_obj_create(icon);
        lv_obj_set_size(cloud_, 30, 22);
        lv_obj_set_pos(cloud_, 1, 6);
        lv_obj_remove_flag(cloud_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(cloud_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cloud_, 0, 0);
        lv_obj_set_style_pad_all(cloud_, 0, 0);
        Shape(cloud_, 2, 8, 26, 10, 0x9DB3BA);
        Shape(cloud_, 5, 3, 12, 12, 0x9DB3BA);
        Shape(cloud_, 12, 0, 14, 15, 0x9DB3BA);
        rain_ = Shape(icon, 12, 24, 4, 5, 0x7A9AA5);
        return icon;
    }
    void UpdateWeatherIcon() {
        bool valid = data_.weather.available;
        const auto& summary = data_.weather.summary;
        if (valid) lv_obj_remove_flag(weather_icon_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(weather_icon_, LV_OBJ_FLAG_HIDDEN);
        if (summary == "晴" || summary == "多云") lv_obj_remove_flag(sun_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(sun_, LV_OBJ_FLAG_HIDDEN);
        if (summary != "晴") lv_obj_remove_flag(cloud_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cloud_, LV_OBJ_FLAG_HIDDEN);
        if (summary.find("雨") != std::string::npos || summary.find("雪") != std::string::npos)
            lv_obj_remove_flag(rain_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(rain_, LV_OBJ_FLAG_HIDDEN);
    }

    static std::string Updated(const std::string& text) {
        return text.empty() ? "等待更新" : "更新 " + text;
    }
    void RenderData() {
        if (weather_ == nullptr) return;
        UpdateWeatherIcon();
        lv_label_set_text(weather_, data_.weather.available ? data_.weather.summary.c_str() : "待更新");
        lv_label_set_text(temperature_, data_.weather.available ? data_.weather.temperature.c_str() : "--°");
        const auto range = livis::FormatTemperatureRange(data_.weather.maximum, data_.weather.minimum);
        lv_label_set_text(weather_detail_, data_.weather.available ? range.c_str() : "");
        lv_label_set_text(weather_stale_, data_.weather.updated.find(" 旧") != std::string::npos ? "天气待更新" : "");
        SetQuote(hk_, hk_change_, data_.hk);
        SetQuote(us_, us_change_, data_.us);
        lv_label_set_text(stocks_updated_, !data_.hk.available || !data_.us.available ? "等待更新" :
                          data_.stocks_updated.find(" 旧") != std::string::npos ? "行情待更新" : "");
        SetQuota(five_, five_reset_, five_bar_, "5 小时", data_.five_hour);
        SetQuota(weekly_, weekly_reset_, weekly_bar_, "每周", data_.weekly);
        lv_label_set_text(codex_updated_, Updated(data_.codex_updated).c_str());
    }
    static void SetQuote(lv_obj_t* price, lv_obj_t* change, const LivisPageData::Quote& quote) {
        const auto formatted_price = quote.available ? livis::FormatPrice(quote.price) : "--";
        const auto formatted_change = quote.available ? livis::FormatChange(quote.change) : "--";
        lv_label_set_text(price, formatted_price.c_str());
        lv_label_set_text(change, formatted_change.c_str());
        double amount = 0;
        const bool has_change = quote.available && livis::ParseNumber(quote.change, amount);
        lv_obj_set_style_text_color(change, lv_color_hex(!has_change || amount == 0 ? 0x806554 :
                                    amount > 0 ? 0xBF564C : 0x4D8C74), 0);
    }
    static void SetQuota(lv_obj_t* label, lv_obj_t* reset, lv_obj_t* bar, const char* title,
                         const LivisPageData::Quota& quota) {
        char text[48];
        if (quota.available && quota.remaining_percent >= 0 && quota.remaining_percent <= 100) {
            std::snprintf(text, sizeof(text), "%s  %d%%", title, quota.remaining_percent);
            lv_label_set_text(label, text);
            lv_label_set_text(reset, quota.reset.empty() ? "重置时间未知" : quota.reset.c_str());
            lv_bar_set_value(bar, quota.remaining_percent, LV_ANIM_OFF);
        } else {
            std::snprintf(text, sizeof(text), "%s  --", title);
            lv_label_set_text(label, text);
            lv_label_set_text(reset, "等待更新");
            lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        }
    }
};

#endif  // LIVIS_PAGES_H
