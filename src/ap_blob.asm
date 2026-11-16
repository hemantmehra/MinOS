BITS 32
SECTION .rdata
GLOBAL _ap_trampoline_start
GLOBAL _ap_trampoline_end
_ap_trampoline_start:
    incbin "ap_trampoline.bin"
_ap_trampoline_end:
