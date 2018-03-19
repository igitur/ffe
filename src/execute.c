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
#include <wchar.h>
#ifdef HAVE_PRINTF_H
#include <printf.h>
#endif


#ifdef PACKAGE
static char *program = PACKAGE;
#else
static char *program = "ffe";
#endif

#define READ_LINE_LEN 33554432
#define READ_LINE_LEN_HIGH (READ_LINE_LEN - 524288)



#define GUESS_LINES 1000
#define GUESS_BUFFER 524288
#define FIELD_SIZE (128 * 1024)
#define WRITE_BUFFER (2 * 1024 * 1024)
#define JUSTIFY_STRING 128

extern int update_anon_info(struct structure *,char *);
extern void init_libgcrypt();


extern struct replace *replace;

struct input_file *files = NULL;
static struct input_file *current_file = NULL;
static FILE *input_fp = NULL;
static int ungetchar = -1;
static char *default_output_file = NULL;
static FILE *default_output_fp = NULL;

static char *output_file = NULL;
static FILE *output_fp = NULL;

static uint8_t *read_buffer = NULL;
static uint8_t *read_buffer_start = NULL;
static uint8_t *read_buffer_high_water = NULL;

static size_t last_consumed = 0;  /* for binary reads */
static uint8_t *field_buffer = NULL;
static int field_buffer_size = FIELD_SIZE;
static int guess_lines = 0;

static int eocf = 0;
static int ccount = -1;
static int orig_ccount = -1;

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


/* Pipe management */
#define PIPE_OUTPUT_LEN 1048576
static uint8_t pipe_output[PIPE_OUTPUT_LEN];

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

uint8_t *bcd_to_ascii;
uint8_t *hex_to_ascii;


static void print_binary_field(uint8_t,struct field *,uint8_t *);
static void print_fixed_field(uint8_t,struct field *,uint8_t *);

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

    read_buffer = read_buffer_start;

    return ret;
}


void
open_input_file(int stype)
{
    read_buffer_start = xmalloc(READ_LINE_LEN);
    read_buffer = read_buffer_start;
    read_buffer_high_water = read_buffer_start + READ_LINE_LEN_HIGH;

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
            if(*ccount >= READ_LINE_LEN) panic("Input file cannot be guessed, use -s option",NULL,NULL);
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


/* find next lineend and return line length
*/
static int
find_next_LF(uint8_t *start,int length)
{
    register uint8_t *p;

    p = memchr(start,'\n',length);
      
    if(p == NULL) 
    {
        start[length] = '\n';      // add  missing LF
        return length;
    }
    return p - start;
}



/* reads one file from input */
/* returns the line length */
/* return -1 on EOF */
int
read_input_line(int stype)
{
    int retval;
    size_t unused;

    do
    {
        if(stype == BINARY) 
        {
            current_offset += (long long) last_consumed;
            current_file_offset += (long long) last_consumed;
        }

        if(ccount <= 0)
        {
             ccount = uc_fread(read_buffer_start,1,READ_LINE_LEN,input_fp);
             if(ccount < READ_LINE_LEN) eocf = 1;
             read_buffer = read_buffer_start;
        } else
        {
            if(read_buffer + last_consumed >= read_buffer_high_water && !eocf)
            {
                unused = READ_LINE_LEN-(read_buffer-read_buffer_start)-last_consumed;

                memmove(read_buffer_start,read_buffer+last_consumed,unused);
                ccount = uc_fread(read_buffer_start+unused,1,READ_LINE_LEN - unused,input_fp);
                if(ccount < READ_LINE_LEN - unused) eocf = 1;
                read_buffer = read_buffer_start;
                ccount += unused;
            } else
            {
                read_buffer += last_consumed;
                ccount -= last_consumed;
            }
        } 

        retval = ccount;

        if(ccount > 0 && stype != BINARY)
        {
            retval = find_next_LF(read_buffer,ccount);
            last_consumed = retval + (ccount > retval ? 1 : 0);   // add lf
#ifdef WIN32
            if(retval && read_buffer[retval - 1]  == '\r') {
                retval--;
                if(retval && read_buffer[retval - 1]  == '\r') retval--; /* There might be two CRs? */
            }
#endif
        } else
        {
            last_consumed = 0;
        }


        if(ccount == 0)
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
                        current_file_offset = 0;
                    } 
                }
                eocf = 0;
                last_consumed = 0;
                current_file_name = current_file->name;
                current_file->lineno = 0;
                current_file_offset = 0;
            } else
            {
                retval = -1;
            }
        } else
        {
            if(stype != BINARY) current_file->lineno++;
        }
    } while(ccount == 0 && current_file != NULL);

    if(ccount > 0)
    {
        current_file_lineno = current_file->lineno;
    }
    return retval;
}

