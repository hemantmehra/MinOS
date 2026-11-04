#ifndef MINOS_KERNEL_LOG_H
#define MINOS_KERNEL_LOG_H

enum KernelLogLevel
{
    KERNEL_LOG_INFO,
    KERNEL_LOG_WARNING,
    KERNEL_LOG_ERROR
};

void kernel_log(
    enum KernelLogLevel level,
    const char *subsystem,
    const char *message);
void kernel_log_value(
    enum KernelLogLevel level,
    const char *subsystem,
    const char *message,
    unsigned int value);

#endif
