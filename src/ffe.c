/*
 *    ffe - Flat File Extractor
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
 *    F607480034
 *    HJ9004-2
 *
 */

/* $Id: ffe.c,v 1.88 2011-04-10 10:12:10 timo Exp $ */

#include "ffe.h"
#include <ctype.h>
#include <stdlib.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef PACKAGE
static char *program = PACKAGE;
#else
static char *program = "ffe";
#endif

#ifdef PACKAGE_VERSION
static char *version = PACKAGE_VERSION;
#else
static char *version = "0.2.5";
#endif

#ifdef HOST
static char *host = HOST;
#else
static char *host = "";
#endif

#ifdef BUILD_DATE
static char *build_date = BUILD_DATE;
#else
static char *build_date = "";
#endif

#ifdef PACKAGE_BUGREPORT
static char *email_address = PACKAGE_BUGREPORT;
#else
static char *email_address = "tjsa@iki.fi";
#endif

static char short_opts[] = "c:s:o:p:f:e:r:l?VavdIX";

#ifdef HAVE_GETOPT_LONG
static struct option long_opts[] = {
    {"configuration",1,NULL,'c'},
    {"structure",1,NULL,'s'},
    {"output",1,NULL,'o'},
    {"print",1,NULL,'p'},
    {"field-list",1,NULL,'f'},
    {"loose",0,NULL,'l'},
    {"expression",1,NULL,'e'},
    {"help",0,NULL,'?'},
    {"version",0,NULL,'V'},
    {"and",0,NULL,'a'},
    {"invert-match",0,NULL,'v'},
    {"replace",1,NULL,'r'},
    {"debug",0,NULL,'d'},
    {"info",0,NULL,'I'},
    {"casecmp",0,NULL,'X'},
    {NULL,0,NULL,0}
};
#endif

extern void print_info();

extern struct input_file *files;

int system_endianess = F_UNKNOWN_ENDIAN;
int max_binary_record_length = 0;
char *ffe_open = NULL;



/* global rc-data */
struct structure *structure = NULL;
struct output *output = NULL;
struct expression *expression = NULL;
struct lookup *lookup = NULL;
struct replace *replace = NULL;
struct field *const_field = NULL;

/* output no marker */
struct output dummy_no;
struct output *no_output = &dummy_no;
struct output dummy_raw;
struct output *raw = &dummy_raw;

void
panic(char *msg,char *info,char *syserror)
{
    if (info == NULL && syserror == NULL)
    {
        fprintf(stderr,"%s: %s\n",program,msg);
    } else if(info != NULL && syserror == NULL)
    {
        fprintf(stderr,"%s: %s: %s\n",program,msg,info);
    } else if(info != NULL && syserror != NULL)
    {
        fprintf(stderr,"%s: %s: %s; %s\n",program,msg,info,syserror);
    } else if(info == NULL && syserror != NULL)
    {
        fprintf(stderr,"%s: %s; %s\n",program,msg,syserror);
    }
    exit(EXIT_FAILURE);
}

void
problem(char *msg,char *info,char *syserror)
{
    if (info == NULL && syserror == NULL)
    {
        fprintf(stderr,"%s: %s\n",program,msg);
    } else if(info != NULL && syserror == NULL)
    {
        fprintf(stderr,"%s: %s: %s\n",program,msg,info);
    } else if(info != NULL && syserror != NULL)
    {
        fprintf(stderr,"%s: %s: %s; %s\n",program,msg,info,syserror);
    } else if(info == NULL && syserror != NULL)
    {
        fprintf(stderr,"%s: %s; %s\n",program,msg,syserror);
    }
}


char *
get_default_rc_name()
{
    char *home;
    char *result;
#ifdef WIN32
    char *file = "ffe.rc";
#else
    char *file = ".fferc";
#endif

    result = NULL;
    home = getenv("HOME");
#ifdef WIN32
    if(home == NULL)
    {
        home = getenv("FFE_HOME");
        if(home == NULL)
        {
            home = getenv("USERPROFILE");
        }
    }
#endif
    if(home != NULL)
    {
            result = xmalloc(strlen(home) + strlen(file) + strlen(PATH_SEPARATOR_STRING) + 2);
            strcpy(result,home);
            strcat(result,PATH_SEPARATOR_STRING);
            strcat(result,file);
    } else
    {
        result = file;
    }
    return result;
}
        
