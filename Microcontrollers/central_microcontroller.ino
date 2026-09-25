#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient espClient;
PubSubClient client(espClient);

typedef struct esp_now_msg {
    float distance;
    uint8_t mac[6];
    uint8_t slot;
} esp_now_msg;

#define SLOT_COUNT         4
#define OCCUPIED_MAX_DIST  150.0f   // cm — <= 150 cm = occupied
#define MIN_REL_CHANGE     0.10f    // publish on > 10 % change

volatile bool newData = false;
esp_now_msg latestMsg;

// per-slot tracking (index 0 unused)
float lastDistance[SLOT_COUNT + 1] = { 0.0f, -1.0f, -1.0f, -1.0f, -1.0f }; // -1 = not yet received
bool  lastOccupied[SLOT_COUNT + 1] = { false };

bool isOccupied(float d) { return d <= OCCUPIED_MAX_DIST; }

bool shouldPublish(uint8_t slot, float distance) {
    if (lastDistance[slot] < 0.0f) return true;               // first reading for this slot

    if (isOccupied(distance) != lastOccupied[slot]) return true;  // occupied/free changed

    // > 10 % relative to the last received value
    // (fmaxf avoids a division by zero when either value is 0)
    float ref = fmaxf(lastDistance[slot], distance);
    return fabsf(distance - lastDistance[slot]) > MIN_REL_CHANGE * ref;
}

void publishSlot(uint8_t slot, float distance) {
    char topic[16];   // "parking/slot/4" + NUL
    char value[12];

    snprintf(topic, sizeof(topic), "parking/slot/%d", slot);
    dtostrf(distance, 5, 2, value);

    bool ok = client.publish(topic, value);
    Serial.printf("[MQTT] publish %s -> %.2f cm (%s)\n",
                  ok ? "ok" : "FAILED", distance,
                  isOccupied(distance) ? "occupied" : "free");
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (len != sizeof(esp_now_msg)) return;
    memcpy((void*)&latestMsg, incomingData, sizeof(esp_now_msg));
    newData = true;
}

void mqttReconnect() {
    Serial.print("[MQTT] connecting... ");
    if (client.connect("esp32-pub")) {
        Serial.println("ok");
    } else {
        Serial.printf("failed, state=%d\n", client.state());
    }
}

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin("thesis", "thesispassword");
    while (WiFi.status() != WL_CONNECTED) delay(500);
    Serial.println("[WiFi] connected");

    client.setServer("192.168.0.150", 1883); // όπως φαίνεται στο report
    mqttReconnect();

    esp_now_init();
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) mqttReconnect();
        client.loop();
    }

    if (newData) {
        noInterrupts();
        esp_now_msg msg = latestMsg;   // atomic copy, so the callback can't tear it
        newData = false;
        interrupts();

        if (msg.slot < 1 || msg.slot > SLOT_COUNT) return;     // invalid slot
        if (isnan(msg.distance) || msg.distance < 0.0f) return; // sensor glitch

        if (shouldPublish(msg.slot, msg.distance)) {
            publishSlot(msg.slot, msg.distance);
        }

        // "previous value" = the last RECEIVED one.
        lastDistance[msg.slot] = msg.distance;
        lastOccupied[msg.slot] = isOccupied(msg.distance);
    }
}
