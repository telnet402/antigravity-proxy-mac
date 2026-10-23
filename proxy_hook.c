/**
 * antigravity-proxy-mac
 * 专为 Antigravity 编辑器打造的 macOS ARM 代理注入库
 *
 * 原理：通过 DYLD_INSERT_LIBRARIES 注入，Hook BSD socket connect()
 * 将目标进程的 TCP 连接重定向到 SOCKS5 代理
 *
 * 参考 Windows 版本：https://github.com/yuaotian/antigravity-proxy
 * macOS 适配版本：使用 dyld interpose 替代 MinHook + DLL 注入
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <pwd.h>

/* ===== 版本信息 ===== */
#define VERSION "1.0.0"
#define PRODUCT "Antigravity-Proxy-Mac"

/* ===== 配置结构 ===== */
typedef struct {
    char    proxy_type[16];   /* "socks5" 或 "http" */
    char    proxy_host[256];  /* 代理服务器地址 */
    int     proxy_port;       /* 代理服务器端口 */
    int     enabled;          /* 是否启用 */
    int     log_level;        /* 0=silent 1=normal 2=verbose */
    char    log_dir[512];     /* 日志目录 */
} ProxyConfig;

/* ===== SOCKS5 协议常量 ===== */
#define SOCKS5_VER          0x05
#define SOCKS5_AUTH_NONE    0x00
#define SOCKS5_CMD_CONNECT  0x01
#define SOCKS5_ATYP_IPV4    0x01
#define SOCKS5_ATYP_DOMAIN  0x03
#define SOCKS5_ATYP_IPV6    0x04

/* ===== 全局状态 ===== */
static ProxyConfig  g_config;
static int          g_initialized = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE        *g_log_file = NULL;

/* ===== 原始函数指针 ===== */
typedef int (*connect_fn_t)(int, const struct sockaddr *, socklen_t);
typedef struct hostent *(*gethostbyname_fn_t)(const char *);
typedef int (*getaddrinfo_fn_t)(const char *, const char *,
                                const struct addrinfo *, struct addrinfo **);

/* ===== 日志函数 ===== */
static void proxy_log(int level, const char *fmt, ...) {
    if (!g_config.enabled || level > g_config.log_level) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    pthread_mutex_lock(&g_mutex);

    if (g_log_file) {
        va_list args;
        va_start(args, fmt);
        fprintf(g_log_file, "[%s] [%s] ", timestamp, PRODUCT);
        vfprintf(g_log_file, fmt, args);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
        va_end(args);
    }

    /* 同时输出到 stderr（调试用） */
    if (g_config.log_level >= 2) {
        va_list args2;
        va_start(args2, fmt);
        fprintf(stderr, "[%s] [%s] ", timestamp, PRODUCT);
        vfprintf(stderr, fmt, args2);
        fprintf(stderr, "\n");
        va_end(args2);
    }

    pthread_mutex_unlock(&g_mutex);
}

/* ===== 初始化日志文件 ===== */
static void init_log_file(void) {
    char log_path[768];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char date_str[16];
    strftime(date_str, sizeof(date_str), "%Y%m%d", tm_info);

    if (strlen(g_config.log_dir) > 0) {
        /* 创建日志目录 */
        mkdir(g_config.log_dir, 0755);
        snprintf(log_path, sizeof(log_path),
                 "%s/proxy-%s.log", g_config.log_dir, date_str);
    } else {
        /* 默认日志目录：~/Library/Logs/antigravity-proxy/ */
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        char default_log_dir[512];
        snprintf(default_log_dir, sizeof(default_log_dir),
                 "%s/Library/Logs/antigravity-proxy", home);
        mkdir(default_log_dir, 0755);
        snprintf(log_path, sizeof(log_path),
                 "%s/proxy-%s.log", default_log_dir, date_str);
    }

    g_log_file = fopen(log_path, "a");
}

