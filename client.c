#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include "common.h"

#define MAX_EVENTS 10

void usage(const char *prog) {
    printf("Usage: %s <server_ip> <tcp_port> <udp_port>\n", prog);
    exit(1);
}

int udp_register(const char *server_ip, int udp_port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr = {
        .sin_family = AF_INET,
        .sin_port = htons(udp_port),
        .sin_addr.s_addr = inet_addr(server_ip)
    };
    RegisterReq req;
    RegisterResp resp;

    printf("注册用户名: ");
    scanf("%31s", req.username);
    printf("注册密码: ");
    scanf("%31s", req.password);

    sendto(sockfd, &req, sizeof(req), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
    socklen_t len = sizeof(servaddr);
    recvfrom(sockfd, &resp, sizeof(resp), 0, (struct sockaddr*)&servaddr, &len);

    printf("注册结果：%s\n", resp.msg);
    close(sockfd);
    return resp.success;
}

int tcp_login(const char *server_ip, int tcp_port, char *username) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr = {
        .sin_family = AF_INET,
        .sin_port = htons(tcp_port),
        .sin_addr.s_addr = inet_addr(server_ip)
    };
    connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

    LoginReq req;
    LoginResp resp;

    printf("登录用户名: ");
    scanf("%31s", req.username);
    printf("登录密码: ");
    scanf("%31s", req.password);
    strcpy(username, req.username);

    write(sockfd, &req, sizeof(req));
    printf("已发送\n");
    read(sockfd, &resp, sizeof(resp));
    printf("登录结果：%s\n", resp.msg);
    printf("success value:%d\n", resp.success);
    if (resp.success == 0) {
        printf("error\n");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

void print_menu() {
    printf("1. 查询在线用户\n");
    printf("2. 查询群组\n");
    printf("3. 私信聊天\n");
    printf("4. 创建群组\n");
    printf("5. 加入群组\n");
    printf("6. 发送群聊消息\n");
    printf("7. 离开群组\n");
    printf("8. 退出登录\n");
}

void user_work(int sockfd, const char *username) {
    struct epoll_event ev, events[MAX_EVENTS];
    int epfd = epoll_create(1);
    ev.events = EPOLLIN;
    ev.data.fd = 0;
    epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &ev);
    ev.data.fd = sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    while (1) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == 0) {
                // 用户输入
                print_menu();
                int cmd; scanf("%d", &cmd); getchar();
                ChatMsg msg = {0};
                strcpy(msg.from, username);
                switch (cmd) {
                    case 1: msg.type = CMD_LIST_USERS; break;
                    case 2: msg.type = CMD_LIST_GROUPS; break;
                    case 3:
                        msg.type = CMD_PRIVATE_MSG;
                        printf("目标用户名: "); scanf("%31s", msg.to); getchar();
                        printf("消息内容: "); fgets(msg.data, MSG_LEN, stdin);
                        break;
                    case 4:
                        msg.type = CMD_GROUP_CREATE;
                        printf("新群组名: "); scanf("%31s", msg.to); getchar();
                        break;
                    case 5:
                        msg.type = CMD_GROUP_JOIN;
                        printf("群组名: "); scanf("%31s", msg.to); getchar();
                        break;
                    case 6:
                        msg.type = CMD_GROUP_MSG;
                        printf("群组名: "); scanf("%31s", msg.to); getchar();
                        printf("消息内容: "); fgets(msg.data, MSG_LEN, stdin);
                        break;
                    case 7:
                        msg.type = CMD_GROUP_LEAVE;
                        printf("群组名: "); scanf("%31s", msg.to); getchar();
                        break;
                    case 8:
                        msg.type = CMD_LOGOUT;
                        write(sockfd, &msg, sizeof(msg));
                        close(sockfd); printf("已退出\n"); exit(0);
                    default: printf("无效命令\n"); continue;
                }
                write(sockfd, &msg, sizeof(msg));
            } else if (events[i].data.fd == sockfd) {
                // 服务器消息
                ChatMsg msg;
                int n = read(sockfd, &msg, sizeof(msg));
                if (n <= 0) { printf("服务器关闭\n"); exit(1);}
                printf("[消息]%s: %s\n", msg.from, msg.data);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) usage(argv[0]);
    char username[USERNAME_LEN];
    int op;
    printf("1. 注册  2. 登录\n");
    scanf("%d", &op); getchar();
    if (op == 1) {
        udp_register(argv[1], atoi(argv[3]));
        printf("请重新启动客户端进行登录\n");
        return 0;
    }
    int sockfd = tcp_login(argv[1], atoi(argv[2]), username);
    if (sockfd < 0) return 1;
    printf("登录成功\n");
    user_work(sockfd, username);
    return 0;
}