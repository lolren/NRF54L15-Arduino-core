/*
 * XiaoRgbLed — XIAO nRF54LM20A RGB LED sweep
 * Red=P1.22, Green=P1.24, Blue=P1.23 (active-low)
 */
#include <Arduino.h>
void setup() {
    pinMode(LED_RED,OUTPUT); pinMode(LED_GREEN,OUTPUT); pinMode(LED_BLUE,OUTPUT);
}
void loop() {
    for(int b=0;b<256;b+=5){
        analogWrite(LED_RED,b); analogWrite(LED_GREEN,255-b); analogWrite(LED_BLUE,(b*3)&255);
        delay(30);
    }
}