/* calculate field count from line containing separated fields */
int
get_field_count(uint8_t quote, uint8_t *type, uint8_t *line)
{
    int inside_quote = 0;
    int fields = 0;
    register uint8_t *p = line;

    if(type[0] != SEPARATED) return 0;

#ifdef WIN32
    if (*p != '\n' || *p != '\r') fields++; /* at least one */
#else
    if (*p != '\n') fields++; /* at least one */
#endif


#ifdef WIN32
    while(*p != '\n' && *p != '\r')
#else
    while(*p != '\n')
#endif
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
#ifdef WIN32
        if(*p != '\n' && *p != '\r') p++;
#else
        if(*p != '\n') p++;
#endif
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

#ifdef WIN32
    while(*p != '\n' && *p != '\r' && fieldno <= position)
#else
    while(*p != '\n' && fieldno <= position)
#endif
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
#ifdef WIN32
        if(*p != '\n' && *p != '\r') p++;
#else
        if(*p != '\n') p++;
#endif
    }
    field_buffer[i] = 0;
    return field_buffer;
}
    


/* read input stream once to read_buffer
   if allready read (orig_ccount > -1)
   reset pointrs and ccount
 */
int
init_guessing()
{            
    if(orig_ccount == -1)
    {
        ccount = uc_fread(read_buffer_start,1,READ_LINE_LEN,input_fp);
        orig_ccount = ccount;
        if(ccount < READ_LINE_LEN) eocf = 1;
    } else
    {
        ccount = orig_ccount;
    }
    read_buffer = read_buffer_start;
    last_consumed = 0;

    return ccount;
}

/* read one input line from buffer for guessing, 
   return line length
   -1 if buffer consumed
 */
static int
read_guess_line()
{
    int retval = -1;

    read_buffer += last_consumed;
    ccount -= last_consumed;

    if(ccount > 0)
    {
        retval = find_next_LF(read_buffer,ccount);
        last_consumed = retval + (ccount > retval ? 1 : 0);
        current_file->lineno++;
        current_file_lineno = current_file->lineno;
#ifdef WIN32
        if(retval && read_buffer[retval - 1]  == '\r') {
            retval--;
            if(retval && read_buffer[retval - 1]  == '\r') retval--; /* There might be two CRs? */
        }
#endif
    }
    return retval;
}

/* guessing done, reset ccount and pointer
 */
static void
reset_guessing()
{
    ccount = orig_ccount;
    read_buffer = read_buffer_start;
    last_consumed = 0;
    current_file_lineno = 0;
    current_file->lineno = 0;
    current_file_offset = 0;
}

/* calculates votes for record, length has the buffer len and buffer 
   contains the line to be examined
 */
    int
vote_record(uint8_t quote,char *type,int header,struct record *record,int length)
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
                    uint8_t *field;
                    field = get_fixed_field(i->position,length - i->position + 1,length,read_buffer);
                    if(length >= i->position && regexec(&i->reg,field,(size_t) 0, NULL, 0) == 0) vote++;
                } else
#endif
                {
                    if(strncmp(i->key,&read_buffer[i->position -1],i->length) == 0) vote++;
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
                        if(regexec(&i->reg,get_separated_field(i->position,quote,type,read_buffer),(size_t) 0, NULL, 0) == 0) vote++;
                    } else
