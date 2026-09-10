#!/usr/bin/env python3

import serial
import time
import sys
import os

# ==========================================
# Configuration
# ==========================================

#PORT = "/dev/ttyUSB3"

# BAUD = 921600
BAUD = 500000

#BITSTREAM_FILE = ("build/arty_35/counter_slave.bit")


# ==========================================
# ESP32 Commands
# ==========================================
CMD_RESET = 0x01
CMD_SHIFT_IR = 0x03
CMD_RUNTEST = 0x05
CMD_DR_START = 0x10
CMD_DR_CHUNK = 0x11
CMD_DR_END = 0x12
ACK = 0xAA


# ==========================================
# Xilinx 7-Series JTAG Instructions
# ==========================================
JPROGRAM = 0x0B
CFG_IN = 0x05
JSTART = 0x0C


# ==========================================
# Extract .bit Payload
# ==========================================
def read_bit_payload(filename):
    print("Reading bitstream...")

    with open(filename, "rb") as f:
        data = f.read()

    search_limit = min(1024, len(data))

    for pos in range(search_limit - 5):

        # Look for field 'e'
        if data[pos] != ord('e'):
            continue

        # 4-byte big-endian
        # payload length
        payload_len = int.from_bytes(data[pos + 1:pos + 5], byteorder="big")
        payload_start = pos + 5
        payload_end = (payload_start + payload_len)

        # Real payload should consume
        # remainder of the file

        if payload_end == len(data):
            print(f"Payload offset : " f"0x{payload_start:X}")
            print(f"Payload size   : " f"{payload_len} bytes")
            return data[payload_start : payload_end]

    raise RuntimeError("Could not find Xilinx "".bit payload")


# ==========================================
# Wait for ESP32 ACK
# ==========================================
def wait_ack(ser):
    response = ser.read(1)

    if response != bytes([ACK]):
        raise RuntimeError( f"ESP32 communication error: " f"{response}")


# ==========================================
# Reset JTAG
# ==========================================
def jtag_reset(ser):
    ser.write(bytes([CMD_RESET]))
    ser.flush()
    wait_ack(ser)


# ==========================================
# Shift Instruction Register
# ==========================================
def shift_ir(ser, instruction):
    ser.write(bytes([CMD_SHIFT_IR, instruction]))
    ser.flush()
    wait_ack(ser)


# ==========================================
# Run-Test / Idle
# ==========================================
def runtest(ser, clocks):
    ser.write(bytes([CMD_RUNTEST]))
    ser.write(clocks.to_bytes(4, byteorder="little"))
    ser.flush()
    wait_ack(ser)


# ==========================================
# Shift FPGA Bitstream
# ==========================================
def shift_bitstream(ser, payload):
    size = len(payload)
    print()
    print(f"Programming "f"{size} bytes...")

    # --------------------------------------
    # Enter Shift-DR
    # --------------------------------------
    ser.write(bytes([CMD_DR_START]))
    ser.flush()
    wait_ack(ser)

    # --------------------------------------
    # Chunk Size
    # --------------------------------------

    # Start with 256 bytes.
    # This prevents UART buffer overflow.
    #chunk_size = 256
    chunk_size = 16384

    offset = 0
    start_time = time.time()

    while offset < size:
        remaining = (size - offset)
        count = min(chunk_size, remaining)
        chunk = payload[offset: offset + count]

        is_last = (offset + count >= size)

        # ----------------------------------
        # Select command
        # ----------------------------------
        if is_last:
            command = CMD_DR_END
        else:
            command = CMD_DR_CHUNK

        # ----------------------------------
        # Send command
        # ----------------------------------
        ser.write(bytes([command]))

        # ----------------------------------
        # Send length
        # ----------------------------------
        ser.write(count.to_bytes(4, byteorder="little"))

        # ----------------------------------
        # Send data
        # ----------------------------------
        ser.write(chunk)
        ser.flush()

        # ----------------------------------
        # Wait until ESP32
        # finishes this chunk
        # ----------------------------------
        wait_ack(ser)
        offset += count

        percent = (offset * 100.0 / size)
        elapsed = (time.time() - start_time)

        print(
            f"\rProgramming: "
            f"{percent:6.2f}% "
            f"Elapsed: "
            f"{elapsed:7.1f}s",
            end="",
            flush=True
        )


    print()
    elapsed = (time.time() -  start_time)
    print()
    print(
        f"Bitstream programming "
        f"finished in "
        f"{elapsed:.2f} seconds"
    )


# ==========================================
# Main
# ==========================================
def main():
    # ==========================================
    # Command Line Arguments
    # ==========================================
    if len(sys.argv) != 3:
        print()
        print("Usage:")
        print(f"  python3 {sys.argv[0]} <serial_port> <bitstream_file>")
        print()
        print("Example:")
        print(f"  python3 {sys.argv[0]} /dev/ttyUSB0 build/arty_35/counter_slave.bit")
        sys.exit(1)

    PORT = sys.argv[1]
    BITSTREAM_FILE = sys.argv[2]

    # Check if bitstream exists
    if not os.path.isfile(BITSTREAM_FILE):
        print(f"ERROR: Bitstream file not found:")
        print(f"  {BITSTREAM_FILE}")
        sys.exit(1)

    print("====================================")
    print("ESP32 -> XC7A35T JTAG Programmer")
    print("====================================")
    print()

    # --------------------------------------
    # Read FPGA bitstream
    # --------------------------------------
    payload = read_bit_payload(BITSTREAM_FILE)
    print()

    # --------------------------------------
    # Open ESP32 serial port
    # --------------------------------------
    print(f"Opening {PORT} "f"at {BAUD} baud...")
    ser = serial.Serial(PORT, BAUD, timeout=30, write_timeout=30)

    # ESP32 may reset when serial opens
    time.sleep(2)

    try:
        # ----------------------------------
        # Step 1
        # ----------------------------------
        print()
        print("1. Resetting JTAG TAP...")
        jtag_reset(ser)

        # ----------------------------------
        # Step 2
        # ----------------------------------
        print("2. Sending JPROGRAM...")
        shift_ir(ser, JPROGRAM)

        # ----------------------------------
        # Step 3
        # ----------------------------------
        print("3. Waiting for FPGA initialization...")
        runtest(ser, 20000)

        # ----------------------------------
        # Step 4
        # ----------------------------------
        print("4. Sending CFG_IN...")
        shift_ir(ser, CFG_IN)

        # ----------------------------------
        # Step 5
        # ----------------------------------
        print("5. Sending FPGA bitstream...")
        shift_bitstream(ser, payload)

        # ----------------------------------
        # Step 6
        # ----------------------------------
        print()
        print("6. Sending JSTART...")
        shift_ir(ser, JSTART)

        # ----------------------------------
        # Step 7
        # ----------------------------------
        print("7. Sending startup clocks...")
        runtest(ser, 2000)

        print()
        print("====================================")
        print("FPGA PROGRAMMING COMPLETE")
        print("====================================")

    finally:
        ser.close()

# ==========================================
# Entry Point
# ==========================================
if __name__ == "__main__":

    try:
        main()

    except Exception as e:

        print()
        print("ERROR:", e)

        sys.exit(1)
