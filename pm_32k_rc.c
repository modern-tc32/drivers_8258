#include <stdint.h>
#include "include/register.h"
#include "include/clock.h"
#include "include/pm.h"
#include "include/analog.h"
#include "include/timer.h"
#include "include/irq.h"

#define areg_wakeup_status 0x44
#define WAKEUP_STATUS_ALL (WAKEUP_STATUS_COMPARATOR | WAKEUP_STATUS_TIMER_CORE | WAKEUP_STATUS_PAD)

extern uint32_t __divsi3(uint32_t a, uint32_t b);
extern uint32_t __udivsi3(uint32_t a, uint32_t b);

__attribute__((used, section(".text.cpu_sleep_wakeup_32k_rc"))) int cpu_sleep_wakeup_32k_rc(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, unsigned int wakeup_tick) {
    uint8_t sleep_mode_u8 = (uint8_t)sleep_mode;
    uint8_t wakeup_src_u8 = (uint8_t)wakeup_src;
    uint8_t irq = irq_disable();
    uint32_t wake_ticks = wakeup_tick;

    uint8_t timer_wakeup = (uint8_t)(wakeup_src_u8 & PM_WAKEUP_TIMER);

    while (tick_32k_calib == 0) {
    }
    uint16_t calib = tick_32k_calib;
    tick_32k_calib = calib;
    uint32_t t0 = reg_system_tick;

    if (timer_wakeup) {
        uint32_t dt = wake_ticks - t0;
        if (dt > 0xE0000000){ //BIT(31)+BIT(30)+BIT(19)   7/8 cycle of 32bit, 268.44*7/8 = 234.88 S
            irq_restore(irq);
            return (int)(analog_read(areg_wakeup_status) & WAKEUP_STATUS_ALL);
        }

        /* Intentional: use struct fields (not byte-wise loads). Keep as-is. */
        uint16_t min_wakeup_us = g_pm_early_wakeup_time_us.min;
        uint32_t ew = ((uint32_t)min_wakeup_us << 4);

        if (dt >= ew) {
            if (dt > (0xffu << 20)) {
                pm_long_suspend = 1;
            } else {
                pm_long_suspend = 0;
            }
        } else {
            analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);
            uint8_t st;
            do {
                st = (uint8_t)(analog_read(areg_wakeup_status) & WAKEUP_STATUS_ALL);
            } while (((reg_system_tick - t0) < dt) && (st == 0u));
            irq_restore(irq);
            return (int)st;
        }
    }

    if (func_before_suspend != 0) {
        if (func_before_suspend() == 0) {
            irq_restore(irq);
            return WAKEUP_STATUS_PAD;
        }
    }

    tick_cur = reg_system_tick + (0x8cu << 2);
    tick_32k_cur = pm_get_32k_tick();

    /* Intentional: use struct fields for early-wakeup timings. */
    uint16_t suspend_early_wakeup_us = g_pm_early_wakeup_time_us.suspend;
    uint16_t deep_ret_early_wakeup_us = g_pm_early_wakeup_time_us.deep_ret;
    uint16_t early = sleep_mode_u8 ? deep_ret_early_wakeup_us : suspend_early_wakeup_us;
    uint32_t target = wake_ticks - ((uint32_t)early << 4);

    analog_write(0x26, wakeup_src_u8);
    analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);

    uint8_t bak66 = reg_clk_sel;
    reg_clk_sel = 0;

    uint8_t sleep_mode_no_retention = (uint8_t)(sleep_mode_u8 & DEEPSLEEP_RETENTION_FLAG);
    uint8_t analog7_mode = 0;
    uint8_t analog2c_high_bits = 0;
    uint8_t analog2b_value = 0xde;
    uint8_t analog7e_value = sleep_mode_u8;

    if (sleep_mode_no_retention != 0) {
        uint8_t t2 = analog_read(0x02);
        analog_write(0x02, (uint8_t)((t2 & (uint8_t)~0x07u) | 0x05u));
        REG_ADDR8(0x63e) = tl_multi_addr;
        analog7_mode = 5;
        analog2c_high_bits = 0x40;
    } else if (sleep_mode_u8 == SUSPEND_MODE) {
        analog_write(0x04, 0x48);
        analog_write(0x7e, 0x00);
        analog7_mode = 4;
        analog2c_high_bits = 0x96;
        analog2b_value = 0x5e;
    } else {
        analog7_mode = 5;
        analog2c_high_bits = 0xc0;
    }

    analog_write(0x7e, analog7e_value);
    analog_write(0x2b, analog2b_value);

    uint8_t cmp = (wakeup_src_u8 & PM_WAKEUP_COMPARATOR) != 0;
    uint8_t any = cmp | timer_wakeup;

    analog_write(0x2c,
        0x16 |
        analog2c_high_bits |
        any |
        (cmp << 3));

    analog_write(0x07, (analog_read(0x07) & ~0x07) | analog7_mode);

    if (sleep_mode_no_retention == 0) {
        REG_ADDR8(0x602) = 0x08;
        analog_write(0x7f, 1);
    } else {
        analog_write(0x7f, 0);
    }

    {
        uint32_t half = (uint32_t)(calib >> 1);
        if (sleep_mode_u8) {
            ANA_SYS_DEEP_SET(SYS_DEEP_SLEEP_FLAG);
            analog_write(0x20, (uint8_t)(0x7fu - (uint8_t)__divsi3(0xfa00u + half, (uint32_t)tick_32k_calib)));
        } else {
            analog_write(0x20, (uint8_t)(0x7fu - (uint8_t)__divsi3(0xfa00u + half, (uint32_t)tick_32k_calib)));
        }

        {
            uint16_t suspend_ret_delay_us = g_pm_r_delay_us.suspend_ret_r_delay_us;
            uint32_t p = ((uint32_t)suspend_ret_delay_us << 7) + half;
            analog_write(0x1f, (uint8_t)__divsi3(p, (uint32_t)tick_32k_calib));
        }
    }

    {
        uint32_t wake_tick;
        uint32_t d = target - tick_cur;
        if (pm_long_suspend) {
            wake_tick = target - (__udivsi3(d, (uint32_t)tick_32k_calib) << 4) + tick_32k_cur;
        } else {
            wake_tick = target - __udivsi3((d << 4) + (uint32_t)(calib >> 1), (uint32_t)tick_32k_calib) + tick_32k_cur;
        }

        reg_system_tick_mode = 0x2c;
        REG_ADDR32(0x754) = wake_tick;
        reg_system_tick_ctrl = 0x08;
        CLOCK_DLY_10_CYC;
        CLOCK_DLY_6_CYC;
        while (reg_system_tick_ctrl & 0x08u) {
        }
    }

    reg_system_tick_mode = 0x20;
    if ((analog_read(areg_wakeup_status) & (uint8_t)~WAKEUP_STATUS_ALL) == 0u) {
        sleep_start();
    }

    if (sleep_mode_u8) {
        ANA_SYS_DEEP_CLR(SYS_DEEP_SLEEP_FLAG);
        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

    {
        uint32_t t32 = pm_get_32k_tick();
        if (pm_long_suspend) {
            tick_cur += ((t32 - tick_32k_cur) >> 4) * (uint32_t)tick_32k_calib;
        } else {
            tick_cur += ((t32 - tick_32k_cur) * (uint32_t)tick_32k_calib) >> 4;
        }
        tick_32k_cur = tick_cur + 20 * CLOCK_16M_SYS_TIMER_CLK_1US;
    }

    reg_system_tick_mode = 0;
    CLOCK_DLY_6_CYC;
    reg_system_tick_mode = 0x92;
    CLOCK_DLY_4_CYC;
    reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
    pm_wait_xtal_ready();

    reg_clk_sel = bak66;
    {
        uint8_t st = analog_read(areg_wakeup_status);
        if ((st & PM_WAKEUP_COMPARATOR) && timer_wakeup) {
            while ((reg_system_tick - wake_ticks) > (0x80u << 23)) {
            }
        }
        irq_restore(irq);
        return st ? (int)(st | STATUS_ENTER_SUSPEND) : STATUS_GPIO_ERR_NO_ENTER_PM;
    }
}


