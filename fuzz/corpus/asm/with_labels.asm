; Loop example with labels
.section .text
.global main

main:
    MOVI R0, 0       ; counter = 0
    MOVI R1, 10      ; limit = 10

loop:
    CMP R0, R1       ; compare counter with limit
    JGE done         ; if counter >= limit, exit
    ADD R0, R0, 1    ; counter++
    JMP loop         ; continue loop

done:
    HALT
