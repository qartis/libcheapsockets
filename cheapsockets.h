typedef void (*cheapsocket_fd_handler)(int fd, void *data);
typedef void (*cheapsocket_timeout_handler)(void *data);

struct cheapsocket_t {
    const char *get_url;
    const char *put_url;
    char netbuf[128];
    CURLM *multi;
    int still_running;
    struct ConnInfo *reconnect_list;
    long unsigned offset;
    void (*packet_handler)(const char *packet);
    void (*add_fd)(int fd, cheapsocket_fd_handler handler);
    void (*remove_fd)(int fd);
    void (*add_timeout)(double t, cheapsocket_timeout_handler handler, void *data);
    void (*remove_timeout)(cheapsocket_timeout_handler handler);
};

typedef struct cheapsocket_t cheapsocket;

struct ConnInfo {
    CURL *easy;
    char *url;
    cheapsocket *global;
    int is_poll;
    struct ConnInfo *next;
};

struct SockInfo {
    curl_socket_t sockfd;
    CURL *easy;
    int action;
    long timeout;
    int fd;
    cheapsocket *global;
};
