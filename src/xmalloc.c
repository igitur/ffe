/*
 *    ffe - flat file extractor
 *
 *    Copyright (C) 2006 Timo Savinen
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

/* $Id: xmalloc.c,v 1.11 2008-05-23 06:43:22 timo Exp $ */

#include "ffe.h"
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

void *
xmalloc (size_t size)
{
    register void *value = malloc(size);
    if (value == NULL) panic("Out of memory",NULL,NULL);
    return value;
}

char *
xstrdup(char *str)
{
    register char *ret = strdup(str);
    if (ret == NULL) panic("Out of memory",NULL,NULL);
    return ret;
}

void *
xrealloc (void *ptr, size_t size)
{
    register void *value = realloc(ptr,size);
    if (value == NULL) panic("Out of memory",NULL,NULL);
    return value;
}

FILE *
xxfopen(char *name, char *mode, char bin_asc)
{
    register FILE *ret;

    ret = fopen(name,mode);
    if(ret == NULL) panic("Error in opening file",name,strerror(errno));

#if defined(HAVE_SETMODE) && defined(WIN32)
    if(bin_asc == 'a') setmode(fileno(ret),O_TEXT);
    if(bin_asc == 'b') setmode(fileno(ret),O_BINARY);
#endif
    return ret;
}

FILE *
xfopen(char *name, char *mode)
{
    return xxfopen(name,mode,'a');
}

FILE *
xfopenb(char *name, char *mode)
{
    return xxfopen(name,mode,'b'); 
}

void
file_to_text(FILE *fp)
{
#if defined(HAVE_SETMODE) && defined(WIN32)
    setmode(fileno(fp),O_TEXT);
#endif
}


