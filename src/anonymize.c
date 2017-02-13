/*
 *    ffe - Flat File Extractor
 *
 *    Copyright (C) 2017 Timo Savinen
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

#include "ffe.h"
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_GCRYPT_H
#include <gcrypt.h>
#endif

#define MAX_NFIELD_LEN 262144

#define CRYPT_ASCII_CHARS "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
#define CRYPT_NUMBER_CHARS "0123456789"

static uint8_t bcd_to_ascii[]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','\000'};

static char crypt_ascii_chars[]=CRYPT_ASCII_CHARS;
static char crypt_number_chars[]=CRYPT_NUMBER_CHARS;

#define NUM_ASCII_CHARS  (sizeof(crypt_ascii_chars) - 1)
#define NUM_NUMBER_CHARS  (sizeof(crypt_number_chars) - 1)

static inline uint8_t
bcdtocl(uint8_t bcd)
{
        return bcd_to_ascii[bcd & 0x0f];
}

static inline uint8_t
bcdtocb(uint8_t bcd)
{
        return bcd_to_ascii[(bcd >> 4) & 0x0f];
}


/* Init libgcrypt
 */
void init_libgcrypt()
{
#ifdef HAVE_WORKING_LIBGCRYPT
    if (gcry_check_version(GCRYPT_VERSION) == NULL) panic("libgcrypt init error",NULL,NULL);
    gcry_control(GCRYCTL_DISABLE_SECMEM,0);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED,0);
#endif
}



/* read field dta from buffer and write it in normalized form to nbuffer,
 * return nbuffer data length
 * rules:
 * - binary data (execpt little endian hex values) and text n fixed and separated data is written as it is
 * - bcd values are written as clear text numbers
 * - little endian hex vlaues are written in big endian order
 */
static int get_normalized_field(struct field *f,char *type,uint8_t quote,int len,uint8_t *buffer,uint8_t *nbuffer)
{
   int copy_length = 0;
   register int i;
   register uint8_t *data;
   int inside_quote;
   uint8_t separator;
   uint8_t *start;
   uint8_t c;
   uint8_t *data_end;
   uint8_t *stop;
  

   if(f->const_data != NULL || f->bposition < 0) return 0;   // const data is not anonymized or nothing is done for non existing field

   switch(type[0])
   {
       case FIXED_LENGTH: 
           copy_length = f->length;
           if(copy_length > MAX_NFIELD_LEN) copy_length = MAX_NFIELD_LEN;
           i = 0;
           data = &buffer[f->bposition];
           if(copy_length)
           {
#ifdef WIN32
               while(i < copy_length && data[i] != '\n' && data[i] != '\r' && data[i]) nbuffer[i] = data[i++];
#else
               while(i < copy_length && data[i] != '\n' && data[i]) nbuffer[i] = data[i++];
#endif
           } else
           {
#ifdef WIN32
               while(data[i] != '\n' && data[i] != '\r' && data[i]) nbuffer[i] = data[i++];
#else
               while(data[i] != '\n' && data[i]) nbuffer[i] = data[i++];
#endif
           }
           copy_length = i;
           break;
       case SEPARATED:
           separator = type[1];

		   if(f->bposition < 0) return 0;
               
		   data = &buffer[f->bposition];

           inside_quote = 0;

           if(*data == quote && quote) {		//skip first quote
               data = &buffer[f->bposition + 1];
               inside_quote = 1;
           }
           start = data;
#ifdef WIN32
           while((*data != separator || inside_quote) && *data != '\n' && *data != '\r' && (data - start) < MAX_NFIELD_LEN)
#else
           while((*data != separator || inside_quote) && *data != '\n' && (data - start) < MAX_NFIELD_LEN)
#endif
           {
               if(((*data == quote && data[1] == quote) || (*data == '\\' && data[1] == quote)) && quote)
               {
                   *nbuffer = *data;
                   nbuffer++;
                   data++;
               } else if(*data == quote)
               {
                   if(inside_quote) inside_quote=0;
               }
#ifdef WIN32
               if(*data != '\n' && *data != '\r') 
#else
                   if(*data != '\n')
#endif
                   {
                       *nbuffer = *data;
                       nbuffer++;
                       data++;
                   }
           }

           if(data > start && data[-1] == quote && quote) data--; // don't take last quote

           copy_length = (int) (data - start);
           break;
       case BINARY:
           switch(f->type)
           {
               case F_ASC:
               case F_CHAR:
               case F_DOUBLE:
               case F_FLOAT:
               case F_INT:
               case F_UINT:
               case F_HEX:
                   copy_length = f->length;
                   if(copy_length > MAX_NFIELD_LEN) copy_length = MAX_NFIELD_LEN;
                   if(f->type == F_ASC) 
                   {
                       stop = memccpy(nbuffer,&buffer[f->bposition],0,copy_length);  // stop at null for text
                       if(stop != NULL) copy_length = (int) (stop - nbuffer) - 1;
                   } else
                   {
                       memcpy(nbuffer,&buffer[f->bposition],copy_length);
                   }
                   break;
               case F_BCD:
                   data = &buffer[f->bposition];
                   data_end = data + f->length;
                   i = 0;
                   switch(f->endianess)
                   {
                       case F_BIG_ENDIAN:
                           do
                           {
                               c = bcdtocb(*data);
                               if(c)
                               {
                                   nbuffer[i++] = c;
                                   c = bcdtocl(*data);
                                   if(c) nbuffer[i++] = c;
                               }
                               data++;
                           } while(data < data_end && c);
                           break;
                       case F_LITTLE_ENDIAN:
                           do
                           {
                               c = bcdtocl(*data);
                               if(c)
                               {
                                   nbuffer[i++] = c;
                                   c = bcdtocb(*data);
                                   if(c) nbuffer[i++] = c;
                               }
                               data++;
                           } while(data < data_end && c);
                           break;
                   }
                   copy_length = i;
           }
           break;
   }
   return copy_length;
}

