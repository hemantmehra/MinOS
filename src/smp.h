#ifndef MINOS_SMP_H
#define MINOS_SMP_H

#define SMP_CPU_LIMIT 2

typedef struct
{
    unsigned int apic_id;
    unsigned int task_index;
    unsigned int fpu_owner;
    unsigned int pending_release;
    unsigned int *idle_stack;
    unsigned char idle_fpu_state[108];
} SmpCpu;

extern SmpCpu smp_cpus[SMP_CPU_LIMIT];

unsigned int smp_cpu_index(void);
void smp_initialize(void);
void smp_lock(void);
void smp_unlock(void);
void smp_timer_eoi(void);
void interrupts_initialize_ap(void);
void process_initialize_ap(void);

#endif
