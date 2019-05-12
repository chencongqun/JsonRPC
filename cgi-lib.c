#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include "cgi-lib.h"

static const char *fcgi_getenv(Request *r,const char *name)
{
    if(r->frequest==NULL)
        return getenv(name);
    else
        return FCGX_GetParam(name, r->frequest->envp);
}

static char *fcgi_val(llist l, char *name)
{
    short FOUND = 0;
    node* window;

    window = l.head;
    while ( (window != 0) && (!FOUND) )
        if (!strcmp(window->entry.name,name))
            FOUND = 1;
        else
            window = window->next;
    if (FOUND && NULL != window->entry.value &&0 != strlen(window->entry.value))
        return window->entry.value;
    else
        return NULL;
}

char* fcgi_getvar(Request *r,char* varname)
{
    return fcgi_val(r->get_list,varname);
}

const char *fcgi_getmethod(Request *r)
{
    return fcgi_getenv(r, "REQUEST_METHOD");
}

const char * fcgi_geturl(Request *r)
{
    return fcgi_getenv( r, "SCRIPT_NAME");
}


char* fcgi_postvar(Request *r,char* varname)
{
    return fcgi_val(r->post_list,varname);
}

void fcgi_header(Request *r)
{
    if(r->header==0)
    {
        fcgi_printf(r, "%s", "Content-type: text/html\n");
        fcgi_printf(r, "%s", "Cache-Control: no-cache\n");
        fcgi_printf(r, "%s", "Expires: 0\n");
        fcgi_printf(r, "%s", "P3P: CP=CAO PSA OUR\r\n\r\n");
        r->header =1;
    }
}

static int fcgi_fread( Request *r, char *str, int n )
{
    if(r->frequest==NULL)
        return fread(str,1,n,stdin);
    else
        return FCGX_GetStr(str,n,r->frequest->in);
}

int fcgi_write( const char *buf, size_t len,void *r )
{
    Request *request =(Request *)r ;

    if(request->frequest==NULL)
        return    fwrite((void*)buf,1,len,stdout);
    else
        return FCGX_PutStr(buf,len,request->frequest->out);
}

static int parse_cookies( Request *r,llist *entries)
{
    const char *cookies = fcgi_getenv( r,"HTTP_COOKIE" );
    node* window;
    entrytype entry;
    int i,len;
    int j = 0;
    int numcookies = 0;
    short NM = 1;

    if (cookies == NULL)
        return 0;
    list_create(entries);
    window = entries->head;
    len = (int)strlen(cookies);
    entry.name = (char *)malloc(sizeof(char) * len + 1);
    entry.value = (char *)malloc(sizeof(char) * len + 1);
    for (i = 0; i < len; i++)
    {
        if (cookies[i] == '=')
        {
            entry.name[j] = '\0';
            if (i == len - 1)
            {
                strcpy(entry.value,"");
                window = list_insafter(entries,window,entry);
                numcookies++;
            }
            j = 0;
            NM = 0;
        }
        else if ( (cookies[i] == '&') || (i == len - 1) )
        {
            if (!NM)
            {
                if (i == len - 1)
                {
                    entry.value[j] = cookies[i];
                    j++;
                }
                entry.value[j] = '\0';
                window = list_insafter(entries,window,entry);
                numcookies++;
                j = 0;
                NM = 1;
            }
        }
        else if ( (cookies[i] == ';') || (i == len - 1) )
        {
            if (!NM)
            {
                if (i == len - 1)
                {
                    entry.value[j] = cookies[i];
                    j++;
                }
                entry.value[j] = '\0';
                window = list_insafter(entries,window,entry);
                numcookies++;
                i++;   /* erases trailing space */
                j = 0;
                NM = 1;
            }
        }
        else if (NM)
        {
            entry.name[j] = cookies[i];
            j++;
        }
        else if (!NM)
        {
            entry.value[j] = cookies[i];
            j++;
        }
    }

    free(entry.name);
    free(entry.value);
    return numcookies;
}

