#include <stdint.h>
#include "include/register.h"
#include "include/irq.h"
#include "include/pm.h"

#define reg_pm_info_sel              REG_ADDR8(0x74)
#define reg_pm_info0                 REG_ADDR32(0x48)
#define reg_pm_info1                 REG_ADDR32(0x4c)
#define reg_ret_slot_idx             REG_ADDR8(0x60d)
#define reg_pm_misc_dummy            REG_ADDR32(0x40)
#define reg_gpio_func_sel_pe         reg_gpio_pe_ie
#define reg_sram_shutdown_sel        REG_ADDR8(0x7d)
#define reg_dma_addrhi0_3            REG_ADDR32(0xc40)
#define reg_dma_addrhi4_7            REG_ADDR32(0xc44)
#define reg_dma_addrhi8              REG_ADDR8(0xc48)
#define reg_gpio_table_reset         REG_ADDR16(0x750)
#define PM_RET_ENTRY_BASE            0x00840058u

#define areg_pwdn_setting            0x34
#define areg_dcdc_ctrl               0x0b
#define areg_ldo_trim                0x8c
#define areg_clk_2m_rc               0x02
#define areg_gpio_wakeup_en_pa       0x27
#define areg_gpio_wakeup_en_pb       0x28
#define areg_gpio_wakeup_en_pc       0x29
#define areg_gpio_wakeup_en_pd       0x2a
#define areg_dig_ldo_cap             0x01
#define areg_pm_status               0x7f
#define areg_wakeup_src              0x44
#define areg_32k_tick_0              0x40
#define areg_32k_tick_1              0x41
#define areg_32k_tick_2              0x42
#define areg_32k_tick_3              0x43

unsigned char tl_24mrc_cal = 0;
volatile pm_r_delay_us_s g_pm_r_delay_us = {0, 0};
volatile uint32_t g_pm_suspend_delay_us = 0;
volatile pm_early_wakeup_time_us_s g_pm_early_wakeup_time_us = {0, 0, 0, 0};
volatile uint32_t g_pm_xtal_stable_loopnum = 0;
volatile uint32_t g_pm_xtal_stable_suspend_nopnum = 0;
suspend_handler_t func_before_suspend = (suspend_handler_t)0;

unsigned int tick_cur;
unsigned int tick_32k_cur;
unsigned short tick_32k_calib;
unsigned char pm_long_suspend;
unsigned char tl_multi_addr;
pm_tim_recover_handler_t pm_tim_recover = 0;
cpu_pm_handler_t cpu_sleep_wakeup = 0;
pm_para_t pmParam = {0, 0, 0};

extern uint8_t analog_read(uint8_t addr);
extern void analog_write(uint8_t addr, uint8_t value);
extern void rc_24m_cal(void);
extern void doubler_calibration(void);
extern void efuse_sys_check(uint32_t info);
extern void flash_vdd_f_calib(void);
extern void adc_set_gpio_calib_vref(uint32_t x);
extern void start_suspend(void);
void soft_reboot_dly13ms_use24mRC(void);
extern uint32_t __udivsi3(uint32_t a, uint32_t b);

static uint32_t __attribute__((noinline, section(".text.clock_time"))) clock_time(void) {
    return reg_system_tick;
}

void __attribute__((section(".text.pm_set_wakeup_time_param"))) pm_set_wakeup_time_param(uint32_t us) {
    volatile uint16_t *rd = (volatile uint16_t *)&g_pm_r_delay_us;
    volatile uint16_t *ew = (volatile uint16_t *)&g_pm_early_wakeup_time_us;

    rd[0] = (uint16_t)us;
    rd[1] = (uint16_t)(us >> 16);

    ew[0] = (uint16_t)(rd[1] + 0x00e6u + g_pm_suspend_delay_us);
    ew[1] = (uint16_t)(rd[1] + 100u);
    ew[2] = (uint16_t)(rd[0] + 240u);

    uint16_t a = ew[2];
    uint16_t b = ew[0];
    if (a < b) {
        ew[3] = (uint16_t)(a + 0x0190u);
    } else {
        ew[3] = (uint16_t)(b + 0x0190u);
    }
}

