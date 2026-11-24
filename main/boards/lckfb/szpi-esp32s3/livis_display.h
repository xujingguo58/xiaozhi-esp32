#ifndef LIVIS_DISPLAY_H
#define LIVIS_DISPLAY_H

#include "assets/lang_config.h"
#include "display/lcd_display.h"
#include "livis_pages.h"

#include <algorithm>
#include <cstring>

// Presentation only: the underlying SPI driver and XiaoZhi display interfaces stay intact.
class LivisDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    ~LivisDisplay() override {
        if (animation_timer_ != nullptr) {
            DisplayLockGuard lock(this);
            if (lock) {
                lv_timer_delete(animation_timer_);
                animation_timer_ = nullptr;
            }
        }
    }

    void SetupUI() override {
        LcdDisplay::SetupUI();
        DisplayLockGuard lock(this);
        if (!lock || eyes_[0] != nullptr || emoji_box_ == nullptr) {
            return;
        }

        const int diameter = std::min(width_, height_);
        auto screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAD9C9), 0);
        lv_obj_set_style_bg_color(container_, lv_color_hex(0xEAD9C9), 0);

        auto face = lv_obj_create(screen);
        lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(face, diameter, diameter);
        lv_obj_center(face);
        lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(face, 0, 0);
        lv_obj_set_style_pad_all(face, 0, 0);
        lv_obj_set_style_bg_color(face, lv_color_hex(0xFFF6E9), 0);
        lv_obj_move_to_index(face, 1);
        pages_.Create(face, diameter, lv_obj_get_style_text_font(network_label_, LV_PART_MAIN));

        // Reserve the center 3/4 of the round face for text and status icons.
        lv_obj_set_width(top_bar_, diameter * 72 / 100);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, diameter * 9 / 100);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
        lv_obj_set_width(status_bar_, diameter * 72 / 100);
        lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, diameter * 9 / 100);
        lv_obj_set_width(status_label_, diameter * 54 / 100);
        lv_obj_set_width(notification_label_, diameter * 54 / 100);
        lv_obj_set_width(bottom_bar_, diameter * 72 / 100);
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, -diameter * 12 / 100);
        lv_obj_set_style_bg_color(bottom_bar_, lv_color_hex(0xFFF6E9), 0);
        lv_obj_set_style_text_color(chat_message_label_, lv_color_hex(0x60402F), 0);
        lv_obj_set_width(chat_message_label_, diameter * 66 / 100);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(0x60402F), 0);
        lv_obj_set_style_text_color(notification_label_, lv_color_hex(0x60402F), 0);

        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(emoji_box_, diameter * 70 / 100, diameter * 45 / 100);
        lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, -diameter * 3 / 100);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);

        const int eye_width = diameter * 14 / 100;
        const int eye_height = diameter * 29 / 100;
        for (int i = 0; i < 2; ++i) {
            eyes_[i] = lv_obj_create(emoji_box_);
            lv_obj_remove_flag(eyes_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_size(eyes_[i], eye_width, eye_height);
            lv_obj_align(eyes_[i], LV_ALIGN_CENTER, (i == 0 ? -1 : 1) * diameter * 13 / 100, 0);
            lv_obj_set_style_radius(eyes_[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(eyes_[i], 0, 0);
            lv_obj_set_style_pad_all(eyes_[i], 0, 0);
            lv_obj_set_style_bg_color(eyes_[i], lv_color_hex(0x4D2919), 0);
            lv_obj_set_style_bg_grad_color(eyes_[i], lv_color_hex(0xA45F2F), 0);
            lv_obj_set_style_bg_grad_dir(eyes_[i], LV_GRAD_DIR_VER, 0);

            highlights_[i] = lv_obj_create(eyes_[i]);
            lv_obj_remove_flag(highlights_[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_size(highlights_[i], eye_width * 40 / 100, eye_width * 52 / 100);
            lv_obj_align(highlights_[i], LV_ALIGN_TOP_MID, -eye_width / 8, eye_height / 8);
            lv_obj_set_style_radius(highlights_[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(highlights_[i], 0, 0);
            lv_obj_set_style_bg_color(highlights_[i], lv_color_hex(0xFFFDF8), 0);
        }
        dots_ = lv_label_create(emoji_box_);
        lv_label_set_text(dots_, "...");
        lv_obj_set_style_text_color(dots_, lv_color_hex(0xA45F2F), 0);
        lv_obj_align(dots_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_flag(dots_, LV_OBJ_FLAG_HIDDEN);
        animation_timer_ = lv_timer_create(AnimationTick, 100, this);
    }

    void SetStatus(const char* status) override {
        LvglDisplay::SetStatus(status);
        DisplayLockGuard lock(this);
        if (!lock || status == nullptr || eyes_[0] == nullptr) {
            return;
        }
        if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            state_ = State::Listening;
            SetInfoVisible(false);
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            state_ = State::Speaking;
            SetInfoVisible(false);
        } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            state_ = State::Idle;
            idle_started_ = lv_tick_get();
            pending_idle_ = true;
        } else if (std::strcmp(status, Lang::Strings::CONNECTING) == 0) {
            state_ = State::Idle;
            pending_idle_ = false;
            SetInfoVisible(false);
        }
        UpdateFace();
    }

    void ShowNotification(const char* notification, int duration_ms = 3000) override {
        LvglDisplay::ShowNotification(notification, duration_ms);
        DisplayLockGuard lock(this);
        if (lock && status_bar_ != nullptr && info_visible_) {
            lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            notification_until_ = lv_tick_get() + duration_ms;
        }
    }

    void ShowNotification(const std::string& notification, int duration_ms = 3000) override {
        ShowNotification(notification.c_str(), duration_ms);
    }

    void NextStandbyPage() {
        DisplayLockGuard lock(this);
        if (!lock || eyes_[0] == nullptr) return;
        pending_idle_ = false;
        SetInfoVisible(true);
        pages_.Next();
        last_page_action_ = lv_tick_get();
    }

    // Values are copied into LVGL labels under the display lock; no provider is connected yet.
    void SetStandbyData(const LivisPageData& data) {
        DisplayLockGuard lock(this);
        if (lock && eyes_[0] != nullptr) pages_.SetData(data);
    }

    void SetWeather(const LivisPageData::Weather& weather) {
        DisplayLockGuard lock(this);
        if (lock && eyes_[0] != nullptr) pages_.SetWeather(weather);
    }

    void SetIndoor(const LivisPageData::Indoor& indoor) {
        DisplayLockGuard lock(this);
        if (lock && eyes_[0] != nullptr) pages_.SetIndoor(indoor);
    }

    void SetVehicle(const LivisPageData::Vehicle& vehicle) {
        DisplayLockGuard lock(this);
        if (lock && eyes_[0] != nullptr) pages_.SetVehicle(vehicle);
    }

    void SetStocks(const LivisPageData::Quote& hk, const LivisPageData::Quote& us,
                   const std::string& updated) {
        DisplayLockGuard lock(this);
        if (lock && eyes_[0] != nullptr) pages_.SetStocks(hk, us, updated);
    }

    void SetQuota(const LivisPageData::Quota& five_hour, const LivisPageData::Quota& weekly,
                  const std::string& updated) {
        DisplayLockGuard lock(this);
        if (lock && eyes_[0] != nullptr) pages_.SetQuota(five_hour, weekly, updated);
    }

    void SetEmotion(const char* emotion) override {
        // Keep the Livis eyes visible when the server sends a stock emoji.
        (void)emotion;
    }

    void SetChatMessage(const char* role, const char* content) override {
        LcdDisplay::SetChatMessage(role, content);
        if (role == nullptr || content == nullptr || content[0] == '\0') {
            return;
        }
        DisplayLockGuard lock(this);
        if (!lock || eyes_[0] == nullptr) {
            return;
        }
        if (std::strcmp(role, "user") == 0) {
            // The upstream protocol has no explicit thinking state: use the STT-to-reply interval.
            state_ = State::Thinking;
            SetInfoVisible(false);
        } else if (std::strcmp(role, "assistant") == 0) {
            state_ = State::Speaking;
            SetInfoVisible(false);
        }
        UpdateFace();
    }

private:
    enum class State { Idle, Listening, Thinking, Speaking };
    State state_ = State::Idle;
    lv_obj_t* eyes_[2] = {nullptr, nullptr};
    lv_obj_t* highlights_[2] = {nullptr, nullptr};
    lv_obj_t* dots_ = nullptr;
    lv_timer_t* animation_timer_ = nullptr;
    unsigned tick_ = 0;
    LivisPages pages_;
    bool info_visible_ = false;
    bool pending_idle_ = false;
    uint32_t idle_started_ = 0;
    uint32_t last_page_action_ = 0;
    uint32_t notification_until_ = 0;

    void SetInfoVisible(bool visible) {
        info_visible_ = visible;
        pages_.SetVisible(visible);
        if (visible) {
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            pending_idle_ = false;
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    static void AnimationTick(lv_timer_t* timer) {
        auto* self = static_cast<LivisDisplay*>(lv_timer_get_user_data(timer));
        ++self->tick_;
        const uint32_t now = lv_tick_get();
        if (self->pending_idle_ && now - self->idle_started_ >= 4000) {
            self->pending_idle_ = false;
            self->pages_.Home();
            self->pages_.UpdateClockAndWifi();
            self->SetInfoVisible(true);
            self->last_page_action_ = now;
        }
        if (self->info_visible_) {
            if (self->tick_ % 10 == 0) self->pages_.UpdateClockAndWifi();
            if (now - self->last_page_action_ >= 30000) {
                self->pages_.Home();
                self->last_page_action_ = now;
            }
            if (self->notification_until_ != 0 &&
                static_cast<int32_t>(now - self->notification_until_) >= 0) {
                lv_obj_add_flag(self->status_bar_, LV_OBJ_FLAG_HIDDEN);
                self->notification_until_ = 0;
            }
        } else {
            self->UpdateFace();
        }
    }

    // Called with the LVGL lock held, or from an LVGL timer callback.
    void UpdateFace() {
        const int diameter = std::min(width_, height_);
        const bool blink = state_ == State::Idle && tick_ % 42 == 0;
        const int eye_percent = state_ == State::Listening ? 32 :
                                state_ == State::Speaking && (tick_ / 4) % 2 == 0 ? 26 : 29;
        const int height = blink ? 4 : diameter * eye_percent / 100;
        for (int i = 0; i < 2; ++i) {
            lv_obj_set_height(eyes_[i], height);
            lv_obj_align(eyes_[i], LV_ALIGN_CENTER,
                         (i == 0 ? -1 : 1) * diameter * 13 / 100,
                         state_ == State::Thinking ? -diameter * 2 / 100 : 0);
            if (blink) {
                lv_obj_add_flag(highlights_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(highlights_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (state_ == State::Thinking) {
            lv_obj_remove_flag(dots_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(dots_, LV_OBJ_FLAG_HIDDEN);
        }
    }
};

#endif  // LIVIS_DISPLAY_H
