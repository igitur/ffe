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

/* $Id: parserc.c,v 1.80 2010-09-15 10:49:54 timo Exp $ */
/* parsing the rc-file */

#include "ffe.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_PROCESS_H
#include <process.h>
#endif

#ifdef HAVE_IO_H
#include <io.h>
#endif


#ifdef PACKAGE
static char *program = PACKAGE;
#else
static char *program = "ffe";
#endif

/* pointer to rc-file */
static FILE *fp = NULL;
static FILE *child = NULL;      /* read from child */

/* current line number */
static int lineno = 0;

/* block chars */
#define BLOCK_START '{'
#define BLOCK_END   '}'

#define COMMENT     '#'

/* reading logical line states */
#define LL_OPTION 1
#define LL_BLOCK_START 2
#define LL_BLOCK_END 3
#define LL_EOF 4

#define COMMAND_LEN 128
static size_t command_len = COMMAND_LEN;
static char *command;                    /* used in command substituion */
static char *cs_temp_file = NULL;

/* structure containing one rc-file option definition */
struct rc_option {
    char *name;              /* the name */
    char *parameters;        /* paramters option must/may have */
};

char *values[100];            /* poister to option name and parameters */

/* rc-file option parameter pictures 
   M_ = mandatory
   O_ = optional
*/
#define M_STRING 'S'
#define O_STRING 's' 
#define M_NUMBER 'N'
#define O_NUMBER 'n'
#define M_CHAR 'C'
#define O_CHAR 'c'

/* option names */
#define N_STRUCTURE         "structure"
#define N_TYPE              "type"
#define N_QUOTE             "quoted"
#define N_HEADER            "header"
#define N_OUTPUT            "output"
#define N_RECORD            "record"
#define N_ID                "id"
#define N_RID               "rid"
#define N_FIELD             "field"
#define N_FIELDSFROM        "fields-from"
#define N_FILE_HEADER       "file-header"
#define N_FILE_TRAILER      "file-trailer"
#define N_DATA              "data"
#define N_SEPARATOR         "separator"
#define N_RECORD_HEADER     "record-header"
#define N_RECORD_TRAILER    "record-trailer"
#define N_GROUP_HEADER      "group-header"
#define N_ELEMENT_HEADER    "element-header"
#define N_GROUP_TRAILER     "group-trailer"
#define N_ELEMENT_TRAILER   "element-trailer"
#define N_JUSTIFY           "justify"
#define N_INDENT            "indent"
#define N_FIELDLIST         "field-list"
#define N_PRINT_NO_DATA     "no-data-print"
#define N_FIELD_EMPTY_PRINT "field-empty-print"
#define N_EMPTY_CHARS       "empty-chars"
#define N_LOOKUP            "lookup"
#define N_PAIR              "pair"
#define N_FILE              "file"
#define N_DEFAULT           "default-value"
#define N_SEARCH            "search"
#define N_CONST             "const"
#define N_FIELD_COUNT       "field-count"
#define N_OFILE             "output-file"
#define N_LEVEL             "level"
#define N_RECORD_LENGTH     "record-length"
#define N_HEX_CAP           "hex-caps"
#define N_PIPE		    "filter"
#define N_VARLEN	    "variable-length"




static struct rc_option rc_opts[] = {
    {N_STRUCTURE,"S"},
    {N_TYPE,"Scc"},
    {N_QUOTE,"c"},
    {N_HEADER,"S"},
    {N_RECORD,"S"},
    {N_OUTPUT,"S"},
    {N_ID,"NS"},
    {N_RID,"NS"},
    {N_FIELD,"Sssss"},
    {N_FIELDSFROM,"S"},
    {N_FILE_HEADER,"S"},
    {N_FILE_TRAILER,"S"},
    {N_DATA,"S"},
    {N_SEPARATOR,"S"},
    {N_RECORD_HEADER,"S"},
    {N_RECORD_TRAILER,"S"},
    {N_GROUP_HEADER,"S"},
    {N_GROUP_TRAILER,"S"},
    {N_ELEMENT_HEADER,"S"},
    {N_ELEMENT_TRAILER,"S"},
    {N_JUSTIFY,"S"},
    {N_INDENT,"S"},
    {N_FIELDLIST,"S"},
    {N_PRINT_NO_DATA,"S"},
    {N_FIELD_EMPTY_PRINT,"S"},
    {N_EMPTY_CHARS,"S"},
    {N_LOOKUP,"S"},
    {N_PAIR,"SS"},
    {N_FILE,"Sc"},
    {N_DEFAULT,"S"},
    {N_SEARCH,"S"},
    {N_CONST,"SS"},
    {N_FIELD_COUNT,"N"},
    {N_OFILE,"S"},
    {N_LEVEL,"Nss"},
    {N_RECORD_LENGTH,"S"},
    {N_HEX_CAP,"S"},
    {N_PIPE,"SS"},
    {N_VARLEN,"SSn"},
    {NULL,NULL}
};

int
is_digit(char *number)
{
    while(*number) if(!isdigit(*number++)) return 0;
    return 1;
}

void remove_temp_file()
{
    if(cs_temp_file) unlink(cs_temp_file);
}


void
open_rc_file(char *file)
{
    fp = xfopen(file,"r");
}

void 
error_in_line()
{
    fprintf(stderr,"%s: Error in rcfile, line %d\n",program,lineno);
}

/* remove leading and trailing whitespace */
void
trim(char *buf)
{
    register char *wpos=buf,*rpos=buf;

    while(isspace(*rpos)) rpos++;

    if(rpos != buf)
    {
        while(*rpos)
        {
            *wpos = *rpos;
            wpos++;
            rpos++;
        }
        *wpos = 0;
    }

    if(*buf)
    {
        rpos = buf;
        while(*rpos) rpos++;
        rpos--;
        while(isspace(*rpos)) rpos--;
        rpos++;
        *rpos = 0;
    }
}

