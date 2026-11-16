#include "accelerometer_reading.h"
#include "receiver.h"
#include <Arduino.h>


//pin out
constexpr int XPIN1 = A0;
constexpr int YPIN1 = A1;
constexpr int ZPIN1 = A2;

constexpr int XPIN2 = A3;
constexpr int YPIN2 = A4; 
constexpr int ZPIN2 = A5;

constexpr int OFF_BUTTON_PIN = 2; 
constexpr int COLLECT_BUTTON_PIN = 3; 

constexpr char MODEL_SIDE = 'L';
constexpr unsigned long SAMPLE_INTERVAL_MS = 10;
constexpr unsigned long MAX_SAMPLES = 60000;


//variables
volatile bool serverOn = true;
volatile bool collectData = false;


Accelerometer accelerometer1(XPIN1, YPIN1, ZPIN1);
Accelerometer accelerometer2(XPIN2, YPIN2, ZPIN2);

bool recording = false;
unsigned long setStartedMs = 0;
unsigned long nextSampleMs = 0;
unsigned long sampleSequence = 0;

void offButtonPress(){
    if(serverOn){
        serverOn = false;
        Serial.println("Server turned off.");
    } else {
        serverOn = true;
        Serial.println("Server turned on.");
    }
}

void collectButtonPress(){
    if(serverOn&& !collectData){
        collectData = true;
    }
    else if (serverOn && collectData){
        collectData = false;
    }
    else{
        return;
    }
}

void sendData(float x1, float y1, float z1, float x2, float y2, float z2){
    Serial1.print("S ");
    Serial1.print(sampleSequence++);
    Serial1.print(" ");
    Serial1.print(millis() - setStartedMs);
    Serial1.print(" ");
    Serial1.print(x1, 8);
    Serial1.print(" ");
    Serial1.print(y1, 8);
    Serial1.print(" ");
    Serial1.print(z1, 8);
    Serial1.print(" ");
    Serial1.print(x2, 8);
    Serial1.print(" ");
    Serial1.print(y2, 8);
    Serial1.print(" ");
    Serial1.println(z2, 8);
}

void setup(){
    Serial.begin(9600);
    Serial1.begin(115200);

    attachInterrupt(digitalPinToInterrupt(OFF_BUTTON_PIN), offButtonPress, FALLING);
    attachInterrupt(digitalPinToInterrupt(COLLECT_BUTTON_PIN), collectButtonPress, FALLING);
}
void loop(){
    if(serverOn && collectData){
        if(!recording){
            recording = true;
            sampleSequence = 0;
            setStartedMs = millis();
            nextSampleMs = setStartedMs;
            Serial1.print("#TALLY2 fs=100 unit=g range=3 side=");
            Serial1.println(MODEL_SIDE);
        }

        if((long)(millis() - nextSampleMs) >= 0){
            float x1 = accelerometer1.convertX();
            float y1 = accelerometer1.convertY();
            float z1 = accelerometer1.convertZ();

            float x2 = accelerometer2.convertX();
            float y2 = accelerometer2.convertY();
            float z2 = accelerometer2.convertZ();

            sendData(x1, y1, z1, x2, y2, z2);
            nextSampleMs += SAMPLE_INTERVAL_MS;
        }
    } else {
        recording = false;
    }
}