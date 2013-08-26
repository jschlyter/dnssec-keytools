/*
 * $Id: util.c 352 2010-05-28 08:52:02Z jakob $
 *
 * Copyright (C) 2006, 2007 Richard H. Lamb (RHL). All rights reserved.
 *
 * Based on
 * "Netwitness.org/net Standalone PKCS#7 Signer,Copyright (C) RHLamb 2006,2007"
 * and other libraries Copyright (C) RHLamb 1995-2007
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND RHL DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL RHL BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: RHLamb
 */

#include "config.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "compat.h"
#include "util.h"
#include "logger.h"
#include "sha2wordlist.h"

static const char base64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";


/*******************************************************************
 * misc routines
 *******************************************************************/
/* parse line /w delc as delimiter into no more than maxargs in argv[]
   skipping whitespace (' ' and \t).  Return the number of args */
int lparse(char *line,char *argv[],int maxargs,char delc)
{
  char *cp;
  int argc,qflag;

  if((cp = strchr(line,'\r')) != (char *)0) *cp = '\0';
  if((cp = strchr(line,'\n')) != (char *)0) *cp = '\0';

  for(argc=0;argc<maxargs;argc++) argv[argc] = (char *)0;

  for(argc=0;argc<maxargs;) {
    qflag = 0;
    while(*line == ' ' || *line == '\t') line++; /* whitespace */
    if(*line == '\0') break; /* done */
    if(*line == '"') { line++; qflag = 1; } /* quote */
    argv[argc++] = line;
    if(qflag) {                         /* quote */
      if((line = strchr(line,'"')) == (char *)0) return -1; /*error*/
      *line++ = '\0';
    } else {
      for(cp=line;*cp;cp++) {
        if(/**cp == ' ' || *cp == '\t' ||*/ *cp == delc) break;
      }
      if(*cp) *cp++ = '\0'; /* non-zero */
      line = cp;
    }
  }
  return argc;
}

/* remove preceeding and trailing whitespace...and trailing cr and lf */
int str_cleanup(char *io)
{
  char *q,*p;

  if (io == NULL) return -1;

  /* rid trailing space (' ' or '\t') and CF/LF */
  for(q = io + strlen(io);q-- != io && isspace(*q) ;) ;

  *(q+1) = '\0';
  
  /* rid leading space */
  for(q=io;isblank(*q);q++) ;
  for(p=io;*q;) *p++ = *q++;
  *p = '\0';

  return 0;
}

/* lowercase a string */
static char mytolower(char c)
{
  if(c >= 'A' && c <= 'Z') return c|0x20;
  return c;
}

void strtolower(char *str)
{
  while(*str) {
    *str = mytolower(*str);
    str++;
  }
}

/* uppercase a string */
static char mytoupper(char c)
{
  if(c >= 'a' && c <= 'z') return c & ~0x20;
  return c;
}

void strtoupper(char *str)
{
  while(*str) {
    *str = mytoupper(*str);
    str++;
  }
}

/* convert UTC ltmp time (secs) into YYYYMMDDHHmmss in str */
void gmtstrtime(time_t ltmp,char *str)
{
  struct tm *t;
  t = gmtime(&ltmp);
  sprintf(str,"%04u%02u%02u%02u%02u%02u",t->tm_year+1900,t->tm_mon + 1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);  /* CVTY FIXME - just dont be so paranoid */
}

/* convert UTC ltmp time (secs) into YYYY-MM-DDTHH:mm:ss+00:00 */
void sec2ztime(time_t zsec,char *str)
{
  struct tm *t;
  t = gmtime(&zsec);
  sprintf(str,"%04u-%02u-%02uT%02u:%02u:%02u+00:00",t->tm_year+1900,t->tm_mon + 1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec);  /* CVTY FIXME - just dont be so paranoid */
}

/* dump (ptr,n) in captital HEX chars */
int hdump(const uint8_t *ptr,int n)
{
  int i;
  for(i=0;i<n;i++) myx_syslog(LOG_INFO,"%02X",ptr[i]);
  myx_syslog(LOG_INFO,"\n");
  return 0;
}

