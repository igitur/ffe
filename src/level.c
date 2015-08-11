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

/* $Id: level.c,v 1.8 2010-09-24 11:45:16 timo Exp $ */

#include "ffe.h"
#include <string.h>

static struct level *levels[MAXLEVEL + 1];
static int last_level = 0;
static int max_level = 0;

void
reset_levels(int start, int stop)
{
    while(start <= stop) levels[start++] = NULL;
}

static void
print_level_text(struct level *l,uint8_t *buffer)
{
    register uint8_t *text = buffer;

    if(!buffer) return;
    start_write();
    while(*text)
    {
        if(*text == '%' && text[1])
        {
            text++;
            switch(*text)
            {
                case 'g':
                    if(l) writes(l->group_name);
                    break;
                case 'm':
                    if(l) writes(l->element_name);
                    break;
                case '%':
                    writec('%');
                    break;
                default:
                    writec('%');
                    writec(*text);
                    break;
            }
        } else
        {
            writec(*text);
        }
        text++;
    }
    flush_write();
}



static 
void print_level(struct level *l, uint8_t *buffer, uint8_t *indent, int indent_level)
{
    if(!l) return;

    if(indent) print_indent(indent,indent_level);

    print_level_text(l,buffer);
}

int
get_indent_depth(int level_count)
{
    register int i,depth = 1;

    for(i = 1;i <= level_count;i++) depth += (levels[i] != NULL ? levels[i]->indent_count : 0);

    return depth;
}

/* Print group and element level headers/trailers BEFORE an element is printed
*/
void
print_level_before(struct record *prev_record, struct record *curr_record)
{
    int pl = 0,i;

    if(!curr_record->level)
    {
        if(prev_record && prev_record->level && prev_record->level->element_name)
            print_level(prev_record->level,prev_record->o->element_trailer,prev_record->o->indent,get_indent_depth(prev_record->level->level) -  1);
        return;
    }

    if(curr_record->level->level > max_level) max_level = curr_record->level->level;
    last_level = curr_record->level->level;

    if(prev_record && prev_record->level) pl = prev_record->level->level;

    if(last_level == pl) // in the same level
    {
        if(prev_record->level->element_name)
            print_level(prev_record->level,prev_record->o->element_trailer,prev_record->o->indent,get_indent_depth(pl) -  1);

        if((prev_record->level->group_name && curr_record->level->group_name &&
            strcmp(prev_record->level->group_name,curr_record->level->group_name) != 0) ||
           (!prev_record->level->group_name || !curr_record->level->group_name))
        {
            if(prev_record->level->group_name) 
                print_level(prev_record->level,prev_record->o->group_trailer,prev_record->o->indent,get_indent_depth(pl - 1));
            if(curr_record->level->group_name) 
                print_level(curr_record->level,curr_record->o->group_header,curr_record->o->indent,get_indent_depth(last_level - 1)); 
        }
    } else if(last_level > pl) // current record is deeper in as previous
    {
        if(curr_record->level->group_name)
            print_level(curr_record->level,curr_record->o->group_header,curr_record->o->indent,get_indent_depth(pl));
    } else if(last_level < pl) // current record is higher as previous, print trailers for elements between current and previous
    {
        i = pl;
        while(i >= last_level)
        {
            if(levels[i] && levels[i]->element_name)
                print_level(levels[i],curr_record->o->element_trailer,curr_record->o->indent,get_indent_depth(i) - 1);
            if(i > last_level && levels[i] && levels[i]->group_name)
                print_level(levels[i],curr_record->o->group_trailer,curr_record->o->indent,get_indent_depth(i - 1));
            i--;
        }

        i++;

        if(levels[i] && ((levels[i]->group_name && curr_record->level->group_name && strcmp(levels[i]->group_name,curr_record->level->group_name) != 0) ||
           (!levels[i]->group_name || !curr_record->level->group_name)))
        {
            if(levels[i]->group_name) 
                print_level(levels[i],curr_record->o->group_trailer,curr_record->o->indent,get_indent_depth(i - 1));
            if(curr_record->level->group_name) 
                print_level(curr_record->level,curr_record->o->group_header,curr_record->o->indent,get_indent_depth(last_level - 1)); 
        }

        reset_levels(last_level + 1,max_level);
    }
    
    levels[last_level] = curr_record->level;

    if(curr_record->level->element_name)
        print_level(curr_record->level,curr_record->o->element_header,curr_record->o->indent,get_indent_depth(last_level) - 1);
}

/* print pending trailers after all data has been read
 */
void
print_level_end(struct record *last)
{
        int i = last_level;

        while(i >= 1)
        {
            if(levels[i]->element_name)
                print_level(levels[i],last->o->element_trailer,last->o->indent,get_indent_depth(i) - 1);
            if(levels[i]->group_name)
                print_level(levels[i],last->o->group_trailer,last->o->indent,get_indent_depth(i - 1));
            i--;
        }
}
