#include <stdint.h>
#include "include/register.h"
#include "include/clock.h"
#include "include/pm.h"
#include "include/analog.h"

extern uint32_t __divsi3(uint32_t a, uint32_t b);
extern uint32_t __udivsi3(uint32_t a, uint32_t b);
__attribute__((used, noinline)) static void switch_ext32kpad_to_int32krc(uint32_t mode);

__attribute__((used, noinline, section(".text.switch_ext32kpad_to_int32krc"))) static void switch_ext32kpad_to_int32krc(uint32_t mode) {
    uint8_t v = analog_read(SYS_DEEP_ANA_REG);
    analog_write(SYS_DEEP_ANA_REG, (uint8_t)(v & 0xfeu));
    analog_write(0x2d, 0x15);
    analog_write(0x05, 0x02);
    analog_write(0x2c, (uint8_t)(mode | 0x16u));
}

__attribute__((used, section(".text.cpu_sleep_wakeup_32k_xtal"))) int cpu_sleep_wakeup_32k_xtal(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, unsigned int wakeup_tick) {
    uint8_t irq = reg_irq_en;
    reg_irq_en = 0;
    uint8_t timer_wakeup = (uint8_t)(wakeup_src & PM_WAKEUP_TIMER);
    uint32_t start = reg_system_tick;

    if (timer_wakeup) {
        uint32_t dt = wakeup_tick - start;
        if (dt > (0xe0u << 24)) {
            reg_irq_en = irq;
            return (int)(analog_read(0x44) & 0x0fu);
        }

        uint16_t e6 = ((volatile uint8_t *)&g_pm_early_wakeup_time_us)[6] |
                      ((uint16_t)((volatile uint8_t *)&g_pm_early_wakeup_time_us)[7] << 8);
        if (dt >= ((uint32_t)e6 << 4)) {
            if (dt > 0x07feffffu) {
                pm_long_suspend = 1;
            } else {
                pm_long_suspend = timer_wakeup;
            }
        } else {
            analog_write(0x44, 0x0f);
            while (((reg_system_tick - start) < dt) && ((analog_read(0x44) & 0x0fu) == 0u)) {
            }
            reg_irq_en = irq;
            return (int)(analog_read(0x44) & 0x0fu);
        }
    }

    if (func_before_suspend != 0) {
        if (((int (*)(void))func_before_suspend)() == 0) {
            reg_irq_en = irq;
            return 8;
        }
    }

    tick_cur = reg_system_tick + (0x8cu << 2);
    tick_32k_cur = pm_get_32k_tick();

    uint16_t e0 = ((volatile uint8_t *)&g_pm_early_wakeup_time_us)[0] |
                  ((uint16_t)((volatile uint8_t *)&g_pm_early_wakeup_time_us)[1] << 8);
    uint16_t e4 = ((volatile uint8_t *)&g_pm_early_wakeup_time_us)[4] |
                  ((uint16_t)((volatile uint8_t *)&g_pm_early_wakeup_time_us)[5] << 8);
    uint32_t target;
    if (sleep_mode == DEEPSLEEP_MODE) {
        target = wakeup_tick - ((uint32_t)e4 << 4);
    } else {
        target = wakeup_tick - ((uint32_t)e0 << 4);
    }

    analog_write(0x26, (uint8_t)wakeup_src);
    analog_write(0x44, 0x0f);
    uint8_t bak66 = reg_clk_sel;
    reg_clk_sel = 0;

    uint8_t sleep_mode_no_retention = (uint8_t)(sleep_mode & DEEPSLEEP_RETENTION_FLAG);
    uint8_t analog7_mode = 0;

    if (sleep_mode_no_retention != 0) {
        uint8_t t2 = analog_read(0x02);
        analog_write(0x02, (uint8_t)((t2 & (uint8_t)~0x07u) | 0x05u));
        REG_ADDR8(0x63e) = tl_multi_addr;
        analog_write(0x7e, (uint8_t)sleep_mode);
        analog_write(0x2b, 0xde);
        analog7_mode = 5;
        {
            uint8_t x = (uint8_t)((((uint8_t)sleep_mode - DEEPSLEEP_MODE) | (uint8_t)(-((int8_t)((uint8_t)sleep_mode - DEEPSLEEP_MODE)))) & 0xffu);
            analog_write(0x2c, (uint8_t)(0x16u | 0xc0u | x | (uint8_t)(x << 3)));
        }
    } else {
        analog_write(0x04, 0x48);
        analog_write(0x7e, 0x00);
        analog_write(0x2b, 0x5e);
        analog7_mode = 4;
        analog_write(0x2c, (uint8_t)(0x80u | (timer_wakeup ? 0x14u : 0x1du)));
    }

    {
        uint8_t r7 = analog_read(0x07);
        analog_write(0x07, (uint8_t)((r7 & (uint8_t)~0x07u) | analog7_mode));
    }

    if (sleep_mode_no_retention == 0) {
        REG_ADDR8(0x602) = 0x08;
        analog_write(0x7f, 1);
    } else {
        analog_write(0x7f, 0);
    }

    if (sleep_mode == DEEPSLEEP_MODE) {
            uint8_t r3c = analog_read(SYS_DEEP_ANA_REG);
            analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r3c | 0x02u));
    }

    analog_write(0x20, 0x77);
    {
        uint16_t hi = ((volatile uint8_t *)&g_pm_r_delay_us)[2] |
                      ((uint16_t)((volatile uint8_t *)&g_pm_r_delay_us)[3] << 8);
        analog_write(0x1f, (uint8_t)__divsi3((((uint32_t)hi << 8) + 0x1e84u), 0x3d09u));
    }

    {
        uint32_t wake_tick;
        uint32_t d = target - tick_cur;
        if (pm_long_suspend != 0) {
            wake_tick = target - (__udivsi3(d, 0x3d09u) << 5) + tick_32k_cur;
        } else {
            wake_tick = target - __udivsi3((d << 5) + 0x1e84u, 0x3d09u) + tick_32k_cur;
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
    if ((analog_read(0x44) & 0xf0u) == 0u) {
        sleep_start();
    }

    if (sleep_mode == DEEPSLEEP_MODE) {
        uint8_t r = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r & 0xfeu));
        r = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r & 0xfdu));
        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

    {
        uint32_t t32 = pm_get_32k_tick();
        uint32_t d = t32 - tick_32k_cur;
        if (pm_long_suspend != 0) {
            d >>= 5;
            {
                uint32_t t = ((d << 5) - d);
                tick_cur += (((t << 6) - t) << 3) + d;
            }
        } else {
            uint32_t t = ((d << 5) - d);
            tick_cur += ((((t << 6) - t) << 3) + d) >> 5;
        }
        tick_32k_cur = tick_cur + 0x140u;
    }

    reg_system_tick_mode = 0;
    CLOCK_DLY_6_CYC;
    reg_system_tick_mode = 0x92;
    CLOCK_DLY_5_CYC;
    reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
    pm_wait_xtal_ready();

    reg_clk_sel = bak66;
    {
        uint8_t st = analog_read(0x44);
        if ((st & PM_WAKEUP_COMPARATOR) && timer_wakeup) {
            while ((reg_system_tick - wakeup_tick) > (0x80u << 23)) {
            }
        }
        reg_irq_en = irq;
        if (st == 0) {
            return 0x100;
        }
        return (int)(st | (1u << 30));
    }
}

