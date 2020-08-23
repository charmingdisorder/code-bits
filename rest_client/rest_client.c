#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#define DEFAULT_CLIENT_ID 0
#define DEFAULT_JSONRPC "2.0"

static unsigned int client_id = DEFAULT_CLIENT_ID;
static char *jsonrpc_ver = NULL;
static char *hostname = NULL;
static unsigned int port = 80;
static char *path = NULL;
static char *method = NULL;
static char **params = NULL;
static int num_params = 0;
static char json_string[4096];
static size_t json_string_len;

static void *safe_malloc(size_t n, unsigned long line)
{
    void *p = malloc(n);

    if (!p)
    {
            fprintf(stderr, "[%s:%lu] malloc() failed (%lu bytes)\n",
                    __FILE__, line, (unsigned long)n);
            exit(EXIT_FAILURE);
    }

    return p;
}

#define SAFEMALLOC(n) safe_malloc(n, __LINE__)

static void usage(int code) {
        fprintf(stdout,
                "./rest_client [-h] [-i ID] [-j JSONRPC] URL METHOD [PARAMS]...\n\n"
                "    -i ID, specifies ID for JSON request (0 used by default)\n"
                "    -j JSONRPC, specifies JSONRPC value for request (\"2.0\" used by default)\n"
                "    -h, print usage information\n");

        exit(code);
}

/*
 * socket_connect -- try establishing TCP connection
 */
int socket_connect()
{
        struct hostent *hp;
        struct sockaddr_in addr;
        int on = 1, sock;

        if((hp = gethostbyname(hostname)) == NULL)
        {
                fprintf(stderr, "Can't resolve %s\n", hostname);
                exit(EXIT_FAILURE);
        }

        bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
        addr.sin_port = htons(port);
        addr.sin_family = AF_INET;
        sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(int));

        if(sock == -1)
        {
                fprintf(stderr, "setsockopt() failed\n");
                exit(EXIT_FAILURE);
        }

        if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) == -1)
        {
                fprintf(stderr, "connect() failed\n");
                exit(EXIT_FAILURE);
        }

        return sock;
}

/*
 * parse_uint -- parses string to unsigned integer
 */
static unsigned int parse_uint(char *st)
{
        char *x;

        for (x = st; *x; x++)
        {
                if (!isdigit(*x))
                {
                        fprintf(stderr,
                                "Malformed URL, can't parse: \"%s\" (excepting unsigned integer)\n", st);
                        exit(EXIT_FAILURE);
                }
        }

        return strtoul(st, 0L, 10);
}

/*
 * parse_url -- my poor man's URL parser
 *
 * Very dumb and straightforward URL parser. Just simple URLs like
 * [http://]hostname[:port]/a/b/c are supported:
 */
static void parse_url(const char *url)
{
        char *ptr, *ptr2;

        ptr = strchr(url, ':');

        if (ptr)
        {
                if (strlen(ptr) < 4)
                {
                        fprintf(stderr, "Malformed URL\n");
                        exit(EXIT_FAILURE);
                }

                if (strncmp(ptr, "://", 3) == 0)
                {
                        if (!((ptr-url == 4) && (strncmp(url, "http", 4) == 0)))
                        {
                                fprintf(stderr, "Only HTTP protocol is supported (http://)\n");
                                exit(EXIT_FAILURE);
                        }

                        ptr += 3;
                        ptr2 = strchr(ptr, ':');

                        if (ptr2) {
                                char *pstr;
                                size_t plen;
                                size_t hlen = ptr2 - ptr;

                                hostname = SAFEMALLOC(hlen+1);
                                strncpy(hostname, ptr, hlen);

                                path = strchr(ptr2, '/');

                                if (!path) {
                                        /* XXX: may be path is '/' for something like http://a ? */
                                        fprintf(stderr, "Malformed URL\n");
                                        exit(EXIT_FAILURE);
                                }


                                ptr2++;

                                plen = path-ptr2;
                                pstr = SAFEMALLOC(plen+1);

                                strncpy(pstr, ptr2, plen);
                                port = parse_uint(pstr);
                                free(pstr);
                        } else {
                                size_t hlen;

                                path = strchr(ptr, '/');

                                if (!path) {
                                        fprintf(stderr, "Malformed URL\n");
                                        exit(EXIT_FAILURE);
                                }

                                hlen = path - ptr;
                                hostname = SAFEMALLOC(hlen+1);
                                strncpy(hostname, ptr, hlen);
                        }
                } else {
                        size_t hlen;
                        size_t plen;
                        char *pstr;

                        hlen = ptr - url;
                        hostname = SAFEMALLOC(hlen+1);
                        strncpy(hostname, url, hlen);

                        ptr++;
                        path = strchr(ptr, '/');

                        if (!path) {
                                fprintf(stderr, "Malformed URL\n");
                                exit(EXIT_FAILURE);
                        }

                        plen = path - ptr;
                        pstr = SAFEMALLOC(plen+1);
                        strncpy(pstr, ptr, plen);
                        port = parse_uint(pstr);
                        free(pstr);
                }
        } else {
                ptr = strchr(url, '/');

                if (!ptr || (strlen(ptr) < 2))
                {
                        fprintf(stderr, "Malformed URL\n");
                        exit(EXIT_FAILURE);
                }

                hostname = SAFEMALLOC(ptr-url+1);
                strncpy(hostname, url, ptr-url);

                path = ptr;

        }

        if (port == 0 || !hostname || !path) {
                fprintf(stderr, "Malformed URL\n");
                exit(EXIT_FAILURE);
        }
}