__attribute__((used, section(".text.pm_tim_recover_32k_rc"))) unsigned int pm_tim_recover_32k_rc(unsigned int tick_32k_now) {
    uint32_t deepRet_tick;
    if (pm_long_suspend) {
        deepRet_tick = tick_cur + (uint32_t)(tick_32k_now - tick_32k_cur) / 16u * tick_32k_calib;
    } else {
        deepRet_tick = tick_cur + (uint32_t)(tick_32k_now - tick_32k_cur) * tick_32k_calib / 16u;
    }
    return deepRet_tick;
}

__attribute__((used, section(".text.pm_long_sleep_wakeup"))) int pm_long_sleep_wakeup(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, unsigned int sleep_duration_us) {
    uint8_t irq = irq_disable();
    uint32_t start_tick = reg_system_tick;
    uint16_t calib = REG_ADDR16(0x750);
    tick_32k_calib = calib;
    uint8_t has_timer = (uint8_t)((wakeup_src & PM_WAKEUP_TIMER) != 0);

    if (has_timer) {
        if (sleep_duration_us < 0x40u) {
            analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);
            uint32_t t = ((sleep_duration_us << 5) - sleep_duration_us);
            uint32_t budget = (t << 2) + sleep_duration_us;
            uint8_t st;
            do {
                st = (uint8_t)(analog_read(areg_wakeup_status) & WAKEUP_STATUS_ALL);
            } while (((reg_system_tick - start_tick) < budget) && (st == 0u));
            irq_restore(irq);
            return (int)st;
        }
    }

    pm_long_suspend = 0;
    if (func_before_suspend != 0) {
        if (func_before_suspend() == 0) {
            irq_restore(irq);
            return WAKEUP_STATUS_PAD;
        }
    }

    tick_cur = reg_system_tick + (0x8cu << 2);
    tick_32k_cur = pm_get_32k_tick();

    uint32_t minus64 = sleep_duration_us - 0x40u;
    analog_write(0x26, wakeup_src);
    analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);

    uint8_t bak66 = reg_clk_sel;
    reg_clk_sel = 0;

    uint8_t sleep_mode_no_retention = (uint8_t)(sleep_mode & DEEPSLEEP_RETENTION_FLAG);
    uint8_t an7 = 0;
    uint8_t v2c_base = 0x16;

    if (sleep_mode_no_retention != 0) {
        uint8_t t2 = analog_read(0x02);
        analog_write(0x02, (uint8_t)((t2 & (uint8_t)~0x07u) | 0x05u));
        REG_ADDR8(0x63e) = tl_multi_addr;
        an7 = 5;
        v2c_base = 0x56;
        analog_write(0x2b, 0xde);
    } else {
        analog_write(0x04, 0x48);
        analog_write(0x7e, 0x00);
        an7 = 4;
        v2c_base = 0x96;
        analog_write(0x2b, 0x5e);
    }

    analog_write(0x7e, (uint8_t)sleep_mode);

    {
        uint8_t cmp = (wakeup_src & PM_WAKEUP_COMPARATOR) != 0;
        uint8_t any = cmp | has_timer;
        analog_write(0x2c, (uint8_t)(v2c_base | any | (cmp << 3)));
    }

    analog_write(0x07, (analog_read(0x07) & ~0x07) | an7);

    if (sleep_mode_no_retention == 0) {
        REG_ADDR8(0x602) = 0x08;
        analog_write(0x7f, 0x01);
    } else {
        analog_write(0x7f, 0x00);
    }

    {
        uint32_t half_calib = (uint32_t)(calib >> 1);
        if (sleep_mode) {
            ANA_SYS_DEEP_SET(SYS_DEEP_SLEEP_FLAG);
        }

        analog_write(0x20, (uint8_t)(0x7fu - (uint8_t)__divsi3(0xfa00u + half_calib, (uint32_t)tick_32k_calib)));

        {
            uint16_t suspend_ret_delay_us = g_pm_r_delay_us.suspend_ret_r_delay_us;
            uint32_t t = ((uint32_t)suspend_ret_delay_us << 7) + half_calib;
            analog_write(0x1f, (uint8_t)__divsi3(t, (uint32_t)tick_32k_calib));
        }
    }

    {
        uint32_t wake_tick;
        uint32_t dt = reg_system_tick - start_tick;
        if (pm_long_suspend) {
            uint32_t base = minus64 + tick_cur;
            uint32_t q = __udivsi3(dt, (uint32_t)tick_32k_calib);
            wake_tick = base - (q << 4);
        } else {
            wake_tick = minus64 + tick_cur - __udivsi3((dt << 4) + (uint32_t)(calib >> 1), (uint32_t)tick_32k_calib);
        }

        reg_system_tick_mode = 0x2c;
        REG_ADDR32(0x754) = wake_tick;
        reg_system_tick_ctrl = 0x08;
        CLOCK_DLY_10_CYC;
        CLOCK_DLY_6_CYC;
        while (reg_system_tick_ctrl & 0x08u) {
        }
    }

    reg_system_tick_mode = 0x20;
    if ((analog_read(areg_wakeup_status) & (uint8_t)~WAKEUP_STATUS_ALL) == 0u) {
        sleep_start();
    }

    if (sleep_mode) {
        ANA_SYS_DEEP_CLR(SYS_DEEP_SLEEP_FLAG);
        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

    {
        uint32_t t32 = pm_get_32k_tick();
        if (pm_long_suspend) {
            tick_cur += (uint32_t)(((t32 - tick_32k_cur) >> 4) * (uint32_t)tick_32k_calib);
        } else {
            tick_cur += (uint32_t)(((t32 - tick_32k_cur) * (uint32_t)tick_32k_calib) >> 4);
        }
        tick_32k_cur = tick_cur + 20 * CLOCK_16M_SYS_TIMER_CLK_1US;
    }

    reg_system_tick_mode = 0x00;
    CLOCK_DLY_6_CYC;
    reg_system_tick_mode = 0x90;
    CLOCK_DLY_4_CYC;
    reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
    pm_wait_xtal_ready();

    reg_clk_sel = bak66;
    {
        uint8_t st = analog_read(areg_wakeup_status);
        irq_restore(irq);
        return st ? (int)(st | STATUS_ENTER_SUSPEND) : STATUS_GPIO_ERR_NO_ENTER_PM;
    }
}
