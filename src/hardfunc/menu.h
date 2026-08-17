#pragma once
#include <Arduino.h>

#define ICON_FRAMES 3

// Menu item definition
struct MenuItem {
    const char* label;
    const uint8_t** icon_frames;  // array of ICON_FRAMES bitmaps (nullptr = no icon)
    void (*action)();             // called on OK press (nullptr = submenu)
    const MenuItem* submenu;
    uint8_t submenu_count;
};

// Initialize menu system
void menu_init(const MenuItem* items, uint8_t count);

// Call when a button is pressed
void menu_handle_button(uint8_t button);

// Render current menu state to OLED
void menu_render();

// Is menu currently visible?
bool menu_is_active();

// Is menu currently animating (scrolling)?
bool menu_is_animating();

// Custom screen rendering (replaces menu when set)
typedef void (*ScreenRenderFunc)();
void menu_set_screen(ScreenRenderFunc func);  // set to nullptr to return to menu
ScreenRenderFunc menu_get_screen();