/* parses a field list from include command or from -f option */
/* returns NULL if not fields */
/* comma is assumed to be escaped as \, */
struct include_field *
parse_include_list(char *list)
{
    struct include_field *ret = NULL,*c = NULL;
    char *p = list;
    char *w = list;
    char *s = list;

    if(p == NULL) return NULL;

    while(*p)
    {
        if(*p == ',' && p[1] == ',')
        {
            p++;
            *w = *p;
            w++;
            p++;
        }
        *w = *p;
        if(*p == ',' || !p[1])
        {
            if(p[1]) *w = 0;
            if(strlen(s))
            {
                if(ret == NULL)
                {
                    ret = xmalloc(sizeof(struct include_field));
                    c = ret;
                } else
                {
                    c->next = xmalloc(sizeof(struct include_field));
                    c = c->next;
                }
                c->next = NULL;
                c->name = xstrdup(s);
                c->found = 0;
                c->reported = 0;
            }
            p++;
            while(*p == ',') p++;
            s = p;
            w = s;
            p = s;
        } else
        {
            w++;
            p++;
        }
    }
    return ret;
}

/* start child for command substitution */
/* returns a stream for reading from child */
FILE *
execute_child(char *command)
{
    int fds[2];
    pid_t pid;
    FILE *ret = NULL;
#if defined(HAVE_WORKING_FORK) && defined(HAVE_DUP2) && defined(HAVE_PIPE)
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
    } else
    {
        panic("Cannot fork",strerror(errno),NULL);
    }
#elif defined(HAVE_TEMPNAM)  /* for win etc. */
    cs_temp_file = tempnam(NULL,"ffe");
    if(tmpfile == NULL) panic("Temporary file cannot be created",strerror(errno),NULL);
    if((size_t) (strlen(command) + 4 + strlen(cs_temp_file)) > command_len)
    {
        command_len = (size_t) (strlen(command) + 4 + strlen(cs_temp_file));
        command = xrealloc(command,command_len);
    }
    strcat(command," > ");
    strcat(command,cs_temp_file);
    if(system(command) == -1) panic("Command failed",command,strerror(errno));
    ret = fopen(cs_temp_file,"r");
    if(ret == NULL) panic("Temporary file cannot be read",cs_temp_file,strerror(errno));
#else
    panic("Command substitution is not supported in this system",NULL,NULL);
#endif
#if defined(HAVE_SETMODE) && defined(WIN32)
    setmode(fileno(ret),O_TEXT);
#endif
    return ret;
}



/* reads a chracter from the rc-file. */
/* if command substition is used, char is read from child process */
int
read_char()
{
    int c;
    static int char_in_buffer = 0; /* contains the "peeked" char after newline */

    if(child != NULL) 
    {
        if(char_in_buffer)
        {
            c = char_in_buffer;
            char_in_buffer = 0;
        } else
        {
            c = getc(child);
            if(c == '\n')  /* check trailing newlines */
            {
                while((c = getc(child)) == '\n');
                if(c != EOF)      /* abandon trailing newlines */
                {
                    char_in_buffer = c; /* there is more lines, put c in safe place, return a single newline */
                    c = '\n';
                } 
            }  /* No else ! */

            if(c == EOF) 
            {
                fclose(child);
                if(cs_temp_file) unlink(cs_temp_file);
                cs_temp_file = NULL;
                child = NULL;
                c = read_char();
            }
        }
    } else
    {
        c = getc(fp);
        if(c == '`')  /* command subst. starts */
        {
            size_t i=0;
            do
            {
                c = getc(fp);
                command[i++] = c;
                if(i == command_len) 
                {
                    command_len = 2*command_len;
                    command = xrealloc(command,command_len);
                }
            } while(c != '`' && c != EOF);
            if(c != '`')
            {
                error_in_line();
                panic("No closing '`' in command substition",NULL,NULL);
            }
            command[--i] = 0;
            child = execute_child(command);
            c = read_char();
        }
    }
    return c;
}

            

        
/* reading one logical line, returns the status of the line read */
int 
read_logical_line(char **buffer,size_t *bufsize)
{
    static char last_eol_char = 0;      /* for what did previous read end */
    register char *bp;
    int prev_char = 0,c;
    int inside_quote = 0;
    register int i=0;
    int retval = 0;

    bp = *buffer;

    bp[0] = 0;

    switch(last_eol_char)
    {
        case BLOCK_START:
            last_eol_char = 0;
            return LL_BLOCK_START;
            break;
        case BLOCK_END:
            last_eol_char = 0;
            return LL_BLOCK_END;
            break;
    }
    
    do
    {
        c = read_char();
        if(prev_char == '\\')
        {
            switch(c)
            {
                case 'a':
                    bp[i] = '\a';
                    c = 0;
                    i++;
                    break;
                case 'b':
                    bp[i] = '\b';
                    c = 0;
                    i++;
                    break;
                case 't':
                    bp[i] = '\t';
                    c = 0;
                    i++;
                    break;
                case 'n':
                    bp[i] = '\n';
                    c = 0;
                    i++;
                    break;
                case 'v':
                    bp[i] = '\v';
                    c = 0;
                    i++;
                    break;
                case 'f':
                    bp[i] = '\f';
                    c = 0;
                    i++;
                    break;
                case 'r':
                    bp[i] = '\r';
                    c = 0;
                    i++;
                    break;
                case '\\':
                    bp[i] = '\\';
                    c = 0;
                    i++;
                    break;
                case COMMENT:
                    bp[i] = COMMENT;
                    c = 0;
                    i++;
                    break;
                case '\n':    /* newline escaped */
                    if(child == NULL) lineno++; 
                    bp[i] = read_char();
                    c = 0;
                    i++;
                    break;
                default:
                    bp[i] = '\\';
                    i++;
                    break;
            }
        } else
        {
            if(c == '"') inside_quote = !inside_quote;
        }
        
        prev_char = c;
                
        if(i >= *bufsize - 10) 
        {
            *bufsize *= 2;
            *buffer = xrealloc(*buffer,*bufsize);
            bp = *buffer;
        }

        if(inside_quote)
        {
            switch(c)
            {
                case '\\':
                case '\r':  /* skip win32 CR */
                case 0:
                    break;
                default:
                    bp[i] = (char) c;
                    i++;
                    break;
            }

        } else
        {
            switch(c)
            {
                case COMMENT:
                    do
                    {
                        c = read_char();
                    } while (c != '\n' && c != EOF);  /* no break !*/
                case '\n':
                    if(c == '\n' && child == NULL) lineno++;
                    bp[i] = 0;
                    retval = LL_OPTION;
                    break;
                case BLOCK_START:
                    bp[i] = 0;
                    retval = LL_OPTION;
                    break;
                case BLOCK_END:
                    bp[i] = 0;
                    retval = LL_OPTION;
                    break;
                case EOF:
                    bp[i] = 0;
                    retval = LL_OPTION;
                    break;
                case '\\':
                case '\r':  /* skip win32 CR */
                case 0:
                    break;
                default:
                    bp[i] = (char) c;
                    i++;
                    break;
            }
        }

        if(retval)
        {
            trim(bp);
            switch(c)
            {
                case '\n':
                    if(bp[0] == 0) 
                    {
                        i = 0;          /* empty line */
                        retval = 0;
                    } else
                    {
                        return retval;
                    }
                    break;
                case EOF:
                    if(bp[0] == 0)
                    {
                         retval = LL_EOF;
                    }
                    return retval;
                case BLOCK_START:
                    if(bp[0] == 0)
                    {
                        retval = LL_BLOCK_START;
                    } else
                    {
                        last_eol_char = c;
                    }
                    return retval;
                case BLOCK_END:
                    if(bp[0] == 0)
                    {
                        retval = LL_BLOCK_END;
                    } else
                    {
                        last_eol_char = c;
                    }
                    return retval;
                    break;
            }
        }
    } while (1);
}

