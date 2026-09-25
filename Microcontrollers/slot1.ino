#include <esp_now.h>
#include <WiFi.h>
#include <stdio.h>

/*
    Μετρήσεις Ultrasound Sensor
*/
#define DEBUG 1
const int trigPin = 10; 
const int echoPin = 11;
const float threshold = 150.0f;
#define mad_samples 15

float duration, distance;
float measurements[mad_samples] = { 0 };
const float mad_kfactor = 3;

int pos = 0;
float to_send = 0;

/*
    Verify that the measurement received is within spec of what to expect,
    the SR-04 ultrasensor module has an operating range of distances between 2cm-450cm .

    For integrity reasons, taking into considerations my design choices, the confidence  range
    as I have stated on the paper is, 5cm-440cm.

    Returns true if measurement is withing range, else false
*/
bool verify_measurement(float measurement) {
    return (measurement >= 5 && measurement <= 440);
}

/*
    Filter the measurements and find the "most probable" and final distance value.

    Step 0: Sort the measurements array by ascending order.
    Step 1: Find the median of the measurements.
    Step 2&3: Calculate the deviation of measurement[i] - median and if it's > k * median, filter it out.
    Step 4: Filter out the bottom 10% and top 10% of leftover measurements.
    Step 5: Take the average of the remaining ones and return it as the assumed confident distance value.
*/
float filter_measurement(float* measurements) {
    if (DEBUG) {
      Serial.println("Measurements: ");
      for (int i = 0; i < mad_samples; i++) {
          Serial.printf("%.2f ", measurements[i]);
      }
      Serial.println();
    }

    float temp = 0;
    // Sort για γρήγορη εύρεση διαμέσου
    for (int i = 0; i < mad_samples - 1; i++) {
        for (int j = mad_samples - 1; j > i; j--) {
            if (measurements[j] < measurements[j - 1]) {
                temp = measurements[j];
                measurements[j] = measurements[j - 1];
                measurements[j - 1] = temp;
            }
        }
    }

    // Εύρεση διαμέσου (δ) για MAD
    float median;
    if (mad_samples % 2 != 0) {
        median = measurements[mad_samples / 2];
    } else {
        median = (measurements[mad_samples / 2] + measurements[mad_samples / 2 - 1]) / 2.0;
    }

    // Εύρεση διαφορών M = | Ni - δ |
    float deviations[mad_samples] = { 0 };
    int dev_pos = 0;
    for (int i = 0; i < mad_samples; i++) {
        if (abs(measurements[i] - median) > mad_kfactor * median) { continue; }

        deviations[dev_pos++] = measurements[i];
    }

    // filter top 10% και bottom 10%
    int bottom_idx = dev_pos / 10;
    int top_idx = (dev_pos)-dev_pos / 10;

    // Υπολογισμός μ.ο
    float avg = 0;
    for (int i = bottom_idx; i < top_idx; i++) { avg += deviations[i]; }

    return avg / (top_idx - bottom_idx);  // float/int = float
}

/*
    ESP-NOW ρυθμίσεις
*/
esp_now_peer_info_t peerInfo = {};
uint8_t gatewayMAC[] = { 0x58, 0x2A, 0xBD, 0x71, 0x82, 0x3C };

typedef struct esp_now_msg {
    float distance;
    uint8_t mac[6];
    int8_t slot;
    //bool is_interrupt;
} esp_now_msg;

esp_now_msg message_to_send;
void setup() {
    // Ultrasound sensor
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);

    Serial.begin(9600);

    // Connect to wireless network
    WiFi.mode(WIFI_STA);
    Serial.print("Sensor node MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    memcpy(peerInfo.peer_addr, gatewayMAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    message_to_send.slot = 1;
    message_to_send.mac[0] = WiFi.macAddress().substring(0,2).toInt();
    message_to_send.mac[1] = WiFi.macAddress().substring(3,5).toInt();
    message_to_send.mac[2] = WiFi.macAddress().substring(6,8).toInt();
    message_to_send.mac[3] = WiFi.macAddress().substring(9,11).toInt();
    message_to_send.mac[4] = WiFi.macAddress().substring(12,14).toInt();
    message_to_send.mac[5] = WiFi.macAddress().substring(15,17).toInt();
}

void loop() {
    if (DEBUG) {
        Serial.printf("pos = %d\n", pos);
    }
    
    // Read from Ultrasound Sensor
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    duration = pulseIn(echoPin, HIGH, 30000); 
    distance = (duration * .0343) / 2; // * spee of sound in air (semi ideal medium) / 2 (to get only one side)
    if (verify_measurement(distance)) {
        measurements[pos++] = distance;
    }

    if (pos >= 15) {
        pos = 0;
        to_send = filter_measurement(measurements);
        message_to_send.distance = to_send;
        
        if (DEBUG) {
            Serial.printf("%.2fcm\n", message_to_send.distance);
        }
        
        esp_err_t result = esp_now_send(gatewayMAC, (uint8_t*)&message_to_send, sizeof(esp_now_msg));

        if (DEBUG) {
            Serial.printf("Sent: %.2fcm -> %s\n",
                          message_to_send.distance,
                          result == ESP_OK ? "OK" : "FAIL");
        }

        // Cleanup
        for (int i = 0; i < mad_samples; i++) { measurements[i] = 0; }
    }

    delay(500);  // .5s
}
