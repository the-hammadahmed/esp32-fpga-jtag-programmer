/*
 * ESP32 JTAG Programmer for Xilinx 7-Series FPGA
 *
 * ESP32 Pin Connections:
 *
 * GPIO 13 -> FPGA TCK
 * GPIO 26 -> FPGA TDI
 * GPIO 27 <- FPGA TDO
 * GPIO 25 -> FPGA TMS
 * GND     <-> FPGA GND
 *
 * Serial Protocol:
 *
 * CMD_RESET      = Reset JTAG TAP
 * CMD_SHIFT_IR   = Shift 6-bit JTAG instruction
 * CMD_RUNTEST    = Generate TCK clocks in Run-Test/Idle
 * CMD_DR_START   = Enter Shift-DR
 * CMD_DR_CHUNK   = Shift normal data chunk
 * CMD_DR_END     = Shift final data chunk and exit Shift-DR
 *
 * ACK = 0xAA
 */

#define SERIAL_BAUD 921600
#define RX_BUFFER_SIZE 32768



#include "soc/gpio_struct.h"


#define PIN_TCK 13
#define PIN_TDI 26
#define PIN_TDO 27
#define PIN_TMS 25


// ================================
// Commands
// ================================

#define CMD_RESET 0x01
#define CMD_SHIFT_IR 0x03
#define CMD_RUNTEST 0x05

#define CMD_DR_START 0x10
#define CMD_DR_CHUNK 0x11
#define CMD_DR_END 0x12

#define ACK 0xAA


// ================================
// JTAG Clock
// ================================
#define TCK_MASK (1UL << PIN_TCK)
#define TDI_MASK (1UL << PIN_TDI)
#define TMS_MASK (1UL << PIN_TMS)

inline void jtagClock(bool tms, bool tdi) {
  // Set TMS
  if (tms) GPIO.out_w1ts = TMS_MASK;
  else GPIO.out_w1tc = TMS_MASK;

  // Set TDI
  if (tdi) GPIO.out_w1ts = TDI_MASK;
  else GPIO.out_w1tc = TDI_MASK;

  // Rising edge
  GPIO.out_w1ts = TCK_MASK;

  // Falling edge
  GPIO.out_w1tc = TCK_MASK;
}


// void jtagClock(bool tms, bool tdi) {
//   // Set signals before clock edge
//   digitalWrite(PIN_TMS, tms);
//   digitalWrite(PIN_TDI, tdi);

//   // Rising edge
//   digitalWrite(PIN_TCK, HIGH);

//   // Optional:
//   // uint8_t tdo = digitalRead(PIN_TDO);

//   // Falling edge
//   digitalWrite(PIN_TCK, LOW);
// }


// ================================
// Read 32-bit Little Endian Value
// ================================

uint32_t read32() {
  uint32_t value = 0;

  for (int i = 0; i < 4; i++) {
    while (!Serial.available()) {
      // Wait for serial data
    }

    value |= ((uint32_t)Serial.read()) << (8 * i);
  }

  return value;
}


// ================================
// JTAG Reset
// ================================

void jtagReset() {
  // Force TAP into Test-Logic-Reset
  // TMS = 1 for at least 5 clocks

  for (int i = 0; i < 6; i++) {
    jtagClock(1, 0);
  }

  // Test-Logic-Reset
  // ->
  // Run-Test/Idle

  jtagClock(0, 0);
}


// ================================
// Shift Instruction Register
// Xilinx 7-Series = 6-bit IR
// ================================

void shiftIR(uint8_t instruction) {
  // Run-Test/Idle
  // ->
  // Select-DR-Scan

  jtagClock(1, 0);

  // Select-DR-Scan
  // ->
  // Select-IR-Scan

  jtagClock(1, 0);

  // Select-IR-Scan
  // ->
  // Capture-IR

  jtagClock(0, 0);

  // Capture-IR
  // ->
  // Shift-IR

  jtagClock(0, 0);


  // Shift 6-bit instruction
  // JTAG instructions are LSB first

  for (int bit = 0; bit < 6; bit++) {
    bool tdi = (instruction >> bit) & 0x01;

    // On final bit:
    // Shift-IR -> Exit1-IR

    bool tms = (bit == 5);

    jtagClock(tms, tdi);
  }


  // Exit1-IR
  // ->
  // Update-IR

  jtagClock(1, 0);


  // Update-IR
  // ->
  // Run-Test/Idle

  jtagClock(0, 0);
}