void __attribute__((section(".text.pm_set_xtal_stable_timer_param"))) pm_set_xtal_stable_timer_param(uint32_t suspend_delay_us, uint32_t loopnum, uint32_t nopnum) {
    g_pm_xtal_stable_suspend_nopnum = nopnum;
    g_pm_xtal_stable_loopnum = loopnum;
    g_pm_suspend_delay_us = suspend_delay_us;

    uint16_t x = (uint16_t)(((volatile uint16_t *)&g_pm_r_delay_us)[1] + 0x00e6u + suspend_delay_us);
    ((volatile uint16_t *)&g_pm_early_wakeup_time_us)[0] = x;

    uint16_t a = ((volatile uint16_t *)&g_pm_early_wakeup_time_us)[2];
    uint16_t b = ((volatile uint16_t *)&g_pm_early_wakeup_time_us)[0];
    if (a < b) {
        ((volatile uint16_t *)&g_pm_early_wakeup_time_us)[3] = (uint16_t)(a + 0x0190u);
    } else {
        ((volatile uint16_t *)&g_pm_early_wakeup_time_us)[3] = (uint16_t)(b + 0x0190u);
    }
}

void __attribute__((section(".text.bls_pm_registerFuncBeforeSuspend"))) bls_pm_registerFuncBeforeSuspend(suspend_handler_t cb) {
    func_before_suspend = cb;
}

unsigned int __attribute__((noinline, section(".text.pm_get_info0"))) pm_get_info0(void) {
    reg_pm_info_sel = 0x62;
    uint32_t v = reg_pm_info0;
    reg_pm_info_sel = 0;
    return v;
}

unsigned int __attribute__((noinline, section(".text.pm_get_info1"))) pm_get_info1(void) {
    reg_pm_info_sel = 0x62;
    uint32_t v = reg_pm_info1;
    reg_pm_info_sel = 0;
    return v;
}

static void __attribute__((section(".text.cpu_wakeup_no_deepretn_back_init"))) cpu_wakeup_no_deepretn_back_init(void) {
    rc_24m_cal();
    doubler_calibration();
    uint32_t info = pm_get_info1();
    if ((info & 0xC0u) == 0xC0u) {
        uint32_t trim = info & 0x3fu;
        adc_set_gpio_calib_vref(trim * 5u + 0x03f7u);
    } else {
        efuse_sys_check(info);
        flash_vdd_f_calib();
    }
}

void __attribute__((section(".ram_code"))) sleep_start(void) {
    analog_write(areg_pwdn_setting, 0x87);
    reg_mspi_ctrl = 0;
    reg_mspi_data = 0xb9;

    for (volatile uint32_t i = 0; i <= 1u; ++i) {
    }

    reg_mspi_ctrl = 1;
    reg_gpio_func_sel_pe = 0;
    analog_write(areg_clk_setting, 0x0c);

    volatile uint8_t idx = reg_ret_slot_idx;
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(PM_RET_ENTRY_BASE + ((uint32_t)idx << 8));
    uint32_t old = *p;
    *p = 0x06c006c0u;
    start_suspend();
    *p = old;

    analog_write(areg_clk_setting, 0x64);
    reg_gpio_func_sel_pe = 0x0f;
    reg_mspi_ctrl = 0;
    reg_mspi_data = 0xab;

    for (volatile uint32_t i = 0; i <= 1u; ++i) {
    }

    reg_mspi_ctrl = 1;
    analog_write(areg_pwdn_setting, 0x80);

    for (volatile uint32_t i = 0; i <= g_pm_xtal_stable_suspend_nopnum; ++i) {
    }
}

unsigned int __attribute__((section(".text.pm_get_32k_tick"))) pm_get_32k_tick(void) {
    uint32_t cnt = 0;
    uint32_t prev = 0;
    uint32_t cur = 0;
    for (;;) {
        uint32_t v = analog_read(areg_32k_tick_3);
        v = (v << 8) + analog_read(areg_32k_tick_2);
        v = (v << 8) + analog_read(areg_32k_tick_1);
        v = (v << 8) + analog_read(areg_32k_tick_0);
        cur = v;
        if (cnt == 0) {
            cnt = 1;
            prev = cur;
            continue;
        }
        if ((cur ^ prev) == 1u) {
            cur = prev;
        }
        if ((cur - prev) <= 1u) {
            return cur;
        }
        cnt = cnt + 1;
        prev = cur;
    }
}

