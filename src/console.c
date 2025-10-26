#include <stdio.h>
#include "globals.h"

#ifndef AGONDEV

void vdp_set_text_colour( int colour ) {

	if(coloroutput) {
		switch(colour) {
			case 1: // DARK_RED
				printf("\033[31m");
				break;
			case 3: // DARK_YELLOW
				printf("\033[33m");
				break;
			default:
				printf("\033[39m"); // BRIGHT_WHITE
				break;
		}
	}
}

#endif
