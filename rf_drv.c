#include <stdint.h>
#include "include/rf_drv.h"

const TBLCMDSET tbl_rf_init[] = {
    {0x12d2, 0x9b, 0xc3},
    {0x12d3, 0x19, 0xc3},
    {0x127b, 0x0e, 0xc3},
    {0x1276, 0x50, 0xc3},
    {0x1277, 0x73, 0xc3},
};

const TBLCMDSET tbl_rf_1m[] = {
    {0x1220, 0x16, 0xc3}, {0x1221, 0x0a, 0xc3}, {0x1222, 0x20, 0xc3}, {0x1223, 0x23, 0xc3},
    {0x124a, 0x0e, 0xc3}, {0x124b, 0x09, 0xc3}, {0x124e, 0x09, 0xc3}, {0x124f, 0x0f, 0xc3},
    {0x0400, 0x1f, 0xc3}, {0x0401, 0x01, 0xc3}, {0x0402, 0x46, 0xc3}, {0x0404, 0xf5, 0xc3},
    {0x0405, 0x04, 0xc3}, {0x0420, 0x1e, 0xc3}, {0x0421, 0xa1, 0xc3}, {0x0430, 0x3e, 0xc3},
    {0x0460, 0x34, 0xc3}, {0x0461, 0x44, 0xc3}, {0x0462, 0x4f, 0xc3}, {0x0463, 0x5f, 0xc3},
    {0x0464, 0x6b, 0xc3}, {0x0465, 0x76, 0xc3}, {0x1276, 0x45, 0xc3}, {0x1277, 0x7b, 0xc3},
    {0x1279, 0x08, 0xc3},
};

const TBLCMDSET tbl_rf_zigbee_250k[] = {
    {0x1220, 0x04, 0xc3}, {0x1221, 0x2b, 0xc3}, {0x1222, 0x43, 0xc3}, {0x1223, 0x86, 0xc3},
    {0x122a, 0x90, 0xc3}, {0x1254, 0x0e, 0xc3}, {0x1255, 0x09, 0xc3}, {0x1256, 0x0c, 0xc3},
    {0x1257, 0x08, 0xc3}, {0x1258, 0x09, 0xc3}, {0x1259, 0x0f, 0xc3}, {0x0400, 0x13, 0xc3},
    {0x0420, 0x18, 0xc3}, {0x0402, 0x46, 0xc3}, {0x0404, 0xc0, 0xc3}, {0x0405, 0x04, 0xc3},
    {0x0421, 0x23, 0xc3}, {0x0422, 0x04, 0xc3}, {0x0408, 0xa7, 0xc3}, {0x0409, 0x00, 0xc3},
    {0x040a, 0x00, 0xc3}, {0x040b, 0x00, 0xc3}, {0x0460, 0x36, 0xc3}, {0x0461, 0x46, 0xc3},
    {0x0462, 0x51, 0xc3}, {0x0463, 0x61, 0xc3}, {0x0464, 0x6d, 0xc3}, {0x0465, 0x78, 0xc3},
};

const uint8_t rf_chn_table[] = {
    0x05, 0x09, 0x0d, 0x11, 0x16, 0x1a, 0x1e, 0x23, 0x28, 0x2d, 0x32, 0x37, 0x3c, 0x41, 0x46, 0x4c,
};

