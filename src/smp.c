#include <intrin.h>
#include "kernel.h"
#include "kernel_log.h"
#include "process.h"
#include "smp.h"

#define AP_TRAMPOLINE_ADDRESS 0x8000
/* The boot image ends below this reserved conventional-memory stack. */
#define AP_STACK_TOP 0x74000
#define LAPIC_VIRTUAL_ADDRESS 0xC2000000
#define LAPIC_ID 0x020
#define LAPIC_EOI 0x0B0
#define LAPIC_SPURIOUS 0x0F0
#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310
#define LAPIC_TIMER 0x320
#define LAPIC_TIMER_INITIAL 0x380
#define LAPIC_TIMER_CURRENT 0x390
#define LAPIC_TIMER_DIVIDE 0x3E0
#define AP_TIMER_VECTOR 0x30

extern const unsigned char ap_trampoline_start[];
extern const unsigned char ap_trampoline_end[];

SmpCpu smp_cpus[SMP_CPU_LIMIT];
static volatile unsigned int *local_apic;
static volatile long kernel_lock;
static volatile unsigned int ap_ready;
static volatile unsigned int ap_decision;
static unsigned int ap_timer_count;

static unsigned int lapic_read(unsigned int offset)
{
    return local_apic[offset / sizeof(unsigned int)];
}

static void lapic_write(unsigned int offset, unsigned int value)
{
    local_apic[offset / sizeof(unsigned int)] = value;
    (void)lapic_read(LAPIC_ID);
}

unsigned int smp_cpu_index(void)
{
    if (local_apic != 0 &&
        (lapic_read(LAPIC_ID) >> 24) == smp_cpus[1].apic_id)
        return 1;
    return 0;
}

void smp_lock(void)
{
    while (_InterlockedExchange(&kernel_lock, 1) != 0)
    {
        while (kernel_lock != 0)
            _mm_pause();
    }
    /* Discard stale shared mappings before entering serialized kernel code. */
    __writecr3(__readcr3());
}

void smp_unlock(void)
{
    _InterlockedExchange(&kernel_lock, 0);
}

void smp_timer_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

static int checksum_valid(const unsigned char *data, unsigned int length)
{
    unsigned int i;
    unsigned char sum = 0;
    for (i = 0; i < length; ++i)
        sum = (unsigned char)(sum + data[i]);
    return sum == 0;
}

static unsigned int read_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int read_u32(const unsigned char *p)
{
    return read_u16(p) | (read_u16(p + 2) << 16);
}

static const unsigned char *find_rsdp(unsigned int start, unsigned int size)
{
    unsigned int address;
    for (address = (start + 15) & ~15u; address + 20 <= start + size; address += 16)
    {
        const unsigned char *p = (const unsigned char *)address;
        if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' ' &&
            p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ' &&
            checksum_valid(p, 20))
            return p;
    }
    return 0;
}

static const unsigned char *acpi_table(unsigned int address, const char *signature)
{
    const unsigned char *table;
    unsigned int length;
    if (address < 0x400 || address > 0x20000000 - 36)
        return 0;
    table = (const unsigned char *)address;
    length = read_u32(table + 4);
    if (table[0] != signature[0] || table[1] != signature[1] ||
        table[2] != signature[2] || table[3] != signature[3] ||
        length < 36 || length > 0x20000000 - address ||
        !checksum_valid(table, length))
        return 0;
    return table;
}

static int find_acpi_ap(unsigned int bsp_id, unsigned int *ap_id)
{
    unsigned int ebda = read_u16((const unsigned char *)0x40E) << 4;
    const unsigned char *rsdp = 0;
    const unsigned char *rsdt;
    unsigned int i, length;
    if (ebda >= 0x400 && ebda <= 0x9FC00)
        rsdp = find_rsdp(ebda, 1024);
    if (rsdp == 0)
        rsdp = find_rsdp(0xE0000, 0x20000);
    if (rsdp == 0)
        return 0;
    rsdt = acpi_table(read_u32(rsdp + 16), "RSDT");
    if (rsdt == 0)
        return 0;
    length = read_u32(rsdt + 4);
    for (i = 36; i + 4 <= length; i += 4)
    {
        const unsigned char *madt = acpi_table(read_u32(rsdt + i), "APIC");
        unsigned int offset, madt_length;
        if (madt == 0)
            continue;
        madt_length = read_u32(madt + 4);
        for (offset = 44; offset + 2 <= madt_length;)
        {
            unsigned int entry_length = madt[offset + 1];
            if (entry_length < 2 || entry_length > madt_length - offset)
                return 0;
            if (madt[offset] == 0 && entry_length >= 8 &&
                (read_u32(madt + offset + 4) & 1) != 0 &&
                madt[offset + 3] != bsp_id)
            {
                *ap_id = madt[offset + 3];
                return 1;
            }
            offset += entry_length;
        }
    }
    return 0;
}

