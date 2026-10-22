# AGENTS.md — ESP32 + SX1276 LoRa P2P Firmware

> **Status:** Project-wide authoritative instructions for AI coding agents.  
> **Scope:** ESP32 firmware written in **pure C** with **ESP-IDF (baseline v6.1)** for a LoRa SX1276 **node** and **gateway**.  
> **Coding Standards:** Synchronized with `docs/C_CODING_STANDARDS.md` (**v1.0.0 — APPROVED**).  
> **Hardware note:** The GPIO mapping below is confirmed for the PCB provided by the project owner. Do **not** assume an unconfirmed PCB has the same mapping.

---

## 1. Project scope and non-negotiable constraints

1. Develop firmware using **ESP-IDF native APIs and pure C**. Do not introduce Arduino APIs, Arduino framework dependencies, or C++ unless explicitly approved (`API-010`).
2. The radio transceiver is **Semtech SX1276** communicating over SPI. The initial radio link is **custom LoRa point-to-point (P2P)**, **not LoRaWAN**.
3. Keep node-specific and gateway-specific application logic separate; share driver, hardware abstraction, and protocol framing components.
4. Treat the **physical PCB wiring** and explicit user requirements as authoritative. Never replace them with wiring from examples, generic skill instructions, or assumed ESP32 defaults (`HW-001`).
5. The project's targeted baseline is **ESP-IDF v6.1** and the RF band is confirmed at **915 MHz**. Verify the installed toolchain and existing project configuration before utilizing version-specific APIs or altering configurations.
6. Do not add major features (e.g., MQTT, LoRaWAN stack, OTA, TLS/crypto libraries, cloud services) unless explicitly requested. Maintain clean modular boundaries to facilitate future extensions.
7. Prioritize correctness, recoverability, memory safety, and empirical verification over speculative optimizations.

---

## 2. Instructions and source-of-truth precedence

When instructions conflict, resolve them in this strict order:

1. Explicit instructions from the project owner for the current task, including approved hardware modifications.
2. Locked hardware constraints in this document and the **confirmed PCB schematic** (`HW-001`..`HW-008`).
3. **`docs/C_CODING_STANDARDS.md` (v1.0.0 — APPROVED)**, and approved specifications (`docs/HARDWARE_SPEC.md`, `docs/PROTOCOL_SPEC.md` when created).
4. Existing component contracts and code conventions that do not contradict the above.
5. ESP-IDF v6.1 documentation, Semtech SX1276 datasheet, and external references.

If a conflict affects pin assignments, hardware safety, radio transmission, or an existing public API, **stop work immediately and request clarification**. Do not silently implement a speculative workaround.

### Active C coding standards rule (Scoped Loading)
`docs/C_CODING_STANDARDS.md` (v1.0.0) is the authoritative coding standard. Because `AGENTS.md` already summarizes the core rules, **do not load the entire `docs/C_CODING_STANDARDS.md` on every turn** unless performing a full compliance audit. Instead, load only the sections relevant to the current task:
- **Formatting & Naming (`FMT-*`, `NAM-*`, `API-*`):** Sections 02–04.
- **Types, Memory & Errors (`TYP-*`, `MEM-*`, `ERR-*`):** Sections 05–07.
- **FreeRTOS & SX1276 Driver (`RTOS-*`, `RAD-*`):** Sections 08–09.
- **Protocol Serialization & Hardware Pins (`PRO-*`, `HW-*`):** Sections 10–11.
- **Pre-completion Checklist (`QA-*`):** Section 13.3.

---

## 3. Locked PCB pin mapping — SX1276 & Peripherals

The following pinout is confirmed for the current PCB revision (`HW-001`). The agent MUST NOT alter, swap, or reassign any of these pins without explicit owner approval.

| SX1276 signal | ESP32 GPIO | Symbolic Constant | Purpose / Notes |
| :--- | :---: | :--- | :--- |
| `LoRa_DIO0` | **GPIO26** | `BOARD_LORA_DIO0` | Radio interrupt line (RxDone / TxDone detection) |
| `LoRa_RST` | **GPIO23** | `BOARD_LORA_RST` | Active-low hardware reset |
| `LoRa_SS` / `CS` | **GPIO18** | `BOARD_LORA_CS` | SPI chip select |
| `LoRa_SCK` | **GPIO5** | `BOARD_LORA_SCK` | SPI clock (Strapping-aware: do not swap) |
| `LoRa_MOSI` | **GPIO27** | `BOARD_LORA_MOSI` | SPI master-out / radio data-in |
| `LoRa_MISO` | **GPIO19** | `BOARD_LORA_MISO` | SPI master-in / radio data-out |