static void generate_json_string()
{
//{"id":0, "jsonrpc":"2.0","method":"systemInfo","params":[]}
        unsigned int i;
        sprintf(json_string, "{\"id\":%u, \"jsonrpc\":\"%s\",\"method\":\"%s\",\"params\":[",
                 client_id, jsonrpc_ver, method);

        for (i = 0; i < num_params; i++) {
                strcat (json_string, params[i]);
                if (i < (num_params - 1)) {
                        strcat(json_string, ",");
                }
        }

        strcat(json_string, "]}");

        json_string_len = strlen(json_string);

        fprintf(stderr, "json_string:\n%s\n", json_string);
        return;
}

int main (int argc, char **argv)
{
        int opt, fd, i;
        char buf[BUFSIZ];
        ssize_t len;

        while ((opt = getopt(argc, argv, "i:j:h")) != -1)
        {
                switch (opt)
                {
                case 'h':
                        usage(EXIT_SUCCESS);
                        break;
                case 'i':
                        client_id = parse_uint(optarg);
                        break;
                case 'j':
                        jsonrpc_ver = strdup(optarg);
                        break;
                case '?':
                        fprintf(stderr, "Unknown option: %c\n\n", optopt);
                        usage(EXIT_FAILURE);
                        break;
                }
        }

        if ((argc-optind) < 2)
                usage(EXIT_FAILURE);

        if (!jsonrpc_ver)
                jsonrpc_ver = strdup(DEFAULT_JSONRPC);

        parse_url(argv[optind]);
        optind++;

        method = strdup(argv[optind]);
        optind++;

        num_params = argc-optind;
        params = SAFEMALLOC(sizeof(char *) * num_params);

        i = 0;

        while (optind < argc) {
                params[i] = argv[optind];
                i++;
                optind++;
        }

        generate_json_string();

        fd = socket_connect();

        len = snprintf(buf, BUFSIZ, "POST %s HTTP/1.0\r\n", path);
        write(fd, buf, len);

        len = snprintf(buf, BUFSIZ, "Content-type: application/json\r\n");
        write(fd, buf, len);

        len = snprintf(buf, BUFSIZ, "Content-Length: %ld\r\n\r\n", json_string_len);
        write(fd, buf, len);

        write(fd, json_string, json_string_len);

        while ((len = read(fd, buf, BUFSIZ)) > 0) {
                fprintf(stdout, "%s\n", buf);
        }

        if (len < 0) {
                fprintf(stderr, "read() failed: %s\n", strerror(errno));
        }

        shutdown(fd, SHUT_RDWR);
        close(fd);
        return (len < 0) ? 1 : 0;
}

/*
  `--> ./rest_client -i 4 http://postman-echo.com/post systeminfo \"test\" 1.0
json_string:
{"id":4, "jsonrpc":"2.0","method":"systeminfo","params":["test",1.0]}
HTTP/1.1 200 OK
Date: Sun, 23 Aug 2020 19:19:43 GMT
Content-Type: application/json; charset=utf-8
Content-Length: 529
Connection: close
ETag: W/"211-VvQUfMCq5EjAXsMFCFKTaTEapdA"
Vary: Accept-Encoding
set-cookie: sails.sid=s%3AFA-eUFIgZ2UWtXyMl0vLHYAM1CK20pAI.f5tISwf%2F6lBH
qGuRYLf%2BaaCB%2FYe9AtNzCxm4E4tNVrg; Path=/; HttpOnly

{"args":{},"data":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":
["test",1]},"files":{},"form":{},"headers":{"x-forwarded-proto":"http","x
-forwarded-port":"80","host":"a0207c42-pmecho-pmechoingr-f077-962204340.u
s-east-1.elb.amazonaws.com","x-amzn-trace-id":"Root=1-5f42c14f-677908691c
5f89d97081ee45","content-length":"69","content-type":"application/json"},
"json":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":["test",1]},"url":"http://a0207c42-pmecho-pmechoingr-f077-962204340.us-east-1.elb.am
azonaws.com/post"
}TTP/1.1 200 OK
Date: Sun, 23 Aug 2020 19:19:43 GMT
Content-Type: application/json; charset=utf-8
Content-Length: 529
Connection: close
ETag: W/"211-VvQUfMCq5EjAXsMFCFKTaTEapdA"
Vary: Accept-Encoding
set-cookie: sails.sid=s%3AFA-eUFIgZ2UWtXyMl0vLHYAM1CK20pAI.f5tISwf%2F6lBHqGuRYLf%2BaaCB%2FYe9AtNzCxm4E4tNVrg; Path=/; HttpOnly

{"args":{},"data":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":["test",1]},"files":{},"form":{},"headers":{"x-forwarded-proto":"http","x-forwarded-port":"80","host":"a0207c42-pmecho-pmechoingr-f077-962204340.us-east-1.elb.amazonaws.com","x-amzn-trace-id":"Root=1-5f42c14f-677908691c5f89d97081ee45","content-length":"69","content-type":"application/json"},"json":{"id":4,"jsonrpc":"2.0","method":"systeminfo","params":["test",1]},"url":"http://a0207c42-pmecho-pmechoingr-f077-962204340.us-east-1.elb.amazonaws.com/post"
*/