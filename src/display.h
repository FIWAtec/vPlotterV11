#pragma once
#include <Arduino.h>

class Display {
public:
    Display();
    void displayText(String text);
    void displayHomeScreen(String line1, String line2, String line3);
};
