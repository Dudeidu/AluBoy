
#include <stdlib.h> 
#include <stdio.h>

#include "application.h"

int main(void) {

    if (application_init("AluGB") == -1) return -1;
    
    application_update();   // Runs until window is closed

    application_cleanup();  // Free memory stuff
    
    return 0;
}
