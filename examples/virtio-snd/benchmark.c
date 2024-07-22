/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <microkit.h>
#include <serial_config.h>
#include <sel4/benchmark_track_types.h>
#include <sel4/benchmark_utilisation_types.h>
#include <sddf/benchmark/bench.h>
#include <sddf/benchmark/sel4bench.h>
#include <sddf/util/fence.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>

#define LOG_BUFFER_CAP 7

/* Notification channels and TCB CAP offsets - ensure these align with .system file! */
#define SERIAL_TX_CH 0
#define START 1
#define STOP 2
#define INIT 3

#define PD_TOTAL        0
#define PD_SND_DRV_ID   1
#define PD_VIRT_ID      2
#define PD_CLIENT_ID    3

uintptr_t cyclecounters_vaddr;

serial_queue_t *serial_tx_queue;
char *serial_tx_data;
serial_queue_handle_t tx_queue_handle;

static ccnt_t counter_values[8];
static counter_bitfield_t benchmark_bf;

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
static benchmark_track_kernel_entry_t *log_buffer;
#endif

static char *counter_names[] = {
    "L1 i-cache misses",
    "L1 d-cache misses",
    "L1 i-tlb misses",
    "L1 d-tlb misses",
    "Instructions",
    "Branch mispredictions",
};

static uint64_t start_cycles; 
static uint64_t start_ccount; 

static event_id_t benchmarking_events[] = {
    SEL4BENCH_EVENT_CACHE_L1I_MISS,
    SEL4BENCH_EVENT_CACHE_L1D_MISS,
    SEL4BENCH_EVENT_TLB_L1I_MISS,
    SEL4BENCH_EVENT_TLB_L1D_MISS,
    SEL4BENCH_EVENT_EXECUTE_INSTRUCTION,
    SEL4BENCH_EVENT_BRANCH_MISPREDICT,
};

#ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
static void microkit_benchmark_start(void)
{
    seL4_BenchmarkResetThreadUtilisation(TCB_CAP);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_SND_DRV_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_VIRT_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_CLIENT_ID);
    seL4_BenchmarkResetLog();
}

static void microkit_benchmark_stop(uint64_t *total, uint64_t* number_schedules, uint64_t *kernel, uint64_t *entries)
{
    seL4_BenchmarkFinalizeLog();
    seL4_BenchmarkGetThreadUtilisation(TCB_CAP);
    uint64_t *buffer = (uint64_t *)&seL4_GetIPCBuffer()->msg[0];

    *total = buffer[BENCHMARK_TOTAL_UTILISATION];
    *number_schedules = buffer[BENCHMARK_TOTAL_NUMBER_SCHEDULES];
    *kernel = buffer[BENCHMARK_TOTAL_KERNEL_UTILISATION];
    *entries = buffer[BENCHMARK_TOTAL_NUMBER_KERNEL_ENTRIES];
}

static void microkit_benchmark_stop_tcb(uint64_t pd_id, uint64_t *total, uint64_t *number_schedules, uint64_t *kernel, uint64_t *entries)
{
    seL4_BenchmarkGetThreadUtilisation(BASE_TCB_CAP + pd_id);
    uint64_t *buffer = (uint64_t *)&seL4_GetIPCBuffer()->msg[0];

    *total = buffer[BENCHMARK_TCB_UTILISATION];
    *number_schedules = buffer[BENCHMARK_TCB_NUMBER_SCHEDULES];
    *kernel = buffer[BENCHMARK_TCB_KERNEL_UTILISATION];
    *entries = buffer[BENCHMARK_TCB_NUMBER_KERNEL_ENTRIES];
}

static void print_benchmark_details(uint64_t pd_id, uint64_t kernel_util, uint64_t kernel_entries, uint64_t number_schedules, uint64_t total_util)
{
    if (pd_id == PD_TOTAL) sddf_printf("Total utilisation details: ");
    else sddf_printf("Utilisation details for PD: ");
    switch (pd_id) {
    case PD_CLIENT_ID: sddf_printf("PD_CLIENT_ID"); break;
    case PD_VIRT_ID: sddf_printf("PD_VIRT_ID"); break;
    case PD_SND_DRV_ID: sddf_printf("PD_SND_DRV_ID"); break;
    }
    if (pd_id != PD_TOTAL) sddf_printf(" ( %lx)", pd_id);
    sddf_printf("{\nKernelUtilisation:  %lx\nKernelEntries:  %lx\nNumberSchedules:  %lx\nTotalUtilisation:  %lx\n}\n",
                kernel_util, kernel_entries, number_schedules, total_util);
}
#endif

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
static inline void seL4_BenchmarkTrackDumpSummary(benchmark_track_kernel_entry_t *logBuffer, uint64_t logSize)
{
    seL4_Word index = 0;
    seL4_Word syscall_entries = 0;
    seL4_Word fastpaths = 0;
    seL4_Word interrupt_entries = 0;
    seL4_Word userlevelfault_entries = 0;
    seL4_Word vmfault_entries = 0;
    seL4_Word debug_fault = 0;
    seL4_Word other = 0;

    while (logBuffer[index].start_time != 0 && index < logSize) {
        if (logBuffer[index].entry.path == Entry_Syscall) {
            if (logBuffer[index].entry.is_fastpath) fastpaths++;
            syscall_entries++;
        } else if (logBuffer[index].entry.path == Entry_Interrupt) interrupt_entries++;
        else if (logBuffer[index].entry.path == Entry_UserLevelFault) userlevelfault_entries++;
        else if (logBuffer[index].entry.path == Entry_VMFault) vmfault_entries++;
        else if (logBuffer[index].entry.path == Entry_DebugFault) debug_fault++;
        else other++;

        index++;
    }

    sddf_printf("Number of system call invocations  %llx and fastpaths  %llx\n", syscall_entries, fastpaths);
    sddf_printf("Number of interrupt invocations  %llx\n", interrupt_entries);
    sddf_printf("Number of user-level faults  %llx\n", userlevelfault_entries);
    sddf_printf("Number of VM faults  %llx\n", vmfault_entries);
    sddf_printf("Number of debug faults  %llx\n", debug_fault);
    sddf_printf("Number of others  %llx\n", other);
}
#endif


