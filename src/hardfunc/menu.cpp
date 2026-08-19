#include "menu.h"
#include "ssd1306.h"
#include "buttons.h"

// --- Layout constants ---
#define ROW_HEIGHT    20
#define ROW_GAP       2
#define ICON_SIZE     16
#define ICON_X        2
#define TEXT_X        22
#define SCROLLBAR_X   124
#define SCROLLBAR_W   3
#define FRAME_PAD     2

// Y positions for the 3 rows (20+2+20+2+20 = 64)
#define ROW_TOP_Y     0
#define ROW_MID_Y     (ROW_HEIGHT + ROW_GAP)
#define ROW_BOT_Y     (ROW_HEIGHT + ROW_GAP + ROW_HEIGHT + ROW_GAP)

#define SCROLL_PIXELS (ROW_HEIGHT + ROW_GAP)  // scroll distance per step

// --- State ---
static const MenuItem* s_items = nullptr;
static uint8_t s_count = 0;
static uint8_t s_selected = 0;

// --- Animation state ---
#define SCROLL_SPEED  6                        // pixels per frame
static int s_scroll_offset = 0;     // current pixel offset (0 = settled)
static uint8_t s_icon_frame = 0;    // current animation frame for icons
static unsigned long s_last_icon_switch = 0;
#define ICON_ANIM_INTERVAL 400      // ms between icon frames

// --- Custom screen mode ---
static ScreenRenderFunc s_custom_screen = nullptr;
static ScreenButtonFunc s_custom_buttons = nullptr;

// Menu stack for submenus
#define MAX_DEPTH 4
static struct {
    const MenuItem* items;
    uint8_t count;
    uint8_t selected;
} s_stack[MAX_DEPTH];
static uint8_t s_depth = 0;

// --- Scrollbar ---
static void draw_scrollbar(uint8_t selected, uint8_t total) {
    if (total <= 1) return;

    int bar_area_h = 64;  // full display height
    int marker_h = max(6, bar_area_h / total);
    int marker_y = (selected * (bar_area_h - marker_h)) / (total - 1);

    // Dotted line
    for (int y = 0; y < 64; y += 3) {
        oled_draw_box(SCROLLBAR_X, y, 1, 1);
    }

    // Solid marker
    oled_draw_box(SCROLLBAR_X - 1, marker_y, SCROLLBAR_W, marker_h);
}

// --- Draw a single row ---
static void draw_row(int y, const MenuItem* item, bool bold) {
    if (item == nullptr) return;

    if (bold) {
        oled_set_font_bold();
    } else {
        oled_set_font_normal();
    }

    // Icon: animated only if bold (highlighted), otherwise frame 0
    if (item->icon_frames) {
        uint8_t frame = bold ? s_icon_frame % ICON_FRAMES : 0;
        oled_draw_xbm(ICON_X, y + 2, 16, 16, item->icon_frames[frame]);
    }

    // Text
    oled_draw_text(TEXT_X, y + 14, item->label);

    oled_set_font_normal();
}

// --- Public API ---

void menu_init(const MenuItem* items, uint8_t count) {
    s_items = items;
    s_count = count;
    s_selected = 0;
    s_depth = 0;
}

void menu_handle_button(uint8_t button) {
    // If custom screen is active, route buttons to its handler
    if (s_custom_screen && s_custom_buttons) {
        s_custom_buttons(button);
        return;
    }
    // If custom screen without handler, LEFT exits
    if (s_custom_screen) {
        if (button == BTN_LEFT) {
            s_custom_screen = nullptr;
            s_custom_buttons = nullptr;
        }
        return;
    }

    switch (button) {
        case BTN_UP:
            if (s_scroll_offset != 0) break;  // ignore if still animating
            if (s_selected > 0) s_selected--;
            else s_selected = s_count - 1;
            s_scroll_offset = -SCROLL_PIXELS;  // items slide down (new from top)
            break;
        case BTN_DOWN:
            if (s_scroll_offset != 0) break;  // ignore if still animating
            if (s_selected < s_count - 1) s_selected++;
            else s_selected = 0;
            s_scroll_offset = SCROLL_PIXELS;   // items slide up (new from bottom)
            break;
        case BTN_OK:
        case BTN_RIGHT:
            // Enter submenu or run action
            if (s_items[s_selected].submenu && s_depth < MAX_DEPTH) {
                s_stack[s_depth] = { s_items, s_count, s_selected };
                s_depth++;
                const MenuItem* sub = s_items[s_selected].submenu;
                uint8_t sub_count = s_items[s_selected].submenu_count;
                s_items = sub;
                s_count = sub_count;
                s_selected = 0;
            } else if (s_items[s_selected].action) {
                s_items[s_selected].action();
            }
            break;
        case BTN_LEFT:
            // Go back in menu
            if (s_depth > 0) {
                s_depth--;
                s_items = s_stack[s_depth].items;
                s_count = s_stack[s_depth].count;
                s_selected = s_stack[s_depth].selected;
            }
            break;
    }
}

