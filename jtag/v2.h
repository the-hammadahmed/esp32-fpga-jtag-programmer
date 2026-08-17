// ESP32 Xilinx FPGA JTAG Programmer

#define PIN_TCK 13
#define PIN_TDI 27
#define PIN_TDO 26
#define PIN_TMS 25

// Commands
#define CMD_RESET       0x01
#define CMD_CLOCK       0x02
#define CMD_SHIFT_IR    0x03
#define CMD_SHIFT_DR    0x04
#define CMD_RUNTEST     0x05

void jtagClock(bool tms, bool tdi)
{
    digitalWrite(PIN_TMS, tms);
    digitalWrite(PIN_TDI, tdi);

    digitalWrite(PIN_TCK, HIGH);

    // TDO could be sampled here if needed
    digitalWrite(PIN_TCK, LOW);
}


uint32_t read32()
{
    uint32_t value = 0;

    for (int i = 0; i < 4; i++)
    {
        while (!Serial.available());

        value |= ((uint32_t)Serial.read()) << (8 * i);
    }

    return value;
}


void jtagReset()
{
    // At least 5 clocks with TMS = 1
    for (int i = 0; i < 6; i++)
    {
        jtagClock(1, 0);
    }

    // Test-Logic-Reset -> Run-Test/Idle
    jtagClock(0, 0);
}


void shiftIR(uint8_t instruction)
{
    // Run-Test/Idle -> Select-DR-Scan
    jtagClock(1, 0);

    // Select-DR-Scan -> Select-IR-Scan
    jtagClock(1, 0);

    // Select-IR-Scan -> Capture-IR
    jtagClock(0, 0);

    // Capture-IR -> Shift-IR
    jtagClock(0, 0);

    // Xilinx 7-series IR = 6 bits
    // Shift LSB first

    for (int i = 0; i < 6; i++)
    {
        bool tdi = (instruction >> i) & 1;

        // Exit Shift-IR on final bit
        bool tms = (i == 5);

        jtagClock(tms, tdi);
    }

    // Exit1-IR -> Update-IR
    jtagClock(1, 0);

    // Update-IR -> Run-Test/Idle
    jtagClock(0, 0);
}


void runTest(uint32_t clocks)
{
    for (uint32_t i = 0; i < clocks; i++)
    {
        jtagClock(0, 0);
    }
}


void shiftDR(uint32_t byteCount)
{
    // Run-Test/Idle -> Select-DR-Scan
    jtagClock(1, 0);

    // Select-DR-Scan -> Capture-DR
    jtagClock(0, 0);

    // Capture-DR -> Shift-DR
    jtagClock(0, 0);

    for (uint32_t byteIndex = 0;
         byteIndex < byteCount;
         byteIndex++)
    {
        while (!Serial.available());

        uint8_t data = Serial.read();

        // Configuration data MSB first
        for (int bit = 7; bit >= 0; bit--)
        {
            bool tdi = (data >> bit) & 1;

            bool lastBit =
                (byteIndex == byteCount - 1) &&
                (bit == 0);

            jtagClock(lastBit, tdi);
        }
    }

    // Exit1-DR -> Update-DR
    jtagClock(1, 0);

    // Update-DR -> Run-Test/Idle
    jtagClock(0, 0);
}


void setup()
{
    Serial.begin(921600);

    pinMode(PIN_TCK, OUTPUT);
    pinMode(PIN_TDI, OUTPUT);
    pinMode(PIN_TMS, OUTPUT);
    pinMode(PIN_TDO, INPUT);

    digitalWrite(PIN_TCK, LOW);
    digitalWrite(PIN_TDI, LOW);
    digitalWrite(PIN_TMS, HIGH);
}


void loop()
{
    if (!Serial.available())
        return;

    uint8_t cmd = Serial.read();

    switch (cmd)
    {
        case CMD_RESET:
        {
            jtagReset();
            Serial.write(0xAA);
            break;
        }

        case CMD_SHIFT_IR:
        {
            while (!Serial.available());

            uint8_t instruction = Serial.read();

            shiftIR(instruction);

            Serial.write(0xAA);
            break;
        }

        case CMD_RUNTEST:
        {
            uint32_t clocks = read32();

            runTest(clocks);

            Serial.write(0xAA);
            break;
        }

        case CMD_SHIFT_DR:
        {
            uint32_t byteCount = read32();

            Serial.write(0xAA); // Ready

            shiftDR(byteCount);

            Serial.write(0xAA); // Done

            break;
        }
    }
}