#endif
                    {
                        if(strcmp(i->key,get_separated_field(i->position,quote,type,read_buffer)) == 0) vote++;
                    }
                }
                break;
            case BINARY:
#ifdef HAVE_REGEX
                if(i->regexp)
                {
                    uint8_t *field;
                    int flen = 64;

                    flen = length - i->position + 1 < flen ? length - i->position + 1 : flen;
                    field = get_fixed_field(i->position,flen,length,read_buffer);

                    if(length >= i->position && regexec(&i->reg,field,(size_t) 0, NULL, 0) == 0) vote++;
                } else
#endif
                {
                    if(memcmp(i->key,&read_buffer[i->position - 1],i->length) == 0) vote++;
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
                if(((record->arb_length == RL_MIN || record->length_field != NULL) && record->length <= length) ||
                        (record->arb_length == RL_STRICT && record->length == length)) vote++;
                break;
            case SEPARATED:
                len = get_field_count(quote,type,read_buffer);
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
vote(int bindex,int line_length)
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
                votes = vote_record(s->quote,s->type,s->header,r,line_length);
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
        fprintf(stderr,"%s: Line %ld in \'%s\' does not match, line length = %d\n",program,current_file->lineno,current_file->name,line_length);
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
                    if(vote_record(s->quote,s->type,s->header,r,buffer_size)) s->vote = 1;
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
        s->vote = 0;
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
        //file_to_text(input_fp);
        return NULL;
    }

    buffer_size = init_guessing();

    if(buffer_size > 0)
    {
        vote_binary(buffer_size);
        ret = check_votes(1);
        reset_guessing();
    }
    
    return ret;
}



/* tries guess the input file structure */
/* returns pointer to the name of guessed structure */

