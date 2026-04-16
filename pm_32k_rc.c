#include <stdint.h>
#include "include/register.h"
#include "include/clock.h"
#ifndef SYS_DEEP_ANA_REG
#define SYS_DEEP_ANA_REG 0x3c
#endif

typedef uint8_t SleepMode_TypeDef;
typedef uint8_t SleepWakeupSrc_TypeDef;
enum {
    SUSPEND_MODE = 0x00,
    DEEPSLEEP_MODE = 0x80,
    DEEPSLEEP_RETENTION_FLAG = 0x7f,
};


extern uint8_t analog_read(uint8_t addr);
extern void analog_write(uint8_t addr, uint8_t value);
extern uint32_t pm_get_32k_tick(void);
extern void pm_wait_xtal_ready(void);
extern void sleep_start(void);
extern void soft_reboot_dly13ms_use24mRC(void);
extern uint32_t __divsi3(uint32_t a, uint32_t b);
extern uint32_t __udivsi3(uint32_t a, uint32_t b);

extern volatile uint8_t pm_long_suspend;
extern volatile void *func_before_suspend;
extern volatile uint32_t tick_32k_cur;
extern volatile uint16_t tick_32k_calib;
extern volatile uint32_t tick_cur;
extern volatile uint8_t tl_multi_addr;
extern volatile uint32_t g_pm_r_delay_us;
extern volatile uint32_t g_pm_early_wakeup_time_us;

