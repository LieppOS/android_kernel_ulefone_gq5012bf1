/* SPDX-License-Identifier: GPL-2.0 */
/* Test-only API model; production fpsgo_cpu_policy.c is compiled unchanged. */
#ifndef FPSGO_CPU_POLICY_MODEL_H
#define FPSGO_CPU_POLICY_MODEL_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
typedef struct cpumask { uint64_t bits; } cpumask_t;
static const unsigned int nr_cpu_ids = 8;
static void cpumask_clear(cpumask_t *m) { m->bits = 0; }
static bool cpumask_test_cpu(unsigned int cpu, const cpumask_t *m) { assert(cpu < 64); return m->bits & (1ULL << cpu); }
static void cpumask_copy(cpumask_t *a, const cpumask_t *b) { a->bits = b->bits; }
static void cpumask_or(cpumask_t *a, const cpumask_t *b, const cpumask_t *c) { a->bits = b->bits | c->bits; }
static bool cpumask_equal(const cpumask_t *a, const cpumask_t *b) { return a->bits == b->bits; }
static unsigned int cpumask_first(const cpumask_t *a) { return a->bits ? __builtin_ctzll(a->bits) : nr_cpu_ids; }
#define for_each_possible_cpu(cpu) for ((cpu) = 0; (cpu) < nr_cpu_ids; (cpu)++)
#define GFP_KERNEL 0
static int alloc_live, alloc_fail = -1, cpu_read_depth;
static void *model_alloc(size_t count, size_t size) {
    if (alloc_fail == 0) return NULL;
    if (alloc_fail > 0) alloc_fail--;
    assert(!size || count <= SIZE_MAX / size);
    void *p = calloc(count, size);
    if (p) alloc_live++;
    return p;
}
#define kcalloc(n,s,g) model_alloc(n,s)
#define kmalloc_array(n,s,g) model_alloc(n,s)
static void *kmemdup(const void *src, size_t bytes, int flags) {
    void *p = model_alloc(1, bytes);
    if (p) memcpy(p, src, bytes);
    return p;
}
static void kfree(void *p) { if (p) { assert(alloc_live > 0); alloc_live--; free(p); } }
#define CPUFREQ_ENTRY_INVALID UINT_MAX
#define CPUFREQ_TABLE_END (UINT_MAX-1)
struct cpufreq_frequency_table { unsigned int frequency; };
struct cpufreq_policy {
    cpumask_t mask;
    const cpumask_t *related_cpus;
    struct cpufreq_frequency_table *freq_table;
    int refs;
    bool present;
};
static struct cpufreq_policy model_policies[3];
static void cpus_read_lock(void) { assert(!cpu_read_depth); cpu_read_depth++; }
static void cpus_read_unlock(void) { assert(cpu_read_depth == 1); cpu_read_depth--; }
static struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu) {
    assert(cpu_read_depth == 1);
    for (int i = 0; i < 3; i++)
        if (model_policies[i].present && cpumask_test_cpu(cpu, &model_policies[i].mask)) {
            model_policies[i].refs++;
            return &model_policies[i];
        }
    return NULL;
}
static void cpufreq_cpu_put(struct cpufreq_policy *p) { assert(p->refs > 0); p->refs--; }
#define cpufreq_for_each_valid_entry(pos,tbl) \
    for ((pos) = (tbl); (pos)->frequency != CPUFREQ_TABLE_END; (pos)++) \
        if ((pos)->frequency != CPUFREQ_ENTRY_INVALID)
static void sort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *), void *swap) {
    qsort(base, count, size, cmp);
}
#endif
