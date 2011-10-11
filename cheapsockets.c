#include <ctype.h>
#include <stdarg.h>
#include <curl/curl.h>

#include "cheapsockets.h"

void cheapsockets_init(struct cheapsockets_t *sock){
    sock->offset = 0;
    sock->reconnect_list = NULL;
    sock->netbuf[0] = '\0';

    curl_global_init(CURL_GLOBAL_ALL);
    sock->multi = curl_multi_init();

    curl_multi_setopt(sock->multi, CURLMOPT_SOCKETFUNCTION, cheapsockets_socket_cb);
    curl_multi_setopt(sock->multi, CURLMOPT_TIMERFUNCTION, cheapsockets_timeout_cb);
    curl_multi_setopt(sock->multi, CURLMOPT_SOCKETDATA, sock);
    curl_multi_setopt(sock->multi, CURLMOPT_TIMERDATA, sock);
}

void cheapsockets_seturls(struct cheapsockets_t *sock, const char *get_url, const char *put_url){
    s->get_url = strdup(get_url);
    s->put_url = strdup(put_url);
}

int cheapsockets_connect(struct cheapsocket_t *sock){
    char buf[128];
    sprintf(buf, "%s?offset=%lu", sock->get_url, sock->offset);
    return cheapsockets_new_conn(sock, buf, 1);
}

int cheapsockets_send(struct cheapsocket_t *sock, void *obj, const char *fmt, ...){
    va_list args;
    char *url;
    char *msg = NULL;
    int n;

    va_start(args, fmt);
    n = 1 + vsnprintf(msg, 0, fmt, args);
    va_end(args);

    msg = (char *)malloc(n);

    va_start(args, fmt);
    vsnprintf(msg, n, fmt, args);
    va_end(args);

    url = (char *)malloc(3 * n + strlen(sock->put_url) + strlen("?") + 1);
    sprintf(url, "%s?", sock->put_url);
    char *endp = url + strlen(url);
    char *msgp = msg;

    while(*msgp){
        if (!isalnum(*msgp)){
            sprintf(endp, "%%%2X", *msgp);
            endp += 3;
        } else {
            *endp++ = *msgp;
        }
        msgp++;
    }
    *endp = '\0';
    cheapsockets_new_conn(sock, url, 0);
    free(url);
    free(msg);
}

size_t cheapsockets_ignore_data(void *buf, size_t size, size_t nmemb, void *data){
    (void)buf;
    (void)data;
    /* TODO handle this data reply */
    return size * nmemb;
}

size_t net_t::parse_packet_cb(void *buf, size_t size, size_t nmemb, void *data){
    struct ConnInfo *conn = (struct ConnInfo *)data;
    struct cheapsocket_t *sock = conn->global;
    unsigned tmp_offset = 0;
    char *packetbuf = (char *)malloc(size * nmemb + strlen(net->netbuf) + 1);
    strcpy(packetbuf, sock->netbuf);
    strncat(packetbuf, (const char *)buf, size * nmemb);
    char *start = packetbuf;
    for(;;){
        if (sscanf(start, "%u", &tmp_offset) != 1){
            break;
        }
        char *nl = strchr(start, '\n');
        if (!nl){
            break;
        }
        *nl = '\0';
        if (tmp_offset > 0){
            char *space = strchr(start, ' ');
            if (!space){
                break;
            }
            start = space + 1;
        }
        (sock->packet_handler)(start);
        start = nl + 1;
        net->offset = tmp_offset;
    }
    strcpy(net->netbuf, start);
    free(packetbuf);

    return size * nmemb;
}

void cheapsockets_timer_cb(void *data){
    struct cheapsockets_t *sock = (struct cheapsockets_t *)data;
    CURLMcode rc = curl_multi_socket_action(sock->multi,
                                            CURL_SOCKET_TIMEOUT,
                                            0,
                                            &sock->still_running);
    if (rc != CURLM_OK){
        //TODO handle error
    }
    check_multi_info(sock);
}

void cheapsockets_perform_reconnects_cb(void *data){
    struct cheapsockets_t *sock = (struct cheapsockets_t *)data;
    struct ConnInfo *conn;
    struct ConnInfo *next;

    for (conn = sock->reconnect_list; conn; conn = next){
        char *url = conn->url;
        if (conn->is_poll){
            char buf[128];
            sprintf(buf, "%s?offset=%lu", sock->get_url, sock->offset);
            url = buf;
        }
        cheapsockets_new_conn(sock, url, conn->is_poll);
        next = conn->next;
        free(conn->url);
        free(conn);
    }
    sock->reconnect_list = NULL;
}