void menu_render() {
    oled_begin();

    // Advance icon animation
    unsigned long now = millis();
    if (now - s_last_icon_switch >= ICON_ANIM_INTERVAL) {
        s_icon_frame = (s_icon_frame + 1) % ICON_FRAMES;
        s_last_icon_switch = now;
    }

    // Animate scroll offset toward 0
    if (s_scroll_offset != 0) {
        if (s_scroll_offset > 0) {
            s_scroll_offset -= SCROLL_SPEED;
            if (s_scroll_offset < 0) s_scroll_offset = 0;
        } else {
            s_scroll_offset += SCROLL_SPEED;
            if (s_scroll_offset > 0) s_scroll_offset = 0;
        }
    }

    // Determine which items to show (wraps around)
    int top_idx = (s_selected - 1 + s_count) % s_count;
    int mid_idx = s_selected;
    int bot_idx = (s_selected + 1) % s_count;

    // Apply scroll offset to Y positions
    int y_top = ROW_TOP_Y + s_scroll_offset;
    int y_mid = ROW_MID_Y + s_scroll_offset;
    int y_bot = ROW_BOT_Y + s_scroll_offset;

    // If scrolling, draw an extra item that's coming into view
    if (s_scroll_offset > 0) {
        int extra_idx = (s_selected - 2 + s_count) % s_count;
        int y_extra = ROW_TOP_Y - SCROLL_PIXELS + s_scroll_offset;
        if (y_extra >= -ROW_HEIGHT) {
            draw_row(y_extra, &s_items[extra_idx], false);
        }
    } else if (s_scroll_offset < 0) {
        int extra_idx = (s_selected + 2) % s_count;
        int y_extra = ROW_BOT_Y + SCROLL_PIXELS + s_scroll_offset;
        if (y_extra <= 64) {
            draw_row(y_extra, &s_items[extra_idx], false);
        }
    }

    // Draw the 3 main rows (text + icons scroll)
    draw_row(y_top, &s_items[top_idx], false);
    draw_row(y_mid, &s_items[mid_idx], true);   // bold for selected
    draw_row(y_bot, &s_items[bot_idx], false);

    // Fixed highlight frame — always at center position
    int fw = SCROLLBAR_X - 4;
    oled_draw_rframe(0, ROW_MID_Y, fw, ROW_HEIGHT, 3);
    // Shadow
    oled_draw_line(4, ROW_MID_Y + ROW_HEIGHT, fw - 1, ROW_MID_Y + ROW_HEIGHT);
    oled_draw_line(fw, ROW_MID_Y + 4, fw, ROW_MID_Y + ROW_HEIGHT - 1);
    oled_draw_box(fw - 1, ROW_MID_Y + ROW_HEIGHT - 1, 2, 2);

    // Scrollbar
    draw_scrollbar(s_selected, s_count);

    oled_end();

    // If still animating, next frame comes from display_task loop
}

bool menu_is_active() {
    return s_items != nullptr;
}

bool menu_is_animating() {
    return s_scroll_offset != 0;
}

// --- Custom screen mode ---

void menu_set_screen(ScreenRenderFunc func) {
    s_custom_screen = func;
    if (!func) s_custom_buttons = nullptr;
}

void menu_set_screen_buttons(ScreenButtonFunc func) {
    s_custom_buttons = func;
}

ScreenRenderFunc menu_get_screen() {
    return s_custom_screen;
}