/* ===== 读取配置文件 ===== */
static void load_config(void) {
    /* 默认配置 */
    strncpy(g_config.proxy_type, "socks5", sizeof(g_config.proxy_type) - 1);
    strncpy(g_config.proxy_host, "127.0.0.1", sizeof(g_config.proxy_host) - 1);
    g_config.proxy_port = 7890;
    g_config.enabled    = 1;
    g_config.log_level  = 1;
    g_config.log_dir[0] = '\0';

    /* 从环境变量读取（优先级最高） */
    const char *env_host = getenv("AGY_PROXY_HOST");
    const char *env_port = getenv("AGY_PROXY_PORT");
    const char *env_type = getenv("AGY_PROXY_TYPE");

    if (env_host) strncpy(g_config.proxy_host, env_host,
                          sizeof(g_config.proxy_host) - 1);
    if (env_port) g_config.proxy_port = atoi(env_port);
    if (env_type) strncpy(g_config.proxy_type, env_type,
                          sizeof(g_config.proxy_type) - 1);

    /* 尝试读取 config.json（简单 key=value 解析） */
    const char *config_paths[] = {
        "./config.json",
        "~/Library/Application Support/antigravity-proxy/config.json",
        NULL
    };

    for (int i = 0; config_paths[i]; i++) {
        char expanded[512];
        if (config_paths[i][0] == '~') {
            const char *home = getenv("HOME");
            if (home)
                snprintf(expanded, sizeof(expanded), "%s%s", home,
                         config_paths[i] + 1);
            else
                continue;
        } else {
            strncpy(expanded, config_paths[i], sizeof(expanded) - 1);
        }

        FILE *fp = fopen(expanded, "r");
        if (!fp) continue;

        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            /* 简单解析 "host": "127.0.0.1" 格式 */
            char *host_key = strstr(line, "\"host\"");
            char *port_key = strstr(line, "\"port\"");
            char *type_key = strstr(line, "\"type\"");
            char *log_key  = strstr(line, "\"log_dir\"");

            if (host_key) {
                char *start = strchr(host_key + 6, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        int len = (int)(end - start);
                        if (len < (int)sizeof(g_config.proxy_host)) {
                            strncpy(g_config.proxy_host, start, len);
                            g_config.proxy_host[len] = '\0';
                        }
                    }
                }
            }
            if (port_key) {
                char *colon = strchr(port_key, ':');
                if (colon) {
                    g_config.proxy_port = atoi(colon + 1);
                }
            }
            if (type_key) {
                char *start = strchr(type_key + 6, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        int len = (int)(end - start);
                        if (len < (int)sizeof(g_config.proxy_type)) {
                            strncpy(g_config.proxy_type, start, len);
                            g_config.proxy_type[len] = '\0';
                        }
                    }
                }
            }
            if (log_key) {
                char *start = strchr(log_key + 9, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        int len = (int)(end - start);
                        if (len < (int)sizeof(g_config.log_dir)) {
                            strncpy(g_config.log_dir, start, len);
                            g_config.log_dir[len] = '\0';
                        }
                    }
                }
            }
        }
        fclose(fp);
        break;
    }
}

/* ===== 判断是否为本地地址（直连） ===== */
static int is_local_addr(const struct sockaddr *addr) {
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        /* 127.x.x.x / 10.x.x.x / 192.168.x.x / 172.16-31.x.x */
        if ((ip >> 24) == 127) return 1;
        if ((ip >> 24) == 10)  return 1;
        if ((ip >> 16) == 0xC0A8) return 1; /* 192.168.x.x */
        if ((ip >> 20) == 0xAC1)  return 1; /* 172.16-31.x.x */
        if (ip == 0) return 1;               /* 0.0.0.0 */
    }
    return 0;
}

