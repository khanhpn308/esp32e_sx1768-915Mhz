# C CODING STANDARDS — ESP32 + SX1276 LoRa P2P

| Thuộc tính | Giá trị |
|---|---|
| Document ID | `ESP32-LORA-CSTD` |
| Version | `1.0.0` |
| Status | **APPROVED — ACTIVE MANDATORY STANDARD** |
| Date | `2026-10-17` |
| Language | Vietnamese (giữ nguyên English technical terms / API names) |
| Project baseline | **ESP-IDF v6.1; Pure C; SX1276 (915 MHz); Custom LoRa P2P; Node/Gateway** |
| Intended repository path | `docs/C_CODING_STANDARDS.md` |
| Governing instructions | `AGENTS.md` (đã đồng bộ hóa với phiên bản v1.0.0 này) |

> **Phạm vi hiệu lực:** Tài liệu này đã được phê duyệt chính thức (`v1.0.0`) và là quy chuẩn bắt buộc cho toàn bộ mã nguồn C trong dự án. Mọi quy tắc `MUST` / `MUST NOT` tại đây có hiệu lực thi hành ngay lập tức cùng với `AGENTS.md`.

---

## 01. Scope, design principles & normative language

### 01.1. Phạm vi áp dụng

Áp dụng cho **mã C do dự án sở hữu** trong `main/` và `components/`, các public/private headers, mã liên quan đến SPI/SX1276, LoRa P2P, UART sensor, FreeRTOS và board configuration. Không tự ý định dạng lại hoặc sửa đổi mã nguồn của ESP-IDF, FreeRTOS hoặc thư viện bên thứ ba.

### 01.2. Các nguyên tắc chi phối

1. **Safety first:** Ưu tiên memory safety, data integrity và fault isolation.
2. **Reliability and maintainability:** Code rõ ràng, ownership minh bạch, dễ phục hồi và kiểm thử.
3. **Deterministic memory:** Phân bổ chủ yếu bằng static/preallocated storage; cấm dynamic allocation trong vòng lặp vận hành (steady state).
4. **Bounded execution:** Timeout, retry, kích thước buffer và stack usage luôn có giới hạn xác định.
5. **Hardware fidelity:** Pinout và cấu hình RF đã được người sở hữu phê duyệt là ràng buộc thiết kế cứng.
6. **No invented hardware evidence:** Build thành công không đồng nghĩa với thử nghiệm phần cứng thành công.

Ưu tiên trên **không có nghĩa tối ưu RAM/CPU bị bỏ qua**. Tối ưu hóa được thực hiện dựa trên số đo hoặc giới hạn tài nguyên cụ thể và không làm suy giảm an toàn bộ nhớ hay khả năng bảo trì.

### 01.3. Từ khóa quy định

- **MUST / MUST NOT:** Bắt buộc / cấm tuyệt đối; vi phạm sẽ chặn nghiệm thu nếu không có ngoại lệ được chủ dự án phê duyệt.
- **SHOULD / SHOULD NOT:** Mặc định tuân thủ; mọi khác biệt phải có lý do kỹ thuật xác đáng và được ghi nhận.
- **MAY:** Được phép thực hiện nếu không mâu thuẫn với các quy tắc ưu tiên cao hơn.
- **TBD:** Chưa đủ bằng chứng/đặc tả; không tự suy đoán để triển khai.

Mỗi quy tắc có mã định danh nội bộ dạng `FMT-001`, `MEM-004`, `RAD-003` để AI agent và lập trình viên dễ dàng truy xuất khi review code.

### 01.4. Thứ tự nguồn tham chiếu

1. Chỉ dẫn hiện hành của chủ dự án và **PCB schematic đã xác nhận**.
2. `AGENTS.md` và các khóa phần cứng (hardware locks) trong đó.
3. Tài liệu `docs/C_CODING_STANDARDS.md` (bản v1.0.0 này) cùng các đặc tả `docs/HARDWARE_SPEC.md`, `docs/PROTOCOL_SPEC.md` khi được ban hành.
4. ESP-IDF v6.1 API Reference / Style Guide và Semtech SX1276 Datasheet.
5. Tập con SEI CERT C được dẫn chiếu tại đây.
6. AI skills, ví dụ mẫu và tài liệu bên ngoài.

Khi có xung đột, agent MUST báo cáo nguồn, phạm vi ảnh hưởng và phương án; MUST NOT tự ý thay đổi pinout, RF settings hoặc public interface.

---

## 02. C code formatting (ESP-IDF v6.1 aligned)

| ID | Mức | Quy tắc |
|---|---|---|
| `FMT-001` | MUST | Indentation: **4 spaces**, không dùng ký tự tab để thụt lề. |
| `FMT-002` | MUST | Định nghĩa hàm: dấu `{` mở hàm đặt ở **dòng riêng**; các cấu trúc điều khiển (`if`, `else`, `for`, `while`, `switch`) đặt dấu `{` trên **cùng dòng** với điều kiện. |
| `FMT-003` | MUST | Dùng một khoảng trắng sau từ khóa `if`, `for`, `while`, `switch` và trước `{`; dùng khoảng trắng quanh toán tử nhị phân và toán tử gán. |
| `FMT-004` | SHOULD | Giới hạn độ dài dòng **120 ký tự**; ngoại lệ khi việc ngắt dòng làm giảm khả năng đọc của chuỗi log hoặc bảng hằng số. |
| `FMT-005` | MUST | Sử dụng ký tự xuống dòng LF (`\n`); không để khoảng trắng thừa cuối dòng (trailing whitespace). |
| `FMT-006` | MUST | Để đúng một dòng trống giữa các hàm; không đặt dòng trống thừa ngay đầu hoặc cuối thân hàm. |
| `FMT-007` | MUST | Không giữ lại code chết qua `#if 0` hoặc comment-out; hãy xóa bỏ và dựa vào lịch sử Git. |
| `FMT-008` | SHOULD | Comment giải thích **tại sao (why) / ràng buộc phần cứng / kịch bản lỗi**, không lặp lại nội dung hiển nhiên của câu lệnh. |
| `FMT-009` | MUST | File `.c` và `.h` sử dụng bảng mã UTF-8 hoặc ASCII; mọi tên định danh C (hàm, biến, kiểu, macro) bắt buộc dùng ký tự ASCII chuẩn. |
| `FMT-010` | SHOULD | Tuân thủ nhất quán cấu hình định dạng chuẩn của ESP-IDF v6.1 Style Guide; không chạy chồng chéo nhiều công cụ auto-format có quy tắc đóng mở ngoặc xung đột. |

