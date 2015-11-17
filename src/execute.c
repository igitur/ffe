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

/* $Id: execute.c,v 1.118 2011-04-10 10:12:09 timo Exp $ */

#include "ffe.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef PACKAGE
static char *program = PACKAGE;
#else
static char *program = "ffe";
#endif

#define GUESS_LINES 10000
#define GUESS_BUFFER (1024 * 1024)
#define READ_LINE_LEN (512 * 1024)
#define FIELD_SIZE (128 * 1024)
#define WRITE_BUFFER (2 * 1024 * 1024)
#define JUSTIFY_STRING 128

extern struct replace *replace;

struct input_file *files = NULL;
static struct input_file *current_file = NULL;
static FILE *input_fp = NULL;
static int ungetchar = -1;
static char *default_output_file = NULL;
static FILE *default_output_fp = NULL;

static char *output_file = NULL;
static FILE *output_fp = NULL;

static uint8_t *guess_buffer[GUESS_LINES];
static int guess_line_length[GUESS_LINES];
static uint8_t *read_buffer = NULL;
static size_t read_buffer_size = READ_LINE_LEN;
static size_t last_consumed = 0;  /* for binary reads */
static uint8_t *field_buffer = NULL;
static int field_buffer_size = FIELD_SIZE;
static int guess_lines = 0;
static int read_guess_line = 0;
static int binary_guessed = 0;

static uint8_t justify_string[JUSTIFY_STRING];

/* write buffer definitions */
static uint8_t *write_buffer = NULL;
static int write_buffer_size = WRITE_BUFFER;
static uint8_t *write_pos;
static uint8_t *write_buffer_end;

/* file number counters */
static long int current_file_lineno;
static long int current_total_lineno;
static long long int current_offset = 0;
static long long int current_file_offset = 0;

/* examples of non matching lines */
#define NO_MATCH_LINES 1
static int no_matching_lines = 0;

/* header definition from structure */
static int headers;

char *current_file_name = NULL;

static char debug_file[128];
static FILE *debug_fp = NULL;
static long int debug_lineno = 0;

