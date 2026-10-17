/**
 * @file    urm14.h
 * @brief   Library driver DFRobot URM14 (SEN0358) — sensor ultrasonik 200KHz, RS485 Modbus-RTU
 *          Target : STM32F103C8T6 (Blue Pill), STM32 HAL
 *
 * Wiring (Blue Pill <-> modul RS485 (MAX485/3.3V) <-> URM14):
 *   PA9  (USART1_TX)  -> DI  modul RS485
 *   PA10 (USART1_RX)  -> RO  modul RS485
 *   PA8  (GPIO out)   -> DE & RE (disambung) modul RS485  (HIGH = kirim, LOW = terima)
 *   3.3V              -> VCC modul RS485 (sesuaikan jika modulnya 5V)
 *   GND               -> GND modul RS485 & GND sensor (WAJIB disambung)
 *   A/B modul RS485   -> Kabel sensor (White = A, Blue = B)
 *   Orange (VCC)      -> 12V (7–15V), Black (GND) -> GND 12V (GND bersama Blue Pill)
 */

#ifndef URM14_H
#define URM14_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* ---------- Konfigurasi default ---------- */
#define URM14_DEFAULT_ADDR      0x0C    /* alamat pabrik */
#define URM14_DEFAULT_TIMEOUT   100     /* ms */
#define URM14_MEASURE_DELAY_MS  40      /* tunggu pengukuran passive (datasheet: ~30 ms) */

/* ---------- Alamat register (lihat wiki SEN0358) ---------- */
#define URM14_REG_PID          0x00   /* R   : 0x0002 */
#define URM14_REG_VID          0x01   /* R   : 0x0010 */
#define URM14_REG_ADDR         0x02   /* R/W : alamat slave (berlaku setelah restart) */
#define URM14_REG_BAUD_PARITY  0x03   /* R/W : baudrate (berlaku setelah restart) */
#define URM14_REG_SERIAL2      0x04   /* R/W : H = parity, L = stop bit */
#define URM14_REG_DISTANCE     0x05   /* R   : jarak, satuan 0.1 mm */
#define URM14_REG_TEMP        0x06   /* R   : suhu onboard, satuan 0.1 C */
#define URM14_REG_EXT_TEMP     0x07   /* R/W : suhu eksternal untuk kompensasi, 0.1 C */
#define URM14_REG_CONTROL      0x08   /* R/W : kontrol mode & trigger */
#define URM14_REG_NOISE        0x09   /* R   : level noise 0..10 */

/* Bit pada register control (0x08) */
#define URM14_CTRL_EXT_TEMP    (1u << 0)  /* 1 = pakai suhu eksternal */
#define URM14_CTRL_TEMP_OFF    (1u << 1)  /* 1 = kompensasi suhu mati */
#define URM14_CTRL_PASSIVE     (1u << 2)  /* 1 = mode passive (default pabrik) */
#define URM14_CTRL_TRIGGER     (1u << 3)  /* 1 = ukur sekali (auto-clear) */

typedef enum {
    URM14_OK = 0,
    URM14_ERR_PARAM,      /* argumen tidak valid */
    URM14_ERR_TIMEOUT,    /* sensor tidak membalas */
    URM14_ERR_CRC,        /* CRC frame balasan salah */
    URM14_ERR_FRAME,      /* frame balasan tidak sesuai */
    URM14_ERR_OUT_OF_RANGE/* jarak di luar 100–1500 mm (0xFFFF) */
} URM14_Status;

typedef struct {
    UART_HandleTypeDef *huart;     /* UART yang terhubung ke RS485 */
    GPIO_TypeDef       *de_port;   /* port pin DE/RE */
    uint16_t            de_pin;    /* pin DE/RE */
    uint8_t             addr;      /* alamat slave Modbus */
    uint32_t            timeout;   /* timeout komunikasi (ms) */
} URM14_HandleTypeDef;

/* Inisialisasi handle (tidak mengakses UART) */
URM14_Status URM14_Init(URM14_HandleTypeDef *dev,
                        UART_HandleTypeDef *huart,
                        GPIO_TypeDef *de_port, uint16_t de_pin,
                        uint8_t addr);

/* Cek komunikasi: baca PID register, harus 0x0002 */
URM14_Status URM14_Ping(URM14_HandleTypeDef *dev);

/* Trigger satu pengukuran (mode passive) lalu baca jarak dalam mm */
URM14_Status URM14_ReadDistanceMM(URM14_HandleTypeDef *dev, float *distance_mm);

/* Baca suhu onboard dalam derajat Celsius */
URM14_Status URM14_ReadTemperatureC(URM14_HandleTypeDef *dev, float *temp_c);

/* Baca level noise (0..10) — makin kecil makin akurat */
URM14_Status URM14_ReadNoise(URM14_HandleTypeDef *dev, uint16_t *noise);

/* Tulis suhu ambient untuk kompensasi eksternal (derajat C) */
URM14_Status URM14_SetExternalTempC(URM14_HandleTypeDef *dev, float temp_c);

/* Akses register mentah (Modbus func 0x03 dan 0x06) */
URM14_Status URM14_ReadRegister(URM14_HandleTypeDef *dev, uint16_t reg, uint16_t *value);
URM14_Status URM14_WriteRegister(URM14_HandleTypeDef *dev, uint16_t reg, uint16_t value);

/* Ganti alamat slave (tersimpan, berlaku setelah sensor di-power ulang) */
URM14_Status URM14_SetAddress(URM14_HandleTypeDef *dev, uint8_t new_addr);

#ifdef __cplusplus
}
#endif

#endif /* URM14_H */
