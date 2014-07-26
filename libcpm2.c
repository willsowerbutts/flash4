#include <string.h>
#include <ctype.h>
#include "libcpm.h"

void cpm_f_prepare(cpm_fcb *fcb, char *name)
{
    char *p;
    int i;

    memset(fcb, 0, sizeof(cpm_fcb));

    p = name;
    if(p[1] == ':'){ /* handle drives other than the default, eg "B:FOO.BAR" */
        fcb->dr = p[0] & 0x9F; /* a=1, b=2 etc; case insensitive */
        p+=2;
    }

    for(i=0; i<8; i++){
        if(*p == 0 || *p == '.')
            break;
        fcb->name[i] = *p++;
    }
    /* pad with spaces */
    for(;i<8;i++)
        fcb->name[i] = ' ';
    
    /* skip dot in filename */
    if(*p == '.')
        p++;

    for(i=0; i<3; i++){
        if(*p == 0 || *p == '.')
            break;
        fcb->ext[i] = *p++;
    }
    /* pad with spaces */
    for(;i<3;i++)
        fcb->ext[i] = ' ';
}
