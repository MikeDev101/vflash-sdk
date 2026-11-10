.syntax unified
.cpu arm926ej-s
.arm
.text
.global vflash_mmu_control
.type vflash_mmu_control,%function
vflash_mmu_control:
    mrc p15,0,r0,c1,c0,0
    bx lr
.global vflash_mmu_processor_state
.type vflash_mmu_processor_state,%function
vflash_mmu_processor_state:
    mrs r0,cpsr
    bx lr
.global vflash_mmu_invalidate_tlb
.type vflash_mmu_invalidate_tlb,%function
vflash_mmu_invalidate_tlb:
    mov r0,#0
    mcr p15,0,r0,c7,c10,4
    mcr p15,0,r0,c8,c7,0
    mcr p15,0,r0,c7,c5,4
    bx lr
.global vflash_mmu_activate
.type vflash_mmu_activate,%function
vflash_mmu_activate:
    mov r1,#0
    mcr p15,0,r1,c7,c10,4
    mcr p15,0,r0,c2,c0,0
    mov r2,#1
    mcr p15,0,r2,c3,c0,0
    mcr p15,0,r1,c13,c0,0
    mcr p15,0,r1,c8,c7,0
    mrc p15,0,r2,c1,c0,0
    bic r2,r2,#0x1000
    bic r2,r2,#0x30c
    orr r2,r2,#1
    mcr p15,0,r2,c1,c0,0
    mcr p15,0,r1,c7,c5,4
    bx lr
.global vflash_mmu_deactivate
.type vflash_mmu_deactivate,%function
vflash_mmu_deactivate:
    mov r1,#0
    mcr p15,0,r1,c7,c10,4
    mrc p15,0,r2,c1,c0,0
    bic r2,r2,#1
    mcr p15,0,r2,c1,c0,0
    mcr p15,0,r1,c8,c7,0
    mcr p15,0,r1,c7,c5,4
    bx lr
.section .note.GNU-stack,"",%progbits
