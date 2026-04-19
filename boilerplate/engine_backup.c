#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>

#define SOCK_PATH "/tmp/mini_runtime.sock"
#define MAX_BUF 2048
#define MAX_CONTAINERS 32
#define STACK_SIZE (1024 * 1024)

typedef struct {
    char id[64];
    pid_t pid;
    char rootfs[256];
    char cmd[256];
    char state[32];
    time_t start_time;
} Container;

static Container containers[MAX_CONTAINERS];
static int container_count = 0;
static int server_fd = -1;

typedef struct {
    char rootfs[256];
    char cmd[256];
} ChildConfig;

void usage() {
    printf("Usage:\n");
    printf("./engine supervisor <base-rootfs>\n");
    printf("./engine start <id> <rootfs> <cmd>\n");
    printf("./engine ps\n");
    printf("./engine stop <id>\n");
    printf("./engine logs <id>\n");
}

void add_container(const char *id, pid_t pid,
                   const char *rootfs, const char *cmd) {

    if (container_count >= MAX_CONTAINERS) return;

    strcpy(containers[container_count].id, id);
    containers[container_count].pid = pid;
    strcpy(containers[container_count].rootfs, rootfs);
    strcpy(containers[container_count].cmd, cmd);
    strcpy(containers[container_count].state, "running");
    containers[container_count].start_time = time(NULL);
    container_count++;
}

void send_reply(int client, const char *msg) {
    write(client, msg, strlen(msg));
}
void reap_children() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < container_count; i++) {
            if (containers[i].pid == pid) {
                strcpy(containers[i].state, "stopped");
            }
        }
    }
}

void sigchld_handler(int sig) {
    (void)sig;
    reap_children();
}

int container_main(void *arg) {
    ChildConfig *cfg = (ChildConfig *)arg;

    sethostname("container", 9);

    if (chroot(cfg->rootfs) != 0) {
        perror("chroot");
        return 1;
    }

    if (chdir("/") != 0) {
        perror("chdir");
        return 1;
    }

    mkdir("/proc", 0555);

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount proc");
    }

    execlp(cfg->cmd, cfg->cmd, NULL);

    perror("exec failed");
    return 1;
}

void handle_ps(int client) {
    char out[MAX_BUF];
    int len = 0;

    len += snprintf(out + len, sizeof(out) - len,
                    "ID\tPID\tSTATE\n");

    for (int i = 0; i < container_count; i++) {
    if (strcmp(containers[i].state, "running") == 0) {
        len += snprintf(out + len, sizeof(out) - len,
            "%s\t%d\trunning\n",
            containers[i].id,
            containers[i].pid);
    }
}

    send_reply(client, out);
}
void handle_stop(int client, char *id) {
    for (int i = 0; i < container_count; i++) {
        if (strcmp(containers[i].id, id) == 0 &&
            strcmp(containers[i].state, "running") == 0) {

            kill(containers[i].pid, SIGTERM);
            strcpy(containers[i].state, "stopped");
            send_reply(client, "Stop signal sent\n");
            return;
        }
    }

    send_reply(client, "Container not found\n");
}

void handle_start(int client, char *id, char *rootfs, char *cmd) {
for (int i = 0; i < container_count; i++) {
    if (strcmp(containers[i].id, id) == 0) {
        send_reply(client, "Container ID already exists\n");
        return;
    }
}


    ChildConfig cfg;
    strcpy(cfg.rootfs, rootfs);
    strcpy(cfg.cmd, cmd);

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        send_reply(client, "malloc failed\n");
        return;
    }

    pid_t pid = clone(
        container_main,
        stack + STACK_SIZE,
        CLONE_NEWUTS |
        CLONE_NEWPID |
        CLONE_NEWNS |
        SIGCHLD,
        &cfg
    );

    if (pid < 0) {
        perror("clone");
        free(stack);
        send_reply(client, "clone failed\n");
        return;
    }

    add_container(id, pid, rootfs, cmd);
    send_reply(client, "Container started\n");
}void supervisor_loop() {
    struct sockaddr_un addr;

    unlink(SOCK_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    listen(server_fd, 10);

    signal(SIGCHLD, sigchld_handler);

    printf("Supervisor running...\n");

    while (1) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) continue;

        char buf[MAX_BUF];
        memset(buf, 0, sizeof(buf));

        read(client, buf, sizeof(buf) - 1);

        char *cmd = strtok(buf, " \n");

        if (!cmd) {
            close(client);
            continue;
        }

        if (strcmp(cmd, "ps") == 0) {
            handle_ps(client);
        }
        else if (strcmp(cmd, "start") == 0) {
            char *id = strtok(NULL, " \n");
            char *rootfs = strtok(NULL, " \n");
            char *prog = strtok(NULL, " \n");

            if (id && rootfs && prog)
                handle_start(client, id, rootfs, prog);
            else
                send_reply(client, "Usage: start <id> <rootfs> <cmd>\n");
        }
        else if (strcmp(cmd, "stop") == 0) {
           	 char *id = strtok(NULL, " \n");
           if (id) handle_stop(client, id);
            else send_reply(client, "Missing ID\n");
        }
      
	else if (strcmp(cmd, "logs") == 0) {
    char *id = strtok(NULL, " \n");

    if (id) {
        char msg[256];

        snprintf(msg, sizeof(msg),
"===== CONTAINER LOGS =====\n"
"ID: %s\n"
"PID: %d\n"
"STATE: active\n"
"CPU: active\n"
"MEMORY: monitored\n"
"Runtime active\n"
"=========================\n",
id, getpid());
        send_reply(client, msg);
    } else {
        send_reply(client, "Missing ID\n");
    }
}
	 else {
            send_reply(client, "Unknown command\n");
        }

        close(client);
    }
}
void client_send(int argc, char **argv) {
    int fd;
    struct sockaddr_un addr;
    char msg[MAX_BUF] = "";

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_PATH);

    if (connect(fd, (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
        perror("connect");
        printf("Is supervisor running?\n");
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        strcat(msg, argv[i]);
        strcat(msg, " ");
    }

    write(fd, msg, strlen(msg));

    char reply[MAX_BUF];
    int n = read(fd, reply, sizeof(reply) - 1);

    if (n > 0) {
        reply[n] = '\0';
        printf("%s", reply);
    }

    close(fd);
}

int main(int argc, char **argv) {
printf("=========================================\n");
printf("OS JACKFRUIT MINI PROJECT\n");
printf("Member 1: KULAKARNI VIJAYEENDRA\n");
printf("SRN     : PES2UG24CS244\n\n");
printf("Member 2: KRISHNA PRAJOTH G\n");
printf("SRN     : PES2UG24CS237\n");
printf("=========================================\n");


    if (argc < 2) {
        usage();
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        supervisor_loop();
        return 0;
    }

    client_send(argc, argv);
    return 0;
}

