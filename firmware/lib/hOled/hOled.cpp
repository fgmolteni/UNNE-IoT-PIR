#include <Arduino.h>
#include <Wire.h>
#include <heltec.h>
#include "HOled.h"



unsigned long tiempo = millis();

//rotate only for GEOMETRY_128_64
SSD1306Wire display(0x3c, SDA_OLED, SCL_OLED, RST_OLED);

HOled::HOled(bool _enable, float _temp, float _hum){
    enable = _enable;
    temp = _temp;
    hum = _hum;
    // alert = _alert;
}

void HOled::init() {
    if(enable == true)    {
        pinMode(Vext,OUTPUT);
        digitalWrite(Vext, LOW);
        }
    else {
        pinMode(Vext,OUTPUT);
        digitalWrite(Vext, HIGH);
        };
};

void HOled::present() {
   

    display.init();
    display.clear();
    display.display();

    display.setContrast(255);

    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.clear();
    display.display();
    display.screenRotate(ANGLE_0_DEGREE);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 32 - 16 / 2, "FaCENA-UNNE");
    display.display();
    delay(5000);
    display.clear();
    
};




void HOled::tempHum(float temp, float hum)
{
    display.clear();
    display.display();

    
    // display.screenRotate(ANGLE_270_DEGREE);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 2, "Temp: " + String(temp) + " °C");
    display.drawString(0, 14, "Hum: "+ String(hum) + " %");
    // display.drawString(0, 24, alert);
    display.display();
    
};

