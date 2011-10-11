struct ConnInfo {
    CURL *easy;
    char *url;
    net_t *global;
    bool is_poll;
    struct ConnInfo *next;
};

struct SockInfo {
    curl_socket_t sockfd;
    CURL *easy;
    int action;
    long timeout;
    int fd;
    net_t *global;
};

struct cheapsocket_t {
    const char *get_url;
    const char *put_url;
    char netbuf[128];
    CURLM *multi;
    int still_running;
    struct ConnInfo *reconnect_list;
    long unsigned offset;
};