#define READ_BUF_SIZE 1024

/* reads logical lines and parses a one option */
/* option name and parameters are in values-array */
/* return the number of elements in array */
int parse_option(char *buf)
{
    register char *rpos = buf;
    char *end = buf;
    char *param;
    register char *p;
    int i = 0,j;
    int valc = 0;
    int quoted;

    values[0] = buf;

    while(*end) end++;

    while(!isspace(*rpos) && *rpos) rpos++;
    *rpos = 0;

    p = buf;

    while(*p)  // convert _ to -
    {
        if(*p == '_') *p = '-';
        p++;
    }

    while(rc_opts[i].name != NULL && strcmp(rc_opts[i].name,values[0]) != 0) i++;

    if(rc_opts[i].name != NULL)  /* found */
    {
        param = rc_opts[i].parameters;
        while(rpos < end)
        {
            if(*param)
            {
                rpos++;
                while(isspace(*rpos)) rpos++;  /* next non space */
                quoted = 0;
                switch(*param)
                {
                    case 'S':
                    case 's':
                    case 'C':
                    case 'c':
                        if(*rpos)
                        {
                            if(*rpos == '"') 
                            {
                                rpos++;
                                quoted = 1;
                            }
                            valc++;
                            values[valc] = rpos;
                            if(*(param + 1))  /* not the last possible paramter */
                            {
                                if(quoted)
                                {
                                    j = 0;
                                    while(*rpos != '"' && *rpos) 
                                    {
                                        rpos++;
                                        if(*rpos == '"' && *(rpos - 1) == '\\')
                                        {
                                            j++;
                                            *(rpos - j) = *rpos;
                                            rpos++;
                                        }
                                        if(j) *(rpos - j) = *rpos;
                                    }
                                    if(*rpos != '"')
                                    {
                                        error_in_line();
                                        panic("Quotation not ended",NULL,NULL);
                                    }
                                    *(rpos - j) = 0;
                                    *rpos = 0;
                                } else
                                {
                                    while(!isspace(*rpos) && *rpos) rpos++;
                                    *rpos=0;
                                }
                            } else  /* last parameter, get the rest of the line */
                            {
                                j = 0;
                                while(*rpos) 
                                {
                                    rpos++;
                                    if(*rpos == '"')
                                    {
                                        if(*(rpos - 1) == '\\')
                                        {
                                            j++;
                                            *(rpos - j) = *rpos;
                                            rpos++;
                                            if(!*rpos)
                                            {
                                                error_in_line();
                                                panic("Quotation not ended",NULL,NULL);
                                            }
                                        } else if(*(rpos + 1) && quoted)
                                        {
                                            error_in_line();
                                            panic("Too many parameters",values[0],NULL);
                                        }
                                    }
                                    if(j && *rpos) *(rpos - j) = *rpos;
                                }
                                if(quoted)
                                {
                                    if(*(rpos - 1) != '"')
                                    {
                                        error_in_line();
                                        panic("Quotation not ended",NULL,NULL);
                                    }
                                    *(rpos - j - 1) = 0;
                                } else
                                {
                                    *(rpos - j) = 0;
                                }
                                *rpos = 0;
                            }  
                            if((*param == 'C' || *param == 'c') && values[valc][1])
                            {
                                error_in_line();
                                panic("Single character parameter expected",values[0],NULL);
                            } 
                        } else
                        {
                            if(*param == 'S' || *param == 'C')
                            {
                                error_in_line();
                                panic("Mandatory parameter missing",values[0],NULL);
                            }
                        }
                        break;
                    case 'N':
                    case 'n':
                        if(*rpos)
                        {
                            valc++;
                            values[valc] = rpos;
                            if(*rpos == '*' && (isspace(rpos[1]) || !rpos[1]))
                            {
                                rpos++;
                                *rpos=0;
                            } else
                            {
                                if (*rpos == '-') rpos++;
                                while(isdigit(*rpos)) rpos++;
                                if(!isspace(*rpos) && *rpos)
                                {
                                    error_in_line();
                                    panic("A number expected",values[0],NULL);
                                }
                                *rpos=0;
                            }
                        } else
                        {
                            if(*param == 'N')
                            {
                                error_in_line();
                                panic("Mandatory parameter missing",values[0],NULL);
                            }
                        }
                        break;
                }
                if(valc > 6)
                {
                    error_in_line();
                    panic("Too many parameters",values[0],NULL);
                }
            } else
            {
                error_in_line();
                panic("Too many parameters",values[0],NULL);
            }
            param++;
        }
        if(isupper(*param))
        {
            error_in_line();
            panic("Mandatory parameter missing",values[0],NULL);
        }
    } else
    {
        error_in_line();
        panic("Unknown option",values[0],NULL);
    }
    return valc;
}

