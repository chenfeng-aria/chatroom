#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <errno.h>
#include "common.h"

typedef struct {
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
    int online;
    int sockfd;
} User;

typedef struct {
    char groupname[GROUPNAME_LEN];
    char members[MAX_CLIENTS][USERNAME_LEN];
    int count;
} Group;

User users[MAX_CLIENTS];
int user_count = 0;
Group groups[MAX_CLIENTS];
int group_count = 0;
pthread_mutex_t user_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t group_mutex = PTHREAD_MUTEX_INITIALIZER;


void print_menu() {
    printf("1. 私信聊天\n");
    printf("2. 发送群聊消息\n");
    printf("3. 广播消息\n");
}

// 1. UDP注册服务（线程）
void *udp_register_service(void *arg) {
    int udp_port = *(int*)arg;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr = {0}, cliaddr = {0};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(udp_port);
    bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    while (1) {
        RegisterReq req; RegisterResp resp;
        socklen_t len = sizeof(cliaddr);
        recvfrom(sockfd, &req, sizeof(req), 0, (struct sockaddr*)&cliaddr, &len);
        pthread_mutex_lock(&user_mutex);
        int exist = 0;
        for (int i = 0; i < user_count; ++i)
            if (strcmp(users[i].username, req.username) == 0) exist = 1;
        if (exist) {
            resp.success = 0; strcpy(resp.msg, "用户名已存在");
        } else {
            strcpy(users[user_count].username, req.username);
            strcpy(users[user_count].password, req.password);
            users[user_count].online = 0; users[user_count].sockfd = -1;
            user_count++;
            resp.success = 1; strcpy(resp.msg, "注册成功");
        }
        pthread_mutex_unlock(&user_mutex);
        sendto(sockfd, &resp, sizeof(resp), 0, (struct sockaddr*)&cliaddr, len);
    }
    return NULL;
}
// 服务器处理stdin线程
void *input_thread(void *arg) {
    while(1) {
        print_menu();
        int cmd; scanf("%d", &cmd); getchar();
        // ChatMsg msg = {0};
        // strcpy(msg.from, "服务器");
        ChatMsg msg = {0};
        switch (cmd)
        {
            case 1:
                /* 私聊 */
                printf("目标用户名: "); scanf("%31s", msg.to); getchar();
                printf("消息内容: "); fgets(msg.data, MSG_LEN, stdin);
                strcpy(msg.from, "服务器");
                int flag = 0;
                pthread_mutex_lock(&user_mutex);
                for (int i = 0; i < user_count; ++i) {
                    if (users[i].online && strcmp(users[i].username, msg.to)==0) {
                        write(users[i].sockfd, &msg, sizeof(msg));
                        flag = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&user_mutex);
                if(flag) {
                    printf("消息已发送！\n");
                }
                else {
                    printf("消息未发送，%s不存在或不下线\n", msg.to);
                }
                break;
            case 2:
                // 
                break;
            case 3:
                // 
                break;
            default:
                printf("无效命令\n");
                continue;
        }
    }
}

// 在线用户广播，排除fd为except_fd的连接套接字
void broadcast(const char *msg, int except_fd) {
    pthread_mutex_lock(&user_mutex);
    for (int i = 0; i < user_count; ++i)
        if (users[i].online && users[i].sockfd != except_fd)
            write(users[i].sockfd, msg, sizeof(ChatMsg));
    pthread_mutex_unlock(&user_mutex);
}


// 2. TCP主服务（epoll主线程）
void tcp_service(int tcp_port) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servaddr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(tcp_port)
    };
    bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    listen(listenfd, MAX_CLIENTS);

    int epfd = epoll_create(1);
    struct epoll_event ev, events[MAX_CLIENTS];
    ev.events = EPOLLIN;

    ev.data.fd = listenfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    while (1) {
        int nfds = epoll_wait(epfd, events, MAX_CLIENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listenfd) {
                // 新连接
                int connfd = accept(listenfd, NULL, NULL);
                ev.data.fd = connfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
            }
            else {
                // 客户端消息
                int fd = events[i].data.fd;
                if (fd < 0) continue;
                char buf[sizeof(ChatMsg)];
                int n = read(fd, buf, sizeof(buf));
                if (n <= 0) {
                    close(fd);
                    pthread_mutex_lock(&user_mutex);
                    for (int i = 0; i < user_count; ++i)
                        if (users[i].sockfd == fd) users[i].online = 0, users[i].sockfd = -1;
                    pthread_mutex_unlock(&user_mutex);
                    continue;
                }
                // 先处理登录请求
                if (n == sizeof(LoginReq)) {
                    printf("接收到一个登录请求\n");
                    LoginReq *req = (LoginReq*)buf;
                    LoginResp resp = {.success=0};
                    pthread_mutex_lock(&user_mutex);
                    int found = 0;
                    for (int i = 0; i < user_count; ++i) {
                        if (strcmp(users[i].username, req->username)==0 &&
                            strcmp(users[i].password, req->password)==0) {
                            if (users[i].online) {
                                strcpy(resp.msg, "已登录");
                            } else {
                                printf("首次登录成功！\n");
                                users[i].online = 1; users[i].sockfd = fd;
                                resp.success = 1; strcpy(resp.msg, "登录成功");
                                found = 1;
                                // 通知所有人，除了该用户
                                ChatMsg msg = {.type=CMD_LOGIN};
                                strcpy(msg.from, req->username);
                                strcpy(msg.data, "上线了");
                                pthread_mutex_unlock(&user_mutex);
                                broadcast((char*)&msg, fd);
                            }
                        }
                    }
                    pthread_mutex_unlock(&user_mutex);
                    if (!found) strcpy(resp.msg, "用户名或密码错误");
                    write(fd, &resp, sizeof(resp));
                    continue;
                }
                // 其它命令
                ChatMsg *msg = (ChatMsg*)buf;
                if (msg->type == CMD_LOGOUT) {
                    pthread_mutex_lock(&user_mutex);
                    for (int i = 0; i < user_count; ++i)
                        if (strcmp(users[i].username, msg->from)==0)
                            users[i].online=0, users[i].sockfd=-1;
                    pthread_mutex_unlock(&user_mutex);
                    ChatMsg outmsg = {.type=CMD_LOGOUT};
                    strcpy(outmsg.from, msg->from);
                    strcpy(outmsg.data, "已下线");
                    broadcast((char*)&outmsg, fd);
                    close(fd);
                    continue;
                } else if (msg->type == CMD_LIST_USERS) {
                    ChatMsg resp = {.type=CMD_LIST_USERS};
                    pthread_mutex_lock(&user_mutex);
                    char *p = resp.data;
                    for (int i = 0; i < user_count; ++i)
                        if (users[i].online)
                            p += sprintf(p, "%s ", users[i].username);
                    pthread_mutex_unlock(&user_mutex);
                    write(fd, &resp, sizeof(resp));
                } else if (msg->type == CMD_LIST_GROUPS) {
                    ChatMsg resp = {.type=CMD_LIST_GROUPS};
                    pthread_mutex_lock(&group_mutex);
                    char *p = resp.data;
                    for (int i = 0; i < group_count; ++i)
                        p += sprintf(p, "%s ", groups[i].groupname);
                    pthread_mutex_unlock(&group_mutex);
                    write(fd, &resp, sizeof(resp));
                } else if (msg->type == CMD_PRIVATE_MSG) {
                    pthread_mutex_lock(&user_mutex);
                    for (int i = 0; i < user_count; ++i)
                        if (users[i].online && strcmp(users[i].username, msg->to)==0)
                            write(users[i].sockfd, msg, sizeof(*msg));
                    pthread_mutex_unlock(&user_mutex);
                } else if (msg->type == CMD_GROUP_CREATE) {
                    pthread_mutex_lock(&group_mutex);
                    int exist = 0;
                    for (int i = 0; i < group_count; ++i)
                        if (strcmp(groups[i].groupname, msg->to)==0) exist = 1;
                    if (!exist && group_count < MAX_CLIENTS) {
                        strcpy(groups[group_count].groupname, msg->to);
                        strcpy(groups[group_count].members[0], msg->from);
                        groups[group_count].count = 1;
                        group_count++;
                        strcpy(msg->data, "群组创建成功");
                    } else {
                        strcpy(msg->data, "群组已存在");
                    }
                    pthread_mutex_unlock(&group_mutex);
                    write(fd, msg, sizeof(*msg));
                } else if (msg->type == CMD_GROUP_JOIN) {
                    pthread_mutex_lock(&group_mutex);
                    int found = 0;
                    for (int i = 0; i < group_count; ++i) {
                        if (strcmp(groups[i].groupname, msg->to)==0) {
                            int in = 0;
                            for (int j = 0; j < groups[i].count; ++j)
                                if (strcmp(groups[i].members[j], msg->from)==0)
                                    in = 1;
                            if (!in && groups[i].count < MAX_CLIENTS)
                                strcpy(groups[i].members[groups[i].count++], msg->from);
                            found = 1; strcpy(msg->data, "加入群组成功");
                        }
                    }
                    if (!found) strcpy(msg->data, "群组不存在");
                    pthread_mutex_unlock(&group_mutex);
                    write(fd, msg, sizeof(*msg));
                } else if (msg->type == CMD_GROUP_LEAVE) {
                    pthread_mutex_lock(&group_mutex);
                    for (int i = 0; i < group_count; ++i) {
                        if (strcmp(groups[i].groupname, msg->to) == 0) {
                            for (int j = 0; j < groups[i].count; ++j) {
                                if (strcmp(groups[i].members[j], msg->from) == 0) {
                                    // 删除成员
                                    for (int k = j; k < groups[i].count-1; ++k)
                                        strcpy(groups[i].members[k], groups[i].members[k+1]);
                                    groups[i].count--;
                                    strcpy(msg->data, "已退出群组");
                                    break;
                                }
                            }
                        }
                    }
                    pthread_mutex_unlock(&group_mutex);
                    write(fd, msg, sizeof(*msg));
                } else if (msg->type == CMD_GROUP_MSG) {
                    pthread_mutex_lock(&group_mutex);
                    int idx = -1;
                    for (int i = 0; i < group_count; ++i)
                        if (strcmp(groups[i].groupname, msg->to) == 0)
                            idx = i;
                    if (idx != -1) {
                        pthread_mutex_lock(&user_mutex);
                        for (int j = 0; j < groups[idx].count; ++j) {
                            for (int u = 0; u < user_count; ++u) {
                                if (strcmp(users[u].username, groups[idx].members[j]) == 0 && users[u].online)
                                    write(users[u].sockfd, msg, sizeof(*msg));
                            }
                        }
                        pthread_mutex_unlock(&user_mutex);
                    }
                    pthread_mutex_unlock(&group_mutex);
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <tcp_port> <udp_port>\n", argv[0]);
        return 1;
    }
    int tcp_port = atoi(argv[1]);
    int udp_port = atoi(argv[2]);
    pthread_t tid;
    pthread_create(&tid, NULL, udp_register_service, &udp_port);
    pthread_t tid2;
    pthread_create(&tid2, NULL, input_thread, NULL);
    tcp_service(tcp_port);
    return 0;
}