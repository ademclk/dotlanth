; Simple arithmetic
.section .text
.global _start

_start:
    MOVI R0, 42      ; Load immediate 42 into R0
    MOVI R1, 10      ; Load immediate 10 into R1
    ADD R2, R0, R1   ; R2 = R0 + R1
    SUB R3, R0, R1   ; R3 = R0 - R1
    MUL R4, R0, R1   ; R4 = R0 * R1
    HALT

.section .data
answer:
    .dword 0
