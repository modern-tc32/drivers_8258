#include <stdint.h>

#define REG8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

extern int LoadTblCmdSet(const void *pt, int size);

extern const uint32_t tbl_rf_init[];
extern const uint32_t tbl_rf_zigbee_250k[];
extern const uint32_t tbl_rf_1m[];
extern const uint8_t rf_chn_table[];
extern const uint8_t rf_power_Level_list[];

extern volatile uint16_t g_RFMode;
extern volatile uint8_t g_RFRxPingpongEn;
extern volatile uint8_t RF_TRxState;

void rf_drv_init(uint16_t rf_mode) {
    LoadTblCmdSet(tbl_rf_init, 5);
    if (rf_mode == 2) {
        LoadTblCmdSet(tbl_rf_1m, 25);
    } else if (rf_mode == 8) {
        LoadTblCmdSet(tbl_rf_zigbee_250k, 28);
    }
    REG8(0x00800c20) = (uint8_t)(REG8(0x00800c20) | 0x0c);
    g_RFMode = rf_mode;
}

void rf_set_channel(int8_t chn, uint8_t option) {
    int16_t ch = chn;
    if ((int32_t)((int16_t)option << 16) < 0) {
        ch = (int8_t)rf_chn_table[(int)chn];
    }

    ch = (int16_t)(ch + 0x960);

    uint8_t step = 0;
    if (ch <= 0x09f5) {
        step = 4;
        if (ch <= 0x09d7) {
            step = 8;
            if (ch <= 0x09be) {
                step = 12;
                if (ch <= 0x09a0) {
                    step = 16;
                    if (ch <= 0x0982) {
                        step = 20;
                        if (ch <= 0x0964) {
                            step = 28;
                            if (ch <= 0x094b) {
                                step = 24;
                            }
                        }
                    }
                }
            }
        }
    }

    uint32_t t = (uint32_t)((uint16_t)ch) << 17;
    uint8_t v0 = (uint8_t)(((t >> 15) | 1u) & 0xffu);
    REG8(0x00801244) = v0;

    uint8_t v1 = REG8(0x00801245);
    v1 = (uint8_t)((v1 & 0xc0u) | ((t >> 23) & 0x3fu));
    REG8(0x00801245) = v1;

    uint8_t v2 = REG8(0x00801229);
    v2 = (uint8_t)((v2 & 0xc3u) | step);
    REG8(0x00801229) = v2;
}

void rf_set_power_level(int8_t level) {
    uint8_t lv = (uint8_t)level;
    if (level < 0) {
        REG8(0x00801225) = (uint8_t)(REG8(0x00801225) | 0x40u);
    } else {
        REG8(0x00801225) = (uint8_t)(REG8(0x00801225) & (uint8_t)~0x40u);
    }

    uint8_t p = (uint8_t)(lv & 0x3fu);
    uint32_t t = ((uint32_t)p) << 24;

    uint8_t r0 = REG8(0x00801226);
    r0 = (uint8_t)((r0 & 0x7fu) | ((t >> 17) & 0x80u));
    REG8(0x00801226) = r0;

    uint8_t r1 = REG8(0x00801227);
    r1 = (uint8_t)((r1 & 0xe0u) | ((t >> 25) & 0x1fu));
    REG8(0x00801227) = r1;
}

void rf_set_power_level_index(uint8_t index) {
    if (index <= 0x3b) {
        rf_set_power_level((int8_t)rf_power_Level_list[index]);
    }
}

int rf_trx_state_set(uint8_t state, int8_t chn) {
    REG8(0x00800f02) = 0x45;
    rf_set_channel(chn, 0);

    if (state == 0) {
        REG8(0x00800f02) = 0x55;
        REG8(0x00800428) = (uint8_t)(REG8(0x00800428) & (uint8_t)~0x01u);
        RF_TRxState = state;
        return 0;
    }

    if (state == 1) {
        REG8(0x00800f02) = 0x65;
        REG8(0x00800428) = (uint8_t)(REG8(0x00800428) | 0x01u);
        RF_TRxState = state;
        return 0;
    }

    if (state == 3) {
        REG8(0x00800f16) = 0x29;
        REG8(0x00800428) = 0xe0;
        REG8(0x00800f02) = 0x45;
        RF_TRxState = state;
        return 0;
    }

    if (state != 2) {
        return -1;
    }

    REG8(0x00800f00) = 0x80;
    REG8(0x00800f16) = 0x29;
    REG8(0x00800428) = (uint8_t)(REG8(0x00800428) & (uint8_t)~0x01u);
    REG8(0x00800f02) = (uint8_t)(REG8(0x00800f02) & (uint8_t)~0x31u);
    RF_TRxState = state;
    return 0;
}

void rf_tx_pkt(uint16_t addr) {
    REG8(0x00800c43) = 4;
    REG16(0x00800c0c) = addr;
    REG8(0x00800c5b) = (uint8_t)(REG8(0x00800c5b) | 0x08u);
}

uint8_t rf_trx_state_get(void) {
    return RF_TRxState;
}

void rf_rx_buffer_set(uint16_t addr, uint8_t size_div_16, uint8_t pingpong_en) {
    uint8_t mode = pingpong_en ? 3u : 1u;
    REG16(0x00800c08) = addr;
    REG8(0x00800c0a) = (uint8_t)((size_div_16 << 20) >> 24);
    REG8(0x00800c0b) = mode;
    g_RFRxPingpongEn = pingpong_en;
}

void rf_rx_cfg(uint8_t size_div_16, uint8_t pingpong_en) {
    uint8_t mode = pingpong_en ? 3u : 1u;
    REG8(0x00800c0a) = (uint8_t)((size_div_16 << 20) >> 24);
    REG8(0x00800c0b) = mode;
    g_RFRxPingpongEn = pingpong_en;
}

void rf_start_stx(uint16_t tick, uint32_t tx_addr) {
    REG32(0x00800f18) = tx_addr;
    REG8(0x00800f16) = (uint8_t)(REG8(0x00800f16) | 0x04u);
    REG8(0x00800f00) = 0x85;
    REG16(0x00800c0c) = tick;
}

void rf_start_srx(uint32_t tick) {
    REG32(0x00800f28) = 0x0fffffff;
    REG32(0x00800f18) = tick;
    REG8(0x00800f16) = (uint8_t)(REG8(0x00800f16) | 0x04u);
    REG16(0x00800f00) = 0x3f86;
}

void rf_pn_disable(void) {
    REG8(0x00800401) = 0;
    REG8(0x00800404) = (uint8_t)(REG8(0x00800404) & (uint8_t)~0x20u);
}
