#ifndef _CGI_LIB
#define _CGI_LIB 1

#include <stdlib.h>
#include "cgi-llist.h"
#include "fcgi_stdio.h"

/* change this if you are using HTTP upload */
#ifndef UPLOADDIR
#define UPLOADDIR "/tmp"
#endif

/* symbol table for CGI encoding */
#define _NAME 0
#define _VALUE 1


typedef struct _request
{
    FCGX_Request *frequest;
    llist get_list;
    llist post_list;
    llist cookie_list;
    char *psz_postbuf;      // store all post data, including file
    int header;
    int accept_gzip;
} Request;

#define fcgi_printf(r,fmt,args...) do{ if(r->frequest!=NULL) FCGX_FPrintF(r->frequest->out,fmt,##args);\
										else FCGI_printf(fmt,##args); }while(0)
#define fcgi_print(r,fmt,args...) do{if(r->frequest!=NULL)FCGX_FPrintF(r->frequest->out,fmt,##args);\
										else FCGI_printf(fmt,##args);}while(0)


char* fcgi_getvar(Request *r,char* varname);
const char *fcgi_getmethod(Request *r);
const char * fcgi_geturl(Request *r);
char* fcgi_postvar(Request *r,char* varname);

void fcgi_header(Request *r);
int fcgi_write( const char *buf, size_t len,void *r );


void fcgi_request_init(Request * r);

void fcgi_request_free(Request * r);


#endif

