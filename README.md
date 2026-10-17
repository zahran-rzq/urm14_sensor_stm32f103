# URM14 Modbus-RTU Library

Driver sederhana untuk sensor jarak ultrasonik DFRobot URM14 (SEN0358) melalui RS485 dan protokol Modbus-RTU menggunakan STM32 HAL.

## Fitur

- Komunikasi Modbus-RTU melalui UART STM32 HAL.
- Dukungan transceiver RS485 half-duplex dengan pin DE/RE.
- Pembacaan jarak dalam milimeter.
- Pembacaan suhu sensor dan level noise.
- Trigger pengukuran mode passive.
- Akses register mentah dengan function code `0x03` dan `0x06`.
- Pemeriksaan CRC-16/MODBUS dan validasi frame.
- Penggantian alamat slave sensor.

## Struktur library

File utama library:

- `Core/Inc/urm14.h`: konfigurasi, alamat register, tipe data, status error, dan deklarasi API.
- `Core/Src/urm14.c`: implementasi Modbus-RTU, CRC, kontrol DE/RE, dan fungsi pembacaan/penulisan.

Library tidak membuat atau menginisialisasi UART sendiri. UART harus sudah dikonfigurasi oleh STM32CubeMX/HAL sebelum `URM14_Init()` dipanggil.

## Konsep kerja

URM14 berperan sebagai Modbus slave. STM32 menjadi Modbus master dan selalu memulai transaksi.

Alur setiap transaksi:

1. Pin DE/RE dibuat `HIGH` agar transceiver RS485 masuk mode kirim.
2. STM32 mengirim frame Modbus melalui `HAL_UART_Transmit()`.
3. Setelah transmisi selesai, pin DE/RE dibuat `LOW` agar transceiver masuk mode terima.
4. STM32 menerima frame melalui `HAL_UART_Receive()`.
5. Driver memeriksa alamat slave, function code, isi data, dan CRC.
6. Fungsi API mengembalikan `URM14_Status`.

### Read register

`URM14_ReadRegister()` memakai Modbus function code `0x03` untuk membaca satu holding register.

Request:

```text
address 03 register_hi register_lo 00 01 crc_lo crc_hi
```

Response yang diharapkan:

```text
address 03 02 data_hi data_lo crc_lo crc_hi
```

### Write register

`URM14_WriteRegister()` memakai function code `0x06` untuk menulis satu register.

Request:

```text
address 06 register_hi register_lo value_hi value_lo crc_lo crc_hi
```

URM14 membalas dengan echo 8 byte yang sama. Driver membandingkan response dengan request untuk memastikan penulisan berhasil.

## Wiring

Gunakan transceiver RS485 yang sesuai dengan level tegangan STM32.

| STM32 / catu daya | RS485 / URM14 |
|---|---|
| USART TX, contoh `PA9` | DI transceiver |
| USART RX, contoh `PA10` | RO transceiver |
| GPIO output DE/RE | DE dan RE transceiver |
| GND STM32 | GND transceiver dan GND sensor |
| A transceiver | A sensor |
| B transceiver | B sensor |
| Catu sensor 7-15 V | VCC/orange URM14 |
| GND catu sensor | GND/black URM14 |

DE dan RE dapat disambungkan bersama jika transceiver hanya dikendalikan oleh satu GPIO:

- `HIGH`: transmit
- `LOW`: receive

Periksa kembali label A/B pada transceiver dan sensor. Jika tidak ada response, coba tukar A dan B setelah memastikan catu daya dan ground benar.

## Konfigurasi UART

Konfigurasi proyek saat ini:

```text
Baud rate : 19200
Data      : 8 bit
Parity    : None
Stop bits : 1
Mode      : TX/RX
Flow ctrl : None
```

Di CubeMX, aktifkan UART yang terhubung ke transceiver RS485, lalu pastikan GPIO untuk DE/RE adalah output push-pull dan kondisi awalnya `LOW`.

## Integrasi ke proyek STM32

Pastikan `urm14.h` masuk ke include path dan `urm14.c` ikut dikompilasi oleh project.

Contoh inisialisasi:

```c
#include "urm14.h"

extern UART_HandleTypeDef huart1;

URM14_HandleTypeDef urm;

void App_Init(void)
{
    URM14_Status status;

    status = URM14_Init(&urm,
                        &huart1,
                        URM14_DE_GPIO_Port,
                        URM14_DE_Pin,
                        URM14_DEFAULT_ADDR);

    if (status != URM14_OK) {
        /* Tangani parameter atau konfigurasi yang tidak valid. */
    }

    status = URM14_Ping(&urm);
    if (status != URM14_OK) {
        /* Sensor tidak merespons atau PID tidak sesuai. */
    }
}
```

Panggil inisialisasi setelah `HAL_Init()`, konfigurasi clock, GPIO, dan UART selesai.