/* expand tilde in path */
char *
expand_home(char *path)
{
    char *r;
    char *home = getenv("HOME");

    if(*path == '~' && strncmp(&path[1],PATH_SEPARATOR_STRING,strlen(PATH_SEPARATOR_STRING)) == 0 && home != NULL)
    {
        r = xmalloc(strlen(home) + strlen(path));
        strcpy(r,home);
        strcat(r,&path[1]);
    } else
    {
        r = xstrdup(path);
    }
    return r;
}


/* read key value pairs from file */
/* to struct lookup data chain */
void
read_lookup_from_file(struct lookup_data **data,char *file,char separator)
{
    FILE *fp;
    register int line_len;
    size_t max_line_size = 1024;
    char *efile;
    char *line;
    register char *p;
    struct lookup_data *c_data;

    efile = expand_home(file);
    
    fp = xfopen(efile,"r");

    line = xmalloc(max_line_size);

    do
    {
#ifdef HAVE_GETLINE
        line_len = getline(&line,&max_line_size,fp);
#else
        if(fgets(line,max_line_size,fp) == NULL)
        {
            line_len = -1;
        } else
        {
            line_len = strlen(line);
        }
#endif
        if(line_len > 0)
        {
            switch(line[line_len - 1])  // remove newline
            {
                case '\n':
                case '\r':
                    line[line_len - 1] = 0;
                    break;
            }
            
            p = line;
            while(*p && *p != separator) p++;
            if(*p)
            {
                *p = 0;
                p++;

                c_data = data[hash(line,0)];

                if(c_data == NULL) 
                {
                    c_data = xmalloc(sizeof(struct lookup_data));
                    data[hash(line,0)] = c_data;
                } else
                {
                    while(c_data->next != NULL) c_data = c_data->next;

                    c_data->next = xmalloc(sizeof(struct lookup_data));
                    c_data = c_data->next;
                }
                c_data->next = NULL;
                c_data->key = xstrdup(line);
                c_data->value = xstrdup(p);
            }
        }
    } while(line_len != -1);
    fclose(fp);
    free(line);
    free(efile);
}

/* non printable characters in form \xnn will be expanded 
   returns id length
*/
int
expand_non_print(char *s,uint8_t **t)
{
    char num[5];
    char *w;
    int c,len;

    num[0] = '0';
    num[1] = 'x';
    num[4] = 0;
    len = 0;
    
    *t = xstrdup(s);
    w = *t;

    while(*s)
    {
        if(*s == '\\' && s[1] == 'x' && isxdigit((int) s[2]) && isxdigit((int) s[3]))
        {
            num[2] = s[2];
            num[3] = s[3];
            sscanf(num,"%i",&c);
            *w = (char) c;
            s += 3;
        } else
        {
            *w = *s;
        }
        s++;
        w++;
        len++;
    }
    *w = 0;
    return len;
}

/* Parse non numeric field type */
    void
parse_field_type(char *t,struct field *f)
{
    if(strcmp(t,"char") == 0)
    {
        f->type = F_CHAR;
        f->length = sizeof(char);
        f->endianess = system_endianess;
    } else if(strcmp(t,"short") == 0)
    {
        f->type = F_INT;
        f->length = sizeof(short int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"int") == 0) 
    {
        f->type = F_INT;
        f->length = sizeof(int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"long") == 0) 
    {
        f->type = F_INT;
        f->length = sizeof(long int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"llong") == 0) 
    {
        f->type = F_INT;
        f->length = sizeof(long long int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"ushort") == 0)
    {
        f->type = F_UINT;
        f->length = sizeof(unsigned short int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"uint") == 0) 
    {
        f->type = F_UINT;
        f->length = sizeof(unsigned int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"ulong") == 0) 
    {
        f->type = F_UINT;
        f->length = sizeof(unsigned long int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"ullong") == 0) 
    {
        f->type = F_UINT;
        f->length = sizeof(long long int);
        f->endianess = system_endianess;
    } else if(strcmp(t,"float") == 0) 
    {
        f->type = F_FLOAT;
        f->length = sizeof(float);
        f->endianess = system_endianess;
    } else if(strcmp(t,"float_be") == 0) 
    {
        f->type = F_FLOAT;
        f->length = sizeof(float);
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"float_le") == 0) 
    {
        f->type = F_FLOAT;
        f->length = sizeof(float);
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strcmp(t,"double") == 0) 
    {
        f->type = F_DOUBLE;
        f->length = sizeof(double);
        f->endianess = system_endianess;
    } else if(strcmp(t,"double_be") == 0) 
    {
        f->type = F_DOUBLE;
        f->length = sizeof(double);
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"double_le") == 0) 
    {
        f->type = F_DOUBLE;
        f->length = sizeof(double);
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strcmp(t,"int8") == 0)
    {
        f->type = F_INT;
        f->length = 1;
        f->endianess = system_endianess;
    } else if(strcmp(t,"int16_be") == 0) 
    {
        f->type = F_INT;
        f->length = 2;
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"int16_le") == 0) 
    {
        f->type = F_INT;
        f->length = 2;
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strcmp(t,"int32_be") == 0) 
    {
        f->type = F_INT;
        f->length = 4;
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"int32_le") == 0) 
    {
        f->type = F_INT;
        f->length = 4;
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strcmp(t,"int64_be") == 0) 
    {
        f->type = F_INT;
        f->length = 8;
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"int64_le") == 0) 
    {
        f->type = F_INT;
        f->length = 8;
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strcmp(t,"uint8") == 0)
    {
        f->type = F_UINT;
        f->length = 1;
        f->endianess = system_endianess;
    } else if(strcmp(t,"uint16_be") == 0) 
    {
        f->type = F_UINT;
        f->length = 2;
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"uint16_le") == 0) 
    {
        f->type = F_UINT;
        f->length = 2;
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strcmp(t,"uint32_be") == 0) 
    {
        f->type = F_UINT;
        f->length = 4;
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"uint32_le") == 0) 
    {
        f->type = F_UINT;
        f->length = 4;
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strcmp(t,"uint64_be") == 0) 
    {
        f->type = F_UINT;
        f->length = 8;
        f->endianess = F_BIG_ENDIAN;
    } else if(strcmp(t,"uint64_le") == 0) 
    {
        f->type = F_UINT;
        f->length = 8;
        f->endianess = F_LITTLE_ENDIAN;
    } else if(strncmp(t,"bcd_be_",7) == 0) 
    {
        f->type = F_BCD;
        if(is_digit(&t[7])) sscanf(&t[7],"%i",&f->length);
        f->endianess = F_BIG_ENDIAN;
    } else if(strncmp(t,"bcd_le_",7) == 0) 
    {
        f->type = F_BCD;
        if(is_digit(&t[7])) sscanf(&t[7],"%i",&f->length);
        f->endianess = F_LITTLE_ENDIAN;
    } else if (strncmp(t,"hex_be_",7) == 0)
    {
        f->type = F_HEX;
        if(is_digit(&t[7])) sscanf(&t[7],"%i",&f->length);
        f->endianess = F_BIG_ENDIAN;
    } else if (strncmp(t,"hex_le_",7) == 0)
    {
        f->type = F_HEX;
        if(is_digit(&t[7])) sscanf(&t[7],"%i",&f->length);
        f->endianess = F_LITTLE_ENDIAN;
    } else
    {
        error_in_line();
        panic("Unknown field type",t,NULL);
    }

    if(f->length < 1)
    {
        error_in_line();
        panic("Error in field type",t,NULL);
    }
}

