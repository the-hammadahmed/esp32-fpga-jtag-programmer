// ESP32 Direct Cable JTAG Bitbanger Firmware

#define PIN_TCK 13
#define PIN_TDI 27
#define PIN_TDO 26
#define PIN_TMS 25

void setup() {
    Serial.begin(115200);

    pinMode(PIN_TCK, OUTPUT);
    pinMode(PIN_TDI, OUTPUT);
    pinMode(PIN_TMS, OUTPUT);
    pinMode(PIN_TDO, INPUT);

    // Initial JTAG idle state
    digitalWrite(PIN_TCK, LOW);
    digitalWrite(PIN_TMS, HIGH);
    digitalWrite(PIN_TDI, LOW);
}

void loop() {
    if (Serial.available()) {

        uint8_t cmd = Serial.read();

        bool tms = cmd & 0x01;
        bool tdi = cmd & 0x02;

        // Set data before rising edge
        digitalWrite(PIN_TMS, tms);
        digitalWrite(PIN_TDI, tdi);

        // Rising edge of TCK
        digitalWrite(PIN_TCK, HIGH);

        // Sample TDO
        uint8_t tdo = digitalRead(PIN_TDO);

        // Falling edge
        digitalWrite(PIN_TCK, LOW);

        // Return TDO
        Serial.write(tdo);
    }
}