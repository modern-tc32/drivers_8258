#include <stdint.h>
#include "include/rf_drv.h"

extern const TBLCMDSET tbl_rf_init[];
extern const TBLCMDSET tbl_rf_zigbee_250k[];
extern const TBLCMDSET tbl_rf_1m[];
extern const uint8_t rf_chn_table[];
extern volatile uint16_t g_RFMode;
extern volatile uint8_t g_RFRxPingpongEn;
extern volatile uint8_t RF_TRxState;

void rf_drv_init(RF_ModeTypeDef rf_mode) {
    LoadTblCmdSet(tbl_rf_init, 5);
    if (rf_mode == RF_MODE_BLE_1M) {
        LoadTblCmdSet(tbl_rf_1m, 25);
    } else if (rf_mode == RF_MODE_ZIGBEE_250K) {
        LoadTblCmdSet(tbl_rf_zigbee_250k, 28);
    }
    reg_dma_chn_en |= (uint8_t)(FLD_DMA_CHN_RF_RX | FLD_DMA_CHN_RF_TX);
    g_RFMode = rf_mode;
}

void rf_set_channel(signed char chn, unsigned short option) {
    int16_t ch = chn;
    if ((int32_t)((int16_t)option << 16) < 0) {
        ch = (int8_t)rf_chn_table[(int)chn];
    }

    ch = (int16_t)(ch + 0x960);

    uint8_t vco_cap_step = 0;
    if (ch <= 0x09f5) {
        vco_cap_step = 4;
        if (ch <= 0x09d7) {
            vco_cap_step = 8;
            if (ch <= 0x09be) {
                vco_cap_step = 12;
                if (ch <= 0x09a0) {
                    vco_cap_step = 16;
                    if (ch <= 0x0982) {
                        vco_cap_step = 20;
                        if (ch <= 0x0964) {
                            vco_cap_step = 28;
                            if (ch <= 0x094b) {
                                vco_cap_step = 24;
                            }
                        }
                    }
                }
            }
        }
    }

    uint32_t rf_chn_word = (uint32_t)((uint16_t)ch) << 17;
    uint8_t rf_tx_chn_l = (uint8_t)(((rf_chn_word >> 15) | 1u) & 0xffu);
    REG_ADDR8(0x1244) = rf_tx_chn_l;

    uint8_t rf_tx_chn_h = REG_ADDR8(0x1245);
    rf_tx_chn_h = (uint8_t)((rf_tx_chn_h & 0xc0u) | ((rf_chn_word >> 23) & 0x3fu));
    REG_ADDR8(0x1245) = rf_tx_chn_h;

    uint8_t rf_vco_cap = REG_ADDR8(0x1229);
    rf_vco_cap = (uint8_t)((rf_vco_cap & 0xc3u) | vco_cap_step);
    REG_ADDR8(0x1229) = rf_vco_cap;
}

void rf_set_power_level(RF_PowerTypeDef level) {
    int8_t signed_level = (int8_t)level;
    uint8_t lv = (uint8_t)level;
    if (signed_level < 0) {
        REG_ADDR8(0x1225) |= BIT(6);
    } else {
        REG_ADDR8(0x1225) &= (uint8_t)~BIT(6);
    }

    uint8_t power_code = (uint8_t)(lv & 0x3fu);
    uint32_t power_word = ((uint32_t)power_code) << 24;

    uint8_t rf_pa_ctrl0 = REG_ADDR8(0x1226);
    rf_pa_ctrl0 = (uint8_t)((rf_pa_ctrl0 & 0x7fu) | ((power_word >> 17) & 0x80u));
    REG_ADDR8(0x1226) = rf_pa_ctrl0;

    uint8_t rf_pa_ctrl1 = REG_ADDR8(0x1227);
    rf_pa_ctrl1 = (uint8_t)((rf_pa_ctrl1 & 0xe0u) | ((power_word >> 25) & 0x1fu));
    REG_ADDR8(0x1227) = rf_pa_ctrl1;
}

