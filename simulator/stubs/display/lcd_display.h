#pragma once
#include <lvgl.h>
#include <string>

LV_FONT_DECLARE(font_noto_sans_basic_20_4);
LV_FONT_DECLARE(font_material_symbols_20_4);
extern lv_font_t simulator_text_font;

class DisplayLockGuard {
public:
    template <typename T> explicit DisplayLockGuard(T*) {}
    explicit operator bool() const { return true; }
};

class LvglDisplay {
public:
    virtual ~LvglDisplay() = default;
    virtual void SetupUI() {}
    virtual void SetStatus(const char* status) { if (status_label_) lv_label_set_text(status_label_, status); }
    virtual void ShowNotification(const char* text, int = 3000) { if (status_label_) lv_label_set_text(status_label_, text); }
    virtual void ShowNotification(const std::string& text, int ms = 3000) { ShowNotification(text.c_str(), ms); }
    virtual void SetEmotion(const char*) {}
    virtual void SetChatMessage(const char*, const char*) {}
protected:
    int width_ = 320, height_ = 240;
    lv_obj_t *network_label_ = nullptr, *status_label_ = nullptr, *notification_label_ = nullptr;
};

class LcdDisplay : public LvglDisplay {
public:
    void SetupUI() override {
        auto screen = lv_screen_active();
        lv_obj_set_style_text_font(screen, &simulator_text_font, 0);
        container_ = lv_obj_create(screen);
        lv_obj_set_size(container_, width_, height_);
        lv_obj_set_style_pad_all(container_, 0, 0);
        lv_obj_set_style_border_width(container_, 0, 0);
        lv_obj_set_style_radius(container_, 0, 0);
        emoji_box_ = lv_obj_create(screen);
        lv_obj_set_size(emoji_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(emoji_box_, 0, 0);
        lv_obj_set_style_pad_all(emoji_box_, 0, 0);
        lv_obj_center(emoji_box_);
        emoji_label_ = lv_label_create(emoji_box_);
        emoji_image_ = lv_image_create(emoji_box_);
        top_bar_ = lv_obj_create(screen);
        lv_obj_set_size(top_bar_, width_, LV_SIZE_CONTENT);
        lv_obj_set_style_border_width(top_bar_, 0, 0);
        lv_obj_set_style_pad_all(top_bar_, 0, 0);
        network_label_ = lv_label_create(top_bar_);
        lv_obj_set_style_text_font(network_label_, &font_material_symbols_20_4, 0);
        status_bar_ = lv_obj_create(screen);
        lv_obj_set_size(status_bar_, width_, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(status_bar_, 0, 0);
        lv_obj_set_style_pad_all(status_bar_, 0, 0);
        status_label_ = lv_label_create(status_bar_);
        lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(status_label_);
        notification_label_ = lv_label_create(status_bar_);
        lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        bottom_bar_ = lv_obj_create(screen);
        lv_obj_set_size(bottom_bar_, width_, 28);
        lv_obj_set_style_border_width(bottom_bar_, 0, 0);
        lv_obj_set_style_pad_all(bottom_bar_, 0, 0);
        chat_message_label_ = lv_label_create(bottom_bar_);
        lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(chat_message_label_);
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    void SetChatMessage(const char*, const char* content) override {
        if (content && *content) { lv_label_set_text(chat_message_label_, content); lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN); }
        else lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
protected:
    lv_obj_t *top_bar_ = nullptr, *status_bar_ = nullptr, *container_ = nullptr;
    lv_obj_t *bottom_bar_ = nullptr, *emoji_label_ = nullptr, *emoji_image_ = nullptr;
    lv_obj_t *emoji_box_ = nullptr, *chat_message_label_ = nullptr;
};
class SpiLcdDisplay : public LcdDisplay {};