void
help(FILE *stream)
{
    char *rc=get_default_rc_name();
    fprintf(stream,"Usage: %s [OPTION]...\n\n",program);
#ifdef HAVE_GETOPT_LONG
    fprintf(stream,"-c, --configuration=FILE\n");
    fprintf(stream,"\t\tRead configuration from FILE, default is \'%s\'.\n",rc);
    fprintf(stream,"-s, --structure=STRUCTURE\n");
    fprintf(stream,"\t\tUse structure STRUCTURE for input file, suppresses guessing.\n");
    fprintf(stream,"-p, --print=FORMAT\n");
    fprintf(stream,"\t\tUse output format FORMAT for printing.\n");
    fprintf(stream,"-o, --output=NAME\n");
    fprintf(stream,"\t\tWrite output to NAME instead of standard output.\n");
    fprintf(stream,"-f, --field-list=LIST\n");
    fprintf(stream,"\t\tPrint only fields and constants listed in comma separated list LIST.\n");
    fprintf(stream,"-e, --expression=EXPRESSION\n");
    fprintf(stream,"\t\tPrint only those records for which the EXPRESSION evaluates to true.\n");
    fprintf(stream,"-a, --and\n");
    fprintf(stream,"\t\tExpressions are combined with logical and, default is logical or.\n");
    fprintf(stream,"-X, --casecmp\n");
    fprintf(stream,"\t\tExpressions are evaluated case insensitive.\n");
    fprintf(stream,"-v, --invert-match\n");
    fprintf(stream,"\t\tPrint only those records which don't match the expression.\n");
    fprintf(stream,"-l, --loose\n");
    fprintf(stream,"\t\tAn invalid input line does not cause %s to abort.\n",program);
    fprintf(stream,"-r, --replace=FIELD=VALUE\n");
    fprintf(stream,"\t\tReplace FIELDs contents with VALUE in output.\n");
    fprintf(stream,"-d, --debug\n");
    fprintf(stream,"\t\tWrite invalid input lines to error log.\n");
    fprintf(stream,"-I, --info\n");
    fprintf(stream,"\t\tShow the structure information and exit.\n");
    fprintf(stream,"-?, --help\n");
    fprintf(stream,"\t\tDisplay this help and exit.\n");
    fprintf(stream,"-V, --version\n");
#else
    fprintf(stream,"-c FILE\n");
    fprintf(stream,"\t\tRead configuration from FILE, default is \'%s\'.\n",rc);
    fprintf(stream,"-s STRUCTURE\n");
    fprintf(stream,"\t\tUse structure STRUCTURE for input file, suppresses guessing.\n");
    fprintf(stream,"-p FORMAT\n");
    fprintf(stream,"\t\tUse output format FORMAT for printing.\n");
    fprintf(stream,"-o NAME\n");
    fprintf(stream,"\t\tWrite output to NAME instead of standard output.\n");
    fprintf(stream,"-f LIST\n");
    fprintf(stream,"\t\tPrint only fields and constants listed in comma separated list LIST.\n");
    fprintf(stream,"-e EXPRESSION\n");
    fprintf(stream,"\t\tPrint only those records for which the EXPRESSION evaluates to true.\n");
    fprintf(stream,"-a\n");
    fprintf(stream,"\t\tExpressions are combined with logical and, default is logical or.\n");
    fprintf(stream,"-X\n");
    fprintf(stream,"\t\tExpressions are evaluated case insensitive.\n");
    fprintf(stream,"-v\n");
    fprintf(stream,"\t\tPrint only those records which don't match the expression.\n");
    fprintf(stream,"-l\n");
    fprintf(stream,"\t\tAn invalid input line does not cause %s to abort.\n",program);
    fprintf(stream,"-r FIELD=VALUE\n");
    fprintf(stream,"\t\tReplace FIELDs contents with VALUE in output.\n");
    fprintf(stream,"-d\n");
    fprintf(stream,"\t\tWrite invalid input lines to error log.\n");
    fprintf(stream,"-I\n");
    fprintf(stream,"\t\tShow the structure information and exit.\n");
    fprintf(stream,"-?\n");
    fprintf(stream,"\t\tDisplay this help and exit.\n");
    fprintf(stream,"-V\n");
#endif
    fprintf(stream,"\t\tShow version and exit.\n");
    fprintf(stream,"\nAll remaining arguments are names of input files;\n");
    fprintf(stream,"if no input files are specified, then the standard input is read.\n");
    fprintf(stream,"\nSend bug reports to %s.\n",email_address);
    free(rc);
}

