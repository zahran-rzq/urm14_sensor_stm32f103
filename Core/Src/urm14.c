/**
 * @file    urm14.c
 * @brief   Implementasi driver URM14 (Modbus-RTU over RS485) untuk STM32 HAL
 */

#include "urm14.h"

#define FC_READ_HOLDING   0x03
#define FC_WRITE_SINGLE   0x06

/* ------------------------------------------------------------------ */
/*  Variabel DEBUG — pasang di Live Expressions                         */
/* ------------------------------------------------------------------ */
volatile HAL_StatusTypeDef urm14_dbg_tx_hs = HAL_OK;   /* hasil HAL_UART_Transmit (0=OK,2=BUSY,3=TIMEOUT) */
volatile HAL_StatusTypeDef urm14_dbg_rx_hs = HAL_OK;   /* hasil HAL_UART_Receive  (0=OK,1=ERROR,3=TIMEOUT) */
volatile uint8_t  urm14_dbg_tx[8];                     /* frame terakhir yang dikirim */
volatile uint8_t  urm14_dbg_rx[8];                     /* byte balasan terakhir */
volatile uint16_t urm14_dbg_rx_remaining = 0;          /* byte yang BELUM diterima (7 = tidak ada balasan) */
volatile uint32_t urm14_dbg_tx_count = 0;              /* jumlah request yang dikirim */

/* ------------------------------------------------------------------ */
/*  CRC-16/MODBUS (poly 0xA001, init 0xFFFF), byte rendah dikirim dulu */
/* ------------------------------------------------------------------ */
static uint16_t urm14_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/*  Kirim frame lalu terima balasan (half-duplex RS485)                */
/* ------------------------------------------------------------------ */
static URM14_Status urm14_transact(URM14_HandleTypeDef *dev,
                                   const uint8_t *tx, uint16_t tx_len,
                                   uint8_t *rx, uint16_t rx_len)
{
    HAL_StatusTypeDef hs;

    /* Mode kirim: DE=1 (receiver ikut mati saat DE/RE disambung,
       sehingga echo data sendiri tidak masuk ke RX) */
    HAL_GPIO_WritePin(dev->de_port, dev->de_pin, GPIO_PIN_SET);

    /* catat frame yang dikirim untuk debug */
    for (uint16_t i = 0; i < tx_len && i < 8; i++) {
        urm14_dbg_tx[i] = tx[i];
    }
    urm14_dbg_tx_count++;

    hs = HAL_UART_Transmit(dev->huart, (uint8_t *)tx, tx_len, dev->timeout);
    urm14_dbg_tx_hs = hs;

    /* HAL_UART_Transmit sudah menunggu flag TC (byte terakhir keluar),
       jadi aman untuk langsung kembali ke mode terima */
    HAL_GPIO_WritePin(dev->de_port, dev->de_pin, GPIO_PIN_RESET);

    if (hs != HAL_OK) {
        return URM14_ERR_TIMEOUT;
    }

    if (rx == NULL || rx_len == 0) {
        return URM14_OK;
    }

    __HAL_UART_CLEAR_OREFLAG(dev->huart);   /* bersihkan overrun lama */

    hs = HAL_UART_Receive(dev->huart, rx, rx_len, dev->timeout);
    urm14_dbg_rx_hs = hs;
    urm14_dbg_rx_remaining = dev->huart->RxXferCount;
    for (uint16_t i = 0; i < rx_len && i < 8; i++) {
        urm14_dbg_rx[i] = rx[i];
    }

    if (hs == HAL_TIMEOUT) {
        return URM14_ERR_TIMEOUT;
    }
    if (hs != HAL_OK) {
        return URM14_ERR_FRAME;
    }
    return URM14_OK;
}

/* ------------------------------------------------------------------ */
/*  API publik                                                         */
/* ------------------------------------------------------------------ */
URM14_Status URM14_Init(URM14_HandleTypeDef *dev,
                        UART_HandleTypeDef *huart,
                        GPIO_TypeDef *de_port, uint16_t de_pin,
                        uint8_t addr)
{
    if (dev == NULL || huart == NULL || de_port == NULL) {
        return URM14_ERR_PARAM;
    }
    if (addr == 0x00 || addr > 0xF7) {
        return URM14_ERR_PARAM;
    }

    dev->huart    = huart;
    dev->de_port  = de_port;
    dev->de_pin   = de_pin;
    dev->addr     = addr;
    dev->timeout  = URM14_DEFAULT_TIMEOUT;

    /* Pastikan transceiver berada di mode terima */
    HAL_GPIO_WritePin(de_port, de_pin, GPIO_PIN_RESET);
    return URM14_OK;
}

