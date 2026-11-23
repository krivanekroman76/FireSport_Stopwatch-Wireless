#include <Arduino.h>

// Define the GPIO pins to exclude
const int excludedPins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23}; // HSPI, VSPI, I2C, UART pins
const int totalPins = 40; // Total GPIO pins on ESP32
const int numExcluded = sizeof(excludedPins) / sizeof(excludedPins[0]);

void setup() {
    Serial.begin(115200);
    
    // Set all usable GPIO pins as input
    for (int pin = 0; pin < totalPins; pin++) {
        bool isExcluded = false;
        for (int i = 0; i < numExcluded; i++) {
            if (pin == excludedPins[i]) {
                isExcluded = true;
                break;
            }
        }
        if (!isExcluded) {
            pinMode(pin, INPUT_PULLDOWN); // Use internal pull-up resistors
        }
    }
}

void loop() {
    Serial.println("Pin States:");
    Serial.println("Pin\tState");
    
    // Read and print the state of each pin
    for (int pin = 0; pin < totalPins; pin++) {
        bool isExcluded = false;
        for (int i = 0; i < numExcluded; i++) {
            if (pin == excludedPins[i]) {
                isExcluded = true;
                break;
            }
        }
        if (!isExcluded) {
            int state = digitalRead(pin);
            Serial.print(pin);
            Serial.print("\t");
            Serial.println(state);
        }
    }
    
    Serial.println();
    delay(1000); // Wait for 1 second before the next read
}