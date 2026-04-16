#include <stdint.h>

extern uint8_t pm_get_info0(void);
volatile uint8_t pm_bit_info_0;
volatile uint8_t pm_bit_info_1;
volatile uint32_t pm_curr_stack;

__attribute__((used, noinline, section(".text.get_sp_normal"))) static void get_sp_normal(void) {
    volatile uint32_t keep_r1;
    pm_curr_stack = (uint32_t)&keep_r1;
}

__attribute__((used, section(".text.MYSEC"))) static void MYSEC(void) {
}

__attribute__((section(".text.normalSpGet"))) uint32_t normalSpGet(void) {
    get_sp_normal();
    return pm_curr_stack;
}

__attribute__((section(".text.efuse_sys_check"))) void efuse_sys_check(uint32_t v) {
    uint32_t info0 = pm_get_info0() & 0x0fu;
    if (info0 > 9u) {
        *(volatile uint8_t *)(uintptr_t)0x0080006fu = 0x20u;
        for (;;) {
        }
    }

    uint32_t sp_sel_1 = (v << 6) >> 30;
    pm_bit_info_1 = (uint8_t)sp_sel_1;
    uint32_t sp_sel_0 = v >> 29;
    pm_bit_info_0 = (uint8_t)sp_sel_0;

    uint32_t need_erase = 0;
    if ((v & 0xC0u) == 0xC0u) {
        need_erase = 1;
        if (sp_sel_1 <= 1u) {
            need_erase = (uint32_t)(((uint32_t)(v << 23)) >> 31);
        }
    }

    if ((need_erase | sp_sel_0) == 0u) {
        return;
    }

    get_sp_normal();
    uint32_t sp = pm_curr_stack;
    uint32_t upper = (sp + 100u) & ~0xffu;

    if (need_erase == 0u) {
        if (sp_sel_0 == 2u) {
            if (sp <= 0x00848000u) {
                return;
            }
        } else if (sp_sel_0 == 4u) {
            if (sp <= 0x0084c000u) {
                return;
            }
        } else {
            return;
        }
    }

    uint32_t p = (sp - 100u) & ~0xffu;
    while (p >= upper) {
        *(volatile uint32_t *)(uintptr_t)(p + 0x00800000u) = 0u;
        p += 16u;
    }
}
