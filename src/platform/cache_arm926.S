.syntax unified
.arm
.text

.global vflash_dcache_clean_range
.type vflash_dcache_clean_range,%function
vflash_dcache_clean_range:
    cmp r0,#0
    bxeq lr
    cmp r1,#0
    bxeq lr
    mrc p15,0,r2,c1,c0,0
    tst r2,#4
    beq 2f
    adds r1,r0,r1
    subcc r1,r1,#1
    mvncs r1,#0
    bic r0,r0,#31
    bic r1,r1,#31
1:
    mcr p15,0,r0,c7,c10,1
    cmp r0,r1
    addlo r0,r0,#32
    blo 1b
2:
    mov r2,#0
    mcr p15,0,r2,c7,c10,4
    bx lr

.global vflash_dcache_clean_invalidate_range
.type vflash_dcache_clean_invalidate_range,%function
vflash_dcache_clean_invalidate_range:
    cmp r0,#0
    bxeq lr
    cmp r1,#0
    bxeq lr
    mrc p15,0,r2,c1,c0,0
    tst r2,#4
    beq 2f
    adds r1,r0,r1
    subcc r1,r1,#1
    mvncs r1,#0
    bic r0,r0,#31
    bic r1,r1,#31
1:
    mcr p15,0,r0,c7,c14,1
    cmp r0,r1
    addlo r0,r0,#32
    blo 1b
2:
    mov r2,#0
    mcr p15,0,r2,c7,c10,4
    bx lr

.global vflash_dcache_invalidate_range
.type vflash_dcache_invalidate_range,%function
vflash_dcache_invalidate_range:
    cmp r0,#0
    bxeq lr
    cmp r1,#0
    bxeq lr
    mrc p15,0,r2,c1,c0,0
    tst r2,#4
    beq 3f
    adds r1,r0,r1
    subcc r1,r1,#1
    mvncs r1,#0
    bic r2,r0,#31
    bic r3,r1,#31
    tst r0,#31
    beq 1f
    mcr p15,0,r2,c7,c14,1
    cmp r2,r3
    beq 3f
    add r2,r2,#32
1:
    and r1,r1,#31
    cmp r1,#31
    beq 2f
    mcr p15,0,r3,c7,c14,1
    cmp r2,r3
    beq 3f
    sub r3,r3,#32
2:
    mcr p15,0,r2,c7,c6,1
    cmp r2,r3
    addlo r2,r2,#32
    blo 2b
3:
    mov r2,#0
    mcr p15,0,r2,c7,c10,4
    bx lr

.global vflash_icache_invalidate_all
.type vflash_icache_invalidate_all,%function
vflash_icache_invalidate_all:
    mov r0,#0
    mcr p15,0,r0,c7,c5,0
    mcr p15,0,r0,c7,c5,4
    bx lr