char *get_POST( Request *r )
{
    unsigned int content_length;
    char *buffer = NULL;

    const char * psz_content_length = fcgi_getenv( r, "CONTENT_LENGTH" );
    if ( psz_content_length != NULL )
    {
        content_length = atoi(psz_content_length);
        if ( content_length < 0 )
        {
            fprintf(stderr,"caught by cgihtml: CONTENT_LENGTH < 0\n");
            exit(1);
        }

        buffer = (char *)malloc(sizeof(char) * content_length + 1);
        if ( fcgi_fread( r, buffer, content_length ) != content_length )
        {
            fprintf(stderr,"caught by cgihtml: input length < CONTENT_LENGTH\n");
            exit(1);
        }
        buffer[content_length] = '\0';
    }
    return buffer;
}

char *get_GET( Request * r )
{
    char *buffer;
	const char * psz_query = fcgi_getenv( r, "QUERY_STRING" );
    if ( psz_query == NULL )
        return NULL;
    buffer = strdup( psz_query );
    return buffer;
}


static int _getline( Request *r,char s[], int lim)
{
    int c=0, i=0, num;

    for (i=0; (i<lim) && ((c=FCGX_GetChar(r->frequest->in))!=EOF) && (c!='\n'); i++)
    {
        s[i] = c;
    }
    if (c == '\n')
    {
        s[i] = c;
    }
    if ((i==0) && (c!='\n'))
        num = 0;
    else if (i == lim)
        num = i;
    else
        num = i+1;
    return num;
}
#define getline _getline

char x2c(char *what)
{
    register char digit;

    digit = (what[0] >= 'A' ? ((what[0] & 0xdf) - 'A')+10 : (what[0] - '0'));
    digit *= 16;
    digit += (what[1] >= 'A' ? ((what[1] & 0xdf) - 'A')+10 : (what[1] - '0'));
    return(digit);
}

void unescape_url(char *url)
{
    register int x,y;

    for (x=0,y=0; url[y]; ++x,++y) {
        if((url[x] = url[y]) == '%') {
            url[x] = x2c(&url[y+1]);
            y+=2;
        }
        if(url[x]=='+') {
            url[x]=' ';
        }
    }
    url[x] = '\0';
}

char *lower_case( char *buffer )
{
    char *tempstr = buffer;

    while (*buffer != '\0')
    {
        if ( isupper(*buffer) )
            *buffer = tolower(*buffer);
        buffer++;
    }
    return tempstr;
}

int parse_CGI_encoded(llist *entries, char *buffer)
{
    int i, j, num, token;
    int len = (int)strlen(buffer);
    char *lexeme = (char *)malloc(sizeof(char) * len + 1);
    entrytype entry;
    node *window;

    list_create(entries);
    window = entries->head;
    entry.name = NULL;
    entry.value = NULL;
    i = 0;
    num = 0;
    token = _NAME;
    while (i < len)
    {
        j = 0;
        while ( (buffer[i] != '=') && (buffer[i] != '&') && (i < len) )
        {
            lexeme[j] = (buffer[i] == '+') ? ' ' : buffer[i];
            i++;
            j++;
        }
        lexeme[j] = '\0';
        if (token == _NAME)
        {
            entry.name = strdup(lexeme);
            unescape_url(entry.name);
            if ( (buffer[i] != '=') || (i == len - 1) )
            {
                entry.value = (char *)malloc(sizeof(char));
                strcpy(entry.value,"");
                window = list_insafter(entries, window, entry);
                free(entry.name);
                entry.name = NULL;
                free(entry.value);
                entry.value = NULL;
                if (i == len - 1) /* null value at end of expression */
                    num++;
                else   /* error in expression */
                {
                    free(lexeme);
                    return -1;
                }
            }
            else
                token = _VALUE;
        }
        else
        {
            entry.value = strdup(lexeme);
            unescape_url(entry.value);
            window = list_insafter(entries, window, entry);
            free(entry.name);
            entry.name = NULL;
            free(entry.value);
            entry.value = NULL;
            token = _NAME;
            num++;
        }
        i++;
        j = 0;
    }
    free(lexeme);
    if (entry.name != NULL)
        free(entry.name);
    if (entry.value != NULL)
        free(entry.value);
    return num;
}