static const unsigned char *find_mp_pointer(unsigned int start, unsigned int size)
{
    unsigned int address;
    for (address = (start + 15) & ~15u; address + 16 <= start + size; address += 16)
    {
        const unsigned char *p = (const unsigned char *)address;
        if (p[0] == '_' && p[1] == 'M' && p[2] == 'P' && p[3] == '_' &&
            p[8] == 1 && checksum_valid(p, 16))
            return p;
    }
    return 0;
}

static int find_ap(unsigned int bsp_id, unsigned int *ap_id)
{
    unsigned int ebda = read_u16((const unsigned char *)0x40E) << 4;
    unsigned int base_memory = read_u16((const unsigned char *)0x413) * 1024;
    const unsigned char *mp = 0;
    const unsigned char *table;
    unsigned int address, length, count, offset, i;

    if (ebda >= 0x400 && ebda <= 0x9FC00)
        mp = find_mp_pointer(ebda, 1024);
    if (mp == 0 && base_memory >= 1024 && base_memory <= 0xA0000)
        mp = find_mp_pointer(base_memory - 1024, 1024);
    if (mp == 0)
        mp = find_mp_pointer(0xF0000, 0x10000);
    if (mp == 0 || mp[11] != 0)
        return 0;
    address = read_u32(mp + 4);
    if (address < 0x400 || address > 0xFFFFF - 44)
        return 0;
    table = (const unsigned char *)address;
    length = read_u16(table + 4);
    count = read_u16(table + 34);
    if (table[0] != 'P' || table[1] != 'C' || table[2] != 'M' ||
        table[3] != 'P' || length < 44 || length > 0x100000 - address ||
        !checksum_valid(table, length))
        return 0;
    offset = 44;
    for (i = 0; i < count; ++i)
    {
        unsigned int entry_size;
        if (offset >= length || table[offset] > 4)
            return 0;
        entry_size = table[offset] == 0 ? 20 : 8;
        if (entry_size > length - offset)
            return 0;
        if (table[offset] == 0 && (table[offset + 3] & 1) != 0 &&
            table[offset + 1] != bsp_id)
        {
            *ap_id = table[offset + 1];
            return 1;
        }
        offset += entry_size;
    }
    return 0;
}

static int wait_ticks(unsigned int ticks)
{
    unsigned int start = tick_count;
    unsigned int attempts;
    for (attempts = 0; attempts < 100000000; ++attempts)
    {
        if ((unsigned int)(tick_count - start) >= ticks)
            return 1;
        _mm_pause();
    }
    return 0;
}

static int send_ipi(unsigned int apic_id, unsigned int command)
{
    unsigned int attempts;
    for (attempts = 0; attempts < 10000000; ++attempts)
        if ((lapic_read(LAPIC_ICR_LOW) & 0x1000) == 0)
            break;
    if (attempts == 10000000)
        return 0;
    lapic_write(LAPIC_ICR_HIGH, apic_id << 24);
    lapic_write(LAPIC_ICR_LOW, command);
    for (attempts = 0; attempts < 10000000; ++attempts)
        if ((lapic_read(LAPIC_ICR_LOW) & 0x1000) == 0)
            return 1;
    return 0;
}

static void __cdecl smp_ap_entry(void)
{
    __asm cli
    interrupts_initialize_ap();
    process_initialize_ap();
    lapic_write(LAPIC_SPURIOUS, 0x1FF);
    lapic_write(LAPIC_TIMER_DIVIDE, 0x3);
    lapic_write(LAPIC_TIMER, 0x20000 | AP_TIMER_VECTOR);
    ap_ready = 1;
    while (ap_decision == 0)
        _mm_pause();
    if (ap_decision != 1)
        for (;;) __halt();
    lapic_write(LAPIC_TIMER_INITIAL, ap_timer_count);
    __asm sti
    for (;;) __halt();
}