/* parse status values */
#define PS_MAIN 1
#define PS_STRUCT 2
#define PS_RECORD 3
#define PS_OUTPUT 4
#define PS_W_STRUCT 5
#define PS_W_RECORD 6
#define PS_W_OUTPUT 7
#define PS_LOOKUP 8
#define PS_W_LOOKUP 9

void
print_info()
{
    struct structure *s = structure;
    struct record *r;
    struct field *f;
    int pos;

    while(s != NULL)
    {
        printf("Structure %s - ",s->name);
        switch(s->type[0])
        {
            case BINARY:
                printf("binary");
                break;
            case FIXED_LENGTH:
                printf("fixed length");
                break;
            case SEPARATED:
                printf("separated by '%c'",(int) s->type[1]);
                break;
        }
        printf(" - %d\n",s->max_record_len);

        r = s->r;
        while(r != NULL)
        {
            f = r->f;
            pos = 1;
            printf("  Record %s  %d%s\n",r->name,r->length,r->length_field_name != NULL ? "+" : "");
            while(f != NULL)
            {
                if(s->type[0] == SEPARATED)
                {
                    printf("    Field %-30s%5d%5d\n",f->name == NULL ? "*" : f->name,pos,f->length);
                    pos++;
                } else
                {
                    printf("    Field %-30s%5d%5d\n",f->name,pos,f->length);
                    pos += f->length;
                }
                f = f->next;
            }
            r = r->next;
            printf("\n");
        }
        s = s->next;
        printf("\n\n");
    }
}

