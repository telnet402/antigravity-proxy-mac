/* test_inject.c — 测试 DYLD_INSERT_LIBRARIES 注入效果 */
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    printf("=== 注入测试程序 ===\n");
    printf("尝试连接 8.8.8.8:80 (Google DNS HTTP)...\n");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == 0) {
        printf("连接成功！（如果注入生效，这条连接应该通过代理）\n");
    } else {
        perror("connect 失败");
    }

    close(fd);
    return 0;
}