### Pin lock enforcement & peripheral allocation

1. **Strict SPI pin lock (`HW-001`):** Never swap `SCK=GPIO5` with `CS=GPIO18`. Do not assume ESP32 default VSPI pins.
2. Centralize all board GPIO assignments in `components/board_config/include/board_pins.h` (`HW-002`). Application and driver code MUST use symbolic `BOARD_LORA_*` constants, never hardcoded numeric literals.
3. The board configuration header MUST resolve to:
   ```c
   #define BOARD_LORA_DIO0 GPIO_NUM_26
   #define BOARD_LORA_RST  GPIO_NUM_23
   #define BOARD_LORA_CS   GPIO_NUM_18
   #define BOARD_LORA_SCK  GPIO_NUM_5
   #define BOARD_LORA_MOSI GPIO_NUM_27
   #define BOARD_LORA_MISO GPIO_NUM_19
   ```
4. Configure `spi_bus_config_t` using the locked `SCK`/`MOSI`/`MISO` constants. Set `spi_device_interface_config_t.spics_io_num` to `BOARD_LORA_CS` when using hardware-managed chip select (`HW-003`).
5. Treat `GPIO5` as a boot-strapping pin on applicable ESP32 variants (`HW-005`). Ensure valid logic levels during boot; do not alter the assignment.
6. **UART peripheral allocation (`HW-006`):** Reserve `UART_NUM_0` exclusively for console logging (`ESP_LOG`) and firmware flashing. Any external UART sensor MUST use `UART_NUM_1` or `UART_NUM_2` with pins explicitly declared in `board_pins.h` once confirmed.
7. During two-board P2P demos using two identical confirmed PCBs, both boards share `board_pins.h`. For any separate unconfirmed gateway PCB, report `HARDWARE_UNCONFIRMED` (`HW-004`).

---

## 4. Mandatory naming, typing, and serialization rules

### Function naming & canonical module prefixes (`NAM-001`..`NAM-007`)
For all project-owned C functions (both public APIs and internal `static` helpers), use lowercase ASCII `snake_case`:
```text
<module>_<verb>_<object>()
```

Use the approved **Canonical Module Prefix Map** (`DEC-01`):

| Component / Directory | Mandatory `<module>_` Prefix | Valid Function Examples |
| :--- | :--- | :--- |
| `components/board_config` | `board_` | `board_init_spi_bus()`, `board_reset_radio_gpio()` |
| `components/sx1276_driver` | `sx1276_` | `sx1276_read_register()`, `sx1276_set_frequency()` |
| `components/app_protocol` | `protocol_` | `protocol_encode_frame()`, `protocol_validate_crc16()` |
| `components/sensor_uart` *(planned)* | `sensor_` | `sensor_init_uart()`, `sensor_read_sample()` |
| `main/` (Node role logic) | `node_` | `node_run_tx_task()`, `node_collect_telemetry()` |
| `main/` (Gateway role logic) | `gateway_` | `gateway_run_rx_task()`, `gateway_log_packet()` |
| `main/` (Shared role/Ping-Pong) | `app_` | `app_start_role_task()`, `app_run_ping_pong()` |

* File-scope `static` variables MUST start with `s_` (e.g., `s_spi_handle`) (`NAM-011`).
* Types/structs/enums use `snake_case_t`; enum constants and macros use `UPPER_SNAKE_CASE` (`NAM-013`..`NAM-015`).
* **Exceptions:** Framework entry points (`app_main`), mandatory IDF callback signatures, and third-party symbols (`NAM-005`).

