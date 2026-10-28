/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include "../public/drivers/misc/mediatek/performance/fpsgo_v3/base/src/fpsgo_cpu_policy.c"

static struct cpufreq_frequency_table tables[3][5] = {
    {{1000}, {CPUFREQ_ENTRY_INVALID}, {3000}, {2000}, {CPUFREQ_TABLE_END}},
    {{16}, {0x80000000U}, {CPUFREQ_ENTRY_INVALID}, {CPUFREQ_TABLE_END}},
    {{4000}, {2000}, {CPUFREQ_TABLE_END}}
};

static void setup(void)
{
    assert(!alloc_live && !cpu_read_depth && !policy_num);
    memset(model_policies, 0, sizeof(model_policies));
    const uint64_t masks[] = {(1ULL << 0) | (1ULL << 4), (1ULL << 1) | (1ULL << 7), (1ULL << 2) | (1ULL << 6)};
    for (int i = 0; i < 3; i++) {
        model_policies[i].present = true;
        model_policies[i].mask.bits = masks[i];
        model_policies[i].related_cpus = &model_policies[i].mask;
        model_policies[i].freq_table = tables[i];
    }
    alloc_fail = -1;
}

static void refs_balanced(void)
{
    assert(!cpu_read_depth);
    for (int i = 0; i < 3; i++) assert(!model_policies[i].refs);
}

int main(void)
{
    /* Every one of four root allocations and three per-domain tables fails. */
    for (int fail = 0; fail < 7; fail++) {
        setup();
        alloc_fail = fail;
        assert(fpsgo_cpu_policy_init() == -ENOMEM);
        assert(!fpsgo_get_cpu_policy_num());
        assert(!alloc_live && !opp_count && !opp_table && !policy_masks && !cpu_policy_id);
        refs_balanced();
    }
    setup();
    for (int i = 0; i < 3; i++) model_policies[i].present = false;
    assert(fpsgo_cpu_policy_init() == -ENODEV);
    assert(!alloc_live);
    refs_balanced();
    setup();
    model_policies[1].freq_table = NULL;
    assert(fpsgo_cpu_policy_init() == -EINVAL);
    assert(!alloc_live);
    refs_balanced();
    setup();
    unsigned int saved = tables[0][0].frequency;
    tables[0][0].frequency = 0;
    assert(fpsgo_cpu_policy_init() == -EINVAL);
    tables[0][0].frequency = saved;
    assert(!alloc_live);
    refs_balanced();
    setup();
    struct cpufreq_frequency_table empty[] = {{CPUFREQ_ENTRY_INVALID}, {CPUFREQ_TABLE_END}};
    model_policies[2].freq_table = empty;
    assert(fpsgo_cpu_policy_init() == -EINVAL);
    assert(!alloc_live);
    refs_balanced();

    setup();
    assert(fpsgo_cpu_policy_init() == 0);
    refs_balanced();
    assert(fpsgo_cpu_policy_init() == -EBUSY);
    assert(fpsgo_get_cpu_policy_num() == 3);
    int expected[] = {0, 1, 2, -1, 0, -1, 2, 1};
    for (int i = 0; i < 8; i++) assert(fpsgo_get_cpu_policy_id(i) == expected[i]);
    assert(fpsgo_get_cpu_policy_id(8) == -ENODEV);
    assert(fpsgo_get_cpu_policy_cpu(3) == -ENODEV);
    assert(!fpsgo_get_cpu_policy_mask(3));
    assert(!fpsgo_cpu_policy_matches(0, NULL));
    assert(!fpsgo_cpu_policy_matches(3, &model_policies[0].mask));
    for (int i = 0; i < 3; i++) {
        assert(fpsgo_get_cpu_policy_cpu(i) == i);
        assert(cpumask_equal(fpsgo_get_cpu_policy_mask(i), &model_policies[i].mask));
        assert(fpsgo_cpu_policy_matches(i, &model_policies[i].mask));
        assert(!fpsgo_cpu_policy_matches(i, &model_policies[(i + 1) % 3].mask));
    }
    assert(opp_count[0] == 3 && opp_count[1] == 2 && opp_count[2] == 2);
    assert(opp_table[0][0] == 3000 && opp_table[0][1] == 2000 && opp_table[0][2] == 1000);
    assert(opp_table[1][0] == 0x80000000U && opp_table[1][1] == 16);
    assert(fpsgo_get_cpu_policy_opp_count(3) == 0);
    assert(fpsgo_get_cpu_policy_freq(3, 0) == 0);
    assert(fpsgo_get_cpu_policy_freq(0, 3) == 0);
    for (int i = 0; i < 3; i++) {
        assert(fpsgo_get_cpu_policy_opp_count(i) == opp_count[i]);
        for (int j = 0; j < opp_count[i]; j++)
            assert(fpsgo_get_cpu_policy_freq(i, j) == opp_table[i][j]);
    }
    /* Snapshot lookups remain immutable and take no live policy references. */
    model_policies[0].mask.bits = 0;
    assert(fpsgo_get_cpu_policy_id(4) == 0);
    assert(fpsgo_get_cpu_policy_mask(0)->bits == ((1ULL << 0) | (1ULL << 4)));
    assert(!fpsgo_cpu_policy_matches(0, &model_policies[0].mask));
    model_policies[0].mask.bits = (1ULL << 0) | (1ULL << 4);
    int live = alloc_live;
    for (int fail = 0; fail < 5; fail++) {
        int *counts = (void *)1;
        unsigned int **copy = (void *)1;
        alloc_fail = fail;
        assert(fpsgo_get_cpu_opp_info(&counts, &copy) == -ENOMEM);
        assert(!counts && !copy && alloc_live == live);
    }
    alloc_fail = -1;
    assert(fpsgo_get_cpu_opp_info(NULL, NULL) == -EINVAL);
    int *counts;
    unsigned int **copy;
    assert(fpsgo_get_cpu_opp_info(&counts, &copy) == 0);
    for (int i = 0; i < 3; i++) {
        assert(counts[i] == opp_count[i] && copy[i] != opp_table[i]);
        assert(!memcmp(copy[i], opp_table[i], counts[i] * sizeof(unsigned int)));
    }
    copy[0][0] = 1;
    assert(opp_table[0][0] == 3000);
    free_opp_snapshot(3, counts, copy);
    assert(alloc_live == live);
    fpsgo_cpu_policy_exit();
    fpsgo_cpu_policy_exit();
    assert(!alloc_live && !policy_num);
    counts = (void *)1; copy = (void *)1;
    assert(fpsgo_get_cpu_opp_info(&counts, &copy) == -ENODEV && !counts && !copy);
    assert(fpsgo_get_cpu_policy_id(0) == -ENODEV);
    assert(!fpsgo_get_cpu_policy_mask(0));
    refs_balanced();
    puts("FPSGO CPU-policy production C: sparse/interleaved domains, valid OPPs, immutable lookups, all allocation failures and deep-copy ownership PASS");
    return 0;
}
