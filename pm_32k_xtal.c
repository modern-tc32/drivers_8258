#include <stdint.h>
#include "include/register.h"
#include "include/clock.h"
#include "include/pm.h"
#include "include/analog.h"
#include "include/timer.h"
#include "include/irq.h"


//system timer clock source is constant 16M, never change
//NOTICE:We think that the external 32k crystal clk is very accurate, does not need to read through the 750 and 751
//register, the conversion error(use 32k:16 cycle, count 16M sys tmr's ticks), at least the introduction of 64ppm.
#define CRYSTAL32768_TICK_PER_32CYCLE		15625  // 7812.5 * 2

#define areg_wakeup_status 0x44
#define WAKEUP_STATUS_ALL (WAKEUP_STATUS_COMPARATOR | WAKEUP_STATUS_TIMER_CORE | WAKEUP_STATUS_PAD)
extern uint32_t __divsi3(uint32_t a, uint32_t b);
extern uint32_t __udivsi3(uint32_t a, uint32_t b);
__attribute__((used, noinline)) static void switch_ext32kpad_to_int32krc(uint32_t mode);

__attribute__((used, noinline, section(".text.switch_ext32kpad_to_int32krc"))) static void switch_ext32kpad_to_int32krc(uint32_t mode) {
    ANA_SYS_DEEP_CLR(SYS_NEED_REINIT_EXT32K);
    analog_write(0x2d, 0x15);
    analog_write(0x05, 0x02);
    analog_write(0x2c, (uint8_t)(mode | 0x16u));
}

