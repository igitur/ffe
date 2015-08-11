/*
 *    ffe - flat file extractor
 *
 *    Copyright (C) 2008 Timo Savinen
 *    This file is part of ffe.
 * 
 *    ffe is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    ffe is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with ffe; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* $Id: endian.c,v 1.11 2008-05-25 15:23:58 timo Exp $ */

#include "ffe.h"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#define LONG_INT 0x0a0b0c0d

uint8_t be[4]={0x0a,0x0b,0x0c,0x0d};
uint8_t le[4]={0x0d,0x0c,0x0b,0x0a};
uint8_t pe[4]={0x0b,0x0a,0x0d,0x0c};

size_t target_size = 16;
uint8_t *target = NULL;

int 
check_system_endianess()
{
    uint32_t l = LONG_INT;

    if(target == NULL) target = xmalloc(target_size); // conversion buffer is reserved with malloc in order to ensure proper aligment

    if(memcmp(&l,be,4) == 0) return F_BIG_ENDIAN;
    if(memcmp(&l,le,4) == 0) return F_LITTLE_ENDIAN;
    if(memcmp(&l,pe,4) == 0)
    {
        fprintf(stderr,"Pdp endianess is not supported");
    }
    return F_UNKNOWN_ENDIAN;
}

/* Endian change functions, s = source 
   returns pointer to aligned and converted number
*/

inline uint8_t *
betole_16(uint8_t *s)
{
    target[0] = s[1];
    target[1] = s[0];
    return target;
}

inline uint8_t *
letobe_16(uint8_t *s)
{
    return betole_16(s);
}

inline uint8_t *
betole_32(uint8_t *s)
{
    target[0] = s[3];
    target[1] = s[2];
    target[2] = s[1];
    target[3] = s[0];
    return target;
}

inline uint8_t *
letobe_32(uint8_t *s)
{
    return betole_32(s);
}

inline uint8_t *
betole_64(uint8_t *s)
{
    target[0] = s[7];
    target[1] = s[6];
    target[2] = s[5];
    target[3] = s[4];
    target[4] = s[3];
    target[5] = s[2];
    target[6] = s[1];
    target[7] = s[0];
    return target;
}

inline uint8_t *
letobe_64(uint8_t *s)
{
    return betole_64(s);
}

inline uint8_t *
betole_128(uint8_t *s)
{
    target[0] = s[15];
    target[1] = s[14];
    target[2] = s[13];
    target[3] = s[12];
    target[4] = s[11];
    target[5] = s[10];
    target[6] = s[9];
    target[7] = s[8];
    target[8] = s[7];
    target[9] = s[6];
    target[10] = s[5];
    target[11] = s[4];
    target[12] = s[3];
    target[13] = s[2];
    target[14] = s[1];
    target[15] = s[0];
    return target;
}


inline uint8_t *
letobe_128(uint8_t *s)
{
    return betole_128(s);
}

uint8_t *
endian_and_align(uint8_t *s,int t_endian, int s_endian, int bytes)
{
    if(t_endian == s_endian || bytes < 2)
    {
        if(bytes > target_size)
        {
            while(bytes > target_size) target_size = target_size * 2;
            target = xrealloc(target,target_size);
        }
        memcpy(target,s,bytes);
        return target;
    }

    if(t_endian == F_BIG_ENDIAN && s_endian == F_LITTLE_ENDIAN && bytes == 2) return letobe_16(s);
    if(t_endian == F_LITTLE_ENDIAN && s_endian == F_BIG_ENDIAN && bytes == 2) return betole_16(s);
    if(t_endian == F_BIG_ENDIAN && s_endian == F_LITTLE_ENDIAN && bytes == 4) return letobe_32(s);
    if(t_endian == F_LITTLE_ENDIAN && s_endian == F_BIG_ENDIAN && bytes == 4) return betole_32(s);
    if(t_endian == F_BIG_ENDIAN && s_endian == F_LITTLE_ENDIAN && bytes == 8) return letobe_64(s);
    if(t_endian == F_LITTLE_ENDIAN && s_endian == F_BIG_ENDIAN && bytes == 8) return betole_64(s);
    if(t_endian == F_BIG_ENDIAN && s_endian == F_LITTLE_ENDIAN && bytes == 16) return letobe_128(s);
    if(t_endian == F_LITTLE_ENDIAN && s_endian == F_BIG_ENDIAN && bytes == 16) return betole_128(s);
    fprintf(stderr,"%d %d %d\n",s_endian,t_endian,bytes);
    panic("Internal endian error",NULL,NULL);
    return NULL;
}

