#include "kernel.h"
#include "kernel_log.h"
#include "process.h"

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
#define TIMER_FREQUENCY 100

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
        call keyboard_handle_irq
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
        push esp
        call process_timer_tick
        add esp, 4
        mov esp, eax
        pop gs
        pop fs
        pop es
        pop ds
        popad
        iretd
    }
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
        push esp
        call process_system_call
        add esp, 4
        mov esp, eax
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

static void pic_remap_and_enable_keyboard(void)
{
    __outbyte(PIC_MASTER_COMMAND, 0x11);
    __outbyte(PIC_SLAVE_COMMAND, 0x11);
    __outbyte(PIC_MASTER_DATA, 0x20);
    __outbyte(PIC_SLAVE_DATA, 0x28);
    __outbyte(PIC_MASTER_DATA, 0x04);
    __outbyte(PIC_SLAVE_DATA, 0x02);
    __outbyte(PIC_MASTER_DATA, 0x01);
    __outbyte(PIC_SLAVE_DATA, 0x01);
    __outbyte(PIC_MASTER_DATA, 0xFC);
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
    interrupt_set_gate(0x20, timer_irq_stub);
    interrupt_set_gate(0x80, system_call_stub);

    interrupt_descriptor.limit = sizeof(interrupt_table) - 1;
    interrupt_descriptor.base = (unsigned int)interrupt_table;
    __asm
    {
        lidt fword ptr [interrupt_descriptor]
    }

    pic_remap_and_enable_keyboard();
    pit_initialize();
    kernel_log(KERNEL_LOG_INFO, "keyboard", "IRQ1 enabled");
    kernel_log(KERNEL_LOG_INFO, "interrupts", "IDT and PIT initialized");
    __asm sti
}