void 
parserc(char *rcfile,char *include_field_list)
{
    struct structure *c_structure = structure;
    struct field *c_field = NULL;
    struct pipe *c_pipe = NULL;
    struct id *c_id =  NULL;
    struct record *c_record = NULL;
    struct output *c_output = output;
    struct lookup *c_lookup = NULL;
    struct lookup_data *c_lookup_data = NULL;
    struct include_field *fl = parse_include_list(include_field_list);

    char *read_buffer;
    size_t read_buffer_size;
    int line_status;
    int status = PS_MAIN;
    int opt_count;
    int field_count;

    open_rc_file(rcfile);

    read_buffer_size = READ_BUF_SIZE;
    read_buffer = xmalloc(read_buffer_size);
    command = xmalloc(command_len);         /* used in command substituion */

#ifdef HAVE_ATEXIT
    atexit(remove_temp_file); /* remove possible temp-file */
#endif

    if(c_structure != NULL) while(c_structure->next != NULL) c_structure = c_structure->next;

    if(c_output != NULL) while(c_output->next != NULL) c_output = c_output->next;

    while((line_status = read_logical_line(&read_buffer,&read_buffer_size)) != LL_EOF)
    {
        switch(line_status)
        {
            case LL_OPTION:
                opt_count = parse_option(read_buffer);
                switch(status)
                {
                    case PS_MAIN:
                        if(strcmp(values[0],N_STRUCTURE) == 0)
                        {
                            if(structure == NULL)
                            {
                                c_structure = xmalloc(sizeof(struct structure));
                                structure = c_structure;
                            } else
                            {
                                c_structure->next = xmalloc(sizeof(struct structure));
                                c_structure = c_structure->next;
                            }
                            c_structure->next = NULL;
                            c_structure->name = xstrdup(values[1]);
                            c_structure->type[0] = FIXED_LENGTH;
                            c_structure->quote = 0;
                            c_structure->header = 0;
                            c_structure->output_name = NULL;
                            c_structure->vote = 0;
                            c_structure->o = NULL;
                            c_structure->r = NULL;
                            status = PS_W_STRUCT;
                        } else if(strcmp(values[0],N_OUTPUT) == 0)
                        {
                            if(c_output == NULL)
                            {
                                c_output = xmalloc(sizeof(struct output));
                                output = c_output;
                            } else
                            {
                                c_output->next = xmalloc(sizeof(struct output));
                                c_output = c_output->next;
                            }
                            c_output->next = NULL;
                            c_output->name = xstrdup(values[1]);
                            c_output->file_header = NULL;
                            c_output->file_trailer = NULL;
                            c_output->header = NULL;
                            c_output->data = "%d";
                            c_output->lookup = NULL;
                            c_output->separator = NULL;
                            c_output->record_header = NULL;
                            c_output->record_trailer = "\n";
                            c_output->group_header = NULL;
                            c_output->group_trailer = NULL;
                            c_output->element_header = NULL;
                            c_output->element_trailer = NULL;
                            c_output->justify = LEFT_JUSTIFY;
                            c_output->indent = NULL;
                            c_output->no_data = 1;
                            c_output->hex_cap = 0;
                            c_output->empty_chars = " \f\n\r\t\v";
                            c_output->print_empty = 1;
                            c_output->output_file = NULL;
                            c_output->ofp = NULL;
                            if(fl != NULL)
                            {
                                c_output->fl = fl;
                            } else
                            {
                                c_output->fl = NULL;
                            }
                            status = PS_W_OUTPUT;
                        } else if(strcmp(values[0],N_LOOKUP) == 0)
                        {
                            register int i;
                            if(c_lookup == NULL)
                            {
                                c_lookup = xmalloc(sizeof(struct lookup));
                                lookup = c_lookup;
                            } else
                            {
                                c_lookup->next = xmalloc(sizeof(struct lookup));
                                c_lookup = c_lookup->next;
                            }
                            c_lookup->next = NULL;
                            c_lookup->name = xstrdup(values[1]);
                            c_lookup->type = EXACT;
                            c_lookup->default_value = "";
                            for(i = 0;i < MAX_EXPR_HASH;i++) c_lookup->data[i] = NULL;
                            status = PS_W_LOOKUP;
                        } else if(strcmp(values[0],N_CONST) == 0)
                        {
                            if(const_field == NULL)
                            {
                                c_field = xmalloc(sizeof(struct field));
                                const_field = c_field;
                            } else
                            {
                                c_field = const_field;
                                while(c_field->next != NULL) c_field = c_field->next;
                                c_field->next = xmalloc(sizeof(struct field));
                                c_field = c_field->next;
                            }
                            c_field->lookup_table_name = NULL;
                            c_field->lookup = NULL;
                            c_field->rep = NULL;
                            c_field->next = NULL;
                            c_field->name = xstrdup(values[1]);
                            c_field->const_data = xstrdup(values[2]);
                            c_field->position = 0;
                            c_field->length = strlen(c_field->const_data);
                            c_field->o = NULL;
                        } else if(strcmp(values[0],N_PIPE) == 0)
                        { 
                            if (pipes == NULL)
                            {
                               c_pipe = xmalloc(sizeof(struct pipe));
                               pipes = c_pipe;
                            } else
                            {
                                c_pipe->next = xmalloc(sizeof(struct pipe));
                                c_pipe = c_pipe->next;
                            }
                            c_pipe->next = NULL;
                            c_pipe->name = xstrdup(values[1]);
                            c_pipe->command = xstrdup(values[2]);
                        } else 
                        {
                            error_in_line();
                            panic("Option not inside structure, output or lookup",values[0],NULL);
                        }
                        break;
                    case PS_STRUCT:
                        if(strcmp(values[0],N_TYPE) == 0)
                        {
                            if(strcmp(values[1],"fixed") == 0)
                            {
                                c_structure->type[0] = FIXED_LENGTH;
                                if(opt_count > 1)
                                {
                                    error_in_line();
                                    panic("too many parameters for",values[0],NULL);
                                }
                            } else if(strcmp(values[1],"separated") == 0)
                            {
                                c_structure->type[0] = SEPARATED;
                                c_structure->type[1] = ',';
                                c_structure->type[2] = 0;

                                if(opt_count > 1)
                                {
                                    c_structure->type[1] = values[2][0];
                                }
                                if(opt_count > 2)
                                {
                                    if(values[3][0] == '*')
                                    {
                                        c_structure->type[2] = values[3][0];
                                    } else
                                    {
                                        error_in_line();
                                        panic("An \'*\' is expected",values[0],NULL);
                                    }
                                }
                            } else if(strcmp(values[1],"binary") == 0)
                            {
                                c_structure->type[0] = BINARY;
                                if(opt_count > 1)
                                {
                                    error_in_line();
                                    panic("too many parameters for",values[0],NULL);
                                }
                            } else
                            {
                                error_in_line();
                                panic("Unknown type",NULL,NULL);
                            }
                        } else if(strcmp(values[0],N_HEADER) == 0)
                        {
                            if(strcmp(values[1],"first") == 0)
                            {
                                c_structure->header = HEADER_FIRST;
                            } else if(strcmp(values[1],"all") == 0)
                            {
                                c_structure->header = HEADER_ALL;
                            } else if(strcmp(values[1],"no") == 0)
                            {
                                c_structure->header = 0;
                            } else
                            {
                                error_in_line();
                                panic("first, all or no expected",NULL,NULL);
                            }
                        } else if(strcmp(values[0],N_OUTPUT) == 0)
                        {
                            c_structure->output_name = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_RECORD) == 0)
                        {
                            if(c_structure->r == NULL)
                            {
                                c_record = xmalloc(sizeof(struct record));
                                c_structure->r = c_record;
                            } else
                            {
                                c_record->next = xmalloc(sizeof(struct record));
                                c_record = c_record->next;
                            }
                            c_record->next = NULL;
                            c_record->name = xstrdup(values[1]);
                            c_record->i = NULL;
                            c_record->f = NULL;
                            c_record->fields_from = NULL;
                            c_record->o = NULL;
                            c_record->output_name = NULL;
                            c_record->vote = 0;
                            c_record->arb_length = RL_STRICT;
                            c_record->var_length_adjust = 0;
                            c_record->length_field_name = NULL;
                            c_record->length_field = NULL;
                            c_record->var_field_name = NULL;
                            c_record->level = NULL;
                            status = PS_W_RECORD;
                        } else if(strcmp(values[0],N_QUOTE) == 0)
                        {
                            if(opt_count > 0)
                            {
                                c_structure->quote = values[1][0];
                            } else
                            {
                                c_structure->quote = '"';
                            }
                        } else
                        {
                            error_in_line();
                            panic("Unknown option in structure",NULL,NULL);
                        }
                        break;
                    case PS_RECORD:
                        if(strcmp(values[0],N_ID) == 0 || strcmp(values[0],N_RID) == 0)
                        {
                            if(c_record->i == NULL)
                            {
                                c_id = xmalloc(sizeof(struct id));
                                c_record->i = c_id;
                            } else
                            {
                                c_id->next = xmalloc(sizeof(struct id));
                                c_id = c_id->next;
                            }
                            c_id->next = NULL;
                            c_id->regexp = 0;
                            if(sscanf(values[1],"%d",&c_id->position) != 1)
                            {
                                error_in_line();
                                panic("Error in number",NULL,NULL);
                            }
                            if(c_id->position < 1)
                            {
                                error_in_line();
                                panic("Position must be greater than zero",NULL,NULL);
                            }
                            c_id->length = expand_non_print(values[2],&c_id->key);
                            if(strcmp(values[0],N_RID) == 0)
                            {
#ifdef HAVE_REGEX
                                int rc,buflen;
                                char *errbuf;

                                rc = regcomp(&c_id->reg,c_id->key,REG_EXTENDED | REG_NOSUB);
                                if(rc)
                                {
                                    buflen = regerror(rc,&c_id->reg,NULL,0);
                                    errbuf = xmalloc(buflen + 1);
                                    regerror(rc,&c_id->reg,errbuf,buflen);
                                    error_in_line();
                                    panic("Error in regular expression",c_id->key,errbuf);
                                }
                                c_id->regexp = 1;
#else
                                error_in_line();
                                panic("Regular expressions are not supported in this system",NULL,NULL);
#endif
                            }
                        } else if(strcmp(values[0],N_FIELD) == 0)
                        {
                            if(c_record->f == NULL)
                            {
                                c_field = xmalloc(sizeof(struct field));
                                c_record->f = c_field;
                            } else
                            {
                                c_field->next = xmalloc(sizeof(struct field));
                                c_field = c_field->next;
                            }
                            c_field->lookup_table_name = NULL;
                            c_field->lookup = NULL;
                            c_field->rep = NULL;
                            c_field->type = F_ASC;
                            c_field->next = NULL;
                            c_field->const_data = NULL;
                            c_field->length = 0;
                            c_field->var_length = 0;
                            c_field->output_name = NULL;
                            c_field->o = NULL;
                            c_field->pipe_name = NULL;
                            c_field->p = NULL;

                            if(values[1][0] == '*' && !values[1][1])
                            {
                                c_field->name = NULL;
                            } else
                            {
                                c_field->name = xstrdup(values[1]);
                            }
                            if(opt_count > 1)
                            {
                                if(!(values[2][0] == '*' && !values[2][1]))
                                {
                                    if(is_digit(values[2]))
                                    {
                                        sscanf(values[2],"%d",&c_field->length);
                                    } else
                                    {
                                        parse_field_type(values[2],c_field);
                                    }
                                }
                                if(opt_count > 2)
                                {
                                    if(!(values[3][0] == '*' && !values[3][1]))
                                    {
                                        c_field->lookup_table_name = xstrdup(values[3]);
                                    }
                                    if(opt_count > 3)
                                    {
                                        if(!(values[4][0] == '*' && !values[4][1]))
                                        {
                                            c_field->output_name = xstrdup(values[4]);
                                        }
                                        if(opt_count > 4)
                                        {
                                            c_field->pipe_name = xstrdup(values[5]);
                                        }
                                    }
                                }
                                
                            } 
                        } else if(strcmp(values[0],N_FIELDSFROM) == 0)
                        {
                            c_record->fields_from = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_OUTPUT) == 0)
                        {
                            c_record->output_name = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_FIELD_COUNT) == 0)
                        {
                            field_count = atoi(values[1]);
                            while(field_count--)
                            {
                                if(c_record->f == NULL)
                                {
                                    c_field = xmalloc(sizeof(struct field));
                                    c_record->f = c_field;
                                } else
                                {
                                    c_field->next = xmalloc(sizeof(struct field));
                                    c_field = c_field->next;
                                }
                                c_field->lookup_table_name = NULL;
                                c_field->lookup = NULL;
                                c_field->rep = NULL;
                                c_field->next = NULL;
                                c_field->const_data = NULL;
                                c_field->length = 0;
                                c_field->var_length = 0;
                                c_field->name = NULL;
                                c_field->output_name = NULL;
                                c_field->o = NULL;
                                c_field->p = NULL;
                                c_field->pipe_name = NULL;
                            }
                        } else if(strcmp(values[0],N_LEVEL) == 0)
                        {
                            c_record->level = xmalloc(sizeof(struct level));
                            c_record->level->element_name = NULL;
                            c_record->level->group_name = NULL;
                            c_record->level->level = atoi(values[1]);
                            c_record->level->indent_count = 0;
                            if (c_record->level->level < 1 || c_record->level->level > MAXLEVEL)
                            {
                                error_in_line();
                                panic("Invalid level value",NULL,NULL);
                            }
                            if(opt_count > 1 && strcmp(values[2],"*") != 0)
                            {
                                c_record->level->element_name = xstrdup(values[2]);
                                c_record->level->indent_count++;
                            }
                            if(opt_count > 2)
                            {
                                c_record->level->group_name = xstrdup(values[3]);
                                c_record->level->indent_count++;
                            }
                        } else if(strcmp(values[0],N_RECORD_LENGTH) == 0)
                        {
                            if(strcmp(values[1],"strict") == 0)
                            {
                                c_record->arb_length = RL_STRICT;
                            } else if(strcmp(values[1],"minimum") == 0)
                            {
                                c_record->arb_length = RL_MIN;
                            } else
                            {
                                error_in_line();
                                panic("Unknown option value in record",NULL,NULL);
                            }
                        } else if(strcmp(values[0],N_VARLEN) == 0)                      
                        {
			    c_record->length_field_name = strdup(values[1]);
                            c_record->var_field_name  = strdup(values[2]);
                            if(opt_count > 2) sscanf(values[3],"%d",&c_record->var_length_adjust);
                        } else
                        {
                            error_in_line();
                            panic("Unknown option in record",NULL,NULL);
                        }
                        break;
                    case PS_OUTPUT:
                        if(strcmp(values[0],N_FILE_HEADER) == 0)
                        {
                            c_output->file_header = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_FILE_TRAILER) == 0)
                        {
                            c_output->file_trailer = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_HEADER) == 0)
                        {
                            c_output->header = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_DATA) == 0)
                        {
                            c_output->data = xstrdup(values[1]);
                        } else if (strcmp(values[0],N_LOOKUP) == 0)
                        {
                            c_output->lookup = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_SEPARATOR) == 0)
                        {
                            c_output->separator = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_RECORD_HEADER) == 0)
                        {
                            c_output->record_header = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_RECORD_TRAILER) == 0)
                        {
                            if (values[1][0])
                            {
                                c_output->record_trailer = xstrdup(values[1]);
                            } else
                            {
                                c_output->record_trailer = NULL;
                            }
                        } else if(strcmp(values[0],N_GROUP_HEADER) == 0)
                        {
                            c_output->group_header = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_GROUP_TRAILER) == 0)
                        {
                            c_output->group_trailer = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_ELEMENT_HEADER) == 0)
                        {
                            c_output->element_header = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_ELEMENT_TRAILER) == 0)
                        {
                            c_output->element_trailer = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_JUSTIFY) == 0)
                        {
                            if(!values[1][1])
                            {
                                c_output->justify = values[1][0];
                            } else if(strcmp(values[1],"right") == 0)
                            {
                                c_output->justify = RIGHT_JUSTIFY;
                            } else if(strcmp(values[1],"left") == 0)
                            {
                                c_output->justify = LEFT_JUSTIFY;
                            } else
                            {
                                error_in_line();
                                panic("Unknown values in justify",NULL,NULL);
                            }
                        } else if(strcmp(values[0],N_PRINT_NO_DATA) == 0)
                        {
                            if(strcmp(values[1],"yes") == 0)
                            {
                                c_output->no_data = 1;
                            } else if(strcmp(values[1],"no") == 0)
                            {
                                c_output->no_data = 0;
                            } else
                            {
                                error_in_line();
                                panic("Unknown values in print-no-data",NULL,NULL);
                            }
                        } else if(strcmp(values[0],N_INDENT) == 0)
                        {
                            c_output->indent = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_FIELDLIST) == 0)
                        {
                            if(c_output->fl ==  NULL) c_output->fl = parse_include_list(values[1]);
                        } else if(strcmp(values[0],N_FIELD_EMPTY_PRINT) == 0)
                        {
                            if(strcmp(values[1],"yes") == 0)
                            {
                                c_output->print_empty = 1;
                            } else if(strcmp(values[1],"no") == 0)
                            {
                                c_output->print_empty = 0;
                            } else
                            {
                                error_in_line();
                                panic("Unknown values in field-empty-print",NULL,NULL);
                            }
                        } else if(strcmp(values[0],N_EMPTY_CHARS) == 0)
                        {
                            c_output->empty_chars = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_OFILE) == 0)
                        {
                            c_output->output_file = xstrdup(values[1]);
                        } else if(strcmp(values[0],N_HEX_CAP) == 0)
                        {
                            if(strcmp(values[1],"yes") == 0)
                            {
                                c_output->hex_cap = 1;
                            } else if(strcmp(values[1],"no") == 0)
                            {
                                c_output->hex_cap = 0;
                            } else
                            {
                                error_in_line();
                                panic("Unknown values in hex_cap",NULL,NULL);
                            }
                        } else 
                        {
                            error_in_line();
                            panic("Unknown option in output definition",NULL,NULL);
                        }
                        break;
                    case PS_LOOKUP:
                        if(strcmp(values[0],N_SEARCH) == 0)
                        {
                            if(strcmp(values[1],"exact") == 0)
                            {
                                c_lookup->type = EXACT;
                            } else if(strcmp(values[1],"longest") == 0)
                            {
                                c_lookup->type = LONGEST;
                            } else
                            {
                                error_in_line();
                                panic("Unknown value for lookup tables search option",values[1],NULL);
                            }
                        } else if(strcmp(values[0],N_PAIR) == 0)
                        {
                            c_lookup_data = c_lookup->data[hash(values[1],0)];

                            if(c_lookup_data == NULL)
                            {
                                c_lookup_data = xmalloc(sizeof(struct lookup_data));
                                c_lookup->data[hash(values[1],0)] = c_lookup_data;
                            } else
                            {
                                while(c_lookup_data->next != NULL) c_lookup_data = c_lookup_data->next;

                                c_lookup_data->next = xmalloc(sizeof(struct lookup_data));
                                c_lookup_data = c_lookup_data->next;
                            }
                            c_lookup_data->next = NULL;
                            c_lookup_data->key = xstrdup(values[1]);
                            c_lookup_data->value = xstrdup(values[2]);
                        } else if(strcmp(values[0],N_FILE) == 0)
                        {
                            if(opt_count == 1)
                            {
                                read_lookup_from_file(c_lookup->data,values[1],';');
                            } else
                            {
                                read_lookup_from_file(c_lookup->data,values[1],values[2][0]);
                            }
                        } else if(strcmp(values[0],N_DEFAULT) == 0)
                        {
                            c_lookup->default_value = xstrdup(values[1]);
                        } else 
                        {
                            error_in_line();
                            panic("Unknown option for lookup",values[0],NULL);
                        }
                        break;
                    case PS_W_RECORD:
                    case PS_W_OUTPUT:
                    case PS_W_STRUCT:
                    case PS_W_LOOKUP:
                        error_in_line();
                        panic("{ expected, found",values[0],NULL);
                        break;
                }
                break;
            case LL_BLOCK_START:
                switch(status)
                {
                    case PS_W_STRUCT:
                        status = PS_STRUCT;
                        break;
                    case PS_W_OUTPUT:
                        status = PS_OUTPUT;
                        break;
                    case PS_W_RECORD:
                        status = PS_RECORD;
                        break;
                    case PS_W_LOOKUP:
                        status = PS_LOOKUP;
                        break;
                    default:
                        error_in_line();
                        panic("{ not expected",NULL,NULL);
                        break;
                }
                break;
            case LL_BLOCK_END:
                switch(status)
                {
                    case PS_STRUCT:
                    case PS_OUTPUT:
                    case PS_LOOKUP:
                        status = PS_MAIN;
                        break;
                    case PS_RECORD:
                        status = PS_STRUCT;
                        break;
                    default:
                        error_in_line();
                        panic("} not expected",NULL,NULL);
                }
                break;
        }
    }
    if(status != PS_MAIN)
    {
        panic("End of file reached before closing }",NULL,NULL);
    }
    free(read_buffer);
    fclose(fp);
}
 