int parse_form_encoded( Request *r,llist* entries )
{
    entrytype entry;
    node* window;
    FILE *uploadfile = NULL;
    char *uploadfname, *tempstr, *boundary;
    char *buffer = (char *)malloc(sizeof(char) * BUFSIZ + 1);
    char *prevbuf = (char *)malloc(sizeof(char) + BUFSIZ + 1);
    short isfile,done,start;
    int i,j;
    int bytesread,prevbytesread = 0;
    int buffersize;
    int numentries = 0;

    char *psz_ua = (char *)fcgi_getenv( r, "HTTP_USER_AGENT" );
    uploadfname = NULL;


    if ( !fcgi_getenv( r, "CONTENT_LENGTH" ) )
        return 0;

    tempstr = strdup( fcgi_getenv( r, "CONTENT_TYPE" ) );
    boundary = strstr(tempstr,"boundary=");
    boundary += (sizeof(char) * 9);
    /* create list */
    list_create(entries);
    window = entries->head;

    getline(r,buffer,BUFSIZ);

    while ((bytesread=getline(r,buffer,BUFSIZ)) != 0)
    {
        start = 1;
        buffer[bytesread] = '\0';
        tempstr = strdup(buffer);
        tempstr += (sizeof(char) * 38); /* 38 is header up to name */
        entry.name = tempstr;
        entry.value = (char *)malloc(sizeof(char) * BUFSIZ + 1);
        buffersize = BUFSIZ;
        strcpy(entry.value,"");
        while (*tempstr != '"')
            tempstr++;
        *tempstr = '\0';
        if (strstr(buffer,"filename=\"") != NULL)
        {
            isfile = 1;
            tempstr = strdup(buffer);
            tempstr = strstr(tempstr,"filename=\"");
            tempstr += (sizeof(char) * 10);
            if (strlen(tempstr) >= BUFSIZ)
                entry.value = (char *) realloc(entry.value, sizeof(char) *
                                               strlen(tempstr)+1);
            free(entry.value);
            entry.value = tempstr;
            while (*tempstr != '"')
                tempstr++;
            *tempstr = '\0';
            /* Netscape's Windows browsers handle paths differently from its
            UNIX and Mac browsers.  It delivers a full path for the uploaded
            file (which it shouldn't do), and it uses backslashes rather than
            forward slashes.  No need to worry about Internet Explorer, since
            it doesn't support HTTP File Upload at all. */
            if (psz_ua && strstr(lower_case(psz_ua),"win") != 0)
            {
                tempstr = strrchr(entry.value, '\\');
                if (tempstr)
                {
                    tempstr++;
                    entry.value = tempstr;
                }
                else
                {
                    /* add by X.J */
                    tempstr = strrchr(entry.value, '/');
                    if (tempstr)
                    {
                        tempstr++;
                        entry.value = tempstr;
                    }
                }
            }
            window = list_insafter(entries,window,entry);
            numentries++;
            uploadfname = (char *)malloc(strlen(UPLOADDIR)+strlen(entry.value)+10);
            sprintf(uploadfname,"%s/%d_%s", UPLOADDIR, getpid(), entry.value);
            if ( (uploadfile = fopen(uploadfname,"w")) == NULL)
            {
                /* null filename; for now, just don't save info.  later, save
                to default file */
                isfile = 0;
            }
        }
        else
            isfile = 0;
        /* ignore rest of headers and first blank line */
        while (getline(r,buffer, BUFSIZ) > 1)
        {
            /* DOS style blank line? */
            if ((buffer[0] == '\r') && (buffer[1] == '\n'))
                break;
        }
        done = 0;
        j = 0;
        while (!done)
        {
            bytesread = getline(r,buffer,BUFSIZ);
            buffer[bytesread] = '\0';
            if (bytesread && strstr(buffer,boundary) == NULL)
            {
                if (start)
                {
                    i = 0;
                    while (i < bytesread)
                    {
                        prevbuf[i] = buffer[i];
                        i++;
                    }
                    prevbytesread = bytesread;
                    start = 0;
                }
                else
                {
                    /* flush buffer */
                    i = 0;
                    while (i < prevbytesread)
                    {
                        if (isfile)
                            fputc(prevbuf[i],uploadfile);
                        else
                        {
                            if (j > buffersize)
                            {
                                buffersize += BUFSIZ;
                                entry.value = (char *) realloc(entry.value, sizeof(char) *
                                                               buffersize+1);
                            }
                            entry.value[j] = prevbuf[i];
                            j++;
                        }
                        i++;
                    }
                    /* buffer new input */
                    i = 0;
                    while (i < bytesread)
                    {
                        prevbuf[i] = buffer[i];
                        i++;
                    }
                    prevbytesread = bytesread;
                }
            }
            else
            {
                done = 1;
                /* flush buffer except last two characters */
                i = 0;
                while (i < prevbytesread - 2)
                {
                    if (isfile)
                        fputc(prevbuf[i],uploadfile);
                    else
                    {
                        if (j > buffersize)
                        {
                            buffersize += BUFSIZ;
                            entry.value = (char *) realloc(entry.value, sizeof(char) *
                                                           buffersize+1);
                        }
                        entry.value[j] = prevbuf[i];
                        j++;
                    }
                    i++;
                }
            }
        }
        if (isfile)
            fclose(uploadfile);
        else
        {
            entry.value[j] = '\0';
            window = list_insafter(entries,window,entry);
            free(entry.value);
            numentries++;
            j = 0;
        }
    }

    if (uploadfname)
        free(uploadfname);
    if (prevbuf)
        free(prevbuf);
    if (buffer)
        free(buffer);
    return numentries;
}