int rdump(const uint8_t *ptr,int n)
{
  int i,j1,j2; char buf[80]; static char htoas[]="0123456789ABCDEF";
  j1 = j2 = 0; /* gcc -W */
  for(i=0;i<n;i++,j1+=3,j2++) {
    if((i&0xf) == 0) {
      if(i) { buf[j2]='\0';  logger_debug("%s|",buf); }
      j1=0; j2=51; memset(buf,' ',80); buf[50]='|';
    }
    buf[j1] = htoas[(ptr[i]&0xf0) >> 4]; buf[j1+1] = htoas[ptr[i]&0x0f];
    if(ptr[i] >= 0x20 && ptr[i] < 0x80) buf[j2]=ptr[i]; else buf[j2]='.';
  }
  buf[j2]='\0'; logger_debug("%s|",buf);
  return 0;
}

/* return as binary long the ASCIIZ number in str */
uint32_t atoul(const char *str)
{
  char *endptr;
  uint32_t val;
  
  errno = 0;
  val = strtoul(str,&endptr,10);
  if(errno == ERANGE || (errno != 0 && val == 0)) {
    logger_error("%s %s",__func__,strerror(errno));
    return -1;
  }
  if(endptr == str) {
    logger_error("No digits were found");
    return -1;
  }
  return val;
}

/* return as binary the ASCII hex char in c */
int hex2i(char c)
{
  if(c >= '0' && c <= '9') return (int)(c - '0');
  if(c >= 'A' && c <= 'F') return (int)((c - 'A') + 10);
  if(c >= 'a' && c <= 'f') return (int)((c - 'a') + 10);
  return -1;
}

/* base64 encode (in,n) into ASCIIZ out. "outlen" should be at least (4/3)*n */
int base64encode(char *out, size_t outlen, const uint8_t *in,int n)
{
  int len;

  /* FIXME: check outlen */

  len = 0;
  while(n > 0) {
    *out++ = base64[ ((in[0]>>2)&0x3f) ];
    if(n > 2) {
      *out++ = base64[ ((in[0]<<4)&0x30) | ((in[1]>>4)&0x0f) ];
      *out++ = base64[ ((in[1]<<2)&0x3c) | ((in[2]>>6)&0x03) ];
      *out++ = base64[ in[2]&0x3f ];
    } else if(n == 2) {
      *out++ = base64[ ((in[0]<<4)&0x30) | ((in[1]>>4)&0x0f) ];
      *out++ = base64[ ((in[1]<<2)&0x3c) ];
      *out++ = '=';
    } else if(n == 1) {
      *out++ = base64[ ((in[0]<<4)&0x30) ];
      *out++ = '=';
      *out++ = '=';
    }
    len += 4;
    n -= 3;
    in += 3;
  }
  *out = '\0';
  return len;
}

static char *util_malloc(int n)
{
  char *p;

  if((p=malloc(n)) == NULL) {
    logger_fatal("Can not malloc(%d) memory in %s",n,__func__);
  }
  return p;
}

/* return binary out from base64 encoded ASCIIZ in */
int base64decode(const char *in,uint8_t *out, size_t outlen)
{
  char *c,*p,*p0;
  int i,n,len;

  /* FIXME: check outlen */

  len = 0;
  n = strlen(in);
  p0 = (char *)util_malloc(n+4);
  p = p0;
  strlcpy(p0,in,n+4); /* we control and add room above but CVTY complains */
  for(i=0;i<n;i++) {
    if((c=strchr(base64,*p)) == NULL) { free(p0); return -1; }
    *p++ = c - base64;
  }
  p = p0;
  while(n > 0) {
    int k;
    k = (p[2] == 64)?1:(p[3] == 64)?2:3;
    if(k != 3) {
      if(p[2] == 64) p[2] = 0;
      if(p[3] == 64) p[3] = 0;
    }
    *out++ = (p[0]<<2)|(p[1]>>4);
    *out++ = (p[1]<<4)|(p[2]>>2);
    *out++ = (p[2]<<6)|(p[3]);
    n -= 4;
    p += 4;
    len += k;
  }
  free(p0);
  return len;
}

/* generate airline record locator style random strings. note: returned value
   out will be overwritten by subsequent calls */