void
usage(int opt)
{
        fprintf(stderr,"Unknown option '-%c'\n",(char) opt);
        help(stderr);
}

void
print_version()
{
    printf("%s version %s\n%s %s\n",program,version,build_date,host);
    printf("Copyright (c) 2007 Timo Savinen\n\n");
    printf("This is free software; see the source for copying conditions.\n");
    printf("There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
}

size_t
hash(char *str,size_t len)
{
    register unsigned long h = 5381;
    int c;

    if(len > 0)
    {
        while ((c = *str++) != 0 && len > 0)
        {
            h = ((h << 5) + h) + c;
            len--;
        }
    }else
    {
        while ((c = *str++) != 0)
        {
            h = ((h << 5) + h) + c;
        }
    }
    return (size_t) (h % MAX_EXPR_HASH);
}



struct output *
search_output(char *name)
{
    struct output *o = output;

    if(strcmp(name,"no") == 0) return no_output;
    if(strcmp(name,"raw") == 0) return raw;

    while(o != NULL)
    {
        if(strcmp(name,o->name) == 0) return o;
        o = o->next;
    }
    fprintf(stderr,"%s: Unknown output \'%s\'\n",program,name);
    return NULL;
}

/* returns a record after name */
struct record *
find_record(struct structure *s,char *name)
{
    struct record *ret = s->r;

    while(ret != NULL)
    {
        if(strcmp(ret->name,name) == 0) return ret;
        ret = ret->next;
    }
    return NULL;
}

/* find a structure after a name */
struct structure *
find_structure(char *name)
{
    struct structure *s = structure;

    while(s != NULL)
    {
        if(strcmp(s->name,name) == 0) return s;
        s = s->next;
    }
    return NULL;
}

/* check structure and output integrity */
/* and initialize some things */
void 
check_rc(char *use_output)
{
    struct structure *s;
    struct output *o;
    struct record *r,*fr;
    struct field *f;
    struct lookup *l;
    int several_records = 0;
    int errors = 0;
    int ordinal;
    int field_count_first;
    char num[64];

    s = structure;
    o = output;

    if(s == NULL)
    {
        errors++;
        fprintf(stderr,"%s: No structure definitions in rc-file\n",program);
    }

    while(s != NULL)
    {
        if(use_output != NULL)
        {
            free(s->output_name);
            s->output_name = xstrdup(use_output);
        }

        if(s->output_name == NULL)
        {
            s->output_name = DEFAULT_OUTPUT;
        }

        s->o = search_output(s->output_name);
        s->max_record_len = 0;
        if(s->o == NULL) errors++;
        r = s->r;
        if(r == NULL) 
        {
            errors++;
            fprintf(stderr,"%s: No records in structure \'%s\'\n",program,s->name);
        } else
        {
            several_records = r->next != NULL ? 1 : 0;
        }
        if(s->quote && s->type[0] == SEPARATED)
        {
            if(s->quote == s->type[1])
            {
                errors++;
                fprintf(stderr,"%s: Quotation and separator cannot be the same character, structure \'%s\'\n",program,s->name);
            }
        }
        if(s->header && s->type[0] != SEPARATED)
        {
            errors++;
            fprintf(stderr,"%s: Headers are valid only in separated input, structure \'%s\'\n",program,s->name);
        }

        field_count_first = 0;

        while(r != NULL)
        {
            if(r->output_name == NULL || use_output != NULL) 
            {
                if(r->output_name != NULL) free(r->output_name);
                r->output_name = s->output_name;
                r->o = s->o;
            } else
            {
                r->o = search_output(r->output_name);
                if(r->o == NULL) errors++;
            }

            if(several_records && s->type[0] == BINARY && r->i == NULL)
            {
                errors++;
                fprintf(stderr,"%s: Every record in a binary multi-record structure must have an id, structure \'%s\', record \'%s\'\n",program,s->name,r->name);
            }

            if(r->fields_from != NULL)
            {
                if(r->f != NULL)
                {
                    errors++;
                    fprintf(stderr,"%s: field and fields-from are mutually exclusive, structure \'%s\', record \'%s\'\n",program,s->name,r->name);
                }
                fr = find_record(s,r->fields_from);
                if(fr != NULL)
                {
                    r->f = fr->f;
                } else
                {
                    errors++;
                    fprintf(stderr,"%s: No record named as '\%s\' in structure \'%s\'\n",program,r->fields_from,s->name);
                }
            }

            f = r->f;
            if(f == NULL)
            {
                errors++;
                fprintf(stderr,"%s: No fields in record \'%s\'\n",program,r->name);
            }
            r->length = 0;
            ordinal = 1;
            while(f != NULL) 
            {
                if(s->type[0] == FIXED_LENGTH || s->type[0] == BINARY)
                {
                    f->position = r->length;
                    f->bposition = r->length;
                    r->length += f->length;
                } else
                {
                    f->position = ordinal;
                    r->length++;
                    if(s->header)
                    {
                        if(r == s->r) 
                        {
                            field_count_first++;
                        } 
                    }
                }

                if(!s->header && f->name == NULL)
                {
                    sprintf(num,"%d",ordinal);
                    f->name = xstrdup(num);
                }

                if(s->type[0] == BINARY && f->length < 1)
                {
                    errors++;
                    fprintf(stderr,"%s: The field \'%s\' must have length in binary structure \'%s\' record \'%s\'\n",program ,f->name,s->name,r->name);
                }

                if(s->type[0] == FIXED_LENGTH && f->length < 1)
                {
                    if(f->next) /* last field can have length 0 */
                    {
                        errors++;
                        fprintf(stderr,"%s: The field \'%s\' must have length in fixed length structure \'%s\' record \'%s\'\n",program,f->name,s->name,r->name);
                    } else
                    {
                        r->arb_length = 1;
                    }
                }

                if(f->lookup_table_name != NULL)
                {
                    l = lookup;

                    while(l != NULL && f->lookup == NULL)
                    {
                        if(strcmp(l->name,f->lookup_table_name) == 0)
                        {
                            f->lookup = l;
                        }
                        l = l->next;
                    }

                    if(f->lookup == NULL)
                    {
                        errors++;
                        fprintf(stderr,"%s: No lookup table named as '%s'\n",program,f->lookup_table_name);
                    }
                }

                if(f->output_name != NULL && use_output == NULL)
                {
                    f->o = search_output(f->output_name);
                    if(f->o == NULL) errors++;
                    if(f->o == raw) {
                        errors++;
                        fprintf(stderr,"%s: Field cannot have output \'raw\', field \'%s\'\n",program,f->name);
                    }
                }

                f = f->next;
                ordinal++;
            }

            if(r->length > s->max_record_len) s->max_record_len = r->length;
            if(s->type[0] == BINARY && s->max_record_len > max_binary_record_length) max_binary_record_length = s->max_record_len;

            if(s->header && r->length != field_count_first)
            {
                errors++;
                fprintf(stderr,"%s: All records in separated structure with header must have equal count of fields, structure \'%s\'\n",program,s->name);
            }
            r = r->next;
        }
        s = s->next;
    }
    
    if(o == NULL)
    {
        errors++;
        fprintf(stderr,"%s: No output definitions in rc-file\n",program);
    }

    while(o != NULL)
    {
        if(o->lookup == NULL) o->lookup = o->data;
        if(o->output_file != NULL)
        {
            if(o->output_file[0] == '-' && o->output_file[1] == 0)
            {
                o->ofp = stdout;
                free(o->output_file);
                o->output_file = "(stdout)";
            } else
            {
                o->ofp = xfopen(o->output_file,"w");
            }
        }
        o = o->next;
    }

    if(errors)
    {
        panic("Errors in rc-file",NULL,NULL);
    }
}

void
add_replace(char *optarg)
{
    char *op_pos;
    struct replace *r;

    if((op_pos = strchr(optarg,'=')) == NULL)
    {
        panic("Replace expression must contain =-character",optarg,NULL);
    }

    *op_pos = 0;

    r = replace;

    if(r == NULL)
    {
        replace = xmalloc(sizeof(struct replace));
        replace->next =  NULL;
        r = replace;
    } else
    {
        while(r->next != NULL) r = r->next;
        r->next = xmalloc(sizeof(struct replace));
        r = r->next;
        r->next = NULL;
    }

    r->field = xstrdup(optarg);
    op_pos++;
    r->value = (uint8_t *) xstrdup(op_pos);
    r->found = 0;
}


/* adds an item to expression hash list 
    */
static void
add_expression_to_list(struct expr_list **list, char *value)
{
    size_t h = hash(value,0);
    register struct expr_list *e = list[h];

    if(e == NULL)
    {
        e = xmalloc(sizeof(struct expr_list));
        list[h] = e;
    } else
    {
        while(e->next != NULL) e = e->next;
        e->next = xmalloc(sizeof(struct expr_list));
        e = e->next;
    }
    e->value = xstrdup(value);
    e->value_len = strlen(e->value);
    e->next = NULL;
}


static void
read_expression_file(struct expr_list **list, char *file)
{
    FILE *fp;
    register int ccount;
    size_t line_len = 1024;
    char *line = xmalloc(line_len);

    fp = xfopen(file,"r");

    do
    {
#ifdef HAVE_GETLINE
        ccount = getline(&line,&line_len,fp);
#else
        if(fgets(line,line_len,fp) != NULL)
        {
            ccount = strlen(line);
        } else
        {
            ccount = -1;
        }
#endif
        if (ccount > 1)
        {
            line[ccount - 1] = 0;
            add_expression_to_list(list,line);
        }
    }
    while(ccount != -1);
    fclose(fp);
    free(line);
}

static void
init_expr_hash(struct expr_list **list)
{
    register int i = 0;

    while(i < MAX_EXPR_HASH) list[i++] = NULL;
}


void
add_expression(char *optarg)
{
    char *op_pos;
    char op = 0;
    struct expression *e,*last;
    struct expr_list *el;
    int found = 0;
    size_t buflen;
    char *value_file;

    if((op_pos = strchr(optarg,OP_REQEXP)) != NULL)
    {
#ifdef HAVE_REGEX
        op = OP_REQEXP;    
#else
        panic("Regular expressions are not supported in this system",optarg,NULL);
#endif
    } else if((op_pos = strchr(optarg,OP_EQUAL)) != NULL)
    {
        op = OP_EQUAL;
    } else if((op_pos = strchr(optarg,OP_START)) != NULL)
    {
        op = OP_START;
    } else if((op_pos = strchr(optarg,OP_CONTAINS)) != NULL)
    {
        op = OP_CONTAINS;
    } else if((op_pos = strchr(optarg,OP_NOT_EQUAL)) != NULL)
    {
        op = OP_NOT_EQUAL;
    } else
    {
        panic("Expression must contain an operator: =,^,~,? or !",optarg,NULL);
    }

    *op_pos = 0;

    e = expression;

    if(e == NULL)
    {
        expression = xmalloc(sizeof(struct expression));
        expression->next =  NULL;
        e = expression;
        e->field = xstrdup(optarg);
        e->exp_min_len = 0;
        e->exp_max_len = 0;
        e->fast_entries = 0;
        init_expr_hash(e->expr_hash); 
    } else
    {
        do
        {
            if(strcasecmp(optarg,e->field) == 0 && e->op == op) 
            {
                found = 1;
            } else
            {
                last = e;
                e = e->next;
            }
        }
        while(e != NULL && !found);

        if(!found)
        {
            last->next = xmalloc(sizeof(struct expression));
            e = last->next;
            e->next = NULL;
            e->field = xstrdup(optarg);
            e->exp_min_len = 0;
            e->exp_max_len = 0;
            e->fast_entries = 0;
            init_expr_hash(e->expr_hash);
        }
    }

    op_pos++;
    if(strstr(op_pos,"file:") == op_pos)
    {
        value_file = expand_home(&op_pos[5]);
        read_expression_file(e->expr_hash,value_file);
        free(value_file);
    } else
    {
        add_expression_to_list(e->expr_hash,op_pos);
    }
    e->found = 0;
    e->op = op;
}

static void
init_expression()
{
    struct expression *e = expression;
    register struct expr_list *l;
    register int i;
    int rc,min_init;
    size_t buflen;
    char *errbuf;

    while(e != NULL)
    {
        i = 0;
        min_init = 0;
        while(i < MAX_EXPR_HASH)
        {
            l = e->expr_hash[i];

	        if (l != NULL && e->fast_entries <= MAX_EXPR_FAST_LIST) e->fast_expr_hash[e->fast_entries++] = i; 

	        while(l != NULL)
            {
                if(!min_init) {min_init = 1;e->exp_min_len = l->value_len;}
                if(l->value_len < e->exp_min_len) e->exp_min_len = l->value_len;
                if(l->value_len > e->exp_max_len) e->exp_max_len = l->value_len;

#ifdef HAVE_REGEX
		        if(e->op == OP_REQEXP)
                {
                    rc = regcomp(&l->reg,l->value,REG_EXTENDED | REG_NOSUB);
                    if(rc)
                    {
                        buflen = regerror(rc,&l->reg,NULL,0);
                        errbuf = xmalloc(buflen + 1);
                        regerror(rc,&l->reg,errbuf,buflen);
                        panic("Error in regular expression",l->value,errbuf);
                    }
                }
#endif
                l = l->next;
            }
            i++;
        }
        if(e->fast_entries > MAX_EXPR_FAST_LIST) e->fast_entries = 0; 
        e = e->next;
    }
}

/* if variable is set it will not be overwritten */
void
set_env(char *name, char *value)
{
        if(name == NULL || value == NULL) return;
#ifdef HAVE_SETENV
        if(setenv(name,value,0) != 0) problem("Cannot set environment",name,strerror(errno));
#elif HAVE_PUTENV
        if(getenv(name) == NULL)
        {
             char *s = xmalloc(strlen(name)+strlen(value)+2);
             strcpy(s,name);
             strcat(s,"=");
             strcat(s,value);
             if(putenv(s) != 0) problem("Cannot set environment",s,strerror(errno));
        }
#else
        problem("Cannot set environment",NULL,NULL);
#endif
}



/* set FFE_STRUCTURE,FFE_FORMAT,FFE_OUTPUT,FFE_FIRST_FILE,FFE_FILES,*/
/* returns 0 in case of error */
/* if variable is set it will not be overwritten */
void
environment(char *structure, char *format,char *ofile)
{
    struct input_file *f=files;
    char *file_names;
    size_t files_len = 1024,used = 0;
    
    set_env("FFE_STRUCTURE",structure);
    set_env("FFE_OUTPUT",ofile);
    set_env("FFE_FORMAT",format);
    set_env("FFE_FIRST_FILE",f->name);
    file_names = xmalloc(files_len);
    file_names[0] = 0;
    while(f != NULL)
    {
        used += strlen(f->name) + 1;
        if(used > files_len) 
        {
            files_len *= 2;
            file_names = xrealloc(file_names,files_len);
        }
        strcat(file_names,f->name);
        if(f->next != NULL) strcat(file_names," ");
        f = f->next;
    }
    set_env("FFE_FILES",file_names);
    free(file_names);
}



int
main(int argc, char **argv)
{
    int opt;
    int strict = 1;
    int debug = 0;
    int info = 0;
    int expression_and = 0;
    int expression_invert = 0;
    int expression_casecmp = 0;
    struct structure *s = NULL;
    char *structure_to_use = NULL;
    char *output_to_use = NULL;
    char *config_to_use = NULL;
    char *ofile_to_use = NULL;
    char *field_list = NULL;

#ifdef HAVE_SIGACTION
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_IGN;
    act.sa_flags = SA_NOCLDWAIT;
    sigaction (SIGCHLD, &act, NULL);
#endif

#ifdef HAVE_GETOPT_LONG
    while ((opt = getopt_long(argc,argv,short_opts,long_opts,NULL)) != -1)
#else
    while ((opt = getopt(argc,argv,short_opts)) != -1)
#endif
        {
            switch(opt)
            {
                case 'c':
                    if(config_to_use == NULL)
                    {
                        config_to_use = xstrdup(optarg);
                    } else
                    {
                        panic("Only one -c option allowed",NULL,NULL);
                    }
                    break;
                case 's':
                    if(structure_to_use == NULL)
                    {
                        structure_to_use = xstrdup(optarg);
                    } else
                    {
                        panic("Only one -s option allowed",NULL,NULL);
                    }
                    break;
                case 'p':
                    if(output_to_use == NULL)
                    {
                        output_to_use = xstrdup(optarg);
                    } else
                    {
                        panic("Only one -p option allowed",NULL,NULL);
                    }
                    break;
                case 'f':
                    if(field_list == NULL)
                    {
                        field_list = xstrdup(optarg);
                    } else
                    {
                        panic("Only one -f option allowed",NULL,NULL);
                    }
                    break;
                 case 'o':
                    if(ofile_to_use == NULL)
                    {
                        ofile_to_use = xstrdup(optarg);
                    } else
                    {
                        panic("Only one -o option allowed",NULL,NULL);
                    }
                    break; 
                case 'e':
                    add_expression(optarg);
                    break;
                case 'r':
                    add_replace(optarg);
                    break;
                case 'a':
                    expression_and = 1;
                    break;
                case 'X':
                    expression_casecmp = 1;
                    break;
                case 'd':
                    debug = 1;
                    break;
                case 'v':
                    expression_invert = !expression_invert;
                    break;
                case 'l':
                    strict = 0;
                    break;
                case '?':
                    help(stdout);
                    exit(EXIT_SUCCESS);
                    break;
                case 'V':
                    print_version();
                    exit(EXIT_SUCCESS);
                    break;
                case 'I':
                    info = 1;
                    break;
                default:
                    usage(opt);
                    exit(EXIT_FAILURE);
                    break;
            }
        }


    if(optind < argc)
    {
        while(optind < argc) set_input_file(argv[optind++]);
    } else
    {
        set_input_file("-");
    }
    
    if(config_to_use == NULL) config_to_use = get_default_rc_name();

    environment(structure_to_use,output_to_use,ofile_to_use);
    
    system_endianess = check_system_endianess();
                
    parserc(config_to_use,field_list);

    check_rc(output_to_use);
     
    init_expression();

    if(info)
    {
        print_info();
        exit(EXIT_SUCCESS);
    }
    
    ffe_open = getenv("FFEOPEN");

    if(structure_to_use == NULL)
    {
        open_input_file(BINARY);  // guess binary first
        structure_to_use = guess_binary_structure();
        if(structure_to_use == NULL)
        {
            structure_to_use = guess_structure();
        }
        if(structure_to_use == NULL) panic("Structure cannot be guessed, use -s option",NULL,NULL);
        s = find_structure(structure_to_use);
    } else
    {
        s = find_structure(structure_to_use);
        if(s != NULL)
        {
            open_input_file(s->type[0]);
        } else
        {
            panic("No structure named as",structure_to_use,NULL);
        }
    }

    free(config_to_use); /* to avoid strange valgrind memory lost */

    set_output_file(ofile_to_use);

    execute(s,strict,expression_and,expression_invert,expression_casecmp,debug);

    close_output_file();

    exit(EXIT_SUCCESS);
}

