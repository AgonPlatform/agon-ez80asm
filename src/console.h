#ifndef CONSOLE_H
#define CONSOLE_H

#ifndef CEDEV
void vdp_set_text_colour( int colour );
#else
#include <agon/vdp_vdu.h>
#endif


#endif