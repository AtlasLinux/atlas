#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "10.0.2.2", &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
        printf("Connected to host!\n");
    else
        perror("connect");

    close(fd);
    return 0;
}