char *
guess_structure()
{
    int memory_used = 0;
    int len;
        
    len = init_guessing();

    if(len <= 0) return NULL;

    do
    {
        len = read_guess_line();
        if(len != -1)
        {
            memory_used += last_consumed;
            vote(guess_lines,len);
            guess_lines++;
        }
    } while(len != -1 && guess_lines < GUESS_LINES && memory_used < GUESS_BUFFER);

    reset_guessing();

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

   returns NULL if no more lines
   length will be written to len
   */
uint8_t *
get_input_line(int *len,int stype)
{
    uint8_t *ret = NULL;

    *len = -1;

    do 
    {
        if(current_file != NULL)
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
 * the real length of bytes consumed returned
   */
 
size_t update_field_positions(char *type,uint8_t quote,struct record *r,int len,uint8_t *buffer)
{
    register uint8_t *p = buffer;
    uint8_t *field_start;
    register int inside_quote = 0;
    struct field *f = r->f;
    int var_record_length;
    int var_field_length = 0;
    int cur_pos,var_field_passed;
    size_t retval;

    retval = last_consumed;

    if(r->length_field)    // If dynamic length
    {
        start_write();
        field_start = write_pos;
        switch(type[0])
        {
             case BINARY:
                 print_binary_field('d',r->length_field,buffer);
             break;
             case FIXED_LENGTH:
                 print_fixed_field('d',r->length_field,buffer);
             break;
         }
         writec(0);  // end of string
         sscanf(field_start,"%d",&var_record_length);
         var_record_length += r->var_length_adjust;
         if(var_record_length >= len) var_record_length = len - 1;
         var_field_length = var_record_length - r->length;
         if(var_field_length < 0) var_field_length = 0;
    }     

    switch(type[0])
    {
        case FIXED_LENGTH:
            if(r->arb_length == RL_MIN)
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
        case BINARY:		   // no break here
            if(type[0] == BINARY) 
            {
                current_file->lineno++;
                current_file_lineno = current_file->lineno;
            }

            if(r->length_field)    // If dynamic length
            {
                if (type[0] == BINARY) retval = var_record_length;
                cur_pos = 0;
                var_field_passed = 0;
                while(f != NULL)
                { 
                    if(var_field_passed && f->const_data == NULL)
                    {
                        f->bposition = cur_pos;
                        cur_pos+=f->length;
                    }
                  
                    if(f->var_length)
                    {
                        f->length = var_field_length;
                        cur_pos = f->bposition + f->length;
                        var_field_passed = 1;
                    }
                    f = f->next;
                }
	        } else
            {
                if (type[0] == BINARY) retval = r->length;
            }
            break;
        case SEPARATED:
            while(f != NULL) 
            {
                f->bposition = -1;
                f = f->next;
            }

            f = r->f;

#ifdef WIN32
            while(*p != '\n' && *p != '\r' && f != NULL)
#else
            while(*p != '\n' && f != NULL)
#endif
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
#ifdef WIN32
                if(*p != '\n' && *p != '\r') p++;
#else
                if(*p != '\n') p++;
#endif
            }
            break;
    }
    return retval;
}
                       
/* check which record applies to current line */
struct record *
select_record(struct structure *s,int length,uint8_t *buffer)
{
    register struct record *r;

    r = s->r;

    while(r != NULL)
    {
        if(vote_record(s->quote,s->type,s->header,r,length)) return r;
        r = r->next;
    }

    return NULL;
}

/* write field content to pipe and read the output, returns bytes read. Output is written to pipe_output  */
int execute_pipe(uint8_t *input,int input_length,struct pipe *p)
{
     int in_fds[2];
     int out_fds[2];
     pid_t pid;
     FILE *rfd,*wfd;
     int ret=0;

     if(!input_length) return 0; // dont pipe with no data

#if defined(HAVE_WORKING_FORK) && defined(HAVE_DUP2) && defined(HAVE_PIPE)
       if (pipe(in_fds) != 0 || pipe(out_fds) != 0) panic("Cannot create pipe",strerror(errno),NULL);
       pid = fork();
       if(pid == (pid_t) 0) /* Child */
       {
          close(in_fds[1]);
          close(out_fds[0]);
          if(dup2(in_fds[0],STDIN_FILENO) == -1) panic("dup2 error",strerror(errno),NULL);
          close(in_fds[0]);
          if(dup2(out_fds[1],STDOUT_FILENO) == -1) panic("dup2 error",strerror(errno),NULL);
          close(out_fds[1]);
          if(execl(SHELL_CMD, "sh", "-c", p->command, NULL) == -1) panic("Starting a shell with execl failed",p->command,strerror(errno));
          _exit(EXIT_SUCCESS);
       } else if(pid > (pid_t) 0)
       {
          close(in_fds[0]);
          close(out_fds[1]);
          wfd = fdopen(in_fds[1],"w");
          if(wfd == NULL) panic("Cannot write to command",p->command,strerror(errno));
          rfd = fdopen(out_fds[0],"r");
          if(rfd == NULL) panic("Cannot read from command",p->command,strerror(errno));

	      if(fwrite(input,input_length,1,wfd) != 1) panic("Cannot write to command",p->command,strerror(errno));
          fclose(wfd);
	      ret = (int) fread(pipe_output,1,PIPE_OUTPUT_LEN,rfd);
          fclose(rfd);
          if(ret && pipe_output[ret - 1] == '\n')  pipe_output[ret - 1] = '\000';    // remove last linefeed
       } else
       {
	      panic("Cannot fork",strerror(errno),NULL);
       }
#else
panic("pipe is not supported in this system",NULL,NULL);
#endif
return ret;
}


/* print a single fixed field */
void
print_fixed_field(uint8_t format,struct field *f,uint8_t *buffer)
{
    register int i = 0;
    register uint8_t *data;
    uint8_t *start = write_pos;
    int len = f->length;

    if(!f->length && f->var_length) return;

    if(f->const_data != NULL)
    {
        data = f->const_data;
        if (format == 't') format = 'd';  // Dont't trim consts
    } else
    {
        if(f->bposition < 0) return;  /* last variable length field is missing */
        data = &buffer[f->bposition];

        if(f->p != NULL)
        {
	        len = execute_pipe(data,f->length,f->p);
	        data = pipe_output;
        }
    }

    switch(format)
    {
        case 'd':
        case 'D':
        case 'C':
        case 'e':
        case 'x':
            if(data == pipe_output)
            {
                while(i < len) writec(data[i++]);
            } else
            {
                if(f->length)
                {
#ifdef WIN32
                    while(i < len && data[i] != '\n' && data[i] != '\r' && data[i]) writec(data[i++]);
#else
                    while(i < len && data[i] != '\n' && data[i]) writec(data[i++]);
#endif
                } else
                {
#ifdef WIN32
                    while(data[i] != '\n' && data[i] != '\r' && data[i]) writec(data[i++]);
#else
                    while(data[i] != '\n' && data[i]) writec(data[i++]);
#endif
                }
            }
            break;
        case 't':
            if(data == pipe_output)
            {
                while(isblank(data[i])) i++;
                while(i < len) writec(data[i++]);
            } else
            {  
                while(isblank(data[i])) i++;
                if(f->length)
                {
#ifdef WIN32
                    while(i < len && data[i] != '\n' && data[i] != '\r' && data[i]) writec(data[i++]);
#else
                    while(i < len && data[i] != '\n' && data[i]) writec(data[i++]);
#endif
                } else
                {
#ifdef WIN32
                    while(data[i] != '\n' && data[i] != '\r' && data[i]) writec(data[i++]);
#else
                    while(data[i] != '\n' && data[i]) writec(data[i++]);
#endif
                }
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
    
    if(!f->length && f->var_length) return;
    
    if(f->const_data != NULL || (f->type == F_ASC && format != 'h'))
    {
        print_fixed_field(format,f,buffer);
        return;
    }

    if(f->p != NULL)
    {
        int i=0,len;
	    len = execute_pipe(&buffer[f->bposition],f->length,f->p);
	    data = pipe_output;
        while(i < len && data[i]) writec(data[i++]);
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

        if(f->p != NULL)
        {
            p = &buffer[f->bposition];
            if(*p == quote && quote) {
                p++;
                inside_quote = 1;
            }
#ifdef WIN32
            while((*p != separator || inside_quote) && *p != '\n' && *p != '\r')
#else
            while((*p != separator || inside_quote) && *p != '\n')
#endif
            {
                if(((*p == quote && p[1] == quote) || (*p == '\\' && p[1] == quote)) && quote) 
                {
                    p++;
                } else if(*p == quote)
                {
                    if(inside_quote) inside_quote=0;
                }
#ifdef WIN32
                if(*p != '\n' && *p != '\r') p++;
#else
                if(*p != '\n') p++;
#endif
            }            
            int i=0,len;
	        len = execute_pipe(&buffer[f->bposition],p - &buffer[f->bposition],f->p);
            while(i < len && pipe_output[i]) writec(pipe_output[i++]);
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
                if(quote || format == 't') while(*p != separator && isblank(*p)) p++;
                if(*p == quote && quote) 
                {
                    p++;
                    inside_quote = 1;
                    if(format == 't') while(isblank(*p)) p++;
                }
#ifdef WIN32
                while((*p != separator || inside_quote) && *p != '\n' && *p != '\r')
#else
                while((*p != separator || inside_quote) && *p != '\n')
#endif
                {
                    if(((*p == quote && p[1] == quote) || (*p == '\\' && p[1] == quote)) && quote) 
                    {
                        p++;
                    } else if(*p == quote)
                    {
                        if(inside_quote)
                        {
                            if(format == 't' && isblank(write_pos[-1]))
                            {
                                write_pos--;     
                                while(write_pos > start && isblank(*write_pos)) write_pos--;
                                write_pos++;
                            }
                            if(format == 'D' || format == 'C') while(write_pos - start == f->length) writec(' ');
                            return;
                        }
                    }

                    if(format == 'C' && (write_pos - start) == f->length) return;
#ifdef WIN32
                    if(*p != '\n' && *p != '\r') {
                       writec(*p);
                       p++;
                    }
#else
                    if(*p != '\n') {
                       writec(*p);
                       p++;
                    }
#endif


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

/* format field with printf
 */
#define CONV_BUF_SIZE 1048576
void
make_conversion(struct format *f,uint8_t *start)
{
    static uint8_t conv_buffer[CONV_BUF_SIZE];

    *write_pos = 0;

    conv_buffer[0] = 0;

#ifdef HAVE_PRINTF_H
    switch(f->type)
    {
        case PA_INT:
        case PA_INT|PA_FLAG_SHORT:
        case PA_CHAR:
        case PA_WCHAR:
            snprintf(conv_buffer,CONV_BUF_SIZE,f->conversion,atoi(start));
            break;
        case PA_INT|PA_FLAG_LONG:
            snprintf(conv_buffer,CONV_BUF_SIZE,f->conversion,atol(start));
            break;
        case PA_INT|PA_FLAG_LONG_LONG:
            snprintf(conv_buffer,CONV_BUF_SIZE,f->conversion,atoll(start));
            break;
        case PA_FLOAT:
        case PA_DOUBLE:
            snprintf(conv_buffer,CONV_BUF_SIZE,f->conversion,atof(start));
            break;
        case PA_DOUBLE|PA_FLAG_LONG_DOUBLE:
            snprintf(conv_buffer,CONV_BUF_SIZE,f->conversion,strtold(start,NULL));
            break;
        case PA_WSTRING:
        case PA_STRING:
        case PA_POINTER:
            snprintf(conv_buffer,CONV_BUF_SIZE,f->conversion,start);
            break;
        default:
            return;
    }
    write_pos = start;
    writes(conv_buffer);
#endif
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
    uint8_t *data_start,*rep_start = NULL,*rep_pos = NULL;
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

                        if(pf->f->f != NULL) make_conversion(pf->f->f,field_start);

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

static inline int
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
    int retval = 0;
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
        while(*line != '\n') fputc(*line++,debug_fp);
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
execute(struct structure *s,int strict, int expression_and,int expression_invert,int expression_case, int debug,char *anon_to_use)
{
    uint8_t *input_line;
    struct record *r = NULL;
    struct record *prev_record = NULL;
    int length;
    int header_printed = 0;
    int fields_printed;
    int first_line = 1;
    int i;
    int anon_field_count=0;

    current_file_lineno = 0;
    current_total_lineno = 0;
    current_file_name = files->name;
    headers = s->header;

    i = 0;
    while(i < JUSTIFY_STRING) justify_string[i++] = ' ';

    reset_levels(0,MAXLEVEL);

    write_buffer = xmalloc(write_buffer_size);
    write_buffer_end = write_buffer + (write_buffer_size - 1);

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
            if(first_line)
            {
                init_structure(s,r,length,input_line);
                anon_field_count = update_anon_info(s,anon_to_use);
                if(anon_field_count) init_libgcrypt();

            }

            if(expression != NULL && (prev_record != r || prev_record == NULL))
            {
                init_expression_list(r);
            }
            
            last_consumed = update_field_positions(s->type,s->quote,r,length,input_line);

            if((!first_line || !headers) && r->o != no_output)
            {
                if((r->pf == NULL && r->o->no_data == 1) || r->pf != NULL || r->o == raw)
                {
                    if(expression == NULL || (eval_expression(s,r,expression_and,expression_invert,expression_case,input_line)))
                    {
                        if(anon_field_count) anonymize_fields(s->type,s->quote,r,length,input_line);  // anonymize after exp. evaluation
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
                first_line = 0;
                if(headers) r = NULL;
            }
        }
    }
    print_level_end(r);
    select_output(s->o);
    print_text(s,r,s->o->file_trailer);
    free(write_buffer);
    if(debug_fp != NULL) fclose(debug_fp);
}



