# PROTOCOL_SPEC.md — LoRa P2P Binary Packet Protocol Specification

> **Status:** APPROVED — Phase 5 Baseline Wire Specification  
> **Applicability:** Node (Initiator) and Gateway (Responder) P2P communication  
> **Coding Standards:** docs/C_CODING_STANDARDS.md (PRO-001..PRO-011, DEC-07)  

---

## 1. Protocol Overview

This document specifies the binary over-the-air packet protocol used for point-to-point (P2P) communication between ESP32 microcontrollers equipped with Semtech SX1276 transceivers operating at 915 MHz.

### Design Principles:
1. **Explicit Bytewise Serialization/Deserialization (`PRO-003`, `DEC-03`):** Every field is serialized byte-by-byte into buffers without `memcpy()` of C structs or casting byte arrays to packed structure pointers.
2. **Deterministic Little-Endian Encoding (`PRO-004`):** All multi-byte integers (16-bit) are encoded in Little-Endian byte order on the wire.
3. **Strict Pre-Validation (`PRO-005`, `PRO-006`):** Untrusted frames are boundary-checked (`rx_len >= PROTOCOL_MIN_FRAME_SIZE`), header length verified against actual received bytes, and CRC16 validated before application processing.
4. **Zero Heap Allocation (`MEM-002`):** Encoding, decoding, and validation operate exclusively on fixed-size buffers with zero dynamic memory allocation.

---

## 2. Frame Structure

Each frame consists of a fixed 9-byte Header, a variable-length Payload (0 to 64 bytes), and a 2-byte CRC16 trailer.

```text
+-------------------+---------+----------+-----------+-----------+-------------+------------------+---------------+
| magic (2 bytes)   | version | msg_type | node_id   | seq_num   | payload_len | payload          | crc16         |
| 0x5358 ('X', 'S') | (1 byte)| (1 byte) | (2 bytes) | (2 bytes) | (1 byte)    | (0..64 bytes)    | (2 bytes)     |
+-------------------+---------+----------+-----------+-----------+-------------+------------------+---------------+
| <----------------------- CRC16 Coverage -----------------------------------> | <--- Checksum -> |
```

### Field Definitions

| Offset | Field Name | Width | Type | Wire Order | Description / Valid Values |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 0 | `magic` | 2 bytes | `uint16_t` | Little-Endian | Protocol identifier: `0x5358` (ASCII 'S', 'X'). Wire: `0x58, 0x53`. |
| 2 | `version` | 1 byte | `uint8_t` | Byte | Protocol version: `0x01` (`PROTOCOL_VERSION_1`). |
| 3 | `msg_type` | 1 byte | `uint8_t` | Byte | Message type: `0x01` (PING), `0x02` (PONG). |
| 4 | `node_id` | 2 bytes | `uint16_t` | Little-Endian | Originating device ID (e.g. `0x0001` for Node, `0x0002` for Gateway). |
| 6 | `seq_num` | 2 bytes | `uint16_t` | Little-Endian | Monotonically incrementing transaction sequence number. |
| 8 | `payload_len`| 1 byte | `uint8_t` | Byte | Number of payload bytes ($0 \le N \le 64$). |
| 9 | `payload` | $N$ bytes| `uint8_t[]`| Bytes | Application payload data (length $N$). |
| $9+N$ | `crc16` | 2 bytes | `uint16_t` | Little-Endian | CRC16 checksum over bytes $0$ to $(8 + N)$. |

### Size Boundaries
- **Header Size:** 9 bytes (`PROTOCOL_HEADER_SIZE`)
- **CRC16 Size:** 2 bytes (`PROTOCOL_CRC_SIZE`)
- **Minimum Frame Size ($N = 0$):** 11 bytes (`PROTOCOL_MIN_FRAME_SIZE`)
- **Maximum Payload Size:** 64 bytes (`PROTOCOL_MAX_PAYLOAD_SIZE`)
- **Maximum Frame Size ($N = 64$):** 75 bytes (`PROTOCOL_MAX_FRAME_SIZE`)

---

## 3. Message Types

| Value | Identifier | Purpose |
| :---: | :--- | :--- |
| `0x01` | `PROTOCOL_MSG_TYPE_PING` | Ping request sent by Initiator to test radio link and RTT. |
| `0x02` | `PROTOCOL_MSG_TYPE_PONG` | Pong response sent by Responder echoing `node_id` and `seq_num`. |

---

## 4. CRC16 Specification

The protocol uses the standard CRC-16/CCITT-FALSE algorithm:

- **Polynomial:** `0x1021` ($x^{16} + x^{12} + x^5 + 1$)
- **Initial Value:** `0xFFFF`
- **Input Reflected:** `false`
- **Result Reflected:** `false`
- **Output XOR:** `0x0000`
- **CRC Coverage:** Bytes $0$ through $(8 + N)$ inclusive (entire Header + Payload).
- **Wire Representation:** Little-Endian (LSB at offset $9+N$, MSB at offset $10+N$).

---

## 5. Test Vectors

### 5.1. Standard CRC-16/CCITT-FALSE Vector
- **Input:** ASCII `"123456789"` (`0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39`)
- **Expected CRC16:** `0x29B1`
- **Wire Little-Endian:** `0xB1, 0x29`

### 5.2. Phase 5 PING Frame Vector
- **Fields:**
  - `magic`: `0x5358`
  - `version`: `0x01`
  - `msg_type`: `0x01` (PING)
  - `node_id`: `0x0001`
  - `seq_num`: `0x0001`
  - `payload_len`: `4`
  - `payload`: `"PING"` (`0x50, 0x49, 0x4E, 0x47`)
- **Covered Bytes (13 bytes):** `58 53 01 01 01 00 01 00 04 50 49 4E 47`
- **Calculated CRC16:** `0x0D12`
- **Complete Wire Frame (15 bytes):**
  `58 53 01 01 01 00 01 00 04 50 49 4E 47 12 0D`