// ================================
// Run-Test / Idle
// ================================

void runTest(uint32_t clocks) {
  for (uint32_t i = 0; i < clocks; i++) {
    jtagClock(0, 0);
  }
}


// ================================
// Enter Shift-DR
// ================================

void enterShiftDR() {
  // Run-Test/Idle
  // ->
  // Select-DR-Scan

  jtagClock(1, 0);


  // Select-DR-Scan
  // ->
  // Capture-DR

  jtagClock(0, 0);


  // Capture-DR
  // ->
  // Shift-DR

  jtagClock(0, 0);
}


// ================================
// Shift Normal Data Chunk
//
// Remain in Shift-DR
// ================================

void shiftDRChunk(uint32_t byteCount) {
  for (uint32_t byteIndex = 0; byteIndex < byteCount; byteIndex++) {
    // Wait for one byte

    while (!Serial.available()) {
    }

    uint8_t data = Serial.read();


    // Xilinx CFG_IN:
    // Send MSB first

    for (int bit = 7; bit >= 0; bit--) {
      bool tdi = (data >> bit) & 0x01;

      // Keep TMS LOW
      // Stay in Shift-DR

      jtagClock(0, tdi);
    }
  }
}


// ================================
// Shift Final Data Chunk
//
// Last bit exits Shift-DR
// ================================

void shiftDRFinalChunk(uint32_t byteCount) {
  for (uint32_t byteIndex = 0; byteIndex < byteCount; byteIndex++) {
    while (!Serial.available()) {}

    uint8_t data = Serial.read();


    for (int bit = 7; bit >= 0; bit--) {
      bool tdi = (data >> bit) & 0x01;

      bool isFinalBit = (byteIndex == byteCount - 1) && (bit == 0);


      if (isFinalBit) {
        // Shift-DR
        // ->
        // Exit1-DR

        jtagClock(1, tdi);
      } else {
        // Remain in Shift-DR

        jtagClock(0, tdi);
      }
    }
  }
}


// ================================
// Exit Shift-DR
// ================================

void exitShiftDR() {
  // Currently in Exit1-DR
  //
  // Exit1-DR
  // ->
  // Update-DR

  jtagClock(1, 0);


  // Update-DR
  // ->
  // Run-Test/Idle

  jtagClock(0, 0);
}


// ================================
// Setup
// ================================

void setup() {
  
  Serial.setRxBufferSize(RX_BUFFER_SIZE);
  Serial.begin(SERIAL_BAUD);
  // Serial.begin(2000000);
  

  pinMode(PIN_TCK, OUTPUT);
  pinMode(PIN_TDI, OUTPUT);
  pinMode(PIN_TMS, OUTPUT);

  pinMode(PIN_TDO, INPUT);


  // Initial states
  digitalWrite(PIN_TCK, LOW);
  digitalWrite(PIN_TDI, LOW);
  digitalWrite(PIN_TMS, HIGH);
}


// ================================
// Main Loop
// ================================

void loop() {
  if (!Serial.available()) {
    return;
  }


  uint8_t cmd = Serial.read();

  switch (cmd) {

      // ------------------------
      // Reset JTAG
      // ------------------------

    case CMD_RESET:
      {
        jtagReset();

        Serial.write(ACK);

        break;
      }


      // ------------------------
      // Shift IR
      // ------------------------

    case CMD_SHIFT_IR:
      {
        while (!Serial.available()) {}

        uint8_t instruction = Serial.read();

        shiftIR(instruction);

        Serial.write(ACK);

        break;
      }


      // ------------------------
      // Run-Test/Idle
      // ------------------------

    case CMD_RUNTEST:
      {
        uint32_t clocks = read32();

        runTest(clocks);

        Serial.write(ACK);

        break;
      }


      // ------------------------
      // Enter Shift-DR
      // ------------------------

    case CMD_DR_START:
      {
        enterShiftDR();

        Serial.write(ACK);

        break;
      }


      // ------------------------
      // Normal DR Chunk
      // ------------------------

    case CMD_DR_CHUNK:
      {
        uint32_t byteCount = read32();

        shiftDRChunk(byteCount);

        Serial.write(ACK);

        break;
      }


      // ------------------------
      // Final DR Chunk
      // ------------------------

    case CMD_DR_END:
      {
        uint32_t byteCount = read32();

        shiftDRFinalChunk(byteCount);

        exitShiftDR();

        Serial.write(ACK);

        break;
      }


    default:
      {
        // Unknown command
        break;
      }
  }
}