### Data types and explicit bytewise serialization (`TYP-001`..`TYP-006`, `PRO-003`..`PRO-005`)
1. **Exact-width types (`DEC-02`):** Use `<stdint.h>` (`uint8_t`, `int16_t`, `uint32_t`, etc.) and `<stdbool.h>` for all hardware registers, protocol fields, sensor values, and domain structs. Use `size_t` for buffer capacities, lengths, and indices. Native `int` is permitted **only** at the boundary of ESP-IDF or C standard library APIs whose signatures require `int` (e.g., `uart_read_bytes()`, `gpio_get_level()`, `snprintf()`), and must be range-checked before converting to exact-width types or `size_t`.
2. **No raw floats over the air (`TYP-006`):** Convert physical quantities to scaled fixed-point integers (e.g., `int16_t temp_x100`) with bounds checking prior to conversion.
3. **Explicit bytewise serialization (`DEC-03`, `PRO-003`, `PRO-004`):** Encode and decode all over-the-air LoRa packets and UART frames **explicitly byte-by-byte in Little-Endian order** with buffer capacity/length validation prior to every access (`PRO-005`). Do **not** `memcpy()` raw C structs onto the wire or cast `uint8_t*` buffers to `__attribute__((packed))` struct pointers.

---

## 5. Firmware architecture and build structure

The project adopts a unified single-codebase structure for shared driver development, node/gateway role execution, and two-board ping-pong testing:

```text
esp32_sx1276_demo/
├── AGENTS.md
├── CMakeLists.txt                  # Project-level CMake
├── sdkconfig                       # Generated configuration
├── docs/
│   ├── C_CODING_STANDARDS.md       # Active v1.0.0 coding standards
│   ├── HARDWARE_SPEC.md            # Hardware verification notes (optional)
│   └── PROTOCOL_SPEC.md            # Detailed wire protocol spec (optional)
├── main/
│   ├── CMakeLists.txt              # Depends on sx1276_driver, board_config, app_protocol
│   ├── Kconfig.projbuild           # Single choice role: [Node TX / Gateway RX / Ping-Pong]
│   └── main.c                      # Initializes hardware, dispatches task for active role
└── components/
    ├── board_config/
    │   ├── CMakeLists.txt
    │   └── include/
    │       └── board_pins.h        # Locked GPIO constants and hardware definitions
    ├── sx1276_driver/              # Reusable SX1276 SPI driver
    │   ├── CMakeLists.txt
    │   ├── include/
    │   │   └── sx1276.h            # Public driver APIs (sx1276_*)
    │   ├── sx1276_regs.h           # Private register map (0x00 - 0x70)
    │   └── sx1276.c                # State machine, SPI driver logic, DIO0 ISR
    └── app_protocol/               # Shared P2P framing & explicit serialization
        ├── CMakeLists.txt
        ├── include/
        │   └── app_protocol.h      # Public frame structs and protocol_* APIs
        └── app_protocol.c          # Bytewise Little-Endian encode/decode + CRC16
```

### Component rules & ESP-IDF v6.1 compliance (`API-001`..`API-008`)
1. **Decomposed driver dependencies (`API-007`):** In component `CMakeLists.txt`, use ESP-IDF v6.x modular driver names (`esp_driver_spi`, `esp_driver_gpio`, `esp_driver_uart`, `esp_timer`, `freertos`) instead of the legacy monolithic `driver` component:
   ```cmake
   idf_component_register(
       SRCS "sx1276.c"
       INCLUDE_DIRS "include"
       PRIV_REQUIRES board_config esp_driver_spi esp_driver_gpio esp_timer freertos
   )
   ```
2. **Encapsulation (`API-008`, `RAD-002`):** Keep register addresses/bitmasks private inside `sx1276_regs.h`. Application code in `main/` must never access SPI handles or SX1276 registers directly.
3. **Role selection (`DEC-08`):** Use a mutually exclusive `choice` block in `main/Kconfig.projbuild` (`CONFIG_APP_ROLE_NODE_TX`, `CONFIG_APP_ROLE_GATEWAY_RX`, `CONFIG_APP_ROLE_PING_PONG`) so only one role is active per build.

---

## 6. SX1276 and LoRa P2P implementation constraints (`RAD-001`..`RAD-012`)