**Ví dụ chuẩn định dạng ESP-IDF:**

```c
static esp_err_t sx1276_validate_frequency(uint32_t frequency_hz)
{
    if (frequency_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}
```

---

## 03. Identifiers & naming conventions

### 03.1. Function naming & canonical module prefixes

| ID | Mức | Quy tắc |
|---|---|---|
| `NAM-001` | MUST | Mọi hàm C do dự án sở hữu (cả public API và hàm nội bộ `static`) phải tuân thủ mẫu **`<module>_<verb>_<object>()`**. |
| `NAM-002` | MUST | Tên hàm dùng chữ thường `snake_case`; `<verb>` là động từ hành động rõ nghĩa (`init`, `reset`, `read`, `write`, `set`, `get`, `send`, `receive`, `encode`, `decode`, `validate`, `parse`, `handle`), `<object>` là đối tượng tác động. |
| `NAM-003` | MUST | `<module>` prefix phải tuân thủ đúng **Bảng ánh xạ Prefix cố định** bên dưới để giữ tên hàm ngắn gọn và nhất quán. |
| `NAM-004` | MUST | Mọi hàm và biến toàn cục trong phạm vi 1 file `.c` (không xuất ra `.h`) phải có từ khóa `static`. |
| `NAM-005` | MUST | Ngoại lệ đặt tên chỉ áp dụng cho điểm vào bắt buộc của framework (`app_main`), hàm callback bị ràng buộc bởi chữ ký thư viện ngoài, hoặc thư viện bên thứ ba. |
| `NAM-006` | MUST | Không tự ý đổi tên các hàm public API đã được phê duyệt nếu chưa có yêu cầu refactor rõ ràng. |
| `NAM-007` | SHOULD | Cấm các tên hàm chung chung thiếu ngữ cảnh như `process()`, `handle()`, `do_task()`, `read_data()`, hoặc kiểu đặt tên đảo ngược `sx1276_frequency_set()`, `sendPacket()`. |

**Bảng ánh xạ Prefix cố định (Canonical Module Prefix Map — Đã phê duyệt):**

| Thư mục / Component | Prefix `<module>_` bắt buộc | Ví dụ tên hàm hợp lệ |
|---|---|---|
| `components/board_config` | `board_` | `board_init_spi_bus()`, `board_reset_radio_gpio()` |
| `components/sx1276_driver` | `sx1276_` | `sx1276_read_register()`, `sx1276_set_frequency()` |
| `components/app_protocol` | `protocol_` | `protocol_encode_frame()`, `protocol_validate_crc16()` |
| `components/sensor_uart` *(khi thêm)* | `sensor_` | `sensor_init_uart()`, `sensor_read_sample()` |
| `main/` (Logic riêng của Node) | `node_` | `node_run_tx_task()`, `node_collect_telemetry()` |
| `main/` (Logic riêng của Gateway) | `gateway_` | `gateway_run_rx_task()`, `gateway_log_packet()` |
| `main/` (Khởi tạo chung / Ping-Pong) | `app_` | `app_start_role_task()`, `app_run_ping_pong()` |

### 03.2. Variables, types, enums, macros

| ID | Mức | Quy tắc / Ví dụ |
|---|---|---|
| `NAM-010` | MUST | Biến cục bộ và tham số hàm: chữ thường `snake_case`, vd: `packet_length`, `frequency_hz`. |
| `NAM-011` | MUST | Biến `static` ở phạm vi file (file-scope static) phải có tiền tố `s_`, vd: `s_radio_spi_handle`, `s_rx_queue`. |
| `NAM-012` | SHOULD | Hạn chế tối đa biến toàn cục có thể thay đổi (mutable global state) xuyên suốt nhiều file; ưu tiên đóng gói trạng thái bên trong module và truy cập qua hàm API. |
| `NAM-013` | MUST | Kiểu `typedef struct` và `typedef enum` dùng chữ thường kết thúc bằng hậu tố `_t`, vd: `sx1276_config_t`, `protocol_frame_t`. |
| `NAM-014` | MUST | Các phần tử của `enum` viết hoa `UPPER_SNAKE_CASE` kèm prefix của module, vd: `SX1276_MODE_SLEEP`, `SX1276_MODE_STANDBY`. |
| `NAM-015` | MUST | Hằng số `#define` và macro viết hoa `UPPER_SNAKE_CASE` kèm prefix module, vd: `PROTOCOL_MAX_PAYLOAD_BYTES`. |
| `NAM-016` | SHOULD | Gắn hậu tố đơn vị vật lý vào tên biến/trường dữ liệu: `_hz`, `_khz`, `_ms`, `_us`, `_bytes`, `_dbm`, `_x100`. |
| `NAM-017` | MUST | Tên file nguồn và header dùng chữ thường `snake_case.c` và `snake_case.h`. |

```c
typedef enum {
    SX1276_MODE_SLEEP = 0,
    SX1276_MODE_STANDBY,
    SX1276_MODE_TX,
    SX1276_MODE_RX_CONTINUOUS
} sx1276_mode_t;

typedef struct {
    uint32_t frequency_hz;
    int8_t tx_power_dbm;
    uint8_t spreading_factor;
    uint8_t coding_rate;
} sx1276_config_t;
```

---

## 04. Source layout, headers, components & interfaces