static uint8_t bcd_to_ascii_cap[]={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','\000'};
static uint8_t hex_to_ascii_cap[]={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};

static uint8_t bcd_to_ascii_low[]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','\000'};
static uint8_t hex_to_ascii_low[]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

static uint8_t *bcd_to_ascii;
static uint8_t *hex_to_ascii;

inline uint8_t
htocl(uint8_t hex)
{
        return hex_to_ascii[hex & 0x0f];
}

inline uint8_t
htocb(uint8_t hex)
{
        return hex_to_ascii[(hex >> 4) & 0x0f];
}

inline uint8_t
bcdtocl(uint8_t bcd)
{
        return bcd_to_ascii[bcd & 0x0f];
}

inline uint8_t
bcdtocb(uint8_t bcd)
{
        return bcd_to_ascii[(bcd >> 4) & 0x0f];
}


void
set_output_file(char *name)
{
    if(name == NULL)
    {
        default_output_fp = stdout;
        default_output_file = "(stdout)";
    } else
    {
        default_output_fp = xfopen(name,"w");
        default_output_file = name;
    }
    output_fp = default_output_fp;
    output_file = default_output_file;
}

void 
close_output_file()
{
    struct output *o = output;
    int stdoutclosed = 0;

    if(default_output_fp == stdout) stdoutclosed = 1;
    if(fclose(default_output_fp) != 0)
    {
        panic("Error closing file",default_output_file,strerror(errno));
    }

    while(o != NULL)
    {
        if(o->ofp != NULL)
        {
            if((o->ofp == stdout && !stdoutclosed) || o->ofp != stdout)
            {
                if(o->ofp == stdout) stdoutclosed = 1;
                if(fclose(o->ofp) != 0)
                {
                    panic("Error closing file",o->output_file,strerror(errno));
                }
            }
        }
        o = o->next;
    }
}

void
set_input_file(char *name)
{
    register struct input_file *f = files;
    
    if(files == NULL)
    {
        files = xmalloc(sizeof(struct input_file));
        f = files;
    } else
    {
        while(f->next != NULL) f = f->next;
        f->next = xmalloc(sizeof(struct input_file));
        f = f->next;
    }

    f->next = NULL;
    f->name = xstrdup(name);
    f->lineno = 0;
}

static FILE *
open_input_stream(char *file,char type)
{
    int fds[2];
    pid_t pid;
    FILE *ret = NULL;
    char command[1024];

    if(ffe_open != NULL && ffe_open[0] != '\000')                // use preprocessor
    {
#if defined(HAVE_WORKING_FORK) && defined(HAVE_DUP2) && defined(HAVE_PIPE)
       sprintf(command,ffe_open,file);
       if (pipe(fds) != 0) panic("Cannot create pipe",strerror(errno),NULL);
       pid = fork();
       if(pid == (pid_t) 0) /* Child */
       {
          close(fds[0]);
          if(dup2(fds[1],STDOUT_FILENO) == -1) panic("dup2 error",strerror(errno),NULL);
          if(execl(SHELL_CMD, "sh", "-c", command, NULL) == -1) panic("Starting a shell with execl failed",command,strerror(errno));
          close(fds[1]);
          _exit(EXIT_SUCCESS);
       } else if(pid > (pid_t) 0)
       {
          close(fds[1]);
          ret = fdopen(fds[0],"r");
          if(ret == NULL) panic("Cannot read from command",command,strerror(errno));
          
          ungetchar = fgetc(ret);

          if(ungetchar == EOF)       // check if pipe returns something, if not open file normally
          {
              ungetchar = -1;
              fclose(ret);
              ret = NULL;
          }
       } else
       {
          panic("Cannot fork",strerror(errno),NULL);
       }
#else
       panic("Input preprocessing is not supported in this system",NULL,NULL);
#endif
    }

    if(ret == NULL)
    {
        if(type == BINARY)
        {
            ret = xfopenb(file,"r");
        } else
        {
            ret = xfopen(file,"r");
        }
    }
    return ret;
}


void
open_input_file(int stype)
{
    read_buffer = xmalloc(read_buffer_size);
    field_buffer = xmalloc(field_buffer_size);

    if(files->name[0] == '-' && !files->name[1])
    {
        input_fp = stdin;
        files->name = "(stdin)";
    } else
    {
        input_fp = open_input_stream(files->name,stype);
    }
    current_file = files;
    current_file->lineno = 0;
    current_file_name = current_file->name;
}

/* read input file until next newline is found
 */
void
complete_line_in_bin_buffer(int *ccount)
{
    int c;

    if(read_buffer[*ccount-1] != '\n') // check if have newline as last char
    {
        do
        {
            c = fgetc(input_fp);
            if(*ccount >= read_buffer_size) panic("Input file cannot be guessed, use -s option",NULL,NULL);
            if(c != EOF) read_buffer[(*ccount)++] = (uint8_t) c;
        } while(c != EOF && c != '\n');
    }
}


/* read from input stream. 
   Check if ungetchar contains a valid char and write it to buffer then read nmenb - 1 chars
   NOTE! it is assumed that size == 1...
*/
static size_t
uc_fread(uint8_t *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t ret = 0;

    if(ungetchar != -1)  // there is peeked char in ungetchar, write it to buffer and read the rest
    {
        *ptr = (uint8_t) ungetchar;
        ungetchar = -1;
        ptr++;
        nmemb--;
        ret = 1;
    }

    ret += fread(ptr,size,nmemb,stream);
    return ret;
}

/* read line from input stream. 
   Check if ungetchar contains a valid char and write it to buffer and then read the rest
*/
static int 
uc_fgets(uint8_t *s, int size, FILE *stream)
{
    int ret;
    char *orig_s = s;

    if(ungetchar != -1)  // there is peeked char in ungetchar, write it to buffer and read the rest
    {
        *s = (uint8_t) ungetchar;
        ungetchar = -1;
        if(*s == '\n')
        {
            s++;
            *s = '\000';
            return 1;
        }
        s++;
        size--;
    } 

    if(fgets(s,size,stream) != NULL)
    {
        ret = strlen(orig_s);
    } else
    {
        ret = -1;
    }

    return ret;
}




/* reads one file from input */
/* returns the line length */
/* return -1 on EOF */
int
read_input_line(int stype)
{
    static int eof = 0;
    static int ccount = 0;
    static uint8_t *bbuffer = NULL;

    do
    {
        if(stype == BINARY)
        {
            if(last_consumed)
            {
                current_offset += (long long) last_consumed;
                current_file_offset += (long long) last_consumed;
                memmove(read_buffer,read_buffer+last_consumed,read_buffer_size - last_consumed);
                if(!eof)
                {
                    ccount = uc_fread(read_buffer + (read_buffer_size - last_consumed),1,last_consumed,input_fp);
                    if(ccount < last_consumed) eof = 1;
                    ccount += read_buffer_size - last_consumed; // number of usable octets
                } else
                {
                    ccount -= last_consumed;
                }
            } else
            {
                if(binary_guessed) // do not read anything new if guessed
                {
                    binary_guessed = 0;   
                    current_file_lineno = current_file->lineno;
                    return ccount;  
                }
                ccount = uc_fread(read_buffer,1,read_buffer_size,input_fp);
                if(ccount < read_buffer_size) eof = 1;
            }

            if(ccount <= 0) 
            {
                ccount = -1;
            } 
        } else
        {
            if(binary_guessed || bbuffer != NULL) // binary guess has been done or still reading from bin buffer
            {
                int len;

                binary_guessed = 0;

                if(bbuffer == NULL)
                {
                    complete_line_in_bin_buffer(&ccount);
                } else
                {
                    memmove(read_buffer,bbuffer,ccount);
                    current_file->lineno++; // not in first read, has been made allready in binary read once
                }
                 
                bbuffer = read_buffer;

                while(*bbuffer && *bbuffer != '\n' && (int) (bbuffer - read_buffer) < ccount) bbuffer++;
                    
#ifdef WIN32
                if(bbuffer > read_buffer && (bbuffer[-1] == '\r')) 
                {
                    bbuffer[-1] = 0;
                    len = (int) (&bbuffer[-1] - read_buffer);
                } else
#endif
                {
                    *bbuffer = 0;
                    len = (int) (bbuffer - read_buffer);
                }
                
                if ((int) (bbuffer - read_buffer) < ccount) bbuffer++;

                ccount -= (int) (bbuffer - read_buffer);

                current_file_lineno = current_file->lineno;

                if(!ccount) bbuffer = NULL;

                return len ;
            }

            ccount = uc_fgets(read_buffer,read_buffer_size,input_fp);
#ifdef WIN32
            if(eof)  ccount = -1;
#endif
        }

        if(ccount == -1)
        {
            if(fclose(input_fp))
            {
                panic("Error closing file",files->name,strerror(errno));
            }
            current_file = current_file->next;
            if(current_file != NULL)
            {
                if(current_file->name[0] == '-' && !current_file->name[1]) 
                {
                    input_fp = stdin;
                    current_file->name = "(stdin)";
                } else
                {
                    input_fp = open_input_stream(current_file->name,stype);
                    if(stype == BINARY)
                    {
                        last_consumed = 0;
                        current_file_offset = 0;
                    } 
                    eof = 0;
                }
                current_file_name = current_file->name;
                current_file->lineno = 0;
                current_file_offset = 0;
            }
        } else
        {
            current_file->lineno++;
        }
    } while(ccount == -1 && current_file != NULL);
    if(ccount > 0)
    {
        current_file_lineno = current_file->lineno;
        if(stype != BINARY)
        {
            if(read_buffer[ccount - 1] == '\n')
            {
                ccount--;   /* remove newline */
                read_buffer[ccount] = 0;
#ifdef WIN32 // setmode might not work after operations performed on file
                if(read_buffer[ccount - 1] == '\r')
                {
                    ccount--;   /* remove carriage return */
                    read_buffer[ccount] = 0;
                }
#endif
            }
        }
    }
    return ccount;
}

/* calculate field count from line containing separated fields */
int
get_field_count(uint8_t quote, uint8_t *type, uint8_t *line)
{
    int inside_quote = 0;
    int fields = 0;
    register uint8_t *p = line;

    if(type[0] != SEPARATED) return 0;

    if (*p) fields++; /* at least one */

    while(*p)
    {
        if(*p == type[1] && !inside_quote)
        {
            fields++;
            if(type[2] == '*') while(*p == type[1]) p++;
        }
        
        if(((*p == quote && p[1] == quote) || (*p == '\\' && p[1] == quote)) && inside_quote && quote)
        {
            p++;
        } else if(*p == quote && quote)
        {
            inside_quote = !inside_quote;
        }
        if(*p) p++;
    }
    return fields;
}

/* returns a pointer for one field of the fixed length record */
/* note first position is 1 */
uint8_t *
get_fixed_field(int position, int length, int line_length,uint8_t *line)
{
    register uint8_t *t,*s;

    if(length >= field_buffer_size - 1) 
    {
        field_buffer_size = length * 2;
        field_buffer = xrealloc(field_buffer,field_buffer_size);
    }

    if(position <= line_length && position)
    {
        position--;            /* to get first as zero */
        s = line + position;
        t = field_buffer;
        memcpy(t,s,length);
        t[length] = 0;
    } else
    {
        field_buffer[0] = 0;
    }
    return field_buffer;
}

/* returns pointer to field in separated record */
uint8_t *
get_separated_field(int position, uint8_t quote,char *type, uint8_t *line)
{
    register uint8_t *p = line;
    int fieldno = 1;
    int inside_quote = 0;
    register int i = 0;

    while(*p && fieldno <= position)
    {
        if(*p == type[1] && !inside_quote)
        {
            fieldno++;
            if(type[2] == '*') while(*p == type[1]) p++;
        }

        if(((*p == quote && p[1] == quote) || (*p == '\\' && p[1] == quote)) && inside_quote && quote)
        {
            p++;
        } else if(*p == quote && quote)
        {
            inside_quote = !inside_quote;
            if(inside_quote && p[1]) p++;
        }

        if(fieldno == position)
        {
            if(*p == type[1] || (*p == quote && quote))
            {
                if(inside_quote)
                {
                    field_buffer[i] = *p;
                    i++;
                }
            } else
            {
                field_buffer[i] = *p;
                i++;
            }

            if(i >= field_buffer_size - 1) 
            {
                field_buffer_size = i * 2;
                field_buffer = xrealloc(field_buffer,field_buffer_size);
            }
        }
        if(*p) p++;
    }
    field_buffer[i] = 0;
    return field_buffer;
}
    

/* calculates votes for record, length has the buffer len and buffer 
 contains the line to be examined
 */
int
vote_record(uint8_t quote,char *type,int header,struct record *record,int length,uint8_t *buffer)
{
    register struct id *i = record->i;
    int vote = 0,len;
    int ids = 0;

    while(i != NULL)
    {
        ids++;
        switch(type[0])
        {
            case FIXED_LENGTH:
#ifdef HAVE_REGEX
                if(i->regexp)
                {
                    if(regexec(&i->reg,&buffer[i->position - 1],(size_t) 0, NULL, 0) == 0) vote++;
                } else
#endif
                {
                    if(strcmp(i->key,get_fixed_field(i->position, strlen(i->key),length,buffer)) == 0) vote++;
                }
                break;
            case SEPARATED:
                if(header && current_file_lineno == 1) // forgive header lines
                {
                    vote++;
                } else
                {
#ifdef HAVE_REGEX
                    if(i->regexp)
                    {
                        if(regexec(&i->reg,get_separated_field(i->position,quote,type,buffer),(size_t) 0, NULL, 0) == 0) vote++;
                    } else
#endif
                    {
                        if(strcmp(i->key,get_separated_field(i->position,quote,type,buffer)) == 0) vote++;
                    }
                }
                break;
            case BINARY:
#ifdef HAVE_REGEX
                if(i->regexp)
                {
                    if(regexec(&i->reg,&buffer[i->position - 1],(size_t) 0, NULL, 0) == 0) vote++;
                } else
#endif
                {
                    if(memcmp(i->key,get_fixed_field(i->position,i->length,length,buffer),i->length) == 0) vote++;
                }
               break;
        }
        i = i->next;
    }
    if(vote || record->i == NULL)    /* if keys are ok or missing, then check line length */
    {
        switch(type[0])
        {
            case FIXED_LENGTH:
                if((record->arb_length == RL_MIN && record->length <= length) ||
                   (record->arb_length == RL_STRICT && record->length == length)) vote++;
                break;
            case SEPARATED:
                len = get_field_count(quote,type,buffer);
                if((record->arb_length == RL_STRICT && record->length == len)  ||
                   (record->arb_length == RL_MIN && record->length <= len)) vote++;
                break;
            case BINARY:
                if(((vote == ids && ids) || !ids) && record->length <= length) vote++; /* exact binary length cannot be checked */
                break;
        }
    }

    if(vote == ids + 1)  /* every id and line length must match to get a vote */
    {
        return 1;
    } else
    {
        return 0;
    }
}
            

/* calculates votes for one input line */
void
vote(int bindex)
{
    struct structure *s = structure;
    struct record *r;
    int votes,total_votes = 0;

    while(s != NULL)
    {
        r = s->r;
        votes = 0;
        if(s->vote == bindex && s->type[0] != BINARY)   // check only structures having all records matched so far
        {
            while(r != NULL && !votes)
            {
                votes = vote_record(s->quote,s->type,s->header,r,guess_line_length[bindex],guess_buffer[bindex]);
                s->vote += votes;             /* only one vote per line */
                r = r->next;
            }
        }
        total_votes += votes;
        s = s->next;
    }
    if(!total_votes && no_matching_lines < NO_MATCH_LINES)
    {
        no_matching_lines++;
        fprintf(stderr,"%s: Line %ld in \'%s\' does not match, line length = %d\n",program,current_file->lineno,current_file->name,guess_line_length[bindex]);
    }
}

void
vote_binary(int buffer_size)
{
    struct structure *s = structure;
    struct record *r;

    while(s != NULL)
    {
        r = s->r;
        if(s->type[0] == BINARY)   // check only structures having all records matched so far
        {
            while(r != NULL && !s->vote)
            {
                if(r->i != NULL)
                {
                    if(vote_record(s->quote,s->type,s->header,r,buffer_size,read_buffer)) s->vote = 1;
                }
                r = r->next;
            }
        }
        s = s->next;
    }
}


/* calculates votes for structures */
/* returns pointer for structure name having all lines/blocks matched, in other case NULL */
/* votes has the vote count hat must mach */
char *
check_votes(int votes)
{
    struct structure *s = structure;
    char *winner = NULL;
    int errors = 0;

    while(s != NULL && votes)
    {
        if(s->vote == votes)
        {
            if(winner == NULL)
            {
                winner = s->name;
            } else
            {
                if(!errors) 
                {
                    fprintf(stderr,"%s: Input data matches several structures: \'%s\'",program,winner);
                } 
                fprintf(stderr," \'%s\'",s->name);
                errors++;
            }
        }
        s = s->next;
    }
    if(errors) 
    {
        fprintf(stderr,"\n");
        winner = NULL;
    }
    return winner;
}


/* guesses using binary only
 */
char *
guess_binary_structure()
{
    int buffer_size;
    char *ret = NULL;

    if(!max_binary_record_length)  // no binary structs
    {
        file_to_text(input_fp);
        return NULL;
    }

    read_buffer_size = max_binary_record_length;
    read_buffer = xrealloc(read_buffer,read_buffer_size);

    buffer_size = read_input_line(BINARY);

    if(buffer_size > 0)
    {
        vote_binary(buffer_size);
        ret = check_votes(1);
    }
    
    if(ret == NULL)
    {
        file_to_text(input_fp);
        read_buffer_size = READ_LINE_LEN;
        read_buffer = xrealloc(read_buffer,read_buffer_size);
    }
    
    binary_guessed = 1;

    return ret;
}



/* tries guess the input file structure */
/* returns pointer to the name of guessed structure */

char *
guess_structure()
{
    int memory_used = 0;
    int len;

    do
    {
        len = read_input_line(FIXED_LENGTH);
        if(len != -1)
        {
            guess_buffer[guess_lines] = xstrdup(read_buffer);
            guess_line_length[guess_lines] = len;
            memory_used += len;
            vote(guess_lines);
            guess_lines++;
        }
    } while(len != -1 && guess_lines < GUESS_LINES && memory_used < GUESS_BUFFER);
    return check_votes(guess_lines);
}

/* start write to write buffer */
void
start_write()
{
    write_pos = write_buffer;
}

/* write uint8_t to write buffer */
inline void
writec(uint8_t c)
{
    *write_pos = c;

    if(write_pos == write_buffer_end)
    {
        int written = write_buffer_end - write_buffer;

        write_buffer_size = write_buffer_size * 2;
        write_buffer = xrealloc(write_buffer,write_buffer_size);
        write_pos = write_buffer + written;
        write_buffer_end = write_buffer + (write_buffer_size - 1);
    }

    write_pos++;
}


/* write string to write buffer */
inline void
writes(uint8_t *string)
{
    register uint8_t *s = string;

    if(s != NULL)
    {
        while(*s)
        {
            writec(*s);
            s++;
        }
    }
}

void
flush_write()
{
    size_t bytes;

    bytes = write_pos - write_buffer;

    if(fwrite(write_buffer,1,bytes,output_fp) != bytes)
    {
        panic("Error writing to",output_file,NULL);
    }
}

void
print_raw(int size, uint8_t *buffer,int stype)
{
    if(fwrite(buffer,1,size,default_output_fp) != size)
    {
        panic("Error writing to",default_output_file,NULL);
    }
    if(stype != BINARY) fputc('\n',default_output_fp);
}
    


/* prints arbitrary text */
/* text can contain %-directives (no %d,%D, or %n) */
void
print_text(struct structure *s, struct record *r,uint8_t *buffer)
{
    register uint8_t *text = buffer;
    char num[64];

    if(text == NULL) return;
    if(r != NULL && (r->o == no_output || r->o == raw)) return;
    if(s->o == no_output || s->o == raw) return;

    start_write();

    while(*text)
    {
        if(*text == '%' && text[1]) 
        {
            text++;
            switch(*text)
            {
                case 'f':
                    writes(current_file_name);
                    break;
                case 's':
                    writes(s->name);
                    break;
                case 'r':
                    if(r != NULL) writes(r->name);
                    break;
                case 'o':
                    sprintf(num,"%ld",current_file_lineno);
                    writes(num);
                    break;
                case 'O':
                    sprintf(num,"%ld",current_total_lineno);
                    writes(num);
                    break;
                case 'I':
                    sprintf(num,"%lld",current_offset);
                    writes(num);
                    break;
                case 'i':
                    sprintf(num,"%lld",current_file_offset);
                    writes(num);
                    break;
                case 'g':
                    if(r->level && r->level->group_name) writes(r->level->group_name);
                    break;
                case 'n':
                    if(r->level && r->level->element_name) writes(r->level->element_name);
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


/* prints a header text */
/* text can contain only one %-directive %n */
/* return values: */
/* 1 - Header is printed or header should not be printed */
/* 0 - Header is not printed and should be printed */
int
print_header(struct structure *s, struct record *r)
{
    struct print_field *pf = r->pf;

    if(r->o == no_output || r->o == raw ||  r->o->header == NULL) return 1; /* no header for this run */

    if(pf == NULL) return 0;   /* no printable fields for this record */

    start_write();

    while(pf != NULL)
    {
        char *text = r->o->header;
        while(*text)
        {
            if(*text == '%' && text[1]) 
            {
                text++;
                switch(*text)
                {
                    case 'n':
                        writes(pf->f->name);
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
        if(pf->next != NULL && r->o->separator != NULL) writes(r->o->separator);
        pf = pf->next;
    }
    writes(r->o->record_trailer);
    flush_write();

    return 1;  /* header printed */
}

/* returns pointer to next input line
   lines are first read from guess buffer (if allocated)
   and after that from input stream, current_file_name,current_file_lineno etc
   are updated accordingly.

   returns NULL if no more lines
   length will be written to len
   */
uint8_t *
get_input_line(int *len,int stype)
{
    static struct input_file *curr_guess_file = NULL;
    uint8_t *ret = NULL;

    *len = -1;

    if(curr_guess_file == NULL) {
        curr_guess_file = files;
        current_file_name = curr_guess_file->name;
    }

    do 
    {
        if(read_guess_line < guess_lines)
        {
            if(current_file_lineno == curr_guess_file->lineno)
            {
                curr_guess_file = curr_guess_file->next;
                while(!curr_guess_file->lineno)
                {
                    curr_guess_file = curr_guess_file->next;
                }
                current_file_name = curr_guess_file->name;
                current_file_lineno = 0;
            }
            current_file_lineno++;
            ret = guess_buffer[read_guess_line];
            *len = guess_line_length[read_guess_line];
            read_guess_line++;
        } else if(current_file != NULL)
        {
            *len = read_input_line(stype);
            ret = read_buffer;
        }
        if(*len == -1) 
        { 
            ret = NULL;
        } else
        {
            current_total_lineno++;
        }
    } while(current_file_lineno == 1 && headers == HEADER_ALL && current_total_lineno > 1);

    return ret;
}

/* bpositions will be updated according current input buffer 
   */
void 
update_field_positions(char *type,uint8_t quote,int arbit_len,struct field *fields,int len,uint8_t *buffer)
{
    register uint8_t *p = buffer;
    register int inside_quote = 0;
    struct field *f = fields;

    switch(type[0])
    {
        case BINARY:
            break;
        case FIXED_LENGTH:
            if(arbit_len == RL_MIN)
            {
                while(f != NULL)
                {
                    if(f->next == NULL) // check last field existense
                    {
                        if(len > f->position)
                        {
                            f->bposition = f->position;
                        } else
                        {
                            f->bposition = -1;
                        }
                    }
                    f = f->next;
                }
            }
            break;
        case SEPARATED:
            while(f != NULL) 
            {
                f->bposition = -1;
                f = f->next;
            }

            f = fields;

            while(*p && f != NULL)
            {
                if(p == buffer)  // first
                {
                    if(*p != type[1]) f->bposition = 0;
                    f = f->next;
                } 
                if((*p == type[1] && !inside_quote))
                {
                    p++;
                    if(type[2] == '*') while(*p == type[1]) p++;
                    if(*p != type[1]) 
                    {
                        f->bposition = (int) (p - buffer);
                    } else
                    {
                        p--;
                    }
                    f = f->next;
                }
                if(((*p == quote && p[1] == quote) || (*p == '\\' && p[1] == quote)) && inside_quote && quote)
                {
                    p++;
                } else if(*p == quote && quote)
                {
                    inside_quote = !inside_quote;
                }
                if(*p) p++;
            }
            break;
    }
}
                       
/* check which record applies to current line */
struct record *
select_record(struct structure *s,int length,uint8_t *buffer)
{
    register struct record *r;

    r = s->r;

    while(r != NULL)
    {
        if(vote_record(s->quote,s->type,s->header,r,length,buffer)) return r;
        r = r->next;
    }

    return NULL;
}

/* print a single fixed field */
void
print_fixed_field(uint8_t format,struct field *f,uint8_t *buffer)
{
    register int i = 0;
    register uint8_t *data;
    uint8_t *start = write_pos;

    if(f->const_data != NULL)
    {
        data = f->const_data;
        if (format == 't') format = 'd';  // Dont't trim consts
    } else
    {
        if(f->bposition < 0) return;  /* last variable length field is missing */
        data = &buffer[f->bposition];
    }

    switch(format)
    {
        case 'd':
        case 'D':
        case 'C':
        case 'e':
        case 'x':
            if(f->length)
            {
                while(i < f->length && data[i]) writec(data[i++]);
            } else
            {
                writes(data);
            }
            break;
        case 't':
            while(isspace(data[i])) i++;
            if(f->length)
            {
                while(i < f->length && data[i]) writec(data[i++]);
            } else
            {
                writes(&data[i]);
            }
            if(write_pos > start && isspace(write_pos[-1]))
            {
                write_pos--;
                while(write_pos > start && (isspace(*write_pos))) write_pos--;
                write_pos++;
            }
            break;
    }
}

/* print a single binary field 
   if field type is ASC, fixed field printing is used 
*/
void
print_binary_field(uint8_t format,struct field *f,uint8_t *buffer)
{
    register uint8_t *p,*data_end;
    uint8_t *data,c;
    char *pf;
    static uint8_t pb[2 * FIELD_SIZE];
    
    if(f->const_data != NULL || (f->type == F_ASC && format != 'h'))
    {
        print_fixed_field(format,f,buffer);
        return;
    }

    switch(f->type)
    {
        case F_INT:
        case F_UINT:
        case F_FLOAT:
        case F_DOUBLE:
            data = endian_and_align(&buffer[f->bposition],system_endianess,f->endianess,f->length);
            break;
        default:
            data = &buffer[f->bposition];
            break;
    }

    pb[0] = 0;

    switch(format)
    {
        case 'h':
            p = &buffer[f->bposition];  // original data before any endian things
            data_end = p + f->length;
            while(p < data_end)
            {
                writec('x');
                writec(htocb(*p));
                writec(htocl(*p));
                p++;
            }
            break;
        case 'd':
        case 't':
        case 'D':
        case 'C':
        case 'x':
            switch(f->type)
            {
                case F_CHAR:
                    writec(*data);
                    break;
                case F_INT:
                    switch(f->length)
                    {
                        case 1:
                            pf = format == 'x' ? "%x" : "%i";
                            sprintf(pb,pf,(int) *(int8_t *) data);
                            break;
                        case 2:
                            pf = format == 'x' ? "%x" : "%i";
                            sprintf(pb,pf,(int) *(int16_t *) data);
                            break;
                        case 4:
                            pf = format == 'x' ? "%lx" : "%li";
                            sprintf(pb,pf,(long int) *(int32_t *) data);
                            break;
                        case 8:
                            pf = format == 'x' ? "%llx" : "%lli";
                            sprintf(pb,pf,(long long int) *(int64_t *) data);
                            break;
                    }
                    writes(pb);
                    break;
                case F_UINT:
                    switch(f->length)
                    {
                        case 1:
                            pf = format == 'x' ? "%x" : "%u";
                            sprintf(pb,pf,(unsigned int) *(uint8_t *) data);
                            break;
                        case 2:
                            pf = format == 'x' ? "%x" : "%u";
                            sprintf(pb,pf,(unsigned int) *(uint16_t *) data);
                            break;
                        case 4:
                            pf = format == 'x' ? "%lx" : "%lu";
                            sprintf(pb,pf,(unsigned long int) *(uint32_t *) data);
                            break;
                        case 8:
                            pf = format == 'x' ? "%llx" : "%llu";
                            sprintf(pb,pf,(unsigned long long int) *(uint64_t *) data);
                            break;
                    }
                    writes(pb);
                    break;
                case F_FLOAT:
                    sprintf(pb,"%f",(double) *(float *) data);
                    writes(pb);
                    break;
                case F_DOUBLE:
                    sprintf(pb,"%f",(double) *(double *) data);
                    writes(pb);
                    break;
                case F_BCD:
                    p = data;
                    data_end = p + f->length;
                    switch(f->endianess)
                    {
                        case F_BIG_ENDIAN:
                            do
                            {
                                c = bcdtocb(*p);
                                if(c)
                                {
                                    writec(c);
                                    c = bcdtocl(*p);
                                    if(c) writec(c);
                                }
                                p++;
                            } while(p < data_end && c);
                            break;
                        case F_LITTLE_ENDIAN:
                            do
                            {
                                c = bcdtocl(*p);
                                if(c)
                                {
                                    writec(c);
                                    c = bcdtocb(*p);
                                    if(c) writec(c);
                                }
                                p++;
                            } while(p < data_end && c);
                            break;
                    }
                    break;
                case F_HEX:
                    p = data;
                    data_end = p + f->length;
                    switch(f->endianess)
                    {
                        case F_BIG_ENDIAN:
                            while(p < data_end)
                            {
                                writec(htocb(*p));
                                writec(htocl(*p));
                                p++;
                            }
                            break;
                        case F_LITTLE_ENDIAN:
                            p += f->length - 1;
                            while(p >= data)
                            {
                                writec(htocb(*p));
                                writec(htocl(*p));
                                p--;
                            }
                            break;
                    }
                    break;
            }
    }
}



/* print a single separated field */
void
print_separated_field(uint8_t format,uint8_t quote,uint8_t separator,struct field *f,uint8_t *buffer)
{
    register uint8_t *p;
    uint8_t *start;
    int inside_quote = 0;

    start = write_pos;

    if(f->const_data != NULL)   // use fixed field printing for const data
    {
        print_fixed_field(format,f,buffer);
    } else
    {
        if(f->bposition < 0)
        {
            if(format == 'D' || format == 'C') while(write_pos - start < f->length) writec(' ');
            return;
        }

        switch(format)
        {
            case 'd':
            case 't':
            case 'D':
            case 'C':
            case 'e':
            case 'x':
                p = &buffer[f->bposition];
                if(quote || format == 't') while(*p != separator && isspace(*p)) p++;
                if(*p == quote && quote) 
                {
                    p++;
                    inside_quote = 1;
                    if(format == 't') while(isspace(*p)) p++;
                }
                while((*p != separator || inside_quote) && *p)
                {
                    if(((*p == quote && p[1] == quote) || (*p == '\\' && p[1] == quote)) && quote) 
                    {
                        p++;
                    } else if(*p == quote)
                    {
                        if(inside_quote)
                        {
                            if(format == 't' && isspace(write_pos[-1]))
                            {
                                write_pos--;     
                                while(write_pos > start && isspace(*write_pos)) write_pos--;
                                write_pos++;
                            }
                            if(format == 'D' || format == 'C') while(write_pos - start == f->length) writec(' ');
                            return;
                        }
                    }

                    if(format == 'C' && (write_pos - start) == f->length) return;
                    writec(*p);
                    if(*p) p++;
                }            
                if(format == 't' && isspace(write_pos[-1]))
                {
                    write_pos--;     
                    while(write_pos > start && isspace(*write_pos)) write_pos--;
                    write_pos++;
                }
                if(format == 'D' || format == 'C') while(write_pos - start < f->length) writec(' ');
                break;
        }
    }
}

/* search the lookup table, return the found value */
uint8_t *
make_lookup(struct lookup *l,uint8_t *search)
{
    register uint8_t *k,*s;
    int search_len = strlen(search);
    uint8_t *ret_val = NULL;
    uint8_t *longest = NULL;
    struct lookup_data *d;

    switch(l->type)
    {
        case EXACT:

            d = l->data[hash(search,0)];

	    while(d != NULL && ret_val == NULL)
	    {
                k = d->key;
		s = search;
		while(*k == *s && *k) 
		{
		   k++;
		   s++;
		}
		if(!*s && !*k) ret_val = d->value;
		d = d->next;
	    }
	    break;
	case LONGEST:
            while(search_len && ret_val == NULL)
	    {
                d = l->data[hash(search,0)];

	        while(d != NULL && ret_val == NULL)
	        {
		    k = d->key;
		    s = search;
		    while(*k == *s && *k) 
		    {
			    k++;
			    s++;
		    }
		    if(!*s && !*k) ret_val = d->value;
		    d = d->next;
	         }
                 search_len--;
                 search[search_len] = 0;
	    }
	    break;
    }

    if(ret_val == NULL) ret_val = l->default_value;

    return ret_val;
}

void
print_indent(uint8_t *buffer,int times)
{
    start_write();
    while(times--) writes(buffer);
    flush_write();
}


/* print fields */
/* returns the count of fields actually printed */
int
print_fields(struct structure *s, struct record *r,uint8_t *buffer)
{
    uint8_t num[64];
    int max_justify_len = 0;
    register uint8_t *d;
    uint8_t *f;
    int i;
    uint8_t justify = LEFT_JUSTIFY;
    uint8_t *indent,*separator;
    uint8_t *data_start,*rep_start,*rep_pos;
    uint8_t *field_start;
    uint8_t *lookup_value;
    int retval = 0;
    int replacing,just_replaced;
    int lookup_len;
    struct print_field *pf = r->pf;
    struct output *o;

    while(pf != NULL) {
        pf->justify_length = -1;
        pf = pf->next;
    }

    pf = r->pf;

    start_write();
    
    while(pf != NULL)
    {
        o = pf->f->o ? pf->f->o : r->o;

        if(o != no_output)
        {
            justify = o->justify;
            d = o->data;
            data_start = write_pos;
            pf->data = data_start;
            pf->empty = 1;
            replacing = 0;
            just_replaced = 0;
            lookup_value = NULL;

            if(pf->f->lookup != NULL) d = o->lookup;

            if(o->hex_cap)
            {
                bcd_to_ascii = bcd_to_ascii_cap;
                hex_to_ascii = hex_to_ascii_cap;
            } else
            {
                bcd_to_ascii = bcd_to_ascii_low;
                hex_to_ascii = hex_to_ascii_low;
            }

            while(*d)
            {
                if(justify == *d)
                {
                    if(justify != LEFT_JUSTIFY && justify != RIGHT_JUSTIFY && pf->justify_length == -1 && !replacing)
                    {
                        pf->justify_length = (int) (write_pos - data_start);
                        if(pf->justify_length > max_justify_len)
                        {
                            max_justify_len = pf->justify_length;
                        }
                    } 
                }

                if(*d == '%')
                {
                    d++;
                    switch(*d)
                    {
                        case 'f':
                            writes(current_file_name);
                            break;
                        case 's':
                            writes(s->name);
                            break;
                        case 'r':
                            if(r != NULL) writes(r->name);
                            break;
                        case 'o':
                            sprintf(num,"%ld",current_file_lineno);
                            writes(num);
                            break;
                        case 'O':
                            sprintf(num,"%ld",current_total_lineno);
                            writes(num);
                            break;
                        case 'I':
                            sprintf(num,"%lld",current_offset);
                            writes(num);
                            break;
                        case 'i':
                            sprintf(num,"%lld",current_file_offset);
                            writes(num);
                            break;
                        case 'p':
                            if(pf->f->const_data == NULL)
                            {
                                sprintf(num,"%d",pf->f->position + (s->type[0] != SEPARATED ? 1 : 0));
                                writes(num);
                            }
                            break;
                        case '%':
                            writec('%');
                            break;
                        case 'n':
                            writes(pf->f->name);
                            break;
                        case 'l':
                        case 'L':
                            if(pf->f->lookup != NULL)
                            {
                                if(lookup_value == NULL)
                                {
                                    field_start = write_pos;  // misuse write buffer for temp space for field value
                                    switch(s->type[0])        // write trimmed data for search key
                                    {
                                        case FIXED_LENGTH:
                                            print_fixed_field('t',pf->f,buffer);
                                            break;
                                        case SEPARATED:
                                            print_separated_field('t',s->quote,s->type[1],pf->f,buffer);
                                            break;
                                        case BINARY:
                                            print_binary_field('t',pf->f,buffer);
                                            break;
                                    }
                                    writec(0);
                                    lookup_value = make_lookup(pf->f->lookup,field_start);
                                    write_pos = field_start;  // restore write buffer
                                }
                            } else if(lookup_value ==  NULL)
                            {
                                lookup_value = "";
                            }

                        writes(lookup_value);

                        if(*d == 'L')
                        {
                            lookup_len = strlen(lookup_value);
                            while(lookup_len++ < pf->f->length) writec(' ');
                        }
                        break;
                    case 'h':
                        if(s->type[0] == BINARY) print_binary_field(*d,pf->f,buffer);
                        break;
                    case 'd':
                    case 't':
                    case 'D':
                    case 'C':
                    case 'e':
                    case 'x':
                        field_start = write_pos;

                        if(pf->f->rep != NULL && !replacing) // start possible replacing
                        {
                            replacing = 1;
                            just_replaced = 1;
                            rep_pos = d;
                            rep_start = write_pos;
                            d = pf->f->rep->value;
                            break; // end case
                        } 
                        
                        switch(s->type[0])
                        {
                            case FIXED_LENGTH:
                                print_fixed_field(*d,pf->f,buffer);
                                break;
                            case SEPARATED:
                                print_separated_field(*d,s->quote,s->type[1],pf->f,buffer);
                                break;
                            case BINARY:
                                print_binary_field(*d,pf->f,buffer);
                                break;
                        }

                        if(!o->print_empty)
                        {
                            f = field_start;
                            while(f < write_pos)
                            {
                                if(strchr(o->empty_chars,*f) == NULL)
                                {
                                    pf->empty = 0;
                                    f = write_pos; // to stop while loop
                                }
                                f++;
                            }
                        }
                        if(*d == 'e') write_pos = field_start;
                        break;
                    default:
                        writec('%');
                        writec(*d);
                        break;
                    }
                    if(!just_replaced) d++;
                } else 
                {
                    writec(*d);
                    d++;
                }


                if(replacing && !*d)
                {
                    d = rep_pos;
                    d++;
                    replacing = 0;
                    if(*rep_pos == 'D' || *rep_pos == 'C')
                    {
                        if(write_pos - rep_start < pf->f->length)
                        {
                            while(write_pos - rep_start < pf->f->length) writec(' ');
                        } else if(write_pos - rep_start > pf->f->length)
                        {
                            write_pos = rep_start + pf->f->length;
                        }
                    }
                }

                if(just_replaced) just_replaced = 0;
            }

            if(justify == RIGHT_JUSTIFY)
            {
                pf->justify_length = (int) (write_pos - data_start);
                if (pf->justify_length > max_justify_len)
                {
                    max_justify_len = pf->justify_length;
                }
            }
            writec(0);    // end of data marker
        }
        pf = pf->next;
    }

    pf = r->pf;

    /* count the number of fields to be printed */
    /* we need this before hand, because we must know if the is att least */
    /* one field to be printed, then all separators must be printed */
    while(pf != NULL) 
    {
        o = pf->f->o ? pf->f->o : r->o;

        if(o != no_output)
        {
            if(o->print_empty || !pf->empty)
            {
                retval++;
            }
        }
        pf = pf->next;
    }

    pf = r->pf;

    while(pf != NULL && retval)
    {
        o = pf->f->o ? pf->f->o : r->o;

        if(o != no_output)
        {
            separator = o->separator;
            indent = o->indent;
            if(o->print_empty || !pf->empty)
            {
                if(indent != NULL)
                {
                    if(r->level)
                    {
                        int i;
                        i = get_indent_depth(r->level->level);
                        while(i--) fputs(indent,output_fp);
                    } else
                    {
                        fputs(indent,output_fp);
                        fputs(indent,output_fp);
                    }
                }
                if((justify != LEFT_JUSTIFY && justify != RIGHT_JUSTIFY && max_justify_len) || justify == RIGHT_JUSTIFY)
                {
                    i = max_justify_len - pf->justify_length;
                    if(pf->justify_length > -1 && i)
                    {
                        while(i > JUSTIFY_STRING - 1)
                        {
                            justify_string[JUSTIFY_STRING - 1] = (uint8_t) 0;
                            fputs(justify_string,output_fp);
                            i -= JUSTIFY_STRING - 1;
                        }
                        justify_string[i] = (uint8_t) 0;
                        fputs(justify_string,output_fp);
                        justify_string[i] = ' ';
                    }
                }
                fputs(pf->data,output_fp);
            }
            if(pf->next != NULL && separator != NULL) fputs(separator,output_fp);
        }
        pf = pf->next;
    }
    return retval;
}
                
        

/* make a list of printable fields 
   if include list is non empty use names from it 
   else use the whole field list in f */
struct print_field *
make_print_list(struct include_field *fl,struct field *f)
{
    struct print_field *ret = NULL,*c = NULL;
    struct field *n;

    if(fl == NULL)
    {
        while(f != NULL)
        {
            if(strcmp(f->name,"FILLER") != 0)
            {
                if(ret == NULL)
                {
                    ret = xmalloc(sizeof(struct print_field));
                    c = ret;
                } else
                {
                    c->next = xmalloc(sizeof(struct print_field));
                    c = c->next;
                }
                c->f = f;
                c->next = NULL;
            }
            f = f->next;
        }
    } else
    {
        while(fl != NULL)
        {
            n = const_field;        // first check const list

            while(n != NULL && (strcasecmp(fl->name,n->name) != 0))
            {
                n = n->next;
            }

            if(n == NULL)           // if not found in const list check input fields
            {
                n = f;
                while(n != NULL && (strcasecmp(fl->name,n->name) != 0))
                {
                    n = n->next;
                }
            }

            if(n != NULL) 
            {
                if(ret == NULL)
                {
                    ret = xmalloc(sizeof(struct print_field));
                    c = ret;
                } else
                {
                    c->next = xmalloc(sizeof(struct print_field));
                    c = c->next;
                }
                c->f = n;
                c->next = NULL;
                fl->found = 1;
            }
            fl = fl->next;
        }
    }
    return ret;
}

/* check that all fields we found from current structure */
/* return the count of unmatched fields */
int
check_field_list(struct include_field *fl)
{
    int ret = 0;

    while(fl != NULL)
    {
        if(!fl->found) 
        {
            if(!fl->reported) fprintf(stderr,"%s: Field '%s' was not found in the current structure\n",program,fl->name);
            fl->reported = 1;
            ret++;
        }
        fl = fl->next;
    }
    return ret;
}

void
init_structure(struct structure *s,struct record *current_record,int length, uint8_t *buffer)
{
    struct record *r;
    struct field *f;
    struct replace *rep;
    struct expression *e;
    int list_errors = 0;

    r = s->r;

    while(r != NULL)
    {
        f = r->f;
        while(f != NULL)
        {
            if(s->header && f->name == NULL) 
                f->name = xstrdup(get_separated_field(f->position,s->quote,s->type,buffer));

            rep = replace;
            while(rep != NULL)
            {
                if(strcasecmp(f->name,rep->field) == 0)
                {
                    f->rep = rep;
                    rep->found = 1;
                }
                rep = rep->next;
            }

            e = expression;
            while(e != NULL)
            {
                if(strcasecmp(f->name,e->field) == 0)
                {
                    e->found = 1;
                }
                e = e->next;
            }

            f = f->next;
        }
        r = r->next;
    }

    r = s->r;

    while(r != NULL)
    {
        if(r->o != no_output) r->pf = make_print_list(r->o->fl,r->f);
        r = r->next;
    }

    r = s->r;

    while(r != NULL)
    {
        if(check_field_list(r->o->fl)) list_errors++;
        r = r->next;
    }
    if(list_errors) panic("Some fields from field list were not found in the current structure or constant values",s->name,NULL);

    rep = replace;
    while(rep != NULL)
    {
        if(!rep->found) panic("Field to be replaced was not found in the current structure",rep->field,NULL);
        rep = rep->next;
    }

    e = expression;
    while(e != NULL)
    {
        if(!e->found) panic("Field in expression was not found in the current structure",e->field,NULL);
        e = e->next;
    }
}

void
invalid_input(char *file, long int lineno,int strict,int length,int stype)
{
    if(stype == BINARY)
    {
        if(strict)
        {
            fprintf(stderr,"%s: Invalid input block in file \'%s\', offset %lld",program,file,current_file_offset);
            if(debug_lineno) fprintf(stderr,". Block was written to debug file");
            fprintf(stderr,"\n"); 
        }
    } else
    {
        fprintf(stderr,"%s: Invalid input line in file \'%s\', line %ld, line length = %d",program,file,lineno,length);
        if(debug_lineno) fprintf(stderr,", line %ld in debug file",debug_lineno);
        fprintf(stderr,"\n");
    }
    if(strict) panic("Using option -l does not cause program to abort in case of invalid input",NULL,NULL);
}


static int
hash_scan_expression(struct expr_list **list,char *value,int casecmp)
{
	register struct expr_list *l = list[hash(value,0)];

	while(l != NULL)
	{
		if(casecmp)
		{
			if(strcasecmp(l->value,value) == 0) return 1;
		} else
		{
			if(strcmp(l->value,value) == 0) return 1;
		}
                l = l->next;
	}
        return 0;
} 
    
static int
hash_scan_expression_len(struct expr_list **list,char *value,int casecmp,size_t comp_len)
{
	register struct expr_list *l = list[hash(value,comp_len)];

	while(l != NULL)
	{
		if(casecmp)
		{
			if(strncasecmp(l->value,value,l->value_len) == 0) return 1;
		} else
		{
			if(strncmp(l->value,value,l->value_len) == 0) return 1;
		}
                l = l->next;
	}
        return 0;
} 

static inline
scan_expression_list(struct expr_list *l,char *value,char op,int casecmp)
{
    while(l != NULL)
    {
        switch(op)
        {
            case OP_START:
                if(casecmp)
                {
                    if(strncasecmp(l->value,value,l->value_len) == 0) return 1;
                } else
                {
                    if(strncmp(l->value,value,l->value_len) == 0) return 1;
                }
                break;
            case OP_EQUAL:
            case OP_NOT_EQUAL:
                if(casecmp)
                {
                    if(strcasecmp(l->value,value) == 0) return 1;
                } else
                {
                    if(strcmp(l->value,value) == 0) return 1;
                }
                break;
            case OP_CONTAINS:
                if(casecmp)
                {
                    if(strcasestr(value,l->value) != NULL) return 1;
                } else
                {
                    if(strstr(value,l->value) != NULL) return 1;
                }
                break;
#ifdef HAVE_REGEX
            case OP_REQEXP:
                if(regexec(&l->reg,value,(size_t) 0, NULL, 0) == 0) return 1;
                break;
#endif
        }
        l = l->next;
    }
    return 0;
}

static int
full_scan_expression(struct expression *e,char *value,int casecmp)
{
    register int i = 0;
    struct expr_list *l;


    if(e->fast_entries)
    {
        while(i < e->fast_entries)
        {
            if(scan_expression_list(e->expr_hash[e->fast_expr_hash[i]],value,e->op,casecmp)) return 1;
            i++;
        }
    } else
    {
        while(i < MAX_EXPR_HASH)
        {
            if(scan_expression_list(e->expr_hash[i],value,e->op,casecmp)) return 1;
            i++;
        }
    }
    return 0;
} 

/* returns true if and = 0 and attleast one expression is true 
 * or and = 1 and all expressions are true
 */
int
eval_expression(struct structure *s,struct record *r, int and,int invert, int casecmp, uint8_t *buffer)
{
    struct expression *e = expression;
    struct expr_list *el;
    int retval = 0;
    int eq_found;
    int prev_retval;
    int loop_break = 0;
    int expression_count = 0;
    struct output *o;
    size_t len;

    if(e == NULL) return 0;

    while(e != NULL && !loop_break)
    {
        if(e->f != NULL)
        {

            o = e->f->o ? e->f->o : r->o;
            if(o != no_output && o->hex_cap)
            {
                bcd_to_ascii = bcd_to_ascii_cap;
                hex_to_ascii = hex_to_ascii_cap;
            } else
            {
                bcd_to_ascii = bcd_to_ascii_low;
                hex_to_ascii = hex_to_ascii_low;
            }


            start_write();
            switch(s->type[0])
            {
                case FIXED_LENGTH:
                    print_fixed_field('d',e->f,buffer);
                    break;
                case SEPARATED:
                    print_separated_field('d',s->quote,s->type[1],e->f,buffer);
                    break;
                case BINARY:
                    print_binary_field('d',e->f,buffer);
                    break;
            }
            writec(0);  // end of string
            prev_retval = retval;
            eq_found = 0;

            switch(e->op)
            {
                case OP_START:
                    len = e->exp_max_len;
                    prev_retval = retval;
                    while(len >= e->exp_min_len && prev_retval == retval)
                    {
                        retval += hash_scan_expression_len(e->expr_hash,write_buffer,casecmp,len);
                        len--;
                    }
                    break;
                case OP_CONTAINS:
                case OP_REQEXP:
                    retval += full_scan_expression(e,write_buffer,casecmp);
                    break;
                case OP_EQUAL:
                    retval += hash_scan_expression(e->expr_hash,write_buffer,casecmp);
                    break;
                case OP_NOT_EQUAL:
                    if(hash_scan_expression(e->expr_hash,write_buffer,casecmp) == 0) retval++;
                    break;
            } 
        }
        if(!and && retval)
        {
            loop_break = 1;
        }
        expression_count++;
        if(and && retval != expression_count)
        {
            loop_break = 1;
        }
        e = e->next;
    }
    if(and)
    {
        if (retval == expression_count)
        {
            retval = 1;
        } else
        {
            retval = 0;
        }
    }
    if(invert) retval = !retval;
    return retval;
}
/* initialize expressions field pointers */
/* pointers are preinitialized for faster execution */
    void
init_expression_list(struct record *r)
{
    struct expression *e;
    struct field *f = r->f;
    int exp_count = 0;

    e = expression;

    while(e != NULL)
    {
        e->f = NULL;
        e = e->next;
        exp_count++;
    }

    e = expression;

    while(f != NULL && exp_count)
    {
        e = expression;
        while(e != NULL && exp_count)
        {
            if(e->f == NULL && strcasecmp(f->name,e->field) == 0)
            {
                e->f = f;
                exp_count--;
            }
            e = e->next;
        }
        f = f->next;
    }
}

    void
open_debug_file(int stype)
{
    sprintf(debug_file,"ffe_error_%d.log",(int) getpid());
    if(stype == BINARY)
    {
        debug_fp = xfopenb(debug_file,"w");
    } else
    {
        debug_fp = xfopen(debug_file,"w");
    }
}


void
write_debug_file(uint8_t *line,int len,int stype)
{
    if(debug_fp == NULL) open_debug_file(stype);

    if(stype == BINARY)
    {
        fwrite(line,len,1,debug_fp);
    } else if(line != NULL)
    {
        fputs(line,debug_fp);
        fputs("\n",debug_fp);
    }
    debug_lineno++;
}

static void
select_output(struct output *o)
{
    if(o->ofp != NULL)
    {
        output_fp = o->ofp;
        output_file = o->output_file;
    } else
    {
        output_fp = default_output_fp;
        output_file = default_output_file;
    }
}


/* main loop for execution */
void 
execute(struct structure *s,int strict, int expression_and,int expression_invert,int expression_case, int debug)
{
    uint8_t *input_line;
    struct record *r = NULL;
    struct record *prev_record = NULL;
    int length;
    int header_printed = 0;
    int fields_printed;
    int first_line = 1;
    int i;

    current_file_lineno = 0;
    current_total_lineno = 0;
    current_file_name = files->name;
    headers = s->header;

    i = 0;
    while(i < JUSTIFY_STRING) justify_string[i++] = ' ';

    reset_levels(0,MAXLEVEL);

    write_buffer = xmalloc(write_buffer_size);
    write_buffer_end = write_buffer + (write_buffer_size - 1);

    if(s->type[0] == BINARY && read_buffer_size == READ_LINE_LEN)
    {
        read_buffer_size = s->max_record_len;
        read_buffer = xrealloc(read_buffer,read_buffer_size);
    }

    select_output(s->o);
    print_text(s,NULL,s->o->file_header);
    while((input_line = get_input_line(&length,s->type[0])) != NULL)
    {
        prev_record = r;
        r = select_record(s,length,input_line);
        if(r == NULL) 
        {
            if(debug) write_debug_file(input_line,length,s->type[0]);
            invalid_input(current_file_name,current_file_lineno,strict,length,s->type[0]);
            if(s->type[0] == BINARY)
            {
                last_consumed = 1; /* advance one byte and check next block, this is a rather expensive way to read ahead... */
            }

        } else
        {
            last_consumed = r->length;
            if(first_line)
            {
                init_structure(s,r,length,input_line);
            }

            if(expression != NULL && (prev_record != r || prev_record == NULL))
            {
                init_expression_list(r);
            }

            if((!first_line || !headers) && r->o != no_output)
            {
                if((r->pf == NULL && r->o->no_data == 1) || r->pf != NULL || r->o == raw)
                {
                    update_field_positions(s->type,s->quote,r->arb_length,r->f,length,input_line);
                    if(expression == NULL || (eval_expression(s,r,expression_and,expression_invert,expression_case,input_line)))
                    {
                        if(r->o == raw)
                        {
                            print_raw(s->type[0] == BINARY ? r->length : length,input_line,s->type[0]);
                        } else
                        {
                            select_output(r->o);
                            print_level_before(prev_record,r);
                            if(r->o->header != NULL && !header_printed) 
                            {
                                header_printed = print_header(s,r);
                            }
                            if(r->o->indent != NULL && r->o->record_header != NULL) 
                                print_indent(r->o->indent,r->level != NULL ? r->level->level : 1);
                            print_text(s,r,r->o->record_header);
                            fields_printed = print_fields(s,r,input_line);
                            if(fields_printed || r->o->print_empty)
                            {
                                if(r->o->indent != NULL && r->o->record_trailer != NULL) 
                                    print_indent(r->o->indent,r->level != NULL ? r->level->level : 1);
                                print_text(s,r,r->o->record_trailer);
                            }
                        }
                    }
                }
            } 
            if(first_line) {
                if(headers) r = NULL;
                first_line = 0;
            }
        }
    }
    print_level_end(r);
    select_output(s->o);
    print_text(s,r,s->o->file_trailer);
    free(write_buffer);
    if(debug_fp != NULL) fclose(debug_fp);
}