static uint8_t ascii_to_bcd(uint8_t asc)
{
        return asc - (asc >= '0' && asc <= '9' ? '0' : ('a' - 10));
}


/* Write scrambled field back to buffer
 */
static void write_scrambled_field(struct field *f,char *type,uint8_t quote,uint8_t *buffer,int scramble_len,uint8_t *scrambled_data)
{
    int quoted;
    uint8_t *data;
    register int i;
    register uint8_t c,t;

    switch(type[0])
    {
        case FIXED_LENGTH: 
            memcpy(&buffer[f->bposition],scrambled_data,scramble_len);
            break;
        case SEPARATED:
            quoted = (buffer[f->bposition] == quote && quote) ? 1 : 0 ; 	//skip first quote
            memcpy(&buffer[f->bposition + quoted],scrambled_data,scramble_len);
            break;
        case BINARY:
            switch(f->type)
            {
                case F_ASC:
                case F_CHAR:
                case F_DOUBLE:
                case F_FLOAT:
                case F_INT:
                case F_UINT:
                case F_HEX:
                    memcpy(&buffer[f->bposition],scrambled_data,scramble_len);
                    break;
                case F_BCD:  // write in big endian and swap after that if little endian
                    data = &buffer[f->bposition];
                    i = 0;
                    c = 0;
                    while(i < scramble_len)
                    {
                        c = (uint8_t) (ascii_to_bcd(scrambled_data[i]) & 0x0f);
                        c = c << 4;
                        i++;
                        if(i < scramble_len)
                        {
                            c = c | (uint8_t) (ascii_to_bcd(scrambled_data[i]) & 0x0f);
                        } else
                        {
                            c = c | 0x0f;
                        }
                        i++;
                        if(f->endianess == F_LITTLE_ENDIAN)
                        {
                            t = (c >> 4) & 0x0f;
                            c = (c << 4) | t;
                        }
                        *data = c;
                        data++;
                    }		
                    if((c & 0x0f) != 0x0f && (c & 0xf0) != 0xf0 && scramble_len/2 < f->length) *data=0xff;
                    break;
            }
    }
}




static void scramble_MASK(uint8_t *scramble,int length,uint8_t mask)
{
    if(length < 0) return;
    do
    {
         scramble[--length] = mask;
    } while (length >= 0);
}

static void scramble_HASH(int ftype,int hash_length,unsigned char *hash,int scramble_length,uint8_t *scramble,int num_chars,char *chars)
{
    register int i;

    if(hash_length < 1) return;

    switch(ftype)
    {
        case F_ASC:
            i = 0;
            while(i < scramble_length) 
            {
                scramble[i] = chars[hash[i % hash_length] % num_chars];
                i++;
            }
            break;
        case F_CHAR:
        case F_DOUBLE:
        case F_FLOAT:
        case F_INT:
        case F_UINT:
        case F_HEX:
            i = 0;
            while(i < scramble_length) 
            {
                scramble[i] = hash[i % hash_length];
                i++;
            }
            break;
        case F_BCD:
            i = 0;
            while(i < scramble_length)
            {
                scramble[i] = '0' +  hash[i % hash_length] % 9;
                i++;
            }
            break;

    }
}

/* make a hash using libcgryot 
 * write hash to hash and return the hash length
 */
static int md_hash(unsigned char *hash,int input_length,uint8_t *input,int bytes)
{
    size_t outlen=0;
#ifdef HAVE_WORKING_LIBGCRYPT
    int algo;
    gcry_md_hd_t hd;
    gpg_error_t err;
    unsigned char *p;

    switch(bytes)
    {
        case 32:
            algo = GCRY_MD_SHA256;
            break;
        case 64:
            algo = GCRY_MD_SHA512;
            break;
        case 16:
        default:
            algo = GCRY_MD_MD5;
            break;
    }

    outlen = gcry_md_get_algo_dlen(algo);

    err = gcry_md_open(&hd,algo,0);
    if(err != GPG_ERR_NO_ERROR) panic("libgcrypt error",NULL,NULL);

    gcry_md_write(hd,input,input_length);

    p = gcry_md_read(hd,algo);   
    if (p == NULL) panic("libgcrypt error",NULL,NULL);

    memcpy(hash,p,outlen);

    gcry_md_close(hd);
#else
    problem("Libgcrypt not availaible in this system",NULL,NULL);
#endif
    return outlen;
}