__attribute__((used, section(".text.pm_tim_recover_32k_xtal"))) unsigned int pm_tim_recover_32k_xtal(unsigned int tick_32k_now) {
    uint32_t d = tick_32k_now - tick_32k_cur;
    if (pm_long_suspend != 0) {
        d >>= 5;
        uint32_t t = ((d << 5) - d);
        d = (((t << 6) - t) << 3) + d;
        return d + tick_cur;
    }
    uint32_t t = ((d << 5) - d);
    d = (((t << 6) - t) << 3) + d;
    d >>= 5;
    return d + tick_cur;
}

__attribute__((used, section(".text.cpu_long_sleep_wakeup_32k_xtal"))) int cpu_long_sleep_wakeup_32k_xtal(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, unsigned int wakeup_tick) {
    uint8_t irq = reg_irq_en;
    reg_irq_en = 0;
    uint8_t wakeup_src_comparator = (uint8_t)(wakeup_src & PM_WAKEUP_COMPARATOR);
    uint32_t start = reg_system_tick;
    uint32_t wake_ticks = wakeup_tick;
    uint8_t timer_wakeup = (uint8_t)(wakeup_src & PM_WAKEUP_TIMER);

    if (timer_wakeup) {
        if (wake_ticks < 0x40u) {
            analog_write(0x44, 0x0f);
            uint32_t t = ((wake_ticks << 5) - wake_ticks);
            uint32_t budget = ((((t << 6) - t) << 3) + wake_ticks) >> 5;
            while (((reg_system_tick - start) < budget) && ((analog_read(0x44) & 0x0fu) == 0u)) {
            }
            reg_irq_en = irq;
            return (int)(analog_read(0x44) & 0x0fu);
        }
    }

    pm_long_suspend = 0;
    if (func_before_suspend != 0) {
        if (((int (*)(void))func_before_suspend)() == 0) {
            reg_irq_en = irq;
            return 8;
        }
    }

    tick_cur = reg_system_tick + (0x8cu << 2);
    tick_32k_cur = pm_get_32k_tick();

    uint32_t wake_m64 = wake_ticks - 0x40u;
    analog_write(0x26, (uint8_t)wakeup_src);
    analog_write(0x44, 0x0f);

    uint8_t bak66 = reg_clk_sel;
    reg_clk_sel = 0;

    uint8_t sleep_mode_no_retention = (uint8_t)(sleep_mode & DEEPSLEEP_RETENTION_FLAG);
    uint8_t an7 = 0;
    uint8_t mode2c = 0x1d;

    if (sleep_mode_no_retention) {
        uint8_t t2 = analog_read(0x02);
        analog_write(0x02, (uint8_t)((t2 & (uint8_t)~0x07u) | 0x05u));
        REG_ADDR8(0x63e) = tl_multi_addr;
        an7 = 5;
        mode2c = 0xde;
        sleep_mode = (uint8_t)(((sleep_mode - DEEPSLEEP_MODE) | (uint8_t)(-(int8_t)(sleep_mode - DEEPSLEEP_MODE))) & 0xffu);
    } else if (sleep_mode == SUSPEND_MODE) {
        analog_write(0x04, 0x48);
        analog_write(0x7e, 0x00);
        analog_write(0x2b, 0x5e);
        an7 = 4;
        mode2c = 0x1d;
    } else {
        analog_write(0x7e, 0x80);
        analog_write(0x2b, 0xde);
        sleep_mode_no_retention = 1;
        if (!timer_wakeup) {
            uint8_t ab = (uint8_t)((irq | (uint8_t)(-((int8_t)irq))) & 0xffu);
            switch_ext32kpad_to_int32krc((uint8_t)(ab | 0xc0u | (uint8_t)(ab << 3)));
            an7 = 5;
            mode2c = 0;
        } else {
            an7 = 5;
            mode2c = 0xc0;
        }
    }

    if (sleep_mode == SUSPEND_MODE) {
        analog_write(0x2c, (uint8_t)(0x80u | (wakeup_src_comparator ? 0x14u : 0x1du)));
    } else {
        analog_write(0x2c, (uint8_t)(0x16u | mode2c));
    }

    {
        uint8_t r7 = analog_read(0x07);
        analog_write(0x07, (uint8_t)((r7 & (uint8_t)~0x07u) | an7));
    }

    if (sleep_mode_no_retention == 0) {
        REG_ADDR8(0x602) = 0x08;
        analog_write(0x7f, 0x01);
    } else {
        analog_write(0x7f, 0x00);
    }

    if (sleep_mode != SUSPEND_MODE) {
        uint8_t r3c = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r3c | 0x02u));
    }

    analog_write(0x20, 0x77);
    {
        uint16_t hi = ((volatile uint8_t *)&g_pm_r_delay_us)[2] |
                      ((uint16_t)((volatile uint8_t *)&g_pm_r_delay_us)[3] << 8);
        uint32_t t = ((uint32_t)hi << 8) + 0x1e84u;
        analog_write(0x1f, (uint8_t)__divsi3(t, 0x3d09u));
    }

    {
        uint32_t wake_tick;
        uint32_t dt = reg_system_tick - start;
        if (pm_long_suspend != 0) {
            wake_tick = wake_m64 + tick_cur - ((__udivsi3(dt, 0x3d09u)) << 5);
        } else {
            wake_tick = wake_m64 + tick_cur - __udivsi3((dt << 5) + 0x1e84u, 0x3d09u);
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
    if ((analog_read(0x44) & 0xf0u) == 0u) {
        sleep_start();
    }

    if (sleep_mode != SUSPEND_MODE) {
        uint8_t r = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r & 0xfeu));
        r = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r & 0xfdu));
        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

    {
        uint32_t t32 = pm_get_32k_tick();
        uint32_t d = t32 - tick_32k_cur;
        if (pm_long_suspend != 0) {
            d >>= 5;
            {
                uint32_t t = ((d << 5) - d);
                tick_cur += (((t << 6) - t) << 3) + d;
            }
        } else {
            uint32_t t = ((d << 5) - d);
            tick_cur += ((((t << 6) - t) << 3) + d) >> 5;
        }
        tick_32k_cur = tick_cur + 0x140u;
    }

    reg_system_tick_mode = 0x00;
    CLOCK_DLY_6_CYC;
    reg_system_tick_mode = 0x90;
    CLOCK_DLY_5_CYC;
    reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
    pm_wait_xtal_ready();

    reg_clk_sel = bak66;
    {
        uint8_t st = analog_read(0x44);
        reg_irq_en = irq;
        if (st == 0) {
            return 0x100;
        }
        return (int)(st | (1u << 30));
    }
}
