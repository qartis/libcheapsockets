#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <signal.h>

void gotline(long offset, const char *buf){
    int maxlen = strlen(buf) - 1;
    printf("%lu %.*s\n", offset, maxlen, buf);
}

int main(int argc, char **argv){
    printf("Content-Type: text/plain; charset=utf-8\r\n\r\n");

    char buf[128];
    struct inotify_event evt;
    long offset;
    char *query_string = getenv("QUERY_STRING");
    if (query_string == NULL || (sscanf(query_string, "offset=%ld", &offset) != 1)){
        printf("Warning: no 'offset' parameter, assuming 0\n");
        offset = 0;
    }

    FILE *file = fopen("db.txt", "r+");
    if (!file){
        perror("fopen");
        exit(1);
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size < offset || (size == 0 && offset == -1)){
	printf("0\n");
	return 0;
    }
    if (offset < 0) offset = 0;
    int inotify = inotify_init();
    inotify_add_watch(inotify, "db.txt", IN_MODIFY);
    setbuf(file, NULL);
    setbuf(stdout, NULL);
    fseek(file, offset, SEEK_SET);
    int read_bytes = 0;
    alarm(30);
    while (fgets(buf, sizeof(buf), file) != NULL){
        read_bytes += strlen(buf);
        gotline(offset + read_bytes, buf);
    }

    offset = ftell(file);
    for(;;){
        int rc = read(inotify, &evt, sizeof(evt));
        if (rc < 0){
            perror("read");
            exit(0);
        }

        fseek(file, offset, SEEK_SET);
        char *retval = fgets(buf, sizeof(buf), file);
        if (retval == NULL){
            printf("0\n");
            fseek(file, 0, SEEK_SET);
            offset = 0;
        } else {
            offset += strlen(buf);
            gotline(offset, buf);
        }
    }
}

