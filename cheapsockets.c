#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <curl/curl.h>

#include "cheapsockets.h"

void cheapsocket_seturls(cheapsocket *sock, const char *get_url, const char *put_url){
    sock->get_url = strdup(get_url);
    sock->put_url = strdup(put_url);
}

int cheapsocket_connect(cheapsocket *sock){
    char buf[128];
    sprintf(buf, "%s?offset=%lu", sock->get_url, sock->offset);
    return cheapsocket_new_conn(sock, buf, 1);
}

int cheapsocket_send(cheapsocket *sock, void *obj, const char *fmt, ...){
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
    cheapsocket_new_conn(sock, url, 0);
    free(url);
    free(msg);
}

size_t cheapsocket_ignore_data(void *buf, size_t size, size_t nmemb, void *data){
    (void)buf;
    (void)data;
    /* TODO handle this data reply */
    return size * nmemb;
}

size_t cheapsocket_packet_cb(void *buf, size_t size, size_t nmemb, void *data){
    struct ConnInfo *conn = (struct ConnInfo *)data;
    cheapsocket *sock = conn->global;
    unsigned tmp_offset = 0;
    char *packetbuf = (char *)malloc(size * nmemb + strlen(sock->netbuf) + 1);
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
        sock->offset = tmp_offset;
    }
    strcpy(sock->netbuf, start);
    free(packetbuf);

    return size * nmemb;
}

void cheapsocket_timer_cb(void *data){
    cheapsocket *sock = (cheapsocket *)data;
    CURLMcode rc = curl_multi_socket_action(sock->multi,
                                            CURL_SOCKET_TIMEOUT,
                                            0,
                                            &sock->still_running);
    if (rc != CURLM_OK){
        //TODO handle error
    }
    check_multi_info(sock);
}

void cheapsocket_perform_reconnects_cb(void *data){
    cheapsocket *sock = (cheapsocket *)data;
    struct ConnInfo *conn;
    struct ConnInfo *next;

    for (conn = sock->reconnect_list; conn; conn = next){
        char *url = conn->url;
        if (conn->is_poll){
            char buf[128];
            sprintf(buf, "%s?offset=%lu", sock->get_url, sock->offset);
            url = buf;
        }
        cheapsocket_new_conn(sock, url, conn->is_poll);
        next = conn->next;
        free(conn->url);
        free(conn);
    }
    sock->reconnect_list = NULL;
}

void cheapsocket_cleanup_transfer(cheapsocket *sock, CURLMsg *msg){
    struct ConnInfo *conn;
    CURL *easy = msg->easy_handle;
    CURLcode res = msg->data.result;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
    curl_multi_remove_handle(sock->multi, easy);
    curl_easy_cleanup(easy);

    if (res != CURLE_OK){
        printf("died: %s%s\n", curl_easy_strerror(res), conn->is_poll ? " (poll)" : " (upload)");
        conn->next = sock->reconnect_list;
        sock->reconnect_list = conn;
        sock->remove_timeout(cheapsocket_perform_reconnects_cb);
        sock->add_timeout(1.0, cheapsocket_perform_reconnects_cb, (void*)sock);
        return;
    }

    if (conn->is_poll){
        printf("connection died!\n");
        sock->remove_timeout(cheapsocket_timer_cb);
        char buf[128];
        sprintf(buf, "%s?offset=%lu", sock->get_url, sock->offset);
        new_conn(buf, 1);
        timer_cb((void*)sock);
    }
    free(conn->url);
    free(conn);
}

void cheapsocket_check_multi_info(cheapsocket *sock){
    CURLMsg *msg;
    int msgs_left;

    while ((msg = curl_multi_info_read(sock->multi, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            cleanup_completed_transfer(sock, msg);
        }
    }
}

int cheapsocket_timeout_cb(CURLM *multi, long timeout_ms, void *userp){
    (void)multi;
    double t = (double)timeout_ms / 1000.0;
    cheapsocket *sock = (cheapsocket *)userp;

    if (timeout_ms > -1){
        sock->add_timeout(t, cheapsocket_timer_cb, (void *)sock);
    }
    return 0;
}

void cheapsocket_event_cb(int fd, void *data){
    cheapsocket *sock = (cheapsocket *)data;
    CURLMcode rc;

    rc = curl_multi_socket_action(sock->multi, fd, 0, &sock->still_running);
    if (rc != CURLM_OK){
        //TODO handle this
    }

    cheapsocket_check_multi_info(sock);
    if(!sock->still_running){
        sock->remove_timeout(cheapsocket_timer_cb);
    }
}

void simplesocket_rmsock(cheapsocket *sock, struct SockInfo *f){
    if (!f){
        return;
    }
    sock->remove_fd(f->fd);
    free(f);
}

void simplesocket_setsock(cheapsocket *sock, struct SockInfo *f, curl_socket_t s, CURL*e, int act){
    int when = (act&CURL_POLL_IN?FL_READ:0)|(act&CURL_POLL_OUT?FL_WRITE:0);

    f->sockfd = s;
    f->action = act;
    f->easy = e;
    Fl::remove_fd(f->fd);
    Fl::add_fd(f->fd, when, event_cb, (void *)sock);
}

void simplesocket_addsock(cheapsocket *sock, curl_socket_t s, CURL *easy, int action){
    struct SockInfo *fdp = (struct SockInfo *)malloc(sizeof(struct SockInfo));
    memset(fdp, sizeof(struct SockInfo), '\0');

    fdp->global = sock;
    fdp->fd = s;
    setsock(sock, fdp, s, easy, action);
    curl_multi_assign(sock->multi, s, fdp);
}

int cheapsocket_socket_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp){
    cheapsocket *sock = (cheapsocket *)cbp;
    struct SockInfo *fdp = (struct SockInfo*) sockp;

    if (what == CURL_POLL_REMOVE) {
        cheapsocket_rmsock(sock, fdp);
    } else if (!fdp) {
        cheapsocket_addsock(sock, s, e, what);
    } else {
        cheapsocket_setsock(sock, fdp, s, e, what);
    }
    return 0;
}

void cheapsocket_new_conn(cheapsocket *sock, const char *url, bool is_poll){
    CURLMcode rc;

    struct ConnInfo *conn = (struct ConnInfo *)malloc(sizeof(struct ConnInfo));
    memset(conn, sizeof(struct ConnInfo), '\0');

    conn->is_poll = is_poll;

    conn->easy = curl_easy_init();
    if (!conn->easy) {
        printf("curl_easy_init() failed, exiting!\n");
        exit(2);
    }
    conn->global = sock;
    conn->url = strdup(url);
    curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
    if (conn->is_poll){
        curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, cheapsocket_parse_packet_cb);
    } else {
        curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, cheapsocket_ignore_data);
    }
    curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
    curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
    curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 30L);

    rc = curl_multi_add_handle(sock->multi, conn->easy);
    if (rc != CURLM_OK){
        //TODO handle this
    }
}

void cheapsocket_init(cheapsocket *sock){
    sock->offset = 0;
    sock->reconnect_list = NULL;
    sock->netbuf[0] = '\0';

    curl_global_init(CURL_GLOBAL_ALL);
    sock->multi = curl_multi_init();

    curl_multi_setopt(sock->multi, CURLMOPT_SOCKETFUNCTION, cheapsocket_socket_cb);
    curl_multi_setopt(sock->multi, CURLMOPT_TIMERFUNCTION, cheapsocket_timeout_cb);
    curl_multi_setopt(sock->multi, CURLMOPT_SOCKETDATA, sock);
    curl_multi_setopt(sock->multi, CURLMOPT_TIMERDATA, sock);
}

