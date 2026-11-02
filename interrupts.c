#include "kernel.h"

#define IDT_ENTRY_COUNT 256
#define KERNEL_CODE_SELECTOR 0x08
#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_COMMAND 0xA0
#define PIC_SLAVE_DATA 0xA1

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
        mov ax, KERNEL_CODE_SELECTOR
        mov ds, ax
        call keyboard_handle_irq
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
    __outbyte(PIC_MASTER_DATA, 0xFD);
    __outbyte(PIC_SLAVE_DATA, 0xFF);
}

void interrupts_initialize(void)
{
    unsigned int vector;

    for (vector = 0; vector < IDT_ENTRY_COUNT; ++vector)
    {
        interrupt_set_gate(vector, interrupt_halt);
    }
    interrupt_set_gate(0x21, keyboard_irq_stub);

    interrupt_descriptor.limit = sizeof(interrupt_table) - 1;
    interrupt_descriptor.base = (unsigned int)interrupt_table;
    __asm
    {
        lidt fword ptr [interrupt_descriptor]
    }

    pic_remap_and_enable_keyboard();
    __asm sti
}
