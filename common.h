#ifndef COMMON_H
#define COMMON_H

#define MAX_CLIENTS 100
#define USERNAME_LEN 32
#define PASSWORD_LEN 32
#define GROUPNAME_LEN 32
#define MSG_LEN 512

// 注册请求
typedef struct {
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
} RegisterReq;

// 注册响应
typedef struct {
    int success; // 1=成功 0=失败
    char msg[MSG_LEN];
} RegisterResp;

// 登录请求
typedef struct {
    char username[USERNAME_LEN];
    char password[PASSWORD_LEN];
} LoginReq;

// 登录响应
typedef struct {
    int success; // 1=成功 0=失败
    char msg[MSG_LEN];
} LoginResp;

// 命令类型
typedef enum {
    CMD_LOGIN,
    CMD_LOGOUT,
    CMD_LIST_USERS,
    CMD_LIST_GROUPS,
    CMD_PRIVATE_MSG,
    CMD_GROUP_CREATE,
    CMD_GROUP_JOIN,
    CMD_GROUP_LEAVE,
    CMD_GROUP_MSG,
    CMD_BROADCAST,
    CMD_ADMIN_MSG
} CmdType;

// 通用消息协议
typedef struct {
    CmdType type;
    char from[USERNAME_LEN];
    char to[USERNAME_LEN]; // 用户名/组名/空
    char data[MSG_LEN];
} ChatMsg;

#endif