void rf_set_power_level_index(RF_PowerIndexTypeDef index) {
    if (index <= 0x3b) {
        rf_set_power_level(rf_power_Level_list[index]);
    }
}

int rf_trx_state_set(RF_StatusTypeDef state, signed char chn) {
    reg_rf_ll_ctrl_0 = RF_TRX_OFF;
    rf_set_channel(chn, 0);

    if (state == RF_MODE_TX) {
        reg_rf_ll_ctrl_0 = RF_TRX_OFF | BIT(4);
        REG_ADDR8(0x428) &= (uint8_t)~BIT(0);
        RF_TRxState = state;
        return 0;
    }

    if (state == RF_MODE_RX) {
        reg_rf_ll_ctrl_0 = RF_TRX_OFF | BIT(5);
        REG_ADDR8(0x428) |= BIT(0);
        RF_TRxState = state;
        return 0;
    }

    if (state == RF_MODE_OFF) {
        reg_rf_ll_ctrl_3 = 0x29;
        REG_ADDR8(0x428) = RF_TRX_MODE;
        reg_rf_ll_ctrl_0 = RF_TRX_OFF;
        RF_TRxState = state;
        return 0;
    }

    if (state != RF_MODE_AUTO) {
        return -1;
    }

    reg_rf_ll_cmd = 0x80;
    reg_rf_ll_ctrl_3 = 0x29;
    REG_ADDR8(0x428) &= (uint8_t)~BIT(0);
    reg_rf_ll_ctrl_0 &= (uint8_t)~(BIT(0) | BIT(4) | BIT(5));
    RF_TRxState = state;
    return 0;
}

void rf_tx_pkt(unsigned char *rf_txaddr) {
    reg_dma3_addrHi = 4;
    reg_dma_rf_tx_addr = (uint16_t)(uintptr_t)rf_txaddr;
    REG_ADDR8(0xc5b) |= BIT(3);
}

RF_StatusTypeDef rf_trx_state_get(void) {
    return RF_TRxState;
}

void rf_rx_buffer_set(unsigned char *rf_rx_addr, int size, unsigned char pingpong_en) {
    uint8_t dma_mode = pingpong_en ? 3u : 1u;
    reg_dma_rf_rx_addr = (uint16_t)(uintptr_t)rf_rx_addr;
    reg_dma_rf_rx_size = (uint8_t)(((unsigned int)size << 20) >> 24);
    reg_dma_rf_rx_mode = dma_mode;
    g_RFRxPingpongEn = pingpong_en;
}

void rf_rx_cfg(int size, unsigned char pingpong_en) {
    uint8_t dma_mode = pingpong_en ? 3u : 1u;
    reg_dma_rf_rx_size = (uint8_t)(((unsigned int)size << 20) >> 24);
    reg_dma_rf_rx_mode = dma_mode;
    g_RFRxPingpongEn = pingpong_en;
}

void rf_start_stx(void *addr, unsigned int tick) {
    reg_rf_ll_cmd_sch = (uint32_t)(uintptr_t)addr;
    reg_rf_ll_ctrl_3 |= FLD_RF_CMD_SCHEDULE_EN;
    reg_rf_ll_cmd = 0x85;
    reg_dma_rf_tx_addr = (uint16_t)tick;
}

void rf_start_srx(unsigned int tick) {
    reg_rf_rx_1st_timeout = 0x0fffffff;
    reg_rf_ll_cmd_sch = tick;
    reg_rf_ll_ctrl_3 |= FLD_RF_CMD_SCHEDULE_EN;
    reg_rf_ll_cmd_2B = 0x3f86;
}

void rf_pn_disable(void) {
    REG_ADDR8(0x401) = 0;
    REG_ADDR8(0x404) &= (uint8_t)~BIT(5);
}
