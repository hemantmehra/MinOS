#include "kernel.h"
#include "kernel_log.h"
#include "process.h"
#include "smp.h"

#define IDT_ENTRY_COUNT 256
#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_COMMAND 0xA0
#define PIC_SLAVE_DATA 0xA1
#define PIT_CHANNEL_0 0x40
#define PIT_COMMAND 0x43
#define PIT_FREQUENCY 1193182
#define TIMER_FREQUENCY 1000

#pragma pack(push, 1)
typedef struct
{
    unsigned short offset_low;
    unsigned short selector;
    unsigned char reserved;
    unsigned char flags;
    unsigned short offset_high;
} InterruptGate;

typedef struct
{
    unsigned short limit;
    unsigned int base;
} InterruptDescriptor;
#pragma pack(pop)

static InterruptGate interrupt_table[IDT_ENTRY_COUNT];
static InterruptDescriptor interrupt_descriptor;

#pragma pack(push, 1)
typedef struct
{
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} GdtEntry;
#pragma pack(pop)

static GdtEntry gdt[SMP_CPU_LIMIT][6];
static InterruptDescriptor gdt_descriptor[SMP_CPU_LIMIT];
static unsigned int task_state_segment[SMP_CPU_LIMIT][26];

static void gdt_set_entry(
    unsigned int cpu,
    unsigned int index,
    unsigned int base,
    unsigned int limit,
    unsigned char access,
    unsigned char granularity)
{
    gdt[cpu][index].limit_low = (unsigned short)limit;
    gdt[cpu][index].base_low = (unsigned short)base;
    gdt[cpu][index].base_middle = (unsigned char)(base >> 16);
    gdt[cpu][index].access = access;
    gdt[cpu][index].granularity =
        (unsigned char)(granularity | ((limit >> 16) & 0x0F));
    gdt[cpu][index].base_high = (unsigned char)(base >> 24);
}

/* Entries 1 and 2 match the boot GDT, so loaded segment registers stay
   valid. Selectors 0x1B and 0x23 are the ring 3 code and data segments. */