static char *get_DEBUG()
{
    int bufsize = 1024;
    char *buffer = (char *)malloc(sizeof(char) * bufsize + 1);
    int i = 0;
    char ch;

    fprintf(stderr,"\n--- cgihtml Interactive Mode ---\n");
    fprintf(stderr,"Enter CGI input string.  Remember to encode appropriate ");
    fprintf(stderr,"characters.\nPress ENTER when done:\n\n");
    while ( (i<=bufsize) && ((ch = getc(stdin)) != '\n') )
    {
        buffer[i] = ch;
        i++;
        if (i>bufsize)
        {
            bufsize *= 2;
            buffer = (char *)realloc(buffer,bufsize);
        }
    }
    buffer[i] = '\0';
    fprintf(stderr,"\n Input string: %s\nString length: %d\n",buffer,i);
    fprintf(stderr,"--- end cgihtml Interactive Mode ---\n\n");
    return buffer;
}

static int read_cgi_input( Request *r, llist* entries, char *method)
{
    char *input;
    int status;

    if (method == NULL)
    {
        input = get_DEBUG();
    }
    else if (!strcmp(method, "POST"))
    {
        const char *psz_content_type = fcgi_getenv( r,"CONTENT_TYPE" );
        if (( psz_content_type != NULL ) && (strstr( psz_content_type, "multipart/form-data" ) != NULL) )
            return parse_form_encoded( r,entries );

        input = get_POST( r );
    }
    else if (!strcmp(method, "GET"))
    {
        input = get_GET( r );
    }
    else
    {
        fprintf(stderr,"caught by cgihtml: REQUEST_METHOD invalid\n");
        exit(1);
    }

    if (input == NULL)
        return 0;
    status = parse_CGI_encoded( entries, input );
    free(input);
    return status;
}

static void parse_encoding(Request * r)
{
    const char * psz_accept_encoding = fcgi_getenv( r, "HTTP_ACCEPT_ENCODING" );
    if ( psz_accept_encoding && NULL != strstr(psz_accept_encoding, "gzip") )
        r->accept_gzip = 1;
    else
        r->accept_gzip = 0;
}

void fcgi_request_init(Request * r)
{
    memset(&r->cookie_list, 0, sizeof(llist));
    memset(&r->get_list, 0, sizeof(llist));
    memset(&r->post_list, 0, sizeof(llist));
    parse_cookies( r,&r->cookie_list );
    read_cgi_input( r,&r->get_list,"GET" );
    read_cgi_input( r,&r->post_list,"POST" );
    parse_encoding( r );
}

void fcgi_request_free(Request * r)
{
    list_clear(&r->get_list);
    list_clear(&r->post_list);
    list_clear(&r->cookie_list);
}


