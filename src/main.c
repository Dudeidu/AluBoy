
#include <stdlib.h> 
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "application.h"

int main(void) {

    if (application_init("AluGB") == -1) return -1;
    
    application_update();   // Runs until window is closed

    application_cleanup();  // Free memory stuff
    
    /*
    BytePair row = { .bytes = {0xff, 0x00} };
    //row.bytes.low = 0
    printf("full: %04x\n", row.full);
    printf("high: %02x\n", row.bytes.high);
    printf("low: %02x\n", row.bytes.low);
    */

    //return -1;
}