void __attribute__((section(".text.start_reboot"))) start_reboot(void) {
    irq_disable();
    soft_reboot_dly13ms_use24mRC();
    reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    for (;;) {
    }
}

void __attribute__((section(".text.pm_wait_xtal_ready"))) pm_wait_xtal_ready(void) {
    uint32_t i = 0;
    while (i < g_pm_xtal_stable_loopnum) {
        uint32_t start = clock_time();
        for (volatile uint32_t j = 0; j <= 0x3bu; ++j) {
        }
        if ((clock_time() - start) > 320u) {
            if (i == g_pm_xtal_stable_loopnum) {
                start_reboot();
            }
            return;
        }
        ++i;
    }
    if (i == g_pm_xtal_stable_loopnum) {
        start_reboot();
    }
}

void __attribute__((section(".text.cpu_stall_wakeup_by_timer0"))) cpu_stall_wakeup_by_timer0(unsigned int tick) {
    reg_tmr0_tick = 0;
    reg_tmr0_capt = tick;
    reg_tmr_ctrl16 &= (uint16_t)~FLD_TMR0_MODE;
    reg_tmr_ctrl8 |= FLD_TMR0_EN;
    reg_mcu_wakeup_mask |= 0x01u;
    reg_tmr_sta = FLD_TMR_STA_TMR0;
    reg_pwdn_ctrl = FLD_PWDN_CTRL_SLEEP;
    reg_tmr_sta = FLD_TMR_STA_TMR0;
    reg_tmr_ctrl8 &= (uint8_t)~FLD_TMR0_EN;
}

void __attribute__((section(".text.cpu_stall_wakeup_by_timer1"))) cpu_stall_wakeup_by_timer1(unsigned int tick) {
    reg_tmr1_tick = 0;
    reg_tmr1_capt = tick;
    reg_tmr0_capt &= (uint16_t)~0x0030u;
    reg_tmr0_capt |= 0x08u;
    reg_mcu_wakeup_mask |= 0x02u;
    reg_tmr_sta = FLD_TMR_STA_TMR1;
    reg_pwdn_ctrl = FLD_PWDN_CTRL_SLEEP;
    reg_tmr_sta = FLD_TMR_STA_TMR1;
    reg_tmr0_capt &= (uint8_t)~0x08u;
}

void __attribute__((section(".text.cpu_stall_wakeup_by_timer2"))) cpu_stall_wakeup_by_timer2(unsigned int tick) {
    reg_tmr2_tick = 0;
    reg_tmr2_capt = tick;
    reg_tmr2_capt &= (uint16_t)~0x3f82u;
    reg_tmr2_capt |= 0x40u;
    reg_mcu_wakeup_mask |= 0x04u;
    reg_tmr_sta = FLD_TMR_STA_TMR2;
    reg_pwdn_ctrl = FLD_PWDN_CTRL_SLEEP;
    reg_tmr_sta = FLD_TMR_STA_TMR2;
    reg_tmr2_capt &= (uint8_t)~0x40u;
}

unsigned int __attribute__((section(".text.cpu_stall"))) cpu_stall(int wakeup_src, unsigned int sleep_us, unsigned int tick_per_us) {
    if (sleep_us != 0u) {
        reg_tmr1_tick = 0;
        reg_tmr1_capt = sleep_us * tick_per_us;
        reg_tmr_sta = FLD_TMR_STA_TMR1;
        reg_tmr_ctrl8 &= (uint8_t)~FLD_TMR1_MODE;
        reg_tmr_ctrl8 |= FLD_TMR1_EN;
    }

    reg_mcu_wakeup_mask |= wakeup_src;
    reg_irq_mask &= ~(FLD_IRQ_TMR1_EN | FLD_IRQ_ZB_RT_EN);
    reg_pwdn_ctrl = FLD_PWDN_CTRL_SLEEP;

    if (sleep_us != 0u) {
        reg_tmr1_tick = 0;
        reg_tmr_ctrl8 &= (uint8_t)~FLD_TMR1_EN;
    }

    (void)reg_pm_misc_dummy;
    reg_tmr_sta = FLD_TMR_STA_TMR1;
    reg_rf_irq_status = 0xffffu;
    return reg_pm_misc_dummy;
}