## Contoh pembacaan jarak

`URM14_ReadDistanceMM()` melakukan tiga langkah:

1. Menulis bit passive dan trigger ke register control.
2. Menunggu waktu pengukuran.
3. Membaca register distance dan mengubah satuan 0.1 mm menjadi mm.

```c
float distance_mm;
URM14_Status status;

status = URM14_ReadDistanceMM(&urm, &distance_mm);
if (status == URM14_OK) {
    /* distance_mm berisi jarak dalam mm. */
} else {
    /* Tangani error komunikasi atau out of range. */
}
```

Contoh pembacaan suhu dan noise:

```c
float temperature_c;
uint16_t noise;

URM14_ReadTemperatureC(&urm, &temperature_c);
URM14_ReadNoise(&urm, &noise);
```

## API

| Fungsi | Kegunaan |
|---|---|
| `URM14_Init()` | Mengisi handle dan menetapkan UART, pin DE/RE, alamat, dan timeout |
| `URM14_Ping()` | Membaca PID dan memastikan nilainya `0x0002` |
| `URM14_ReadDistanceMM()` | Trigger pengukuran passive dan membaca jarak dalam mm |
| `URM14_ReadTemperatureC()` | Membaca suhu onboard dalam derajat Celsius |
| `URM14_ReadNoise()` | Membaca level noise `0..10` |
| `URM14_SetExternalTempC()` | Menulis suhu eksternal untuk kompensasi |
| `URM14_ReadRegister()` | Membaca satu register mentah |
| `URM14_WriteRegister()` | Menulis satu register mentah |
| `URM14_SetAddress()` | Mengubah alamat slave sensor |

## Register penting

| Konstanta | Alamat | Akses | Keterangan |
|---|---:|---|---|
| `URM14_REG_PID` | `0x00` | R | PID, biasanya `0x0002` |
| `URM14_REG_VID` | `0x01` | R | Version ID |
| `URM14_REG_ADDR` | `0x02` | R/W | Alamat slave |
| `URM14_REG_DISTANCE` | `0x05` | R | Jarak dalam 0.1 mm |
| `URM14_REG_TEMP` | `0x06` | R | Suhu dalam 0.1 C |
| `URM14_REG_EXT_TEMP` | `0x07` | R/W | Suhu eksternal dalam 0.1 C |
| `URM14_REG_CONTROL` | `0x08` | R/W | Mode dan trigger |
| `URM14_REG_NOISE` | `0x09` | R | Level noise |

## Status dan troubleshooting

- `URM14_OK`: transaksi berhasil.
- `URM14_ERR_PARAM`: handle, pointer, alamat, atau parameter tidak valid.
- `URM14_ERR_TIMEOUT`: sensor tidak membalas dalam batas waktu.
- `URM14_ERR_CRC`: CRC response salah.
- `URM14_ERR_FRAME`: alamat, function code, panjang, atau isi frame tidak sesuai.
- `URM14_ERR_OUT_OF_RANGE`: hasil jarak `0xFFFF`, berarti tidak ada echo atau di luar range.

Jika selalu mendapat timeout, periksa secara berurutan:

1. Sensor dan transceiver mendapat catu daya yang benar.
2. Ground STM32, transceiver, dan sensor tersambung bersama.
3. UART menggunakan `19200 8N1` dan UART yang benar.
4. DE/RE LOW saat idle dan berubah HIGH saat transmit.
5. Kabel A/B tidak tertukar.
6. Alamat sensor sesuai, default-nya `URM14_DEFAULT_ADDR` (`0x0C`).
7. Pin TX/RX terhubung ke DI/RO transceiver, bukan langsung ke A/B.

## Debug dengan Live Expressions

Library menyediakan variabel `volatile` untuk dipantau saat debugging. Variabel ini berada di `Core/Src/urm14.c` dan tidak perlu ditambahkan ke `main.c`.

### Membuka Live Expressions

1. Build project dengan konfigurasi Debug.
2. Jalankan debugging menggunakan ST-LINK.
3. Saat program berhenti pada breakpoint, buka `Window > Show View > Live Expressions` di STM32CubeIDE.
4. Klik `Add new expression` dan tambahkan nama variabel di bawah.
5. Lanjutkan program dengan `Resume`, lalu hentikan kembali setelah transaksi selesai. Nilai akan diperbarui ketika debugger melakukan refresh.

Jangan menaruh breakpoint di tengah fungsi komunikasi jika ingin melihat transaksi selesai. Breakpoint pada baris setelah pemanggilan API di `main.c` lebih mudah digunakan.

### Variabel utama

