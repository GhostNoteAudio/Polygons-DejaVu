#include "Z4.h"

void setup()
{
    Polygons::init();
    Z4::start();
}

void loop()
{
    delay(5);
    Z4::loop();
}