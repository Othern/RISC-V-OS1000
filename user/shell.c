#include "user.h"

static int string_length(const char *s) {
    int len = 0;
    while (s[len])
        len++;
    return len;
}

static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix)
            return false;
        s++;
        prefix++;
    }
    return true;
}

static bool parse_ipv4(const char *text, uint32_t *ip) {
    uint32_t value = 0;

    for (int part = 0; part < 4; part++) {
        int number = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            number = number * 10 + (*text - '0');
            if (number > 255)
                return false;
            text++;
            digits++;
        }
        if (digits == 0)
            return false;
        value = (value << 8) | number;
        if (part < 3) {
            if (*text != '.')
                return false;
            text++;
        }
    }

    if (*text != '\0')
        return false;
    *ip = value;
    return true;
}

void main(void) {
    while (1) {
prompt:
        printf("> ");
        char cmdline[128];
        for (int i = 0;; i++) {
            char ch = getchar();
            putchar(ch);
            if (i == sizeof(cmdline) - 1) {
                printf("command line too long\n");
                goto prompt;
            } else if (ch == '\r') {
                printf("\n");
                cmdline[i] = '\0';
                break;
            } else {
                cmdline[i] = ch;
            }
        }

        if (strcmp(cmdline, "hello") == 0)
            printf("Hello world from shell!\n");
        else if(strcmp(cmdline, "exit") == 0) {
            printf("Exit ... \n");
            exit();
        }
        else if (strcmp(cmdline, "readfile") == 0) {
            char buf[128];
            int len = readfile("hello.txt", buf, sizeof(buf));
            buf[len] = '\0';
            printf("%s\n", buf);
        }
        else if (strcmp(cmdline, "writefile") == 0)
            writefile("hello.txt", "Hello from shell!\n", 19);
        else if (strcmp(cmdline, "send") == 0)
            printf("usage: send <message>\n");
        else if (starts_with(cmdline, "send ")) {
            const char *message = cmdline + 5;
            int message_len = string_length(message);
            int sent = send(message, message_len);
            if (sent < 0)
                printf("send: failed\n");
            else
                printf("send: %d bytes\n", sent);
        }
        else if (strcmp(cmdline, "arp") == 0)
            arp_dump();
        else if (starts_with(cmdline, "arp ")) {
            uint32_t ip;
            if (!parse_ipv4(cmdline + 4, &ip)) {
                printf("usage: arp <IPv4 address>\n");
            } else if (arp_request(ip) < 0) {
                printf("arp: request failed\n");
            } else {
                printf("arp: request sent\n");
            }
        }
        else
            printf("unknown command: %s\n", cmdline);
    }
}