1. Refer to the **Semtech SX1276 datasheet** and ESP-IDF v6.1 documentation as authoritative. Do not apply SX126x register maps or BUSY-pin logic (`RAD-001`).
2. **Silicon identity check (`RAD-004`):** `sx1276_init()` MUST read `RegVersion (0x42)` and verify `0x12` before configuring RF registers.
3. **RF band & antenna safety (`RAD-011`):** The target operating band is **915 MHz**. Never trigger a transmission without an attached 915 MHz antenna.
4. **Default P2P demo profile (915 MHz):**
   * **Carrier Frequency:** `915000000` Hz (915.0 MHz — `RegFrf = 0xE4C000`)
   * **Bandwidth:** `125` kHz (`RegModemConfig1`)
   * **Spreading Factor:** `SF7` (`RegModemConfig2`)
   * **Coding Rate:** `4/5` (`RegModemConfig1`)
   * **Sync Word:** `0x12` (`RegSyncWord` — Private LoRa network)
   * **Preamble Length:** `8` symbols
   * **Header Mode:** Explicit Header mode
   * **Payload CRC:** Enabled (`RxPayloadCrcOn = 1`)
5. **RF output pin (`DEC-04`, `RAD-010`):** Make the PA output path configurable in `sx1276_config_t` (`SX1276_PA_PIN_BOOST` vs `SX1276_PA_PIN_RFO`), defaulting to `PA_BOOST` at a safe bench-test power level (`+2 dBm` to `+7 dBm`) when two boards are tested within 1–2 meters.
6. **LoRa mode switching constraint (`RAD-005`):** The `LongRangeMode` bit (bit 7 in `RegOpMode 0x01`) can **only be written while the chip is in SLEEP mode** (`RegOpMode = 0x00`).
7. **Interrupts & FreeRTOS task coordination (`RTOS-001`..`RTOS-004`, `RAD-006`):**
   * Keep the `DIO0` GPIO ISR minimal (`IRAM_ATTR` if registered with `ESP_INTR_FLAG_IRAM`). Perform no SPI transactions or `ESP_LOG*` calls inside the ISR.
   * Wake the single radio owner task via `vTaskNotifyGiveFromISR()` / `ulTaskNotifyTake()`.
   * Read and clear `RegIrqFlags (0x12)` (write-1-to-clear) inside the task context.
8. Check `PayloadCrcError` on every `RxDone` event; discard corrupted frames and increment diagnostic counters (`RAD-007`).

---

## 7. FreeRTOS, memory, and fault handling (`MEM-001`..`MEM-013`, `ERR-001`..`ERR-010`)

1. **Static/bounded memory (`MEM-001`, `MEM-002`, `MEM-006`):** Prefer static allocation and fixed-size buffers. Never call `malloc()`, `calloc()`, `realloc()`, or `free()` inside steady-state task loops or packet handlers. Variable Length Arrays (VLAs) are forbidden.
2. **SPI transaction safety (`MEM-013`):** Use `SPI_TRANS_USE_TXDATA` / `SPI_TRANS_USE_RXDATA` for 1–4 byte register reads/writes. Ensure any buffer used for multi-byte FIFO bursts meets ESP-IDF SPI master alignment/lifetime rules.
3. **Bounded waits (`ERR-005`, `RTOS-007`):** Avoid indefinite busy-waiting loops. Apply explicit timeouts to all SPI transactions, queues, and radio state transitions.
4. **Error handling (`ERR-001`..`ERR-004`):** Propagate `esp_err_t` across function calls. Do **not** call `ESP_ERROR_CHECK()` or `assert()` in recoverable runtime paths (SPI timeouts, invalid packets, CRC failures).

---

## 8. Change workflow & verification states (`QA-001`..`QA-008`)

**Before implementation**
1. Read this `AGENTS.md` and `docs/C_CODING_STANDARDS.md` (v1.0.0).
2. Inspect existing component headers and contracts before introducing new functions.

**During implementation**
1. Enforce the locked GPIO constants in Section 3 (`HW-001`..`HW-003`), the `<module>_<verb>_<object>()` prefix map in Section 4 (`NAM-001`..`NAM-003`), and ESP-IDF v6.1 formatting (`FMT-001`..`FMT-010`).
2. Keep public `.h` declarations matched with `.c` definitions and `CMakeLists.txt` dependencies.
3. Never alter radio parameters or wire framing on only one endpoint.

