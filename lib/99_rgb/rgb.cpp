#include "rgb.h"

#include <Adafruit_NeoPixel.h>

namespace rgb
{
    int gpio = 21;
    Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, gpio, NEO_GRB + NEO_KHZ800);
    bool isOn = false;

    void init()
    {
        pinMode(gpio, OUTPUT);
        strip.begin();
        strip.show();            // Initialize all pixels to 'off'
    }

    void set(bool on)
    {
        if (on)
        {
            strip.setPixelColor(0, strip.Color(155, 155, 155)); // White color
        }
        else
        {
            strip.setPixelColor(0, strip.Color(0, 0, 0)); // Off
        }

        strip.show();
        isOn = on;
    }

    void toggle()
    {
        uint32_t color = strip.getPixelColor(0);
        if (color == strip.Color(0, 0, 0))
        {
            strip.setPixelColor(0, strip.Color(155, 155, 155)); // White color
        }
        else
        {
            strip.setPixelColor(0, strip.Color(0, 0, 0)); // Off
        }

        strip.show();
        isOn = !isOn;
    }
}