const char *randomstring()
{
  int i,j;
  static char out[10];
  static int first=1;
  const char aa[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const char nn[]="23456789";

  if(first) {
    time_t t;
    time(&t);
    srand((unsigned)t);
    first = 0;
  }
  j = (rand()%3) + 1;  /* Note: CVTY FIXME - rand() is bad */
  for(i=0;i<5;i++) {
    if(i == j) out[i] = nn[(rand()%8)];
    else out[i] = aa[(rand()%26)];
  }
  out[5] = '\0';
  return out;
}

/* return mbuf filled with pgpwordlist representation of (hash,hashlen) */
mbuf *pgp_wordlist2(const uint8_t *hash,int hashlen)
{
  mbuf *bp;
  int i,j,n;
  char *p;

  n = 0;
  for(i=0;i<256;i++) {
    j = strlen(wordlist_odd[i]);
    if(j > n) n = j;
    j = strlen(wordlist_even[i]);
    if(j > n) n = j;
  }
  bp = alloc_mbuf((hashlen*(n+1)));
  p = (char *)bp->p0;
  for(i=0;i<hashlen;i++) {
    if(i%2) n = sprintf(p,"%s",wordlist_odd[hash[i]]); /* CVTY sufficient mem alloced above */
    else n = sprintf(p,"%s",wordlist_even[hash[i]]);
    p += n;
    if(i != (hashlen-1)) *p++ = ' ';
  }
  *p = '\0';
  return bp;
}

/*********************************************************
 * Hash abstraction
 *********************************************************/
#include "sha1.h"
#include "sha2.h"
/*
 * buf == NULL: Init
 * len = 0: END and return signature in buf
 * else digest len bytes of buf into signature
 *
 genhashctx gh;
 gh.type = HASH_SHA256;
 hashit(&gh,NULL,0);
 hashit(&gh,buf,n);
 hashlen = hashit(&gh,hash,0);
*/
/*#define MAX_HASH_CTX max(sizeof(SHA1Context),sizeof(SHA256_CTX)) */
#define MAX_HASH_CTX 1000 /* FIXME - above works for plain compile but not advanced */
int hashit(genhashctx *gh,uint8_t *buf,int len)
{
  if(buf == NULL) {
    switch(gh->type) {
    case HASH_SHA1:
    case HASH_SHA256:
      break;
    default:
      logger_error("Unsupported hash type %d",gh->type);
      return -1;
    }
    gh->ctx = (char *)util_malloc(MAX_HASH_CTX);
  }
  if(buf == NULL) {
    switch(gh->type) {
    case HASH_SHA1:
      if(SHA1Reset((SHA1Context *)gh->ctx)) {
        logger_error("SHA1Reset() returned error in %s", __func__);
        return -1;
      }
      return 0;
    case HASH_SHA256:
      SHA256_Init((SHA256_CTX *)gh->ctx);
      return 0;
    default:
      logger_error("Unsupported hash type %d",gh->type);
      return -1;
    }
  }
  if(len == 0) {
    switch(gh->type) {
    case HASH_SHA1:
      /* sizeof(buf[])>=20 bytes for msgs digest */
      if(SHA1Result((SHA1Context *)gh->ctx,buf)) {
        logger_error("SHA1Result() returned error in %s", __func__);
        return -1;
      }
      free(gh->ctx);
      return 20;
    case HASH_SHA256:
      SHA256_Final(buf,(SHA256_CTX *)gh->ctx);
      free(gh->ctx);
      return 32;
    default:
      logger_error("Unsupported hash type %d",gh->type);
      return -1;
    }
  }
  switch(gh->type) {
  case HASH_SHA1:
    if(SHA1Input((SHA1Context *)gh->ctx,buf,len)) {
      logger_error("SHA1Context() can't hash in %s", __func__);
      return -1;
      return -1;
    }
    return 0;
  case HASH_SHA256:
    SHA256_Update((SHA256_CTX *)gh->ctx,buf,len);
    return 0;
  default:
    logger_error("Unsupported hash type %d",gh->type);
    return -1;
  }
}

int hashfile(FILE *fp,int htype,uint8_t *dgst)
{
  uint8_t lbuf[1024];
  int hashlen;
  genhashctx gh;

  switch(htype) {
  case HASH_SHA1:
    break;
  case HASH_SHA256:
    break;
  default:
    logger_error("%s: Unknown hash type %d",__func__,htype);
    return -1;
  }
  gh.type = htype;
  hashit(&gh,NULL,0);
  while((hashlen=fread(lbuf,1,sizeof(lbuf),fp)) > 0) {
    hashit(&gh,lbuf,hashlen);
  }
  hashlen = hashit(&gh,dgst,0);
  return hashlen;
}