const RF_PowerTypeDef rf_power_Level_list[60] = {
    0x3f, 0x3d, 0x3a, 0x38, 0x35, 0x33, 0x31, 0x2f, 0x2d, 0x2b, 0x29, 0x27, 0x25, 0x23, 0x21, 0x1f,
    0x1d, 0x1b, 0x19, 0x17, 0xbf, 0xbd, 0xbb, 0xb9, 0xb6, 0xb4, 0xb2, 0xb0, 0xae, 0xac, 0xa9, 0xa8,
    0xa4, 0xa2, 0xa0, 0x9e, 0x9c, 0x9a, 0x98, 0x96, 0x94, 0x92, 0x90, 0x8e, 0x8c, 0x8a, 0x88, 0x86,
    0x84, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

volatile uint16_t g_RFMode;
static volatile uint8_t g_RFRxPingpongEn;
static volatile uint8_t RF_TRxState;

enum {
    FLD_RF_TRX_CTRL_TX = BIT(4),     /* reg_rf_ll_ctrl_0 */
    FLD_RF_TRX_CTRL_RX = BIT(5),     /* reg_rf_ll_ctrl_0 */
    FLD_RF_TRX_CTRL_CMD0 = BIT(0),   /* reg_rf_ll_ctrl_0 */
    FLD_RF_MODE_RX_EN = BIT(0),      /* reg 0x428 */
};

__attribute__((section(".text.rf_drv_init"))) void rf_drv_init(RF_ModeTypeDef rf_mode) {
    LoadTblCmdSet(tbl_rf_init, 5);
    if (rf_mode == RF_MODE_BLE_1M) {
        LoadTblCmdSet(tbl_rf_1m, 25);
    } else if (rf_mode == RF_MODE_ZIGBEE_250K) {
        LoadTblCmdSet(tbl_rf_zigbee_250k, 28);
    }
    reg_dma_chn_en |= (uint8_t)(FLD_DMA_CHN_RF_RX | FLD_DMA_CHN_RF_TX);
    g_RFMode = rf_mode;
}

__attribute__((section(".text.rf_set_channel"))) void rf_set_channel(signed char chn, unsigned short option) {
    int16_t ch = chn;
    if ((int32_t)((int16_t)option << 16) < 0) {
        ch = (int8_t)rf_chn_table[(int)chn];
    }

    ch = (int16_t)(ch + 0x960);

    uint8_t vco_cap_step = 0;
    if (ch <= 0x094b) {
        vco_cap_step = 24;
    } else if (ch <= 0x0964) {
        vco_cap_step = 28;
    } else if (ch <= 0x0982) {
        vco_cap_step = 20;
    } else if (ch <= 0x09a0) {
        vco_cap_step = 16;
    } else if (ch <= 0x09be) {
        vco_cap_step = 12;
    } else if (ch <= 0x09d7) {
        vco_cap_step = 8;
    } else if (ch <= 0x09f5) {
        vco_cap_step = 4;
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

__attribute__((section(".text.rf_set_power_level"))) void rf_set_power_level(RF_PowerTypeDef level) {
    if ((int8_t)level < 0) {
        REG_ADDR8(0x1225) |= BIT(6);
    } else {
        REG_ADDR8(0x1225) &= (uint8_t)~BIT(6);
    }

    uint8_t power_code = (uint8_t)(((uint8_t)level) & 0x3fu);
    uint32_t power_word = ((uint32_t)power_code) << 24;

    uint8_t rf_pa_ctrl0 = REG_ADDR8(0x1226);
    rf_pa_ctrl0 = (uint8_t)((rf_pa_ctrl0 & 0x7fu) | ((power_word >> 17) & 0x80u));
    REG_ADDR8(0x1226) = rf_pa_ctrl0;

    uint8_t rf_pa_ctrl1 = REG_ADDR8(0x1227);
    rf_pa_ctrl1 = (uint8_t)((rf_pa_ctrl1 & 0xe0u) | ((power_word >> 25) & 0x1fu));
    REG_ADDR8(0x1227) = rf_pa_ctrl1;
}

__attribute__((section(".text.rf_set_power_level_index"))) void rf_set_power_level_index(RF_PowerIndexTypeDef index) {
    if (index <= 0x3b) {
        rf_set_power_level(rf_power_Level_list[index]);
    }
}

__attribute__((section(".text.rf_trx_state_set"))) int rf_trx_state_set(RF_StatusTypeDef state, signed char chn) {
    reg_rf_ll_ctrl_0 = RF_TRX_OFF;
    rf_set_channel(chn, 0);

    if (state == RF_MODE_TX) {
        rf_set_txmode();
        REG_ADDR8(0x428) &= (uint8_t)~BIT(0);
        RF_TRxState = state;
        return 0;
    }

    if (state == RF_MODE_RX) {
        reg_rf_ll_ctrl_0 = RF_TRX_OFF | FLD_RF_TRX_CTRL_RX;
        REG_ADDR8(0x428) |= FLD_RF_MODE_RX_EN;
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

    STOP_RF_STATE_MACHINE;
    reg_rf_ll_ctrl_3 = 0x29;
    REG_ADDR8(0x428) &= (uint8_t)~FLD_RF_MODE_RX_EN;
    reg_rf_ll_ctrl_0 &= (uint8_t)~(FLD_RF_TRX_CTRL_CMD0 | FLD_RF_TRX_CTRL_TX | FLD_RF_TRX_CTRL_RX);
    RF_TRxState = state;
    return 0;
}

__attribute__((section(".text.rf_tx_pkt"))) void rf_tx_pkt(unsigned char *rf_txaddr) {
    reg_dma3_addrHi = 4;
    reg_dma_rf_tx_addr = (uint16_t)(uintptr_t)rf_txaddr;
    REG_ADDR8(0xc5b) |= BIT(3);
}

__attribute__((section(".text.rf_trx_state_get"))) RF_StatusTypeDef rf_trx_state_get(void) {
    return RF_TRxState;
}

__attribute__((section(".text.rf_rx_buffer_set"))) void rf_rx_buffer_set(unsigned char *rf_rx_addr, int size, unsigned char pingpong_en) {
    uint8_t dma_mode = pingpong_en ? 3u : 1u;
    reg_dma_rf_rx_addr = (uint16_t)(uintptr_t)rf_rx_addr;
    reg_dma_rf_rx_size = (uint8_t)(((unsigned int)size >> 4) & 0xffu);
    reg_dma_rf_rx_mode = dma_mode;
    g_RFRxPingpongEn = pingpong_en;
}

__attribute__((section(".text.rf_rx_cfg"))) void rf_rx_cfg(int size, unsigned char pingpong_en) {
    uint8_t dma_mode = pingpong_en ? 3u : 1u;
    reg_dma_rf_rx_size = (uint8_t)(((unsigned int)size >> 4) & 0xffu);
    reg_dma_rf_rx_mode = dma_mode;
    g_RFRxPingpongEn = pingpong_en;
}

__attribute__((section(".text.rf_start_stx"))) void rf_start_stx(void *addr, unsigned int tick) {
    reg_rf_ll_cmd_sch = (uint32_t)(uintptr_t)addr;
    reg_rf_ll_ctrl_3 |= FLD_RF_CMD_SCHEDULE_EN;
    reg_rf_ll_cmd = 0x85;
    reg_dma_rf_tx_addr = (uint16_t)tick;
}

__attribute__((section(".text.rf_start_srx"))) void rf_start_srx(unsigned int tick) {
    reg_rf_rx_1st_timeout = 0x0fffffff;
    reg_rf_ll_cmd_sch = tick;
    reg_rf_ll_ctrl_3 |= FLD_RF_CMD_SCHEDULE_EN;
    reg_rf_ll_cmd_2B = 0x3f86;
}

__attribute__((section(".text.rf_pn_disable"))) void rf_pn_disable(void) {
    REG_ADDR8(0x401) = 0;
    REG_ADDR8(0x404) &= (uint8_t)~BIT(5);
}