static int md_random(unsigned char *rand,int rand_length)
{
#ifdef HAVE_WORKING_LIBGCRYPT
    gcry_create_nonce(rand,rand_length);
    return rand_length;
#else
    problem("Libgcrypt not availaible in this system",NULL,NULL);
    return 0;
#endif
}

    
    

/* make scramble data based on anonymization info and normalized input data
 * scramble length is the length indicated byt start pos, length and actual data length
 * return scramble length
 */
#define HASH_BUFFER_LEN (MAX_NFIELD_LEN)
static int create_scramble(int ftype,uint8_t *scramble,int normalized_length,uint8_t *normalized_field,struct anon_field *a)
{
    int scramble_length;
    int hash_length;
    static unsigned char hash[HASH_BUFFER_LEN]; 

    if(a->start >= 0)   // from beginning
    {
        if(a->start > normalized_length) return 0;
        scramble_length = normalized_length - a->start + 1;
    } else 
    {
        if(abs(a->start) > normalized_length) return 0;
        scramble_length = normalized_length + a->start + 1;
    }
    if(scramble_length > a->length && a->length > 0) scramble_length = a->length;    
    if(scramble_length > MAX_NFIELD_LEN) scramble_length = MAX_NFIELD_LEN;

    switch(a->method)
    {
        case A_MASK:
            scramble_MASK(scramble,scramble_length,a->key_length > 0 ? a->key[0] : '0');
            break;
        case A_RANDOM:
            hash_length = md_random(hash,scramble_length > HASH_BUFFER_LEN ? HASH_BUFFER_LEN : scramble_length);
            if(hash_length) scramble_HASH(ftype,hash_length,hash,scramble_length,scramble,NUM_ASCII_CHARS,crypt_ascii_chars); else scramble_length = 0;
            break;
        case A_NRANDOM:
            hash_length = md_random(hash,scramble_length > HASH_BUFFER_LEN ? HASH_BUFFER_LEN : scramble_length);
            if(hash_length) scramble_HASH(ftype,hash_length,hash,scramble_length,scramble,NUM_NUMBER_CHARS,crypt_number_chars); else scramble_length = 0;
            break;
        case A_HASH:
            hash_length = md_hash(hash,normalized_length,normalized_field,a->key_length > 0 ? atoi(a->key) : 16);
            if(hash_length) scramble_HASH(ftype,hash_length,hash,scramble_length,scramble,NUM_ASCII_CHARS,crypt_ascii_chars); else scramble_length = 0;
	    break;
        case A_NHASH:
            hash_length = md_hash(hash,normalized_length,normalized_field,a->key_length > 0 ? atoi(a->key) : 16);
            if(hash_length) scramble_HASH(ftype,hash_length,hash,scramble_length,scramble,NUM_NUMBER_CHARS,crypt_number_chars); else scramble_length = 0;
	    break;
    }
    return scramble_length;
}

/* write scrambled data to normalized field
 */
static void scramble_normalized(int scramble_len,uint8_t *scramble,int normalized_length,uint8_t *normalized_field,struct anon_field *a)
{

    if(scramble_len == 0) return;

    if(a->start >= 0)   // from beginning
    {
        memcpy(&normalized_field[a->start - 1],scramble,scramble_len);
    } else
    {
        memcpy(&normalized_field[normalized_length + a->start - scramble_len + 1],scramble,scramble_len);
    }
}

        


/* Anonymize those fields which have anonymize info, 
 * data will be anonymized in input buffer
 */ 
void anonymize_fields(char *type,uint8_t quote,struct record *r,int len,uint8_t *buffer)
{
    static uint8_t normalized_field[MAX_NFIELD_LEN];
    static uint8_t scramble[MAX_NFIELD_LEN];
    struct field *f = r->f;
    int normalized_length;
    int scramble_len;

    while(f != NULL)
    {
        if(f->a != NULL)
        {
            normalized_length = get_normalized_field(f,type,quote,len,buffer,normalized_field);
            if(normalized_length)
            {
                scramble_len = create_scramble(f->type,scramble,normalized_length,normalized_field,f->a);
                if(scramble_len)
                {
                    scramble_normalized(scramble_len,scramble,normalized_length,normalized_field,f->a);
                    write_scrambled_field(f,type,quote,buffer,normalized_length,normalized_field);
                }
            } 
        }
        f = f->next;
    }
}