| Live Expression | Keterangan |
|---|---|
| `urm14_dbg_tx_count` | Jumlah request yang sudah dikirim |
| `urm14_dbg_tx_hs` | Hasil `HAL_UART_Transmit()`: `0=HAL_OK`, `2=HAL_BUSY`, `3=HAL_TIMEOUT` |
| `urm14_dbg_rx_hs` | Hasil `HAL_UART_Receive()`: `0=HAL_OK`, `1=HAL_ERROR`, `3=HAL_TIMEOUT` |
| `urm14_dbg_rx_remaining` | Jumlah byte response yang belum diterima saat timeout/error |
| `urm14_dbg_tx[0]` sampai `urm14_dbg_tx[7]` | Frame Modbus terakhir yang dikirim |
| `urm14_dbg_rx[0]` sampai `urm14_dbg_rx[7]` | Byte response terakhir yang diterima |
| `urm_ping_status` | Status ping terakhir dari `main.c` |
| `urm_read_status` | Status pembacaan jarak terakhir dari `main.c` |
| `urm_distance_mm` | Jarak terakhir yang berhasil dibaca |
| `urm_temp_c` | Suhu terakhir yang berhasil dibaca |
| `urm_noise` | Level noise terakhir |
| `urm_ok_count` | Jumlah pembacaan jarak yang berhasil |
| `urm_err_count` | Jumlah pembacaan jarak yang gagal |

### Ekspresi yang disarankan

Tambahkan ekspresi berikut agar seluruh frame terlihat sebagai nilai heksadesimal:

```text
urm14_dbg_tx[0]
urm14_dbg_tx[1]
urm14_dbg_tx[2]
urm14_dbg_tx[3]
urm14_dbg_tx[4]
urm14_dbg_tx[5]
urm14_dbg_tx[6]
urm14_dbg_tx[7]
urm14_dbg_rx[0]
urm14_dbg_rx[1]
urm14_dbg_rx[2]
urm14_dbg_rx[3]
urm14_dbg_rx[4]
urm14_dbg_rx[5]
urm14_dbg_rx[6]
urm14_dbg_rx[7]
urm14_dbg_tx_hs
urm14_dbg_rx_hs
urm14_dbg_rx_remaining
urm14_dbg_tx_count
```

Atur format tampilan ke `Hexadecimal` untuk byte frame. Isi buffer RX dapat berisi data transaksi sebelumnya pada posisi yang belum ditimpa, sehingga gunakan `urm14_dbg_rx_remaining` dan status HAL sebagai acuan utama.

### Membaca frame saat debug

Untuk request baca register, pola TX yang benar adalah:

```text
[alamat] 03 [register_hi] [register_lo] 00 01 [crc_lo] [crc_hi]
```

Response baca 7 byte yang valid memiliki pola:

```text
[alamat] 03 02 [data_hi] [data_lo] [crc_lo] [crc_hi]
```

Untuk request tulis register, byte TX ke-2 adalah `06`, dan response seharusnya sama persis dengan seluruh 8 byte TX.

### Diagnosis cepat

- `urm14_dbg_tx_count` bertambah, tetapi `urm14_dbg_tx_hs` bukan `0`: masalah pada UART atau HAL sedang sibuk.
- `urm14_dbg_rx_hs == 3` dan `urm14_dbg_rx_remaining` masih besar: tidak ada response lengkap, periksa DE/RE, A/B, alamat, baud rate, dan catu daya.
- `urm14_dbg_rx_hs == 0`, tetapi API mengembalikan `URM14_ERR_CRC`: response diterima, namun CRC tidak sesuai.
- `urm14_dbg_rx_hs == 0`, tetapi API mengembalikan `URM14_ERR_FRAME`: alamat, function code, panjang, atau isi response tidak sesuai.
- `urm14_dbg_tx[1] == 0x03`: transaksi baca register.
- `urm14_dbg_tx[1] == 0x06`: transaksi tulis register.

## Mengubah alamat sensor

```c
URM14_SetAddress(&urm, 0x0D);
```

Perubahan alamat disimpan oleh sensor, tetapi aktif setelah sensor di-power-cycle. Setelah restart, panggil `URM14_Init()` kembali dengan alamat baru:

```c
URM14_Init(&urm,
           &huart1,
           URM14_DE_GPIO_Port,
           URM14_DE_Pin,
           0x0D);
```

Jangan menjalankan beberapa sensor dengan alamat Modbus yang sama pada satu bus.

## Catatan penggunaan

- API bersifat blocking karena memakai `HAL_UART_Transmit()` dan `HAL_UART_Receive()`.
- Jangan menjalankan transaksi bersamaan dari interrupt atau task lain tanpa mekanisme sinkronisasi.
- `URM14_MEASURE_DELAY_MS` saat ini bernilai 40 ms untuk memberi waktu sensor menyelesaikan pengukuran.
- Satu handle mewakili satu sensor/alamat. Untuk beberapa sensor, gunakan handle terpisah dan pastikan hanya satu master yang mengakses bus pada satu waktu.
