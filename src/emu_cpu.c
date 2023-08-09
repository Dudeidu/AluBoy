#include "emu_cpu.h"

#include <stdbool.h>
#include <stdio.h>

const int MAXCYCLES = 69905;

void emu_cpu_update()
{
    int cycles_this_update = 0;
    while (cycles_this_update < MAXCYCLES)
    {
        int cycles = 4; // instead of 4, a value is returned by the operand instruction
        cycles_this_update += cycles;
        //UpdateTimers(cycles);
        //UpdateGraphics(cycles);
        //DoInterupts();
    }
    //printf("cpu cycles: %d\n", cycles_this_update);
    //printf("fps: %d, cpu: %d\n", timer_total, cycles_this_update);
}