void __attribute__((noinline, section(".text.soft_reboot_dly13ms_use24mRC"))) soft_reboot_dly13ms_use24mRC(void) {
    volatile uint32_t i = 0;
    while (i <= 0x3c8bu) {
        i++;
    }
}

void __attribute__((section(".text.cpu_set_gpio_wakeup"))) cpu_set_gpio_wakeup(GPIO_PinTypeDef pin, GPIO_LevelTypeDef pol, int en) {
    uint8_t bit = (uint8_t)((pin << 8) >> 24);
    uint8_t reg1 = (uint8_t)(((pin << 16) >> 24) + 0x21u);
    uint8_t v = analog_read(reg1);
    if (pol) {
        v = (uint8_t)(v & (uint8_t)~bit);
    } else {
        v = (uint8_t)(v | bit);
    }
    analog_write(reg1, v);

    uint8_t reg2 = (uint8_t)(((pin << 16) >> 24) + 0x27u);
    uint8_t v2 = analog_read(reg2);
    if (!en) {
        v2 = (uint8_t)(v2 & (uint8_t)~bit);
    } else {
        v2 = (uint8_t)(v2 | bit);
    }
    analog_write(reg2, v2);
}

void __attribute__((section(".text.cpu_wakeup_init"))) cpu_wakeup_init(void) {
    reg_rst0 = 0;
    reg_rst1 = 0;
    reg_rst2 = 0;

    reg_clk_en0 = 0xff;
    reg_clk_en1 = 0xff;
    reg_clk_en2 = 0xff;

    analog_write(areg_clk_setting, 0x64);
    analog_write(areg_pwdn_setting, 0x80);
    analog_write(areg_dcdc_ctrl, 0x38);
    analog_write(areg_ldo_trim, 0x02);
    analog_write(areg_clk_2m_rc, 0xa2);
    analog_write(areg_gpio_wakeup_en_pa, 0x00);
    analog_write(areg_gpio_wakeup_en_pb, 0x00);
    analog_write(areg_gpio_wakeup_en_pc, 0x00);
    analog_write(areg_gpio_wakeup_en_pd, 0x00);

    reg_dma_addrhi0_3 = 0x04040404u;
    reg_dma_addrhi4_7 = 0x04040404u;
    reg_dma_addrhi8 = 4;
    reg_gpio_table_reset = 0;

    if (reg_sram_shutdown_sel == 1) {
        analog_write(areg_dig_ldo_cap, 0x3c);
    } else {
        analog_write(areg_dig_ldo_cap, 0x4c);
    }

    uint8_t v7f = analog_read(areg_pm_status);
    if ((v7f & 1u) != 0) {
        pmParam.mcu_status = 3;
        uint8_t v3c = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(v3c & 0xfdu));
        if (pmParam.mcu_status > 1u) {
            reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
            pm_wait_xtal_ready();
            cpu_wakeup_no_deepretn_back_init();
        } else {
            pmParam.mcu_status = 0;
        }
    } else {
        pmParam.mcu_status = 1;
    }

    pmParam.wakeup_src = analog_read(areg_wakeup_src);
    uint8_t ws = (uint8_t)(pmParam.wakeup_src & 0x0au);
    pmParam.is_pad_wakeup = (uint8_t)(ws == 8u);

    if (pmParam.mcu_status == 1) {
        uint32_t t = pm_get_32k_tick();
        t = pm_tim_recover ? pm_tim_recover(t) : t;
        tick_cur = t;
        reg_system_tick_mode = 0;
        reg_system_tick_mode = 0x92;
        reg_system_tick_ctrl = pmParam.mcu_status;
        pm_wait_xtal_ready();
    } else {
        reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
        pm_wait_xtal_ready();
        cpu_wakeup_no_deepretn_back_init();
    }

    reg_dma_chn_en = 0;
    reg_dma_chn_irq_msk = 0;
    reg_gpio_wakeup_irq |= (FLD_GPIO_CORE_WAKEUP_EN | FLD_GPIO_CORE_INTERRUPT_EN);
}