void smp_initialize(void)
{
    int cpu_info[4];
    unsigned __int64 apic_msr;
    unsigned int bsp_id, ap_id, i, size;
    volatile unsigned char *trampoline = (volatile unsigned char *)AP_TRAMPOLINE_ADDRESS;
    unsigned int attempts;

    smp_cpus[1].apic_id = 0xFFFFFFFF;
    __cpuid(cpu_info, 1);
    if ((cpu_info[3] & ((1 << 9) | (1 << 5))) != ((1 << 9) | (1 << 5)))
    {
        kernel_log(KERNEL_LOG_WARNING, "smp", "local APIC unavailable; using one CPU");
        return;
    }
    apic_msr = __readmsr(0x1B);
    if ((apic_msr & 0xC00) != 0x800 || (apic_msr >> 32) != 0 ||
        !memory_map_page(LAPIC_VIRTUAL_ADDRESS,
            (unsigned int)apic_msr & 0xFFFFF000, 0x1A))
    {
        kernel_log(KERNEL_LOG_WARNING, "smp", "cannot map local APIC; using one CPU");
        return;
    }
    local_apic = (volatile unsigned int *)LAPIC_VIRTUAL_ADDRESS;
    bsp_id = lapic_read(LAPIC_ID) >> 24;
    smp_cpus[0].apic_id = bsp_id;
    if (!find_acpi_ap(bsp_id, &ap_id) && !find_ap(bsp_id, &ap_id))
    {
        kernel_log(KERNEL_LOG_WARNING, "smp", "no second CPU in firmware tables; using one CPU");
        return;
    }
    if (read_u16((const unsigned char *)0x413) * 1024 < AP_STACK_TOP ||
        (read_u16((const unsigned char *)0x40E) != 0 &&
         (read_u16((const unsigned char *)0x40E) << 4) < AP_STACK_TOP))
    {
        kernel_log(KERNEL_LOG_ERROR, "smp", "insufficient low memory for AP stack");
        return;
    }
    size = (unsigned int)(ap_trampoline_end - ap_trampoline_start);
    if (size > 4096)
    {
        kernel_log(KERNEL_LOG_ERROR, "smp", "AP trampoline exceeds one page");
        return;
    }
    smp_cpus[1].apic_id = ap_id;
    for (i = 0; i < size; ++i)
        trampoline[i] = ap_trampoline_start[i];
    *(volatile unsigned int *)(AP_TRAMPOLINE_ADDRESS + 16) = memory_kernel_address_space();
    *(volatile unsigned int *)(AP_TRAMPOLINE_ADDRESS + 20) = AP_STACK_TOP;
    *(volatile unsigned int *)(AP_TRAMPOLINE_ADDRESS + 24) = (unsigned int)smp_ap_entry;

    lapic_write(LAPIC_SPURIOUS, 0x1FF);
    lapic_write(LAPIC_TIMER_DIVIDE, 0x3);
    lapic_write(LAPIC_TIMER, 0x10000 | AP_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INITIAL, 0xFFFFFFFF);
    if (!wait_ticks(20))
    {
        kernel_log(KERNEL_LOG_ERROR, "smp", "timer calibration timed out");
        return;
    }
    ap_timer_count = (0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURRENT)) / 20;
    lapic_write(LAPIC_TIMER_INITIAL, 0);
    if (ap_timer_count == 0 || !send_ipi(ap_id, 0xC500) ||
        !wait_ticks(10) || !send_ipi(ap_id, 0x8500) ||
        !wait_ticks(1) || !send_ipi(ap_id, 0x600 | (AP_TRAMPOLINE_ADDRESS >> 12)) ||
        !wait_ticks(1) || !send_ipi(ap_id, 0x600 | (AP_TRAMPOLINE_ADDRESS >> 12)))
    {
        ap_decision = 2;
        kernel_log(KERNEL_LOG_ERROR, "smp", "AP startup IPI failed");
        return;
    }
    for (attempts = 0; attempts < 1000 && !ap_ready; ++attempts)
        if (!wait_ticks(1)) break;
    if (!ap_ready)
    {
        ap_decision = 2;
        kernel_log(KERNEL_LOG_ERROR, "smp", "AP startup timed out");
        return;
    }
    ap_decision = 1;
    kernel_log_value(KERNEL_LOG_INFO, "smp", "started APIC ID", ap_id);
}
