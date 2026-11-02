#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host-only tests of the actual MTK IR callbacks, with mocked kernel resources.

Run with python3 mtk_pwm_ir_test.py. Requires a host C compiler with ASan/UBSan.
No LIRC device is opened and no signal is transmitted. This is not a hardware
or kernel-concurrency test. Extracted driver functions are compiled unchanged;
only kernel types, arithmetic macros and resource APIs are mocked below.
"""
import os
from pathlib import Path
import subprocess
import tempfile

KERNEL = Path(__file__).resolve().parents[4]
SOURCE = KERNEL / "drivers/misc/mediatek/irtx/mtk_irtx_pwm.c"

PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t dma_addr_t;
#define U16_MAX UINT16_MAX
#define U32_MAX UINT32_MAX
#define USEC_PER_SEC 1000000U
#define IR_MAX_DURATION 500000U
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_UP_ULL(n, d) DIV_ROUND_UP((u64)(n), (d))
#define DIV_ROUND_CLOSEST_ULL(n, d) (((u64)(n) + (d) / 2) / (d))
#define check_add_overflow(a, b, p) __builtin_add_overflow(a, b, p)
#define BIT(n) (1UL << (n))
#define GFP_KERNEL 0
#define PWM_MODE_MEMORY 0
#define CLK_DIV1 0
#define PWM_CLK_NEW_MODE_BLOCK 0
#define IDLE_FALSE 0
#define GUARD_FALSE 0
#define dev_err(...) do {} while (0)
struct device { int unused; };
struct platform_device { struct device dev; };
struct regulator { int unused; };
struct rc_dev { void *priv; };
struct pwm_spec_config {
    u32 pwm_no, mode, clk_div, clk_src, pmic_pad;
    struct {
        u32 IDLE_VALUE, GUARD_VALUE, STOP_BITPOS_VALUE;
        uint16_t HDURATION, LDURATION, GDURATION;
        dma_addr_t BUF0_BASE_ADDR;
        uint16_t BUF0_SIZE, WAVE_NUM;
    } PWM_MODE_MEMORY_REGS;
};
static int alloc_fail, enable_fail, pwm_fail, disable_fail;
static int allocations, enables, disables, starts, stops, sleeps, refs;
static u32 slept;
static size_t allocated;
static u32 *memory, *snapshot;
static struct pwm_spec_config programmed;
static void *dma_alloc_coherent(struct device *d, size_t n,
                               dma_addr_t *addr, int flags)
{
    (void)d; (void)flags;
    if (alloc_fail) return NULL;
    assert(!memory);
    memory = malloc(n);
    assert(memory);
    allocated = n;
    *addr = (dma_addr_t)memory;
    allocations++;
    return memory;
}
static void dma_free_coherent(struct device *d, size_t n,
                              void *p, dma_addr_t addr)
{
    (void)d;
    assert(memory == p && addr == (dma_addr_t)p && allocated == n);
    assert(starts == stops);
    free(snapshot);
    snapshot = malloc(n);
    assert(snapshot);
    memcpy(snapshot, p, n);
    free(p);
    memory = NULL;
    allocations--;
}
static int regulator_enable(struct regulator *r)
{
    (void)r; enables++;
    if (enable_fail) return enable_fail;
    refs++;
    return 0;
}
static int regulator_disable(struct regulator *r)
{
    (void)r; disables++;
    assert(refs > 0 && stops == starts);
    if (disable_fail) return disable_fail;
    refs--;
    return 0;
}
static int pwm_set_spec_config(struct pwm_spec_config *p)
{
    assert(refs > 0 && allocations == 1);
    programmed = *p;
    assert((size_t)(p->PWM_MODE_MEMORY_REGS.BUF0_SIZE + 1) * 4 == allocated);
    starts++;
    return pwm_fail;
}
static void mt_pwm_disable(u32 channel, u32 pad)
{
    assert(channel == programmed.pwm_no && pad == programmed.pmic_pad);
    stops++;
}
static void usleep_range(u32 lo, u32 hi)
{
    assert(lo > 0 && hi == lo + 100);
    sleeps++;
    slept = lo;
}
'''

TESTS = r'''
static struct platform_device pdev;
static struct regulator supply;
static struct mtk_pwm_ir state;
static struct rc_dev rc = { .priv = &state };
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while (0)
static void reset(void)
{
    assert(!memory && allocations == 0);
    free(snapshot); snapshot = NULL;
    alloc_fail = enable_fail = pwm_fail = disable_fail = 0;
    enables = disables = starts = stops = sleeps = refs = 0;
    slept = 0; allocated = 0;
    state = (struct mtk_pwm_ir) {
        .regulator = &supply, .pdev = &pdev,
        .carrier = IRTX_DEFAULT_CARRIER,
        .carrier_valid = true, .duty_valid = true,
    };
}
static u32 rounded_cycles(u32 duration, u32 carrier)
{
    return ((u64)duration * carrier + 500000) / 1000000;
}
static void waveform_test(u32 *pattern, u32 count, u32 invert)
{
    u32 pos = 0;
    reset();
    state.pwm_data_invert = invert;
    CHECK(mtk_pwm_ir_tx(&rc, pattern, count) == (int)count);
    CHECK(enables == 1 && disables == 1 && refs == 0);
    CHECK(starts == 1 && stops == 1 && sleeps == 1);
    u32 effective = count - (pattern[count - 1] == 1);
    for (u32 i = 0; i < effective; i++) {
        u32 n = rounded_cycles(pattern[i], state.carrier) * 3;
        for (u32 j = 0; j < n; j++, pos++) {
            u32 expected = ((i % 2 == 0 && j % 3 == 0) ^ invert);
            CHECK(((snapshot[pos / 32] >> (pos % 32)) & 1) == expected);
        }
    }
    for (; pos < allocated * 8; pos++)
        CHECK(((snapshot[pos / 32] >> (pos % 32)) & 1) == invert);
    u32 ticks = programmed.PWM_MODE_MEMORY_REGS.HDURATION + 1;
    CHECK(slept == (allocated * 8ULL * ticks * USEC_PER_SEC +
                   IRTX_PWM_CLOCK - 1) / IRTX_PWM_CLOCK);
}
int main(void)
{
    u32 ticks, pattern[] = {9000, 4500, 560, 560, 560};
    u32 padded[] = {9000, 4500, 560, 560, 1};
    u32 one[] = {1}, zero[] = {0}, max[] = {500000};
    u32 over[] = {500001}, sum_over[] = {250000, 250000, 2};
    u32 large[1025];
    reset();
    u32 low = DIV_ROUND_UP(IRTX_PWM_CLOCK, 3U * 65536U);
    u32 high = IRTX_PWM_CLOCK / 3U;
    CHECK(low == 133 && high == 8666666);
    u32 invalid[] = {0, 1, low - 1, high + 1, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) {
        CHECK(mtk_pwm_ir_set_carrier(&rc, invalid[i]) == -EINVAL);
        CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == -EINVAL);
        CHECK(allocations == 0 && starts == 0);
        CHECK(mtk_pwm_ir_set_carrier(&rc, 38000) == 0);
    }
    for (u32 c = low; c <= high; c++) {
        CHECK(mtk_pwm_ir_period(c, &ticks) == 0);
        CHECK(ticks >= 1 && ticks <= 65536);
    }
    for (u32 d = 0; d <= 100; d++) {
        CHECK(mtk_pwm_ir_set_duty_cycle(&rc, d) == (d == 33 ? 0 : -EINVAL));
        if (d != 33) CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == -EINVAL);
        CHECK(mtk_pwm_ir_set_duty_cycle(&rc, 33) == 0);
    }
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 0) == -EINVAL);
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 2) == -EINVAL);
    CHECK(mtk_pwm_ir_tx(&rc, large, 1025) == -EINVAL);
    CHECK(mtk_pwm_ir_tx(&rc, zero, 1) == -EINVAL);
    CHECK(mtk_pwm_ir_tx(&rc, over, 1) == -EINVAL);
    CHECK(mtk_pwm_ir_tx(&rc, sum_over, 3) == -EINVAL);
    CHECK(mtk_pwm_ir_tx(&rc, one, 1) == 1 && enables == 0);
    waveform_test(pattern, 5, 0);
    waveform_test(pattern, 5, 1);
    waveform_test(padded, 5, 0);
    waveform_test(padded, 5, 1);
    waveform_test(max, 1, 0);
    reset();
    CHECK(mtk_pwm_ir_set_carrier(&rc, low) == 0);
    CHECK(mtk_pwm_ir_tx(&rc, max, 1) == 1);
    CHECK(slept >= 500000 && refs == 0);
    reset();
    CHECK(mtk_pwm_ir_set_carrier(&rc, high) == 0);
    CHECK(mtk_pwm_ir_tx(&rc, max, 1) == -EINVAL && enables == 0);
    /* Adjacent whole-microsecond inputs straddling the DMA word cap. */
    u32 dma_edge[] = {80658}, dma_over[] = {80659};
    CHECK(mtk_pwm_ir_tx(&rc, dma_edge, 1) == 1);
    CHECK(programmed.PWM_MODE_MEMORY_REGS.BUF0_SIZE == U16_MAX);
    CHECK(allocated == 262144);
    CHECK(mtk_pwm_ir_tx(&rc, dma_over, 1) == -EINVAL);
    reset();
    for (u32 i = 0; i < 1023; i++) large[i] = 100;
    CHECK(mtk_pwm_ir_tx(&rc, large, 1023) == 1023);
    reset(); alloc_fail = 1;
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == -ENOMEM && enables == 0);
    reset(); enable_fail = -EIO;
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == -EIO);
    CHECK(disables == 0 && starts == 0 && allocations == 0);
    reset(); pwm_fail = -EIO;
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == -EIO);
    CHECK(disables == 1 && refs == 0 && starts == stops && sleeps == 0);
    reset(); disable_fail = -EBUSY;
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == -EBUSY);
    CHECK(disables == 1 && allocations == 0 && starts == stops);
    reset(); pwm_fail = -EIO; disable_fail = -EBUSY;
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == -EIO);
    CHECK(disables == 1 && allocations == 0);
    reset(); refs = 1; /* Another consumer owns the existing enable. */
    CHECK(mtk_pwm_ir_tx(&rc, pattern, 5) == 5 && refs == 1);
    reset();
    printf("PASS: %u checks; real driver callbacks, ASan/UBSan, mocked hardware\n", checks);
    return 0;
}
'''


def main():
    source = SOURCE.read_text()
    state = source[source.index("#define DRIVER_NAME"):source.index("/* HDURATION")]
    callbacks = source[source.index("/* HDURATION"):source.index("static int mtk_pwm_ir_probe")]
    with tempfile.TemporaryDirectory(prefix="mtk-pwm-ir-") as temp:
        c_file = Path(temp) / "test.c"
        binary = Path(temp) / "test"
        c_file.write_text(PREAMBLE + state + callbacks + TESTS)
        command = [os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
                   "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                   "-fno-omit-frame-pointer", str(c_file), "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