URM14_Status URM14_ReadRegister(URM14_HandleTypeDef *dev, uint16_t reg, uint16_t *value)
{
    if (dev == NULL || value == NULL) {
        return URM14_ERR_PARAM;
    }

    /* Request: addr | 0x03 | reg_hi | reg_lo | qty_hi | qty_lo | crc_lo | crc_hi */
    uint8_t tx[8];
    tx[0] = dev->addr;
    tx[1] = FC_READ_HOLDING;
    tx[2] = (uint8_t)(reg >> 8);
    tx[3] = (uint8_t)(reg & 0xFF);
    tx[4] = 0x00;
    tx[5] = 0x01;                       /* jumlah register = 1 */
    uint16_t crc = urm14_crc16(tx, 6);
    tx[6] = (uint8_t)(crc & 0xFF);
    tx[7] = (uint8_t)(crc >> 8);

    /* Response: addr | 0x03 | byte_count(2) | data_hi | data_lo | crc_lo | crc_hi */
    uint8_t rx[7];
    URM14_Status st = urm14_transact(dev, tx, sizeof(tx), rx, sizeof(rx));
    if (st != URM14_OK) {
        return st;
    }

    if (rx[0] != dev->addr || rx[1] != FC_READ_HOLDING || rx[2] != 0x02) {
        return URM14_ERR_FRAME;
    }

    crc = urm14_crc16(rx, 5);
    if ((rx[5] != (uint8_t)(crc & 0xFF)) || (rx[6] != (uint8_t)(crc >> 8))) {
        return URM14_ERR_CRC;
    }

    *value = ((uint16_t)rx[3] << 8) | rx[4];
    return URM14_OK;
}

URM14_Status URM14_WriteRegister(URM14_HandleTypeDef *dev, uint16_t reg, uint16_t value)
{
    if (dev == NULL) {
        return URM14_ERR_PARAM;
    }

    /* Request: addr | 0x06 | reg_hi | reg_lo | val_hi | val_lo | crc_lo | crc_hi */
    uint8_t tx[8];
    tx[0] = dev->addr;
    tx[1] = FC_WRITE_SINGLE;
    tx[2] = (uint8_t)(reg >> 8);
    tx[3] = (uint8_t)(reg & 0xFF);
    tx[4] = (uint8_t)(value >> 8);
    tx[5] = (uint8_t)(value & 0xFF);
    uint16_t crc = urm14_crc16(tx, 6);
    tx[6] = (uint8_t)(crc & 0xFF);
    tx[7] = (uint8_t)(crc >> 8);

    /* Sensor membalas echo 8 byte yang sama dengan request */
    uint8_t rx[8];
    URM14_Status st = urm14_transact(dev, tx, sizeof(tx), rx, sizeof(rx));
    if (st != URM14_OK) {
        return st;
    }

    for (uint8_t i = 0; i < 8; i++) {
        if (rx[i] != tx[i]) {
            return URM14_ERR_FRAME;
        }
    }
    return URM14_OK;
}

URM14_Status URM14_Ping(URM14_HandleTypeDef *dev)
{
    uint16_t pid = 0;
    URM14_Status st = URM14_ReadRegister(dev, URM14_REG_PID, &pid);
    if (st != URM14_OK) {
        return st;
    }
    return (pid == 0x0002) ? URM14_OK : URM14_ERR_FRAME;
}

URM14_Status URM14_ReadDistanceMM(URM14_HandleTypeDef *dev, float *distance_mm)
{
    if (dev == NULL || distance_mm == NULL) {
        return URM14_ERR_PARAM;
    }

    /* 1. Trigger satu pengukuran: control = passive (bit2) | trigger (bit3) */
    URM14_Status st = URM14_WriteRegister(dev, URM14_REG_CONTROL,
                                          URM14_CTRL_PASSIVE | URM14_CTRL_TRIGGER);
    if (st != URM14_OK) {
        return st;
    }

    /* 2. Tunggu hasil pengukuran (~30 ms) */
    HAL_Delay(URM14_MEASURE_DELAY_MS);

    /* 3. Baca register jarak (satuan 0.1 mm) */
    uint16_t raw = 0;
    st = URM14_ReadRegister(dev, URM14_REG_DISTANCE, &raw);
    if (st != URM14_OK) {
        return st;
    }

    if (raw == 0xFFFF) {
        return URM14_ERR_OUT_OF_RANGE;   /* nilai default = tidak ada echo / di luar range */
    }

    *distance_mm = (float)raw * 0.1f;
    return URM14_OK;
}

URM14_Status URM14_ReadTemperatureC(URM14_HandleTypeDef *dev, float *temp_c)
{
    if (dev == NULL || temp_c == NULL) {
        return URM14_ERR_PARAM;
    }

    uint16_t raw = 0;
    URM14_Status st = URM14_ReadRegister(dev, URM14_REG_TEMP, &raw);
    if (st != URM14_OK) {
        return st;
    }

    *temp_c = (int16_t)raw * 0.1f;      /* signed, satuan 0.1 C */
    return URM14_OK;
}

URM14_Status URM14_ReadNoise(URM14_HandleTypeDef *dev, uint16_t *noise)
{
    if (dev == NULL || noise == NULL) {
        return URM14_ERR_PARAM;
    }
    return URM14_ReadRegister(dev, URM14_REG_NOISE, noise);
}

URM14_Status URM14_SetExternalTempC(URM14_HandleTypeDef *dev, float temp_c)
{
    if (dev == NULL) {
        return URM14_ERR_PARAM;
    }

    int16_t raw = (int16_t)(temp_c * 10.0f);
    return URM14_WriteRegister(dev, URM14_REG_EXT_TEMP, (uint16_t)raw);
}

URM14_Status URM14_SetAddress(URM14_HandleTypeDef *dev, uint8_t new_addr)
{
    if (dev == NULL || new_addr == 0x00 || new_addr > 0xF7) {
        return URM14_ERR_PARAM;
    }

    /* Alamat baru disimpan sensor, tetapi baru aktif setelah power-cycle.
       Karena itu dev->addr TIDAK diubah di sini: setelah sensor di-restart,
       panggil URM14_Init() lagi dengan alamat baru. */
    return URM14_WriteRegister(dev, URM14_REG_ADDR, new_addr);
}
