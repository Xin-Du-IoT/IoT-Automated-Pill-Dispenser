# IoT Automated Pill Dispenser (Embedded C / RP2040)

##  Project Context
Developed an automated medication dispensing system using the **Raspberry Pi Pico (RP2040)**. The device ensures patient safety by precisely timing dosages, confirming dispensing via sensors, and logging status to a remote server.

##  Advanced Engineering Features
  **Finite State Machine (FSM)**: Designed a robust state-driven logic to manage calibration, dispensing, and error recovery.
  **Sensor Integration & Logic**:
    * **Photoelectric (Opto-fork)**: Implemented automatic homing sequence.
    * **Piezoelectric**: Developed a real-time signal processing logic to detect pill drops (falling edge detection).
  **System Reliability**: 
    * **EEPROM Persistence**: Utilized I2C EEPROM to store pill counts and device states, ensuring continuity across power cycles.
    * **Watchdog Integration**: Optimized LoRaWAN join sequences to prevent timing conflicts with the hardware watchdog.
   **IoT Connectivity**: Implemented **LoRaWAN (OTAA)** using AT commands over UART. The device reports status changes (boot, dispensed, empty, power-off during turn).

##  Hardware Stack
* RP2040 Microcontroller
* 28BYJ-48 Stepper Motor & Driver
* LoRa-E5 Module
* Piezoelectric and Optical sensors
