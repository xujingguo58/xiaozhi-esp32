#include <SDL.h>
#include "livis_display.h"
#include <cstdio>
#include <cstring>

lv_font_t simulator_text_font;
static LivisDisplay* display;
static int on_event(void*, SDL_Event* event) {
    if (event->type != SDL_KEYDOWN || event->key.repeat) return 1;
    switch (event->key.keysym.sym) {
    case SDLK_SPACE: display->NextStandbyPage(); break;
    case SDLK_1: display->SetStatus(Lang::Strings::STANDBY); break;
    case SDLK_2: display->SetStatus(Lang::Strings::LISTENING); break;
    case SDLK_3: display->SetChatMessage("user", "Hello!"); break;
    case SDLK_4: display->SetStatus(Lang::Strings::SPEAKING); display->SetChatMessage("assistant", "Hello!"); break;
    case SDLK_5: display->SetStatus(Lang::Strings::CONNECTING); break;
    default: break;
    }
    return 1;
}
static bool has_visible_text(lv_obj_t* obj, const char* text, bool hidden = false) {
    hidden = hidden || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (!hidden && lv_obj_check_type(obj, &lv_label_class) && std::strcmp(lv_label_get_text(obj), text) == 0) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (has_visible_text(lv_obj_get_child(obj, i), text, hidden)) return true;
    return false;
}
static void key(SDL_Keycode code) {
    SDL_Event event = {};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = code;
    on_event(nullptr, &event);
}
int main(int argc, char** argv) {
    lv_init();
    auto chinese_font = lv_freetype_font_create("/System/Library/Fonts/PingFang.ttc",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 20, LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!chinese_font) {
        std::fputs("Unable to load macOS PingFang font.\n", stderr);
        return 1;
    }
    simulator_text_font = font_noto_sans_basic_20_4;
    simulator_text_font.fallback = chinese_font;
    auto window = lv_sdl_window_create(320, 240);
    lv_sdl_window_set_title(window, "Livis simulator - Space: next page, 1-5: state");
    lv_sdl_mouse_create();
    LivisDisplay view;
    display = &view;
    view.SetupUI();
    LivisPageData data;
    data.weather = {true, "上海", "多云", "23°", "18", "26", "12:00"};
    data.indoor = {true, true, false, false, "21~25°", "38~46%"};
    data.hk = {true, "102.50", "+2.18", "交易中"};
    data.us = {true, "25.60", "-0.85", "交易中"};
    data.stocks_updated = "12:00";
    data.five_hour = {true, 72, "14:30 重置"};
    data.weekly = {true, 46, "周日重置"};
    data.codex_updated = "12:00";
    view.SetStandbyData(data);
    view.SetStatus(Lang::Strings::STANDBY);
    SDL_AddEventWatch(on_event, nullptr);
    if (argc > 1 && std::strcmp(argv[1], "--font-check") == 0) {
        const uint32_t glyphs[] = {0x5ba4, 0x5185, 0x6e7f, 0x5ea6, 0x5f85, 0x66f4, 0x65b0,
                                   0x6570, 0x636e, 0x8fc7, 0x671f, 0x90e8, 0x5206, 0x4f20,
                                   0x611f, 0x5668, 0x79bb, 0x7ebf};
        bool ok = true;
        for (auto codepoint : glyphs) {
            lv_font_glyph_dsc_t glyph = {};
            if (!lv_font_get_glyph_dsc(&livis_indoor_font, &glyph, codepoint, 0)) {
                std::fprintf(stderr, "Missing firmware glyph U+%04X\n", codepoint);
                ok = false;
            }
        }
        return ok ? 0 : 1;
    }
    std::puts("Space: next page | 1: standby | 2: listening | 3: thinking | 4: speaking | 5: connecting");
    if (argc > 1 && std::strcmp(argv[1], "--self-test") == 0) {
        const auto end = SDL_GetTicks() + 4300;
        while (SDL_GetTicks() < end) { lv_timer_handler(); SDL_Delay(5); }
        auto screen = lv_screen_active();
        lv_font_glyph_dsc_t glyph = {};
        bool ok = lv_font_get_glyph_dsc(&simulator_text_font, &glyph, 0x9f98, 0) &&
                  glyph.resolved_font == chinese_font && lv_font_get_glyph_bitmap(&glyph, nullptr) != nullptr;
        ok = ok && has_visible_text(screen, "多云");
        ok = ok && has_visible_text(screen, "室内 21~25°") && has_visible_text(screen, "湿度 38~46%");
        auto indoor = data.indoor;
        indoor.stale = true;
        view.SetIndoor(indoor);
        ok = ok && has_visible_text(screen, "室内数据过期") && !has_visible_text(screen, "室内 21~25°");
        indoor = data.indoor;
        indoor.partial = true;
        view.SetIndoor(indoor);
        ok = ok && has_visible_text(screen, "部分传感器离线");
        view.SetIndoor(LivisPageData::Indoor{});
        ok = ok && has_visible_text(screen, "室内待更新") && !has_visible_text(screen, "湿度 38~46%");
        view.SetIndoor(data.indoor);
        key(SDLK_SPACE);
        ok = ok && has_visible_text(screen, "理想汽车");
        key(SDLK_SPACE);
        ok = ok && has_visible_text(screen, "Codex 用量") && has_visible_text(screen, "5 小时") && has_visible_text(screen, "72%");
        key(SDLK_2);
        ok = ok && has_visible_text(screen, Lang::Strings::LISTENING) && !has_visible_text(screen, "Codex 用量");
        std::puts(ok ? "Simulator UI self-test passed" : "Simulator UI self-test failed");
        return ok ? 0 : 1;
    }
    if (argc > 2 && std::strcmp(argv[1], "--screenshot") == 0) {
        const auto end = SDL_GetTicks() + 4300;
        while (SDL_GetTicks() < end) { lv_timer_handler(); SDL_Delay(5); }
        lv_obj_update_layout(lv_screen_active());
        auto snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
        auto surface = snapshot ? SDL_CreateRGBSurfaceWithFormatFrom(snapshot->data, 320, 240, 32,
                                  snapshot->header.stride, SDL_PIXELFORMAT_ARGB8888) : nullptr;
        bool ok = surface && SDL_SaveBMP(surface, argv[2]) == 0;
        if (surface) SDL_FreeSurface(surface);
        if (snapshot) lv_draw_buf_destroy(snapshot);
        if (!ok) std::fprintf(stderr, "Screenshot failed: %s\n", SDL_GetError());
        return ok ? 0 : 1;
    }
    while (true) { lv_timer_handler(); SDL_Delay(5); }
}