__attribute__((used, section(".text.cpu_sleep_wakeup_32k_rc"))) int cpu_sleep_wakeup_32k_rc(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, uint32_t wakeup_tick) {
    uint8_t irq = reg_irq_en;
    reg_irq_en = 0;
    uint8_t ws = wakeup_src;
    uint32_t wake_ticks = wakeup_tick;

    uint8_t has_timer = (uint8_t)(ws & 0x40u);

    while (tick_32k_calib == 0) {
    }
    uint16_t calib = tick_32k_calib;
    tick_32k_calib = calib;
    uint32_t t0 = reg_system_tick;

    if (has_timer) {
        uint32_t dt = wake_ticks - t0;
        if (dt > (0xe0u << 24)) {
            reg_irq_en = irq;
            return (int)(analog_read(0x44) & 0x0fu);
        }

        uint16_t ew6 = ((volatile uint8_t *)&g_pm_early_wakeup_time_us)[6] |
                       ((uint16_t)((volatile uint8_t *)&g_pm_early_wakeup_time_us)[7] << 8);
        uint32_t ew = ((uint32_t)ew6 << 4);

        if (dt >= ew) {
            if (dt > (0xffu << 20)) {
                pm_long_suspend = 1;
            } else {
                pm_long_suspend = 0;
            }
        } else {
            analog_write(0x44, 0x0f);
            while (((reg_system_tick - t0) < dt) && ((analog_read(0x44) & 0x0fu) == 0u)) {
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
    uint16_t e2 = ((volatile uint8_t *)&g_pm_early_wakeup_time_us)[2] |
                  ((uint16_t)((volatile uint8_t *)&g_pm_early_wakeup_time_us)[3] << 8);
    uint16_t early = sleep_mode ? e2 : e0;
    uint32_t target = wake_ticks - ((uint32_t)early << 4);

    analog_write(0x26, ws);
    analog_write(0x44, 0x0f);

    uint8_t bak66 = reg_clk_sel;
    reg_clk_sel = 0;

    uint8_t sm7 = (uint8_t)(sleep_mode & DEEPSLEEP_RETENTION_FLAG);
    uint8_t v7 = 0;
    uint8_t v2c_hi = 0;
    uint8_t v2b = 0xde;
    uint8_t v7e = sleep_mode;

    if (sm7 != 0) {
        uint8_t t2 = analog_read(0x02);
        analog_write(0x02, (uint8_t)((t2 & (uint8_t)~0x07u) | 0x05u));
        REG_ADDR8(0x63e) = tl_multi_addr;
        v7 = 5;
        v2c_hi = 0x40;
    } else if (sleep_mode == SUSPEND_MODE) {
        analog_write(0x04, 0x48);
        analog_write(0x7e, 0x00);
        v7 = 4;
        v2c_hi = 0x96;
        v2b = 0x5e;
    } else {
        v7 = 5;
        v2c_hi = 0xc0;
    }

    analog_write(0x7e, v7e);
    analog_write(0x2b, v2b);

    {
        uint8_t src80 = (uint8_t)(ws & 0x80u);
        uint8_t m = (uint8_t)((src80 | has_timer) | (uint8_t)(-((int8_t)(src80 | has_timer))));
        uint8_t m80 = (uint8_t)(src80 | (uint8_t)(-((int8_t)src80)));
        analog_write(0x2c, (uint8_t)(0x16u | v2c_hi | m | (uint8_t)(m80 << 3)));
    }

    {
        uint8_t r7 = analog_read(0x07);
        analog_write(0x07, (uint8_t)((r7 & (uint8_t)~0x07u) | v7));
    }

    if (sm7 == 0) {
        REG_ADDR8(0x602) = 0x08;
        analog_write(0x7f, 1);
    } else {
        analog_write(0x7f, 0);
    }

    {
        uint32_t half = (uint32_t)(calib >> 1);
        if (sleep_mode) {
            uint8_t r = analog_read(SYS_DEEP_ANA_REG);
            analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r | 0x02u));
            analog_write(0x20, (uint8_t)(0x7fu - (uint8_t)__divsi3(0xfa00u + half, (uint32_t)tick_32k_calib)));
        } else {
            analog_write(0x20, (uint8_t)(0x7fu - (uint8_t)__divsi3(0xfa00u + half, (uint32_t)tick_32k_calib)));
        }

        {
            uint16_t hi = ((volatile uint8_t *)&g_pm_r_delay_us)[2] |
                          ((uint16_t)((volatile uint8_t *)&g_pm_r_delay_us)[3] << 8);
            uint32_t p = ((uint32_t)hi << 7) + half;
            analog_write(0x1f, (uint8_t)__divsi3(p, (uint32_t)tick_32k_calib));
        }
    }

    {
        uint32_t wake_tick;
        uint32_t d = target - tick_cur;
        if (pm_long_suspend != 0) {
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
    if ((analog_read(0x44) & 0xf0u) == 0u) {
        sleep_start();
    }

    if (sleep_mode) {
        uint8_t r = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r & 0xfdu));
        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

    {
        uint32_t t32 = pm_get_32k_tick();
        if (pm_long_suspend != 0) {
            tick_cur += ((t32 - tick_32k_cur) >> 4) * (uint32_t)tick_32k_calib;
        } else {
            tick_cur += ((t32 - tick_32k_cur) * (uint32_t)tick_32k_calib) >> 4;
        }
        tick_32k_cur = tick_cur + 0x140u;
    }

    reg_system_tick_mode = 0;
    CLOCK_DLY_6_CYC;
    reg_system_tick_mode = 0x92;
    CLOCK_DLY_4_CYC;
    reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
    pm_wait_xtal_ready();

    reg_clk_sel = bak66;
    {
        uint8_t st = analog_read(0x44);
        if ((st & 0x80u) && has_timer) {
            while ((reg_system_tick - wake_ticks) > (0x80u << 23)) {
            }
        }
        reg_irq_en = irq;
        if (st == 0) {
            return 0x100;
        }
        return (int)(st | (1u << 30));
    }
}


__attribute__((used, section(".text.pm_tim_recover_32k_rc"))) uint32_t pm_tim_recover_32k_rc(uint32_t tick_32k_now) {
    if (pm_long_suspend) {
        uint32_t t = tick_32k_now - tick_32k_cur;
        t = (t >> 4) * (uint32_t)tick_32k_calib;
        return t + tick_cur;
    } else {
        uint32_t t = tick_32k_now - tick_32k_cur;
        t = (t * (uint32_t)tick_32k_calib) >> 4;
        return t + tick_cur;
    }
}

__attribute__((used, section(".text.pm_long_sleep_wakeup"))) int pm_long_sleep_wakeup(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, uint32_t sleep_duration_us) {
    uint8_t irq = reg_irq_en;
    reg_irq_en = 0;
    uint8_t sm = sleep_mode;
    uint32_t start_tick = reg_system_tick;
    uint16_t calib = REG_ADDR16(0x750);
    tick_32k_calib = calib;
    uint8_t has_timer = (uint8_t)(wakeup_src & 0x40u);

    if (has_timer) {
        if (sleep_duration_us < 0x40u) {
            analog_write(0x44, 0x0f);
            uint32_t t = ((sleep_duration_us << 5) - sleep_duration_us);
            uint32_t budget = (t << 2) + sleep_duration_us;
            while (((reg_system_tick - start_tick) < budget) && ((analog_read(0x44) & 0x0fu) == 0u)) {
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

    uint32_t minus64 = sleep_duration_us - 0x40u;
    analog_write(0x26, wakeup_src);
    analog_write(0x44, 0x0f);

    uint8_t bak66 = reg_clk_sel;
    reg_clk_sel = 0;

    uint8_t sm7 = (uint8_t)(sm & 0x7fu);
    uint8_t an7 = 0;
    uint8_t v2c_base = 0x16;

    if (sm7 != 0) {
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

    analog_write(0x7e, sm);

    {
        uint8_t src80 = (uint8_t)(wakeup_src & 0x80u);
        uint8_t x = (uint8_t)(src80 | (has_timer ? 0x40u : 0x00u));
        uint8_t abx = (uint8_t)((x | (uint8_t)(-(int8_t)x)) & 0xffu);
        uint8_t abs80 = (uint8_t)((src80 | (uint8_t)(-(int8_t)src80)) & 0xffu);
        uint8_t mode2c = (uint8_t)(v2c_base | abx | (uint8_t)(abs80 << 3));
        analog_write(0x2c, mode2c);
    }

    {
        uint8_t r7 = analog_read(0x07);
        analog_write(0x07, (uint8_t)((r7 & (uint8_t)~0x07u) | an7));
    }

    if (sm7 == 0) {
        REG_ADDR8(0x602) = 0x08;
        analog_write(0x7f, 0x01);
    } else {
        analog_write(0x7f, 0x00);
    }

    {
        uint32_t half_calib = (uint32_t)(calib >> 1);
        if (sm) {
            uint8_t r3c = analog_read(SYS_DEEP_ANA_REG);
            analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r3c | 0x02u));
        }

        analog_write(0x20, (uint8_t)(0x7fu - (uint8_t)__divsi3(0xfa00u + half_calib, (uint32_t)tick_32k_calib)));

        {
            uint16_t hi = (uint16_t)(g_pm_r_delay_us >> 16);
            uint32_t t = ((uint32_t)hi << 7) + half_calib;
            analog_write(0x1f, (uint8_t)__divsi3(t, (uint32_t)tick_32k_calib));
        }
    }

    {
        uint32_t wake_tick;
        uint32_t dt = reg_system_tick - start_tick;
        if (pm_long_suspend != 0) {
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
    if ((analog_read(0x44) & 0xf0u) == 0u) {
        sleep_start();
    }

    if (sm) {
        uint8_t r3c = analog_read(SYS_DEEP_ANA_REG);
        analog_write(SYS_DEEP_ANA_REG, (uint8_t)(r3c & 0xfdu));
        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

    {
        uint32_t t32 = pm_get_32k_tick();
        if (pm_long_suspend != 0) {
            tick_cur += (uint32_t)(((t32 - tick_32k_cur) >> 4) * (uint32_t)tick_32k_calib);
        } else {
            tick_cur += (uint32_t)(((t32 - tick_32k_cur) * (uint32_t)tick_32k_calib) >> 4);
        }
        tick_32k_cur = tick_cur + 0x140u;
    }

    reg_system_tick_mode = 0x00;
    CLOCK_DLY_6_CYC;
    reg_system_tick_mode = 0x90;
    CLOCK_DLY_4_CYC;
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