/* ===== SOCKS5 握手 ===== */
static int socks5_handshake(int proxy_fd, const struct sockaddr *dst_addr) {
    unsigned char buf[512];
    int ret;

    /* Step 1: 发送认证协商 */
    buf[0] = SOCKS5_VER;
    buf[1] = 1;              /* 1 种认证方法 */
    buf[2] = SOCKS5_AUTH_NONE;
    if (send(proxy_fd, buf, 3, 0) != 3) {
        proxy_log(1, "SOCKS5: 发送认证协商失败");
        return -1;
    }

    /* Step 2: 接收服务器认证方法选择 */
    if (recv(proxy_fd, buf, 2, MSG_WAITALL) != 2) {
        proxy_log(1, "SOCKS5: 接收认证响应失败");
        return -1;
    }
    if (buf[0] != SOCKS5_VER || buf[1] != SOCKS5_AUTH_NONE) {
        proxy_log(1, "SOCKS5: 认证方法不支持 (ver=%d method=%d)",
                  buf[0], buf[1]);
        return -1;
    }

    /* Step 3: 发送连接请求 */
    int req_len = 0;
    buf[req_len++] = SOCKS5_VER;
    buf[req_len++] = SOCKS5_CMD_CONNECT;
    buf[req_len++] = 0x00;  /* reserved */

    if (dst_addr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)dst_addr;
        buf[req_len++] = SOCKS5_ATYP_IPV4;
        memcpy(buf + req_len, &sin->sin_addr, 4);
        req_len += 4;
        memcpy(buf + req_len, &sin->sin_port, 2);
        req_len += 2;
    } else if (dst_addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)dst_addr;
        buf[req_len++] = SOCKS5_ATYP_IPV6;
        memcpy(buf + req_len, &sin6->sin6_addr, 16);
        req_len += 16;
        memcpy(buf + req_len, &sin6->sin6_port, 2);
        req_len += 2;
    } else {
        proxy_log(1, "SOCKS5: 不支持的地址族 %d", dst_addr->sa_family);
        return -1;
    }

    if (send(proxy_fd, buf, req_len, 0) != req_len) {
        proxy_log(1, "SOCKS5: 发送连接请求失败");
        return -1;
    }

    /* Step 4: 接收连接响应 */
    ret = (int)recv(proxy_fd, buf, 4, MSG_WAITALL);
    if (ret < 4) {
        proxy_log(1, "SOCKS5: 接收连接响应失败 (ret=%d)", ret);
        return -1;
    }
    if (buf[1] != 0x00) {
        proxy_log(1, "SOCKS5: 连接被代理拒绝 (code=%d)", buf[1]);
        return -1;
    }

    /* 读取剩余的 BND.ADDR / BND.PORT */
    int skip = 0;
    switch (buf[3]) {
        case SOCKS5_ATYP_IPV4:   skip = 4 + 2; break;
        case SOCKS5_ATYP_IPV6:   skip = 16 + 2; break;
        case SOCKS5_ATYP_DOMAIN: {
            unsigned char domain_len = 0;
            recv(proxy_fd, &domain_len, 1, MSG_WAITALL);
            skip = domain_len + 2;
            break;
        }
    }
    if (skip > 0) {
        unsigned char discard[256];
        recv(proxy_fd, discard, skip, MSG_WAITALL);
    }

    return 0;
}