void notified(microkit_channel ch)
{
    struct bench *bench = (void *)cyclecounters_vaddr;
    uint64_t end_cycles, end_ccount;

    switch(ch) {
    case START:
        sddf_printf("Starting\n");
        sel4bench_reset_counters();
        THREAD_MEMORY_RELEASE();
        sel4bench_start_counters(benchmark_bf);

        #ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        microkit_benchmark_start();
        #endif

        #ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
        seL4_BenchmarkResetLog();
        #endif

        #ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        start_cycles = bench->ts;
        start_ccount = bench->ccount;
        #endif

        break;
    case STOP:

        #ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        end_cycles = bench->ts;
        end_ccount = bench->ccount;
        #endif

        sel4bench_get_counters(benchmark_bf, &counter_values[0]);
        sel4bench_stop_counters(benchmark_bf);

        // sddf_printf("Counter values: {\n");
        // for (int i = 0; i < ARRAY_SIZE(benchmarking_events); i++) {
        //     sddf_printf("%s: %llX\n", counter_names[i], counter_values[i]);
        // }
        // sddf_printf("}\n");

        #ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        uint64_t total;
        uint64_t kernel;
        uint64_t entries;
        uint64_t number_schedules;

        sddf_printf("Benchmark details\r\n");

        microkit_benchmark_stop(&total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_TOTAL, kernel, entries, number_schedules, total);

        // for (int i = 0; i < PD_ID_COUNT; i++) {
        //     microkit_benchmark_stop_tcb(pd_ids[i], &total, &number_schedules, &kernel, &entries);
        //     print_benchmark_details(pd_ids[i], kernel, entries, number_schedules, total);
        // }

        // sddf_printf("s_cy %20lu\r\n", start_cycles);
        // sddf_printf("e_cy %20lu\r\n", end_cycles);
        // sddf_printf("s_ccount %20lu\r\n", start_ccount);
        // sddf_printf("e_ccount %20lu\r\n", end_ccount);
        sddf_printf("total %16lu\r\n", end_cycles - start_cycles);
        sddf_printf("idle  %16lu\r\n", end_ccount - start_ccount);

        #endif

        #ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
        entries = seL4_BenchmarkFinalizeLog();
        sddf_printf("KernelEntries:  %llx\n", entries);
        seL4_BenchmarkTrackDumpSummary(log_buffer, entries);
        #endif

        break;
    default:
        sddf_printf("Bench thread notified on unexpected channel\n");
    }
}

void init(void)
{
    serial_cli_queue_init_sys(microkit_name, NULL, NULL, NULL, &tx_queue_handle, serial_tx_queue, serial_tx_data);
    serial_putchar_init(SERIAL_TX_CH, &tx_queue_handle);

    sel4bench_init();
    seL4_Word n_counters = sel4bench_get_num_counters();

    counter_bitfield_t mask = 0;
    
    for (seL4_Word counter = 0; counter < n_counters; counter++) {
        if (counter >= ARRAY_SIZE(benchmarking_events)) break;
        sel4bench_set_count_event(counter, benchmarking_events[counter]);
        mask |= BIT(counter);
    }

    sel4bench_reset_counters();
    sel4bench_start_counters(mask);
    benchmark_bf = mask;

    /* Notify the idle thread that the sel4bench library is initialised. */
    sddf_printf("STARTING IDLE");
    microkit_notify(INIT);

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
    int res_buf = seL4_BenchmarkSetLogBuffer(LOG_BUFFER_CAP);
    if (res_buf) sddf_printf("Could not set log buffer:  %llx\n", res_buf);
    else sddf_printf("Log buffer set\n");
#endif
}
