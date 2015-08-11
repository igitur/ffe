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
 *
 */

/* $Id: ffe.h,v 1.71 2011-04-10 10:12:10 timo Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_FEATURES_H
#include <features.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_ERROR_H
#include <error.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_REGEX_H
#include <regex.h>
#endif


#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif 

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <stdio.h>

#if defined(__MINGW32__)
#ifndef WIN32
#define WIN32 1
#endif
#endif

#if defined(HAVE_REGEX) && !defined(HAVE_REGCOMP)
#undef HAVE_REGEX
#endif


#ifdef WIN32 
#define PATH_SEPARATOR_STRING "\\"
#else
#define PATH_SEPARATOR_STRING "/"
#endif

#ifndef HAVE_STRCASECMP
#define strcasecmp strcmp
#endif

#ifndef HAVE_STRNCASECMP
#define strncasecmp strncmp
#endif

#ifndef HAVE_STRCASESTR
#define strcasestr strstr
#endif

/* Types */

#define DEFAULT_OUTPUT "default"
#define LEFT_JUSTIFY 1
#define RIGHT_JUSTIFY 2

#define EXACT 1
#define LONGEST 2

/* contains field names from include-option or from -f paramter */
struct include_field {
    char *name;
    int found;  /* does this belong to any record */
    int reported; /* is this field reported to be unmatched */
    struct include_field *next;
};

struct output {
    char *name;
    uint8_t *file_header;
    uint8_t *file_trailer;
    uint8_t *header;
    uint8_t *data;
    uint8_t *lookup;
    uint8_t *separator;
    uint8_t *record_header;
    uint8_t *record_trailer;
    uint8_t *group_header;
    uint8_t *group_trailer;
    uint8_t *element_header;
    uint8_t *element_trailer;
    uint8_t justify;
    uint8_t *indent;
    int no_data;
    char *empty_chars;
    int print_empty;
    int hex_cap;
    struct include_field *fl;
    char *output_file;
    FILE *ofp;
    struct output *next;
};

struct lookup_data {
    uint8_t *key;
    uint8_t *value;
    int key_len;
    struct lookup_data *next;
};

struct lookup {
    char *name;
    char type; 
    uint8_t *default_value;
    int max_key_len;
    struct lookup_data *data;
    struct lookup *next;
};

/* replace, field and value */
struct replace {
    char *field;
    uint8_t *value;
    int found;
    struct replace *next;
};

/* expression values */
struct expr_list {
    char *value;
    int value_len;
#if HAVE_REGEX
    regex_t reg;
#endif
    struct expr_list *next;
};

#define MAX_EXPR_HASH 32771
#define MAX_EXPR_FAST_LIST 61


/* search expression */
struct expression {
    char *field;
    char op;
    int found;
    struct field *f;  /* pointer to field used in expression */
    size_t exp_min_len;
    size_t exp_max_len;
    size_t fast_entries;
    size_t fast_expr_hash[MAX_EXPR_FAST_LIST + 1];  /* fast access list for cases there is low number of values in hash list */
    struct expr_list *expr_hash[MAX_EXPR_HASH]; /* value list*/
    struct expression *next;
};


/* Information for one field */
struct field {
    char *name;
    char *const_data;
    int type;     /* field type ASC,CHAR,SHORT,.... */
    int endianess; /* binary data endianess */
    int position; /* first position = 0 byte position for fixed, first position = 1 for field number for separated */
    int bposition; /* position in current input buffer */
    int length;
    int print;
    char *lookup_table_name;
    struct lookup *lookup;
    struct replace *rep;  /* non NULL if value should be replaced */
    char *output_name;
    struct output *o;
    struct field *next;
};


/* contains pointer to fields which will be printed */
struct print_field {
    struct field *f;
    uint8_t *data;                 // data start position in output buffer;
    int justify_length;
    int empty;                  // does the field contain only "empty" chars
    struct print_field *next;
};

struct id {
    int position;
    uint8_t *key;
    int regexp; /* 1 or 0 for regexp */
#if HAVE_REGEX
    regex_t reg;
#endif
    int length;
    struct id *next;
};

struct level {
    int level;
    char *group_name;
    char *element_name;
    int indent_count;
};


struct record {
    char *name;
    struct id *i;
    struct field *f;
    char *fields_from;
    struct print_field *pf;
    struct output *o;
    char *output_name;
    int vote;
    int length;
    int arb_length; /* arbitrary length */
    struct level *level;
    struct record *next;
};

#define NO_READ '0'
#define FIXED_LENGTH 'f'
#define SEPARATED 's'
#define BINARY 'b'

/* header types of input files */
#define HEADER_NO 0
#define HEADER_FIRST 1
#define HEADER_ALL 2

struct structure {
    char *name;
    char type[3]; /* [0] = f,b or s,[1] = separator, [2] = asterisk or 0 */
    int max_record_len;
    char quote;
    int header;
    char *output_name;
    int vote;
    struct output *o;
    struct record *r;
    struct structure *next;
};

struct input_file {
    char *name;
    long int lineno;
    struct input_file *next;
};


/* Constants */
#define MAXLEVEL 1024
/* exit values */
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

#define OP_START '^'
#define OP_EQUAL '='
#define OP_CONTAINS '~'
#define OP_NOT_EQUAL '!'
#define OP_REQEXP '?'

/* field types */
#define F_ASC 1
#define F_INT 3
#define F_CHAR 4
#define F_FLOAT 7
#define F_DOUBLE 8
#define F_BCD 9
#define F_UINT 10
#define F_HEX 11

/* endianess */
#define F_UNKNOWN_ENDIAN 0
#define F_BIG_ENDIAN 1
#define F_LITTLE_ENDIAN 3
#define F_SYSTEM_ENDIAN 4

/* record length values */
#define RL_STRICT 0
#define RL_MIN 1
#define TL_MAX 2

/* function prototypes */
extern void 
panic(char *msg,char *info,char *syserror);

extern void 
problem(char *msg,char *info,char *syserror);

extern void *
xmalloc (size_t size);

extern char *
xstrdup(char *str);

extern void
parserc(char *,char *);

extern void
set_input_file(char *);

extern void
open_input_file();

extern void *
xrealloc (void *, size_t);

extern char *
guess_structure();

extern void
set_output_file(char *);

extern void 
close_output_file();

extern void 
execute(struct structure *,int,int,int,int,int);

extern char *
expand_home(char *);

extern FILE *
xfopen(char *, char *);

extern FILE *
xfopenb(char *, char *);

extern uint8_t *
endian_and_align(uint8_t *,int,int,int);

extern int
check_system_endianess();

extern char *
guess_binary_structure();

extern void
file_to_text(FILE *);

extern void
writec(uint8_t);

extern void
writes(uint8_t *);

extern void
start_write();

extern void
flush_write();

extern void
reset_levels(int,int);

extern void
print_level_before(struct record *, struct record *);

extern void
print_level_end(struct record *);

extern int
get_indent_depth(int);

extern void
print_indent(uint8_t *,int);

extern size_t
hash(char *,size_t);



/* global variables */
extern struct structure *structure;
extern struct output *output;
extern struct expression *expression;
extern struct lookup *lookup;
extern struct output *no_output;
extern struct output *raw;
extern struct field *const_field;
extern int system_endianess;
extern int max_binary_record_length;
extern char *ffe_open;