/* ===== 通过代理建立连接（核心逻辑） ===== */
static int connect_via_proxy(int sockfd, const struct sockaddr *addr,
                              socklen_t addrlen __attribute__((unused))) {
    char dst_ip[INET6_ADDRSTRLEN] = "unknown";
    int dst_port = 0;

    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        inet_ntop(AF_INET, &sin->sin_addr, dst_ip, sizeof(dst_ip));
        dst_port = ntohs(sin->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
        inet_ntop(AF_INET6, &sin6->sin6_addr, dst_ip, sizeof(dst_ip));
        dst_port = ntohs(sin6->sin6_port);
    }

    proxy_log(2, "拦截连接: %s:%d", dst_ip, dst_port);

    /* 建立到代理服务器的连接 */
    struct sockaddr_in proxy_addr;
    memset(&proxy_addr, 0, sizeof(proxy_addr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_port   = htons(g_config.proxy_port);

    if (inet_pton(AF_INET, g_config.proxy_host, &proxy_addr.sin_addr) <= 0) {
        /* 尝试 DNS 解析代理地址 */
        struct hostent *he = gethostbyname(g_config.proxy_host);
        if (!he) {
            proxy_log(1, "无法解析代理地址: %s", g_config.proxy_host);
            return -1;
        }
        memcpy(&proxy_addr.sin_addr, he->h_addr, he->h_length);
    }

    /* 保存原始 socket 标志 */
    int orig_flags = fcntl(sockfd, F_GETFL, 0);
    int is_nonblock = (orig_flags & O_NONBLOCK) != 0;

    /* 临时切换为阻塞模式进行握手 */
    if (is_nonblock) {
        fcntl(sockfd, F_SETFL, orig_flags & ~O_NONBLOCK);
    }

    /* 调用原始 connect 连接到代理 */
    connect_fn_t orig_connect = (connect_fn_t)dlsym(RTLD_NEXT, "connect");
    int ret = orig_connect(sockfd, (struct sockaddr *)&proxy_addr,
                           sizeof(proxy_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        proxy_log(1, "连接代理服务器失败: %s:%d - %s",
                  g_config.proxy_host, g_config.proxy_port, strerror(errno));
        if (is_nonblock) fcntl(sockfd, F_SETFL, orig_flags);
        return -1;
    }

    /* 等待连接完成（用于非阻塞 socket 临时切阻塞） */
    if (ret < 0) {
        fd_set wfds;
        struct timeval tv = {10, 0};
        FD_ZERO(&wfds);
        FD_SET(sockfd, &wfds);
        if (select(sockfd + 1, NULL, &wfds, NULL, &tv) <= 0) {
            proxy_log(1, "连接代理超时");
            if (is_nonblock) fcntl(sockfd, F_SETFL, orig_flags);
            return -1;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            proxy_log(1, "代理连接错误: %s", strerror(err));
            if (is_nonblock) fcntl(sockfd, F_SETFL, orig_flags);
            errno = err;
            return -1;
        }
    }

    /* 执行 SOCKS5 握手 */
    if (strncmp(g_config.proxy_type, "socks5", 6) == 0) {
        ret = socks5_handshake(sockfd, addr);
    } else {
        /* HTTP CONNECT 代理 */
        char req[512];
        int req_len = snprintf(req, sizeof(req),
            "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n\r\n",
            dst_ip, dst_port, dst_ip, dst_port);
        send(sockfd, req, req_len, 0);

        char resp[256];
        recv(sockfd, resp, sizeof(resp) - 1, 0);
        resp[sizeof(resp) - 1] = '\0';
        ret = (strstr(resp, "200") != NULL) ? 0 : -1;
    }

    /* 恢复原始标志 */
    if (is_nonblock) fcntl(sockfd, F_SETFL, orig_flags);

    if (ret == 0) {
        proxy_log(1, "SOCKS5: 隧道建立成功, 目标=%s:%d", dst_ip, dst_port);
    } else {
        proxy_log(1, "SOCKS5: 握手失败, 目标=%s:%d", dst_ip, dst_port);
        errno = ECONNREFUSED;
    }

    return ret;
}

/* ===== 初始化（dylib 构造函数） ===== */
__attribute__((constructor))
static void proxy_init(void) {
    pthread_mutex_lock(&g_mutex);
    if (g_initialized) {
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    load_config();
    init_log_file();
    g_initialized = 1;

    proxy_log(1, "=================================================");
    proxy_log(1, "%s v%s 已加载", PRODUCT, VERSION);
    proxy_log(1, "代理类型: %s", g_config.proxy_type);
    proxy_log(1, "代理地址: %s:%d", g_config.proxy_host, g_config.proxy_port);
    proxy_log(1, "状态: %s", g_config.enabled ? "已启用" : "已禁用");
    proxy_log(1, "=================================================");

    pthread_mutex_unlock(&g_mutex);
}

/* ===== 清理（dylib 析构函数） ===== */
__attribute__((destructor))
static void proxy_fini(void) {
    proxy_log(1, "%s 已卸载", PRODUCT);
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

/* ===== Hook: connect() ===== */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    connect_fn_t orig_connect = (connect_fn_t)dlsym(RTLD_NEXT, "connect");

    if (!g_initialized || !g_config.enabled) {
        return orig_connect(sockfd, addr, addrlen);
    }

    /* 只代理 TCP 连接 */
    int sock_type = 0;
    socklen_t opt_len = sizeof(sock_type);
    getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &sock_type, &opt_len);
    if (sock_type != SOCK_STREAM) {
        proxy_log(2, "非 SOCK_STREAM socket 直连 (type=%d)", sock_type);
        return orig_connect(sockfd, addr, addrlen);
    }

    /* 本地地址直连 */
    if (is_local_addr(addr)) {
        proxy_log(2, "本地地址直连");
        return orig_connect(sockfd, addr, addrlen);
    }

    /* 连接到代理 */
    return connect_via_proxy(sockfd, addr, addrlen);
}
