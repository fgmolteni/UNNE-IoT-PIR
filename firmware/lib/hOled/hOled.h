#ifndef __HOLED__H
#define __HOLED__H

#include <Arduino.h>
#include <Wire.h>
#include <heltec.h>

class HOled {
    private:
    bool enable;
    float temp;
    float hum;
    // String alert;

    public:
        HOled(bool, float, float);
        void init();
        void present();
        void tempHum(float, float);
        // void message();
};

#endif