static void gdt_initialize(void)
{
    unsigned int cpu = smp_cpu_index();
    InterruptDescriptor *descriptor = &gdt_descriptor[cpu];
    gdt_set_entry(cpu, 0, 0, 0, 0, 0);
    gdt_set_entry(cpu, 1, 0, 0xFFFFF, 0x9A, 0xC0);
    gdt_set_entry(cpu, 2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set_entry(cpu, 3, 0, 0xFFFFF, 0xFA, 0xC0);
    gdt_set_entry(cpu, 4, 0, 0xFFFFF, 0xF2, 0xC0);
    gdt_set_entry(
        cpu, 5, (unsigned int)task_state_segment[cpu],
        sizeof(task_state_segment[cpu]) - 1,
        0x89, 0x00);
    task_state_segment[cpu][2] = KERNEL_DATA_SELECTOR;
    task_state_segment[cpu][25] = sizeof(task_state_segment[cpu]) << 16;

    descriptor->limit = sizeof(gdt[cpu]) - 1;
    descriptor->base = (unsigned int)gdt[cpu];
    __asm
    {
        mov eax, descriptor
        lgdt fword ptr [eax]
        mov ax, 0x28
        ltr ax
    }
}

void interrupts_set_kernel_stack(unsigned int stack_top)
{
    task_state_segment[smp_cpu_index()][1] = stack_top;
}

static void __declspec(naked) interrupt_halt(void)
{
    __asm
    {
        cli
    interrupt_halt_loop:
        hlt
        jmp interrupt_halt_loop
    }
}

static void __declspec(naked) exception_common(void)
{
    __asm
    {
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, KERNEL_DATA_SELECTOR
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        cld
        test byte ptr [esp + 60], 3
        jz exception_kernel_fault
        call smp_lock
        push esp
        call process_handle_exception
        add esp, 4
        mov esp, eax
        call process_finish_switch
        call smp_unlock
        pop gs
        pop fs
        pop es
        pop ds
        popad
        iretd
    exception_kernel_fault:
        push esp
        call process_handle_exception
    exception_kernel_halt:
        cli
        hlt
        jmp exception_kernel_halt
    }
}

static void __declspec(naked) exception_divide_stub(void)
{
    __asm
    {
        push 0
        push 0
        jmp exception_common
    }
}

static void __declspec(naked) exception_invalid_opcode_stub(void)
{
    __asm
    {
        push 0
        push 6
        jmp exception_common
    }
}

static void __declspec(naked) exception_general_protection_stub(void)
{
    __asm
    {
        push 13
        jmp exception_common
    }
}

static void __declspec(naked) exception_page_fault_stub(void)
{
    __asm
    {
        push 14
        jmp exception_common
    }
}

static void __declspec(naked) keyboard_irq_stub(void)
{
    __asm
    {
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, KERNEL_DATA_SELECTOR
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        cld
        call smp_lock
        call keyboard_handle_irq
        call smp_unlock
        pop gs
        pop fs
        pop es
        pop ds
        popad
        iretd
    }
}

static void __declspec(naked) mouse_irq_stub(void)
{
    __asm
    {
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, KERNEL_DATA_SELECTOR
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        cld
        call smp_lock
        call mouse_handle_irq
        call smp_unlock
        pop gs
        pop fs
        pop es
        pop ds
        popad
        iretd
    }
}

static void __declspec(naked) timer_irq_stub(void)
{
    __asm
    {
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, KERNEL_DATA_SELECTOR
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        cld
        call smp_lock
        push esp
        call process_timer_tick
        add esp, 4
        mov esp, eax
        call process_finish_switch
        call smp_unlock
        pop gs
        pop fs
        pop es
        pop ds
        popad
        iretd
    }
}

static void __declspec(naked) ap_timer_irq_stub(void)
{
    __asm
    {
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, KERNEL_DATA_SELECTOR
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        cld
        call smp_lock
        push esp
        call process_ap_timer_tick
        add esp, 4
        mov esp, eax
        call process_finish_switch
        call smp_unlock
        pop gs
        pop fs
        pop es
        pop ds
        popad
        iretd
    }
}

static void __declspec(naked) apic_spurious_stub(void)
{
    __asm iretd
}

static void __declspec(naked) system_call_stub(void)
{
    __asm
    {
        pushad
        push ds
        push es
        push fs
        push gs
        mov ax, KERNEL_DATA_SELECTOR
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        cld
        call smp_lock
        push esp
        call process_system_call
        add esp, 4
        mov esp, eax
        call process_finish_switch
        call smp_unlock
        pop gs
        pop fs
        pop es
        pop ds
        popad
        iretd
    }
}

static void interrupt_set_gate(
    unsigned int vector,
    void (__cdecl *handler)(void))
{
    unsigned int address = (unsigned int)handler;

    interrupt_table[vector].offset_low = (unsigned short)address;
    interrupt_table[vector].selector = KERNEL_CODE_SELECTOR;
    interrupt_table[vector].reserved = 0;
    interrupt_table[vector].flags = 0x8E;
    interrupt_table[vector].offset_high = (unsigned short)(address >> 16);
}

static void pic_remap(void)
{
    __outbyte(PIC_MASTER_COMMAND, 0x11);
    __outbyte(PIC_SLAVE_COMMAND, 0x11);
    __outbyte(PIC_MASTER_DATA, 0x20);
    __outbyte(PIC_SLAVE_DATA, 0x28);
    __outbyte(PIC_MASTER_DATA, 0x04);
    __outbyte(PIC_SLAVE_DATA, 0x02);
    __outbyte(PIC_MASTER_DATA, 0x01);
    __outbyte(PIC_SLAVE_DATA, 0x01);
    __outbyte(PIC_MASTER_DATA, 0xF8);
    __outbyte(PIC_SLAVE_DATA, 0xFF);
}

static void pit_initialize(void)
{
    unsigned short divisor =
        (unsigned short)(PIT_FREQUENCY / TIMER_FREQUENCY);

    __outbyte(PIT_COMMAND, 0x36);
    __outbyte(PIT_CHANNEL_0, (unsigned char)divisor);
    __outbyte(PIT_CHANNEL_0, (unsigned char)(divisor >> 8));
}

void interrupts_initialize(void)
{
    unsigned int vector;

    for (vector = 0; vector < IDT_ENTRY_COUNT; ++vector)
    {
        interrupt_set_gate(vector, interrupt_halt);
    }
    interrupt_set_gate(0x21, keyboard_irq_stub);
    interrupt_set_gate(0x2C, mouse_irq_stub);
    interrupt_set_gate(0x20, timer_irq_stub);
    interrupt_set_gate(0x30, ap_timer_irq_stub);
    interrupt_set_gate(0xFF, apic_spurious_stub);
    interrupt_set_gate(0x80, system_call_stub);
    interrupt_table[0x80].flags = 0xEE;
    interrupt_set_gate(0, exception_divide_stub);
    interrupt_set_gate(6, exception_invalid_opcode_stub);
    interrupt_set_gate(13, exception_general_protection_stub);
    interrupt_set_gate(14, exception_page_fault_stub);
    gdt_initialize();

    interrupt_descriptor.limit = sizeof(interrupt_table) - 1;
    interrupt_descriptor.base = (unsigned int)interrupt_table;
    __asm
    {
        lidt fword ptr [interrupt_descriptor]
    }

    pic_remap();
    pit_initialize();
    if (mouse_initialize())
    {
        __outbyte(PIC_SLAVE_DATA, 0xEF);
        kernel_log(KERNEL_LOG_INFO, "mouse", "PS/2 mouse IRQ12 enabled");
    }
    else
    {
        kernel_log(
            KERNEL_LOG_WARNING,
            "mouse",
            "PS/2 mouse initialization failed");
    }
    kernel_log(KERNEL_LOG_INFO, "keyboard", "IRQ1 enabled");
    kernel_log(KERNEL_LOG_INFO, "interrupts", "IDT and PIT initialized");
    __asm sti
}

void interrupts_initialize_ap(void)
{
    gdt_initialize();
    __asm
    {
        lidt fword ptr [interrupt_descriptor]
    }
}