| ID | Mức | Quy tắc |
|---|---|---|
| `API-001` | MUST | Mỗi component phải tách biệt rõ thư mục public header (`include/`) và các file thực thi/private header bên trong component. |
| `API-002` | MUST | Mọi file `.h` phải có `#pragma once`, chỉ `#include` những header thực sự cần thiết cho khai báo trong file đó, và bọc trong khối `extern "C"`. |
| `API-003` | MUST | Dùng dấu `<...>` cho thư viện chuẩn C (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>`) và dấu `"..."` cho header của ESP-IDF / dự án. |
| `API-004` | SHOULD | Thứ tự `#include`: (1) Header tương ứng của chính file `.c` đó → (2) Thư viện chuẩn C → (3) Header của ESP-IDF & FreeRTOS → (4) Public header của component khác → (5) Private header nội bộ. |
| `API-005` | MUST | Chữ ký hàm khai báo trong `.h` và định nghĩa trong `.c` phải khớp nhau 100%; không đưa nguyên mẫu hàm nội bộ (`static`) ra file public `.h`. |
| `API-006` | MUST | Trong `CMakeLists.txt`, hàm `idf_component_register()` phải phân định đúng `REQUIRES` (phụ thuộc xuất hiện trong public `.h`) và `PRIV_REQUIRES` (phụ thuộc chỉ dùng trong `.c`); cấm tạo vòng lặp phụ thuộc (circular dependencies). |
| `API-007` | MUST | Sử dụng tên các component ngoại vi phân rã của **ESP-IDF v6.1** (`esp_driver_spi`, `esp_driver_gpio`, `esp_driver_uart`, `esp_timer`, `freertos`) thay vì component `driver` tổng hợp đã lỗi thời. |
| `API-008` | MUST | Tầng ứng dụng (`main/`, logic Node/Gateway) không được đọc/ghi trực tiếp thanh ghi SX1276 hoặc gọi trực tiếp `spi_device_transmit()`; mọi thao tác phải đi qua API của `sx1276_driver`. |
| `API-009` | SHOULD | Tài liệu hóa rõ quyền sở hữu bộ đệm (buffer ownership), đơn vị timeout và tính an toàn đa luồng (thread-safety) trên mỗi hàm public API. |
| `API-010` | MUST | Không tự ý thêm phụ thuộc Arduino, C++ hoặc thư viện bên ngoài khi chưa được chủ dự án chấp thuận. |

---

## 05. Types, conversions, arithmetic & data representation

| ID | Mức | Quy tắc |
|---|---|---|
| `TYP-001` | MUST | **Bắt buộc dùng các kiểu nguyên cố định kích thước từ `<stdint.h>`** (`uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`) cho mọi thanh ghi phần cứng, trường gói tin truyền thông, dữ liệu cảm biến, bộ đếm và struct dữ liệu; dùng `<stdbool.h>` (`bool`) cho giá trị logic. |
| `TYP-002` | MUST | Dùng `size_t` cho kích thước bộ đệm, độ dài mảng và biến đếm dung lượng. **Chỉ cho phép dùng kiểu `int` (hoặc kiểu nguyên gốc của C) tại biên giao tiếp với các hàm API của ESP-IDF hoặc C Standard Library có chữ ký bắt buộc dùng `int`** (ví dụ kết quả trả về của `uart_read_bytes()`, `snprintf()`, `gpio_get_level()`), và phải kiểm tra miền giá trị trước khi chuyển sang kiểu `<stdint.h>` hoặc `size_t`. |
| `TYP-003` | MUST | Mọi phép ép kiểu thu hẹp độ rộng bit (narrowing cast, vd từ `int` hoặc `uint32_t` xuống `uint8_t`/`int16_t`) phải kiểm tra giới hạn giá trị trước khi ép kiểu (**CERT INT31-C**). |
| `TYP-004` | MUST | Ngăn chặn tràn số nguyên không dấu (unsigned wrap), tràn số có dấu (signed overflow), chia cho 0 và dịch bit vượt độ rộng kiểu dữ liệu (**CERT INT30-C, INT32-C, INT33-C, INT34-C**). |
| `TYP-005` | MUST | Không phụ thuộc vào thứ tự byte ngầm định của CPU khi đóng gói bản tin; thứ tự byte trên đường truyền (wire endianness) phải được lập trình tường minh. |
| `TYP-006` | MUST | Cấm truyền trực tiếp kiểu số thực (`float` / `double`) dưới dạng byte thô qua sóng LoRa. Phải quy đổi sang số nguyên dấu phẩy tĩnh (fixed-point scaling, vd: `int16_t temp_x100`) sau khi đã kiểm tra giới hạn miền giá trị. |
| `TYP-007` | MUST | Không dùng từ khóa `volatile` để thay thế cho Mutex, Queue hoặc cơ chế đồng bộ giữa các FreeRTOS Task. |
| `TYP-008` | SHOULD | Luôn thêm từ khóa `const` cho các tham số con trỏ chỉ đọc (input buffers, config structs) và các bảng hằng số tĩnh. |
| `TYP-009` | MUST | Khởi tạo giá trị xác định cho mọi biến cục bộ trước khi đọc (**CERT EXP33-C**). |

**Ví dụ kiểm tra tràn số nguyên khi xác thực vùng nhớ gói tin:**

```c
static esp_err_t protocol_validate_slice(size_t offset, size_t requested, size_t capacity)
{
    if ((offset > capacity) || (requested > (capacity - offset))) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
```

---

## 06. Memory management, pointers & buffer ownership

### 06.1. Chính sách quản lý bộ nhớ

| ID | Mức | Quy tắc |
|---|---|---|
| `MEM-001` | MUST | **Ưu tiên cấp phát tĩnh (static allocation)**, bộ đệm kích thước cố định (fixed-size buffers) và các hàng đợi được cấp phát một lần lúc khởi động. |
| `MEM-002` | MUST | **Cấm tuyệt đối** gọi `malloc()`, `calloc()`, `realloc()`, `free()` trong các vòng lặp vận hành (steady-state TX/RX loops), hàm xử lý gói tin định kỳ hoặc callback sự kiện. |
| `MEM-003` | MUST | Đối với các hàm khởi tạo của ESP-IDF (`spi_bus_initialize`, `uart_driver_install`, `xTaskCreate`, `xQueueCreate`), chỉ được gọi một lần trong giai đoạn khởi động hệ thống và phải kiểm tra mã lỗi trả về. |
| `MEM-004` | MUST | Nếu bắt buộc phải cấp phát động lúc khởi tạo, phải kiểm tra kích thước hợp lệ, kiểm tra con trỏ trả về khác `NULL` và xác định rõ vòng đời sở hữu (**CERT MEM35-C**). |
| `MEM-005` | MUST | Mọi hàm nhận con trỏ bộ đệm phải kèm theo tham số kích thước (`size_t capacity` hoặc `size_t length`) và kiểm tra `ptr != NULL` cùng `length <= capacity` trước khi truy cập. |
| `MEM-006` | MUST | Cấm sử dụng mảng có độ dài biến thiên trên stack (Variable Length Arrays - VLA, vd: `uint8_t buf[n];`) hoặc dùng `alloca()`. |
| `MEM-007` | MUST | Không trả về địa chỉ của biến cục bộ nằm trên stack; không giữ lại con trỏ sau khi vùng nhớ đã hết vòng đời sử dụng. |
| `MEM-008` | MUST | Khi truyền dữ liệu giữa các FreeRTOS Task qua Queue, ưu tiên truyền theo giá trị (copy by value một struct kích thước nhỏ/cố định) thay vì truyền con trỏ trỏ tới vùng nhớ dùng chung. |
| `MEM-009` | SHOULD | Ưu tiên sử dụng `xTaskCreateStatic()` và `xQueueCreateStatic()` ở các module cốt lõi khi cần kiểm soát chặt chẽ bộ nhớ SRAM. |
| `MEM-010` | MUST | Không khai báo mảng bộ đệm lớn (ví dụ `> 256 bytes`) trực tiếp trên stack của Task; hãy đặt ở vùng nhớ `static` của module hoặc trong cấu trúc ngữ cảnh được quản lý. |
| `MEM-011` | MUST | Phòng chống triệt để lỗi dereference con trỏ `NULL` và đọc/ghi vượt biên mảng (**CERT EXP34-C, ARR30-C**). |
| `MEM-012` | MUST | Trước khi gọi `memcpy()`, `memmove()`, hoặc `memset()`, phải xác minh độ dài sao chép không vượt quá dung lượng vùng đích (`dst_capacity`). |
| `MEM-013` | SHOULD | Đối với các giao dịch SPI ngắn (1–4 bytes như đọc/ghi thanh ghi SX1276), ưu tiên dùng `SPI_TRANS_USE_TXDATA` và `SPI_TRANS_USE_RXDATA` của ESP-IDF. Nếu dùng buffer ngoài với SPI DMA, bộ đệm phải nằm trong vùng nhớ `DMA_ATTR` / DRAM hợp lệ và căn chỉnh theo yêu cầu phần cứng. |

---

## 07. Error handling, diagnostics & failure recovery

| ID | Mức | Quy tắc |
|---|---|---|
| `ERR-001` | MUST | Mọi hàm có khả năng thất bại (giao tiếp phần cứng, kiểm tra tham số, đóng gói/giải mã bản tin) phải trả về kiểu `esp_err_t`; dữ liệu đầu ra được trả qua tham số con trỏ `out_*`. |
| `ERR-002` | MUST | Bắt buộc kiểm tra mã trả về của mọi lệnh gọi SPI, GPIO, UART và FreeRTOS API. Cấm bỏ qua lỗi bằng cách ép kiểu `(void)err;`. |
| `ERR-003` | MUST | Lỗi phát sinh trong lúc vận hành (runtime) như gói tin hỏng CRC, SPI timeout, cảm biến mất phản hồi phải được trả về cho tầng gọi xử lý phục hồi, không được làm sập (crash/abort) hệ thống. |
| `ERR-004` | MUST | **Cấm dùng `ESP_ERROR_CHECK()` hoặc `assert()` trong luồng chạy runtime thông thường.** Chỉ được phép dùng `ESP_ERROR_CHECK()` tại hàm khởi tạo ban đầu (`app_main`) đối với những lỗi phần cứng chí mạng không thể chạy tiếp nếu thất bại. |
| `ERR-005` | MUST | Mọi thao tác chờ I/O hoặc chờ cờ trạng thái phải có thời gian chờ tối đa (bounded timeout) và giới hạn số lần thử lại (max retries); cấm dùng `portMAX_DELAY` ở luồng thu/phát nếu không có cơ chế giám sát Watchdog tương ứng. |
| `ERR-006` | MUST | Khi phát hiện chip SX1276 bị treo trạng thái hoặc thanh ghi cấu hình bị lệch do nhiễu, driver phải thực hiện quy trình Reset phần cứng (`BOARD_LORA_RST`) và nạp lại cấu hình RF một cách tường minh. |
| `ERR-007` | SHOULD | Sử dụng đúng cấp độ log (`ESP_LOGE`, `ESP_LOGW`, `ESP_LOGI`, `ESP_LOGD`) và giới hạn tần suất in log khi lỗi lặp lại liên tục trong vòng lặp để không làm nghẽn cổng `UART_NUM_0`. |
| `ERR-008` | MUST | Không dùng chuỗi nhận từ mạng/UART làm chuỗi định dạng trực tiếp cho hàm log/printf (Format String Vulnerability); giới hạn độ dài khi in hex dump gói tin. |
| `ERR-009` | MUST | Kiểm tra kết quả trả về của các hàm thư viện chuẩn C khi sử dụng (**CERT ERR33-C**). |
| `ERR-010` | MUST | Không ghi log hoặc báo cáo "Gửi/Nhận thành công" trước khi phần cứng thực sự xác nhận hoàn tất (ví dụ phải đợi cờ `TxDone` và không có lỗi `PayloadCrcError`). |

---

## 08. FreeRTOS, ISR, concurrency & execution bounds

| ID | Mức | Quy tắc |
|---|---|---|
| `RTOS-001` | MUST | Mỗi thiết bị SPI (`spi_device_handle_t` của SX1276) phải có quyền sở hữu Task rõ ràng; cấm để nhiều Task gọi song song các hàm truyền nhận SPI trên cùng một handle mà không có cơ chế khóa đồng bộ. |
| `RTOS-002` | SHOULD | Ưu tiên kiến trúc **Single Radio Owner Task**: chỉ một Task duy nhất trực tiếp điều khiển trạng thái và SPI của SX1276; các Task khác giao tiếp với Radio Task thông qua FreeRTOS Queue. |
| `RTOS-003` | MUST | Hàm phục vụ ngắt (`DIO0` GPIO ISR) phải cực kỳ ngắn gọn: cấm gọi giao dịch SPI, cấm gọi `ESP_LOG*` thông thường, cấm gọi hàm blocking hoặc cấp phát bộ nhớ trong ISR. Nếu đăng ký ngắt với cờ `ESP_INTR_FLAG_IRAM`, hàm ISR bắt buộc phải có thuộc tính `IRAM_ATTR`. |
| `RTOS-004` | MUST | Trong hàm ngắt ISR, chỉ sử dụng các API FreeRTOS có hậu tố `...FromISR()` (như `vTaskNotifyGiveFromISR()` hoặc `xQueueSendFromISR()`) và phải kiểm tra biến `pxHigherPriorityTaskWoken` để gọi `portYIELD_FROM_ISR()` đúng chuẩn. |
| `RTOS-005` | MUST | Không dùng biến `volatile` đơn thuần để đồng bộ sự kiện giữa ISR và Task; hãy dùng **Task Notification** hoặc **Queue**. |
| `RTOS-006` | MUST | Nếu sử dụng Mutex (`SemaphoreHandle_t`), luôn lấy khóa với một thời gian timeout hữu hạn và giải phóng khóa trên mọi nhánh thoát của hàm (**CERT CON35-C**). |
| `RTOS-007` | MUST | Cấm dùng vòng lặp bận (busy-wait polling) chiếm dụng CPU trong thời gian dài; phải nhường CPU bằng `ulTaskNotifyTake()`, `xQueueReceive()` hoặc `vTaskDelay()`. |
| `RTOS-008` | SHOULD | Theo dõi dung lượng Stack trống tối thiểu bằng `uxTaskGetStackHighWaterMark()` và số lượng phần tử tồn đọng trong Queue khi kiểm thử tải. |
| `RTOS-009` | MUST | Đảm bảo Queue và Task Notification đã được khởi tạo xong trước khi bật ngắt GPIO `DIO0` để tránh ISR gọi vào handle `NULL`. |
| `RTOS-010` | SHOULD | Khi đẩy dữ liệu vào Queue (`xQueueSend`), phải có chính sách xử lý khi Queue đầy (timeout ngắn, tăng biến đếm `queue_drop_count` và ghi cảnh báo) thay vì treo vĩnh viễn. |

---

## 09. SX1276 radio driver standards

| ID | Mức | Quy tắc |
|---|---|---|
| `RAD-001` | MUST | Driver SX1276 phải bám sát **Semtech SX1276 Datasheet**; tuyệt đối không áp dụng bảng thanh ghi, chân `BUSY` hay tập lệnh Opcode của dòng chip SX126x. |
| `RAD-002` | MUST | Toàn bộ địa chỉ thanh ghi (`0x00`–`0x70`) và mặt nạ bit (bitmasks) phải được định nghĩa bằng hằng số có tên rõ ràng trong file private header `sx1276_regs.h`; cấm viết số Hex trần (magic numbers) rải rác trong file `sx1276.c`. |
| `RAD-003` | MUST | Kiểm tra đầy đủ kết quả khởi tạo SPI bus, cấu hình chân `CS`/`RST`/`DIO0` và kết quả của từng lần đọc/ghi thanh ghi. |
| `RAD-004` | MUST | Trong hàm `sx1276_init()`, bắt buộc đọc thanh ghi định danh `RegVersion (0x42)` và xác nhận giá trị trả về là **`0x12`** trước khi cấu hình RF. Nếu khác `0x12`, phải trả về lỗi `ESP_ERR_INVALID_RESPONSE` hoặc `ESP_ERR_NOT_FOUND`. |
| `RAD-005` | MUST | Tuân thủ nghiêm ngặt quy trình bật chế độ LoRa của SX1276: phải đưa chip về **`SLEEP` mode (`RegOpMode` bits `[2:0] = 000`)** trước khi bật bit 7 (`LongRangeMode = 1`), sau đó mới chuyển sang `STANDBY`. |
| `RAD-006` | MUST | Chân ngắt `DIO0` chỉ dùng để đánh thức Radio Task; việc đọc thanh ghi `RegIrqFlags (0x12)` và xóa cờ ngắt (ghi bit `1` vào cờ tương ứng — write-1-to-clear) phải được thực hiện trong ngữ cảnh của Task. |
| `RAD-007` | MUST | Khi nhận gói tin (`RxDone`), bắt buộc kiểm tra cờ `PayloadCrcError` trong `RegIrqFlags (0x12)`. Nếu cờ lỗi CRC bật, phải hủy bỏ gói tin, xóa cờ ngắt và tăng bộ đếm `rx_crc_error`. |
| `RAD-008` | MUST | Trước khi đọc/ghi bộ đệm `RegFifo (0x00)`, phải thiết lập chính xác con trỏ `RegFifoAddrPtr (0x0D)` về `RegFifoTxBaseAddr (0x0E)` (khi phát) hoặc `RegFifoRxCurrentAddr (0x10)` (khi thu), và kiểm tra số byte không vượt quá `255` bytes. |
| `RAD-009` | MUST | Các thông số sóng P2P (`Frequency`, `Bandwidth`, `Spreading Factor`, `Coding Rate`, `Sync Word`, `Preamble`, `Header Mode`, `CRC`) giữa Node và Gateway phải khớp nhau 100%. |
| `RAD-010` | MUST | Đường khuếch đại công suất phát (`PA_BOOST` vs `RFO` tại `RegPaConfig 0x09`) phải được cấu hình tường minh qua `sx1276_config_t`. Mặc định cho module 915 MHz thương mại là `PA_BOOST` ở mức công suất thử nghiệm an toàn trên bàn (`+2 dBm` đến `+7 dBm`). |
| `RAD-011` | MUST | Tuyệt đối không kích hoạt phát sóng (`TX mode`) khi chưa gắn ăng-ten đúng băng tần 915 MHz. |
| `RAD-012` | SHOULD | Quản lý rõ ràng máy trạng thái (State Machine) của driver (`UNINIT`, `SLEEP`, `STANDBY`, `TX`, `RX_CONTINUOUS`, `FAULT`) và đảm bảo chip luôn về `STANDBY` trước khi nạp gói tin mới vào FIFO. |

### 09.1. Cấu hình sóng P2P mặc định (Baseline 915 MHz Demo Profile)

| Thông số | Giá trị chuẩn | Ghi chú thanh ghi SX1276 |
|---|---|---|
| Carrier Frequency | `915000000` Hz (915.0 MHz) | `RegFrfMsb=0xE4`, `RegFrfMid=0xC0`, `RegFrfLsb=0x00` |
| Bandwidth (BW) | `125 kHz` | `RegModemConfig1 (0x1D)` |
| Spreading Factor (SF) | `SF7` | `RegModemConfig2 (0x1E)` |
| Coding Rate (CR) | `4/5` | `RegModemConfig1 (0x1D)` |
| Sync Word | `0x12` | `RegSyncWord (0x39)` — Mạng P2P riêng |
| Preamble Length | `8 symbols` | `RegPreambleMsb/Lsb (0x20–0x21)` |
| Header Mode | `Explicit Header` | `RegModemConfig1` bit 0 = `0` |
| Payload CRC | `Enabled` | `RegModemConfig2` bit 2 (`RxPayloadCrcOn = 1`) |
| Output Power (Bench Test) | `+5 dBm` qua `PA_BOOST` | An toàn khi đặt 2 mạch gần nhau 1–2m trên bàn |

---

## 10. LoRa P2P protocol & explicit serialization

| ID | Mức | Quy tắc |
|---|---|---|
| `PRO-001` | MUST | Mọi mảng byte nhận được từ sóng vô tuyến LoRa hoặc từ cổng UART đều được coi là **dữ liệu không đáng tin cậy (untrusted input)**, kể cả khi đã vượt qua kiểm tra CRC phần cứng của SX1276. |
| `PRO-002` | MUST | Cấu trúc khung truyền P2P phải có trường định danh (`magic`/`header`), phiên bản giao thức (`version`), loại bản tin (`msg_type`), định danh thiết bị (`node_id`), số thứ tự gói (`seq_num`), độ dài tải (`payload_len`) và mã kiểm lỗi `crc16`. |
| `PRO-003` | MUST | **Bắt buộc dùng kỹ thuật đóng gói/giải mã từng byte tường minh (Explicit Bytewise Serialization & Deserialization)**. Cấm dùng `memcpy()` để chép thẳng một `struct` C ra mảng byte truyền tin, và cấm ép kiểu con trỏ mảng `uint8_t*` thành con trỏ `struct*` (kể cả khi có `__attribute__((packed))`). |
| `PRO-004` | MUST | Mọi trường đa byte (16-bit, 32-bit) trong khung truyền P2P phải được đóng gói theo chuẩn **Little-Endian** thống nhất giữa Node và Gateway. |
| `PRO-005` | MUST | Trước khi giải mã bất kỳ trường nào trong mảng byte nhận được, hàm giải mã phải kiểm tra tổng chiều dài gói tin (`rx_len >= PROTOCOL_MIN_FRAME_SIZE`) và đối chiếu `payload_len` trong header khớp với số byte thực tế nhận được. |
| `PRO-006` | MUST | Kiểm tra tính hợp lệ của `magic`, `version`, `msg_type` và xác minh `crc16` tầng giao thức trước khi chuyển dữ liệu cho tầng ứng dụng xử lý. |
| `PRO-007` | MUST | Đảm bảo tổng kích thước khung truyền sau khi đóng gói không vượt quá dung lượng bộ đệm đích và giới hạn tối đa `255 bytes` của FIFO SX1276 (khuyến nghị giữ dưới `51 bytes` cho LoRa). |
| `PRO-008` | SHOULD | Sử dụng `seq_num` tăng dần trên mỗi gói tin để phía thu phát hiện mất gói (packet loss) hoặc gói tin trùng lặp (duplicate packet). |
| `PRO-009` | MUST | Cả Node và Gateway phải dùng chung duy nhất một component `app_protocol` để đảm bảo hàm `protocol_encode_*` và `protocol_decode_*` luôn đối xứng tuyệt đối. |
| `PRO-010` | MUST | Không coi mã kiểm lỗi CRC là cơ chế bảo mật hay xác thực mật mã; CRC chỉ dùng để phát hiện lỗi đảo bit trên đường truyền. |
| `PRO-011` | MUST | Khi quy đổi giá trị cảm biến sang số nguyên (ví dụ `temp_x100`), phải kiểm tra giới hạn trên/dưới để không xảy ra tràn số `int16_t` / `uint16_t`. |

**Mẫu chuẩn viết hàm Explicit Serialization / Deserialization (Little-Endian):**

```c
static inline void protocol_write_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static inline uint16_t protocol_read_u16_le(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8U));
}
```

---

## 11. Hardware pin lock & peripheral ownership

### 11.1. Bảng khóa chân GPIO cố định cho PCB hiện tại

| Tín hiệu SX1276 | Chân ESP32 GPIO | Hằng số bắt buộc trong `board_pins.h` |
|---|---:|---|
| `LoRa_DIO0` | **GPIO26** | `BOARD_LORA_DIO0` |
| `LoRa_RST` | **GPIO23** | `BOARD_LORA_RST` |
| `LoRa_SS` / `CS` | **GPIO18** | `BOARD_LORA_CS` |
| `LoRa_SCK` | **GPIO5** | `BOARD_LORA_SCK` |
| `LoRa_MOSI` | **GPIO27** | `BOARD_LORA_MOSI` |
| `LoRa_MISO` | **GPIO19** | `BOARD_LORA_MISO` |

| ID | Mức | Quy tắc |
|---|---|---|
| `HW-001` | MUST | Cấm tự ý đổi hoặc tráo chân GPIO của PCB đã xác nhận; đặc biệt **tuyệt đối không tráo `SCK=GPIO5` với `CS=GPIO18`**. |
| `HW-002` | MUST | Mọi định nghĩa chân GPIO phải đặt tập trung tại `components/board_config/include/board_pins.h`; cấm viết số chân GPIO trực tiếp trong file `.c` của driver hay ứng dụng. |
| `HW-003` | MUST | Cấu hình `spi_bus_config_t` và `spi_device_interface_config_t` bằng các hằng số `BOARD_LORA_*`; không lấy cấu hình chân VSPI mặc định của ESP32 làm thay thế. |
| `HW-004` | MUST | Trong giai đoạn Demo với 2 mạch cùng PCB revision, cả 2 mạch dùng chung `board_pins.h`. Nếu triển khai trên một mạch Gateway có PCB khác, phải dừng lại và báo `HARDWARE_UNCONFIRMED` cho đến khi chủ dự án cung cấp sơ đồ chân. |
| `HW-005` | MUST | Lưu ý `GPIO5` là chân Strapping của ESP32; không gắn mạch kéo xuống mức thấp cố định làm ảnh hưởng quá trình boot của chip. |
| `HW-006` | MUST | Dành riêng `UART_NUM_0` cho việc nạp firmware và in log (`ESP_LOG`); mọi cảm biến UART ngoại vi bắt buộc dùng `UART_NUM_1` hoặc `UART_NUM_2` với chân GPIO được khai báo trong `board_pins.h`. |
| `HW-007` | MUST | Không tái sử dụng bất kỳ chân nào trong 6 chân `BOARD_LORA_*` cho ngoại vi khác (LED, nút nhấn, UART, I2C). |
| `HW-008` | MUST | Không tự ý thay đổi bảng phân vùng Flash (partition table), cấu hình eFuse hoặc thông số tần số RF khi chưa có yêu cầu trực tiếp. |
| `HW-009` | SHOULD | Ghi nhận đầy đủ kết quả đo kiểm thực tế (Version chip `0x12`, RSSI/SNR, dạng sóng SPI) vào báo cáo nghiệm thu từng Phase. |

---

## 12. Documentation & public contracts

| ID | Mức | Quy tắc |
|---|---|---|
| `DOC-001` | MUST | Mọi hàm public API trong file `.h` phải có khối chú thích Doxygen ngắn gọn nêu rõ: mục đích, tham số đầu vào/đầu ra, điều kiện con trỏ khác `NULL`, mã lỗi `esp_err_t` trả về và yêu cầu ngữ cảnh gọi hàm. |
| `DOC-002` | MUST | Mô tả trong chú thích header phải khớp 100% với hành vi kiểm tra lỗi thực tế bên trong hàm `.c`. |
| `DOC-003` | MUST | Mọi hằng số thời gian trễ phần cứng (ví dụ: thời gian giữ chân `RST` mức thấp, thời gian chờ ổn định thạch anh) hoặc thao tác bit đặc biệt trên thanh ghi SX1276 phải có comment giải thích rõ lý do theo Datasheet. |
| `DOC-004` | SHOULD | Chỉ mô tả hợp đồng giao tiếp (API contract) tại file `.h`; tránh sao chép cùng một đoạn văn bản Doxygen vào cả file `.h` lẫn file `.c` gây lệch thông tin khi cập nhật. |
| `DOC-005` | MUST | Cấm ghi chú thích hoặc báo cáo là `HARDWARE_VERIFIED` nếu chỉ mới viết code hoặc biên dịch thành công mà chưa chạy thử trên 2 mạch thật. |
| `DOC-006` | SHOULD | Cập nhật lịch sử thay đổi khi có sự điều chỉnh được phê duyệt đối với cấu trúc gói tin P2P hoặc giao diện driver. |

---

## 13. Tooling, review gates & evidence

### 13.1. Quy tắc kiểm tra chất lượng (Quality Gates)

| ID | Mức | Quy tắc |
|---|---|---|
| `QA-001` | MUST | Trước khi hoàn tất bất kỳ tác vụ viết/sửa code nào, AI agent phải tự rà soát toàn bộ tên hàm mới theo đúng quy tắc `NAM-001` và Bảng ánh xạ Prefix tại §03.1. |
| `QA-002` | SHOULD | Định dạng mã nguồn tuân thủ nghiêm ngặt mục §02 (ESP-IDF v6.1 Style Guide). |
| `QA-003` | MUST | Mã nguồn phải biên dịch sạch sẽ trên ESP-IDF v6.1 mà không phát sinh cảnh báo (compiler warnings) về sai kiểu dữ liệu, biến không sử dụng hay thiếu kiểm tra giá trị trả về. |
| `QA-004` | MUST | Kiểm tra file `board_pins.h` để đảm bảo 6 chân GPIO của SX1276 giữ nguyên giá trị đã khóa tại §11.1. |
| `QA-005` | MUST | Báo cáo trung thực các bước kiểm tra đã thực sự chạy; tuyệt đối không suy diễn kết quả. |
| `QA-006` | SHOULD | Viết kịch bản kiểm thử (hoặc self-test lúc khởi động) cho hàm đóng gói/giải mã bản tin: kiểm tra gói tin rỗng, gói tin cụt (truncated), sai CRC16 và tràn kích thước. |
| `QA-007` | MUST | Sử dụng cụm từ "Tuân thủ các quy tắc SEI CERT C được chọn lọc trong tài liệu này" thay vì tuyên bố "Full CERT C compliance". |
| `QA-008` | MUST | Phân biệt rạch ròi giữa kiểm증 biên dịch phần mềm và kiểm chứng thu phát sóng phần cứng. |

### 13.2. Bốn trạng thái nghiệm thu chuẩn (Verification States)

1. **`CODE_WRITTEN`**: Mã nguồn đã được viết và rà soát tĩnh theo `C_CODING_STANDARDS.md` và `AGENTS.md`, nhưng chưa chạy lệnh `idf.py build`.
2. **`BUILD_VERIFIED`**: Đã chạy thực tế lệnh `idf.py build` thành công trên toolchain ESP-IDF v6.1 mà không có lỗi biên dịch hay liên kết.
3. **`HARDWARE_VERIFIED`**: Đã nạp lên mạch ESP32 + SX1276 thực tế và có bằng chứng log UART xác nhận (ví dụ: đọc `RegVersion = 0x12`, truyền nhận gói tin Ping-Pong kèm chỉ số RSSI/SNR).
4. **`NOT_VERIFIED`**: Chưa được kiểm tra hoặc đang bị chặn bởi thông tin phần cứng còn thiếu.

### 13.3. Checklist nghiệm thu tối thiểu trước khi bàn giao code

- [ ] Cấu trúc thư mục và phân tách component đúng chuẩn §04; `CMakeLists.txt` dùng tên driver phân rã của ESP-IDF v6.1 (`esp_driver_spi`, `esp_driver_gpio`, ...).
- [ ] Giữ nguyên tuyệt đối 6 chân GPIO đã khóa trong `board_pins.h` (`SCK=5`, `CS=18`, `MOSI=27`, `MISO=19`, `RST=23`, `DIO0=26`).
- [ ] 100% tên hàm mới tuân thủ `<module>_<verb>_<object>()` với đúng prefix trong bảng §03.1 (`board_`, `sx1276_`, `protocol_`, `node_`, `gateway_`, `app_`).
- [ ] Mọi thanh ghi, trường bản tin, dữ liệu cảm biến dùng `<stdint.h>`; chỉ dùng `int` tại biên hàm ESP-IDF/C stdlib có kiểm tra miền giá trị.
- [ ] Không có lệnh `malloc`/`calloc`/`free` trong vòng lặp Task hoặc hàm xử lý gói tin.
- [ ] Đóng gói và giải mã bản tin P2P dùng **Explicit Bytewise Serialization (Little-Endian)** có kiểm tra biên `capacity` trước khi đọc/ghi.
- [ ] Hàm ngắt `DIO0` ISR không gọi SPI hay `ESP_LOG*`; đồng bộ với Radio Task qua Task Notification hoặc Queue.
- [ ] Không dùng `ESP_ERROR_CHECK()` trong luồng xử lý runtime có thể phục hồi.

---

## 14. Resolved architecture decisions & exception procedure

### 14.1. Bảng quyết định kỹ thuật đã chốt (Resolved Decisions Log — v1.0.0)

Toàn bộ 8 vấn đề mở từ bản dự thảo v0.1.0 đã được chủ dự án xem xét và chốt chính thức như sau:

| ID | Vấn đề | Quyết định chính thức đã chốt (Active in v1.0.0) | Quy tắc áp dụng |
|---|---|---|---|
| `DEC-01` | Function namespace | Áp dụng **Bảng ánh xạ Prefix cố định** (`board_`, `sx1276_`, `protocol_`, `sensor_`, `node_`, `gateway_`, `app_`) thay vì bắt trùng tên thư mục dài dòng. | `NAM-001`, `NAM-003` |
| `DEC-02` | Native integer types | Bắt buộc `<stdint.h>` cho thanh ghi, gói tin, cảm biến và struct nội bộ; **cho phép `int` tại biên API ESP-IDF / C stdlib** kèm kiểm tra miền giá trị ngay sau khi gọi. | `TYP-001`, `TYP-002`, `TYP-003` |
| `DEC-03` | Wire serialization | Bắt buộc **Explicit Bytewise Serialization/Deserialization (Little-Endian)**; không dùng `memcpy` hay ép kiểu con trỏ `packed struct` trực tiếp lên mảng byte truyền thông. | `PRO-003`, `PRO-004` |
| `DEC-04` | PA routing (`PA_BOOST`) | Cấu hình đường công suất tường minh trong `sx1276_config_t`; mặc định chọn `PA_BOOST` ở mức công suất thấp an toàn (`+2` đến `+7 dBm`) khi thử nghiệm gần trên bàn. | `RAD-010` |
| `DEC-05` | Code formatting | Tuân thủ chuẩn **ESP-IDF v6.1 Style Guide** (4 spaces, dấu `{` của hàm xuống dòng riêng, giới hạn 120 ký tự). | `FMT-001`..`FMT-010` |
| `DEC-06` | Gateway PCB pinout | Dùng chung `board_pins.h` khi thử nghiệm trên 2 mạch cùng PCB revision; nếu mạch thứ 2 khác phần cứng, bắt buộc báo `HARDWARE_UNCONFIRMED`. | `HW-004` |
| `DEC-07` | Baseline P2P Frame | Áp dụng khung truyền P2P cơ sở có Header định danh (`magic`, `version`, `msg_type`, `node_id`, `seq_num`, `payload_len`) và `crc16` Little-Endian trong `app_protocol`. | `PRO-002`, `PRO-006` |
| `DEC-08` | Role selection | Sử dụng một khối `choice` duy nhất trong `main/Kconfig.projbuild` (`NODE_TX`, `GATEWAY_RX`, `PING_PONG`) để chọn vai trò lúc build. | `API-006` |

### 14.2. Quy trình xin ngoại lệ (Exception Procedure)

Nếu trong quá trình phát triển phát sinh trường hợp đặc biệt không thể tuân thủ một quy tắc `MUST`:
1. Nêu rõ **Mã quy tắc (Rule ID)**, tên file/hàm liên quan và nguyên nhân kỹ thuật.
2. Trình bày phương án thay thế cùng biện pháp kiểm soát rủi ro bộ nhớ/phần cứng.
3. Chờ sự phê duyệt tường minh từ chủ dự án trước khi áp dụng và ghi chú mã ngoại lệ ngay trong comment phía trên đoạn code đó.

---

## References (Normative Sources)

1. **Espressif**, *ESP-IDF v6.1 Style Guide* — https://docs.espressif.com/projects/esp-idf/en/stable/esp32/contribute/style-guide.html
2. **Espressif**, *ESP-IDF v6.1 SPI Master Driver* — https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/spi_master.html
3. **Espressif**, *ESP-IDF FreeRTOS Reference* — https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html
4. **SEI / CERT**, *SEI CERT C Coding Standard* (Selected rules: `ARR30-C`, `EXP33-C`, `EXP34-C`, `INT30-C`, `INT31-C`, `INT32-C`, `INT33-C`, `INT34-C`, `MEM35-C`, `ERR33-C`, `CON35-C`) — https://cmu-sei.github.io/secure-coding-standards/sei-cert-c-coding-standard/rules/
5. **Semtech**, *SX1276/77/78/79 137 MHz to 1020 MHz Low Power Long Range Transceiver Datasheet*.
6. **Project**, `AGENTS.md` (Synchronized v1.0 baseline).

**Document History**

| Version | Date | Notes |
|---|---|---|
| `0.1.0` | `2026-10-17` | Initial draft with open architectural questions (`OPEN-01`..`OPEN-08`). |
| `1.0.0` | `2026-10-17` | **APPROVED** release. Resolved all 8 open items (`DEC-01`..`DEC-08`), added ESP32 IRAM ISR and SPI transaction safety rules, and synchronized 100% with `AGENTS.md`. |