void net_t::cleanup_completed_transfer(CURLMsg *msg){
    struct ConnInfo *conn;
    CURL *easy = msg->easy_handle;
    CURLcode res = msg->data.result;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
    curl_multi_remove_handle(multi, easy);
    curl_easy_cleanup(easy);

    if (res != CURLE_OK){
        printf("died: %s%s\n", curl_easy_strerror(res), conn->is_poll ? " (poll)" : " (upload)");
        conn->next = reconnect_list;
        reconnect_list = conn;
        Fl::remove_timeout(perform_reconnects_cb);
        Fl::add_timeout(1.0, perform_reconnects_cb, (void*)this);
        return;
    }

    if (conn->is_poll){
        printf("connection died!\n");
        Fl::remove_timeout(timer_cb);
        char buf[128];
        sprintf(buf, "%s?offset=%lu", get_url, offset);
        new_conn(buf, 1);
        timer_cb((void*)this);
    }
    free(conn->url);
    free(conn);
}

void net_t::check_multi_info(){
    CURLMsg *msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(multi, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            cleanup_completed_transfer(msg);
        }
    }
}

int net_t::update_timeout_cb(CURLM *multi, long timeout_ms, void *userp){
    (void)multi;
    double t = (double)timeout_ms / 1000.0;
    net_t *net=(net_t *)userp;

    if (timeout_ms > -1){
        Fl::add_timeout(t, timer_cb, (void *)net);
    }
    return 0;
}

void net_t::event_cb(int fd, void *data){
    net_t *net = (net_t*) data;
    CURLMcode rc;

    rc = curl_multi_socket_action(net->multi, fd, 0, &net->still_running);
    net->mcode_or_die("event_cb: curl_multi_socket_action", rc);

    net->check_multi_info();
    if(!net->still_running) {
        Fl::remove_timeout(timer_cb);
    }
}

void net_t::remsock(struct SockInfo *f){
    if (!f){
        return;
    }
    Fl::remove_fd(f->fd);
    free(f);
}

void net_t::setsock(struct SockInfo *f, curl_socket_t s, CURL*e, int act){
    int when = (act&CURL_POLL_IN?FL_READ:0)|(act&CURL_POLL_OUT?FL_WRITE:0);

    f->sockfd = s;
    f->action = act;
    f->easy = e;
    Fl::remove_fd(f->fd);
    Fl::add_fd(f->fd, when, event_cb, (void *)this);
}

void net_t::addsock(curl_socket_t s, CURL *easy, int action) {
    struct SockInfo *fdp = (struct SockInfo *)malloc(sizeof(struct SockInfo));
    memset(fdp, sizeof(struct SockInfo), '\0');

    fdp->global = this;
    fdp->fd = s;
    setsock(fdp, s, easy, action);
    curl_multi_assign(multi, s, fdp);
}

int net_t::sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp){
    net_t *net = (net_t*) cbp;
    struct SockInfo *fdp = (struct SockInfo*) sockp;

    if (what == CURL_POLL_REMOVE) {
        net->remsock(fdp);
    } else if (!fdp) {
        net->addsock(s, e, what);
    } else {
        net->setsock(fdp, s, e, what);
    }
    return 0;
}

void net_t::new_conn(const char *url, bool is_poll){
    CURLMcode rc;

    struct ConnInfo *conn = (struct ConnInfo *)malloc(sizeof(struct ConnInfo));
    memset(conn, sizeof(struct ConnInfo), '\0');

    conn->is_poll = is_poll;

    conn->easy = curl_easy_init();
    if (!conn->easy) {
        printf("curl_easy_init() failed, exiting!\n");
        exit(2);
    }
    conn->global = this;
    conn->url = strdup(url);
    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
    if (conn->is_poll){
        curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, parse_packet_cb);
    } else {
        curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, ignore_data);
    }
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1L);
    //curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 30L);

    rc = curl_multi_add_handle(multi, conn->easy);
    mcode_or_die("new_conn: curl_multi_add_handle", rc);
}