__attribute__((used, section(".text.cpu_sleep_wakeup_32k_xtal"))) int cpu_sleep_wakeup_32k_xtal(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, unsigned int wakeup_tick) {
    uint8_t irq = irq_disable();
    uint8_t timer_wakeup = (uint8_t)(wakeup_src & PM_WAKEUP_TIMER);
    uint32_t start = reg_system_tick;

    if (timer_wakeup) {
        uint32_t dt = wakeup_tick - start;
        if (dt > 0xE0000000u) {  //BIT(31)+BIT(30)+BIT(19)   7/8 cycle of 32bit
            irq_restore(irq);
            return (int)(analog_read(areg_wakeup_status) & WAKEUP_STATUS_ALL);
        }

        uint16_t min_wakeup_us = g_pm_early_wakeup_time_us.min;
        if (dt >= ((uint32_t)min_wakeup_us << 4)) {
            if (dt > 0x07feffffu) {
                pm_long_suspend = 1;
            } else {
                pm_long_suspend = timer_wakeup;
            }
        } else {
            analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);
            uint8_t st;
            do {
                st = (uint8_t)(analog_read(areg_wakeup_status) & WAKEUP_STATUS_ALL);
            } while (((reg_system_tick - start) < dt) && (st == 0u));
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

    uint16_t suspend_early_wakeup_us = g_pm_early_wakeup_time_us.suspend;
    uint16_t deep_early_wakeup_us = g_pm_early_wakeup_time_us.deep;
    uint32_t target;
    if (sleep_mode == DEEPSLEEP_MODE) {
        target = wakeup_tick - ((uint32_t)deep_early_wakeup_us << 4);
    } else {
        target = wakeup_tick - ((uint32_t)suspend_early_wakeup_us << 4);
    }

    analog_write(0x26, (uint8_t)wakeup_src);
    analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);
    uint8_t bak66 = reg_clk_sel;
    reg_clk_sel = 0;

    uint8_t sleep_mode_no_retention = (uint8_t)(sleep_mode & DEEPSLEEP_RETENTION_FLAG);
    uint8_t analog7_mode = 0;

    if (sleep_mode_no_retention) {
        uint8_t t2 = analog_read(0x02);
        analog_write(0x02, (uint8_t)((t2 & (uint8_t)~0x07u) | 0x05u));
        REG_ADDR8(0x63e) = tl_multi_addr;
        analog_write(0x7e, (uint8_t)sleep_mode);
        analog_write(0x2b, 0xde);
        analog7_mode = 5;
        {
            uint8_t sleep_mode_delta = (uint8_t)sleep_mode - DEEPSLEEP_MODE;
            /* Branchless form of (sleep_mode != DEEPSLEEP_MODE): 0 or 1. */
            uint8_t sleep_mode_not_deep = (uint8_t)((sleep_mode_delta | (uint8_t)(-((int8_t)sleep_mode_delta))) >> 7);
            analog_write(0x2c, (uint8_t)(0xD6u | sleep_mode_not_deep | (uint8_t)(sleep_mode_not_deep << 3)));
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
        ANA_SYS_DEEP_SET(SYS_DEEP_SLEEP_FLAG);
    }

    analog_write(0x20, 0x77);
    {
        uint16_t suspend_ret_delay_us = g_pm_r_delay_us.suspend_ret_r_delay_us;
        analog_write(0x1f, (uint8_t)__divsi3((((uint32_t)suspend_ret_delay_us << 8) + (CRYSTAL32768_TICK_PER_32CYCLE>>1)), CRYSTAL32768_TICK_PER_32CYCLE));
    }

    {
        uint32_t wake_tick;
        uint32_t d = target - tick_cur;
        if (pm_long_suspend) {
            wake_tick = target - (__udivsi3(d, CRYSTAL32768_TICK_PER_32CYCLE) << 5) + tick_32k_cur;
        } else {
            wake_tick = target - __udivsi3((d << 5) + (CRYSTAL32768_TICK_PER_32CYCLE>>1), CRYSTAL32768_TICK_PER_32CYCLE) + tick_32k_cur;
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

    if (sleep_mode == DEEPSLEEP_MODE) {
        ANA_SYS_DEEP_CLR(SYS_NEED_REINIT_EXT32K);
        ANA_SYS_DEEP_CLR(SYS_DEEP_SLEEP_FLAG);
        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

	unsigned int now_tick_32k = pm_get_32k_tick();
	{
		if(pm_long_suspend){
			tick_cur += (unsigned int)(now_tick_32k - tick_32k_cur) / 32 * CRYSTAL32768_TICK_PER_32CYCLE;
		}
		else{
			tick_cur += (unsigned int)(now_tick_32k - tick_32k_cur) * CRYSTAL32768_TICK_PER_32CYCLE / 32;		// current clock
		}
	}

	tick_32k_cur = tick_cur + 20 * CLOCK_16M_SYS_TIMER_CLK_1US;

    reg_system_tick_mode = 0;
    CLOCK_DLY_7_CYC;
    reg_system_tick_mode = 0x92;
    CLOCK_DLY_4_CYC;
    reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
    pm_wait_xtal_ready();

    reg_clk_sel = bak66;
    {
        uint8_t st = analog_read(areg_wakeup_status);
        if ((st & PM_WAKEUP_COMPARATOR) && timer_wakeup) {
            while ((reg_system_tick - wakeup_tick) > BIT(30)) {
            }
        }
        irq_restore(irq);
        return st ? (int)(st | STATUS_ENTER_SUSPEND) : STATUS_GPIO_ERR_NO_ENTER_PM;
    }
}

__attribute__((used, section(".text.pm_tim_recover_32k_xtal"))) unsigned int pm_tim_recover_32k_xtal(unsigned int tick_32k_now) {
    uint32_t deepRet_tick;
    if (pm_long_suspend) {
        deepRet_tick = tick_cur + (uint32_t)(tick_32k_now - tick_32k_cur) / 32u * CRYSTAL32768_TICK_PER_32CYCLE;
    } else {
        deepRet_tick = tick_cur + (uint32_t)(tick_32k_now - tick_32k_cur) * CRYSTAL32768_TICK_PER_32CYCLE / 32u;
    }
    return deepRet_tick;
}

__attribute__((used, section(".text.cpu_long_sleep_wakeup_32k_xtal"))) int cpu_long_sleep_wakeup_32k_xtal(SleepMode_TypeDef sleep_mode, SleepWakeupSrc_TypeDef wakeup_src, unsigned int wakeup_tick) {
    uint8_t irq = irq_disable();
    uint8_t wakeup_src_comparator = (uint8_t)((wakeup_src & PM_WAKEUP_COMPARATOR) != 0);
    uint32_t start = reg_system_tick;
    uint32_t wake_ticks = wakeup_tick;
    uint8_t timer_wakeup = (uint8_t)(wakeup_src & PM_WAKEUP_TIMER);

    if (timer_wakeup) {
        if (wake_ticks < 0x40u) {
            analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);
            uint32_t t = ((wake_ticks << 5) - wake_ticks);
            uint32_t budget = ((((t << 6) - t) << 3) + wake_ticks) >> 5;
            uint8_t st;
            do {
                st = (uint8_t)(analog_read(areg_wakeup_status) & WAKEUP_STATUS_ALL);
            } while (((reg_system_tick - start) < budget) && (st == 0u));
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

    uint32_t wake_m64 = wake_ticks - 0x40u;
    analog_write(0x26, (uint8_t)wakeup_src);
    analog_write(areg_wakeup_status, WAKEUP_STATUS_ALL);

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
        /* Branchless form of (sleep_mode != DEEPSLEEP_MODE): 0 or 1. */
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
            uint8_t ab = irq != 0;
            switch_ext32kpad_to_int32krc((uint8_t)(ab | 0xc0u | (ab << 3)));
            an7 = 5;
            mode2c = 0;
        } else {
            an7 = 5;
            mode2c = 0xc0;
        }
    }

    if (sleep_mode == SUSPEND_MODE) {
        analog_write(0x2c, (uint8_t)(0x80u | wakeup_src_comparator | PM_WAKEUP_TIMER | (wakeup_src_comparator << 3)));
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
        ANA_SYS_DEEP_SET(SYS_DEEP_SLEEP_FLAG);
    }

    analog_write(0x20, 0x77);
    {
        uint16_t suspend_ret_delay_us = g_pm_r_delay_us.suspend_ret_r_delay_us;
        analog_write(0x1f, (uint8_t)__divsi3((((uint32_t)suspend_ret_delay_us << 8) + (CRYSTAL32768_TICK_PER_32CYCLE>>1)), CRYSTAL32768_TICK_PER_32CYCLE));
    }

    {
        uint32_t wake_tick;
        uint32_t dt = reg_system_tick - start;
        if (pm_long_suspend) {
            wake_tick = wake_m64 + tick_cur - ((__udivsi3(dt, CRYSTAL32768_TICK_PER_32CYCLE)) << 5);
        } else {
            wake_tick = wake_m64 + tick_cur - __udivsi3((dt << 5) + (CRYSTAL32768_TICK_PER_32CYCLE>>1), CRYSTAL32768_TICK_PER_32CYCLE);
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

    if (sleep_mode != SUSPEND_MODE) {
        ANA_SYS_DEEP_CLR(SYS_NEED_REINIT_EXT32K);
        ANA_SYS_DEEP_CLR(SYS_DEEP_SLEEP_FLAG);

        soft_reboot_dly13ms_use24mRC();
        reg_pwdn_ctrl = FLD_PWDN_CTRL_REBOOT;
    }

    {
        uint32_t t32 = pm_get_32k_tick();
        uint32_t d = t32 - tick_32k_cur;
        if (pm_long_suspend) {
            d >>= 5;
            {
                uint32_t t = ((d << 5) - d);
                tick_cur += (((t << 6) - t) << 3) + d;
            }
        } else {
            uint32_t t = ((d << 5) - d);
            tick_cur += ((((t << 6) - t) << 3) + d) >> 5;
        }
        tick_32k_cur = tick_cur + 20 * 16;
    }

    reg_system_tick_mode = 0x00;
    CLOCK_DLY_6_CYC;
    reg_system_tick_mode = 0x90;
    CLOCK_DLY_5_CYC;
    reg_system_tick_ctrl = FLD_SYSTEM_TICK_START;
    pm_wait_xtal_ready();

    reg_clk_sel = bak66;
    {
        uint8_t st = analog_read(areg_wakeup_status);
        irq_restore(irq);
        return st ? (int)(st | STATUS_ENTER_SUSPEND) : STATUS_GPIO_ERR_NO_ENTER_PM;
    }
}