**At completion**
1. Summarize modified files, verified rule compliance (`NAM-001`, `MEM-002`, `PRO-003`, `HW-001`), and key behavior.
2. State the exact verification stage achieved (`QA-005`):
   * `CODE_WRITTEN`: Source written and statically checked against `docs/C_CODING_STANDARDS.md` v1.0.0.
   * `BUILD_VERIFIED`: Compiled cleanly under ESP-IDF v6.1 (`idf.py build`).
   * `HARDWARE_VERIFIED`: Validated on physical ESP32 + SX1276 boards with captured UART logs or instrument traces.
   * `NOT_VERIFIED`: Blocked or not yet executed.

---

## 9. Approval boundaries and open hardware questions

**Explicit owner approval is mandatory before:**
* Remapping any locked GPIOs or changing board pin assignments (`HW-001`).
* Assigning pins for any secondary/gateway PCB whose schematic has not been confirmed (`HW-004`).
* Changing ESP-IDF versions, switching to C++, or introducing external non-IDF frameworks (`API-010`).
* Altering the 915 MHz default radio profile or over-the-air packet structures (`RAD-009`, `PRO-009`).
* Executing destructive operations: erasing NVS/Flash, modifying partition tables, or burning eFuses (`HW-008`).

**Information still to confirm with the project owner (for later phases):**
1. Pinout and schematic for the Gateway board if not physically identical to the Node PCB.
2. Target UART sensor model, baud rate, frame format, and assigned GPIOs for `UART_NUM_1` / `UART_NUM_2`.

---

---

## 10. Antigravity Multi-Agent Workflow (3.8 Flash Optimized)

To maximize code quality and minimize token consumption on the **3.8 Flash** model, this repository enforces a strict separation of duties between the **Primary Coder Agent** and the **Independent Auditor Subagent**.

### 10.1. Role definitions & context boundaries

| Agent Role | Active Trigger | Context Allowed to Load | Prohibited Actions |
| :--- | :--- | :--- | :--- |
| **Role A: Primary Coder Agent** | Default role during Phase implementation and build fixing | 1. `AGENTS.md` (Always-on)<br>2. Only the specific `.c`/`.h`/`CMakeLists.txt` files being edited<br>3. Only the single relevant section of `docs/C_CODING_STANDARDS.md` if needed | - MUST NOT load all 400+ lines of `docs/C_CODING_STANDARDS.md` during active coding.<br>- MUST NOT self-certify final Phase completion without an Audit pass. |
| **Role B: Auditor Subagent** | Triggered ONLY at Phase Gate (after `idf.py build` passes) | 1. `docs/C_CODING_STANDARDS.md` (Focus on target rule groups & §13.3)<br>2. Only the modified/created `.c`, `.h`, and `CMakeLists.txt` files in the current Phase | - MUST NOT modify or rewrite source files directly.<br>- MUST NOT carry over conversational bias from the coding session.<br>- Outputs ONLY a structured Audit Report. |

### 10.2. Mandatory 4-Step Phase-Gate Execution Pipeline

For every implementation phase, execute the following sequence strictly in order:

1. **Step 1 — Scoped Implementation (Coder Agent):**
   Write or modify the target component files obeying the locked pins in Section 3 (`HW-001`..`HW-006`), the prefix map in Section 4 (`NAM-001`..`NAM-003`), and ESP-IDF v6.1 component rules (`API-007`).
2. **Step 2 — Compiler Gate (Zero-Token Check):**
   Run `idf.py build` (or syntax/type verification if toolchain execution is delegated). The Coder Agent MUST resolve all compiler errors and warnings (`QA-003`) **before** invoking the Auditor Subagent.
3. **Step 3 — Compliance Gate (Spawn Auditor Subagent):**
   Once compilation succeeds (`BUILD_VERIFIED`), spawn/invoke the **Auditor Subagent** using a clean context containing only the changed files and the target sections of `docs/C_CODING_STANDARDS.md`.
4. **Step 4 — Remediation Loop (Max 2 Iterations):**
   - If the Auditor Subagent returns `AUDIT_STATUS: FAIL`, the Primary Coder Agent fixes **only** the exact `File:Line` items cited by `[Rule ID]`.
   - Re-run `idf.py build` and re-verify until `AUDIT_STATUS: PASS` is achieved.