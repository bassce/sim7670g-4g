# Local Bluetooth fixes

The WS-SIM7670G-V2_BLE environment uses these source directories through
PlatformIO file dependencies. Do not replace the fixes with edits only to `.pio`.

- BLESerial: based on adlerre/BLESerial 1.0.0, with its original GPL license.
  Fix client ownership after failed connections and rejected deletion; check TX
  capability and RX notify/indicate subscription; preserve disconnect diagnostics;
  synchronize the receive buffer and retain repeated received bytes.
- ELMDuino: based on PowerBroker2/ELMDuino 3.4.1, with its original license.
  Reuse/reallocate the payload safely across repeated initialization; preserve the
  buffer on allocation failure; record initialization command and response state.
- NimBLE-Arduino remains pinned to 2.3.7. Client deletion handling follows the
  synchronous/asynchronous ownership behavior of that version's deleteClient.

`test/host/run_ble_regression.py` compiles the actual patched methods against
fault-injecting mocks using Python package ziglang 0.14.1. It tests repeated failed
connections, missing GATT features, rejected subscription, indicate, delayed
callbacks, failed cleanup, repeated RX bytes, write modes and allocation failures
across 1,000 ELM initializations. It also checks the application's shared attempt
result handling and clearing stale errors after success. This supplements the ESP32 firmware build and
device verification; it does not emulate the BLE radio or vehicle ECU.
