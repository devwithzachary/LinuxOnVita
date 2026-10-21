#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: vita-tun <add|del|status> [ifname]\n");
        return 1;
    }
    const char *cmd = argv[1];
    const char *name = (argc > 2) ? argv[2] : "tun0";
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/net/tun");
        return 1;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        perror("ioctl TUNSETIFF");
        close(fd);
        return 1;
    }
    if (strcmp(cmd, "add") == 0) {
        if (ioctl(fd, TUNSETPERSIST, 1) < 0) {
            perror("ioctl TUNSETPERSIST add");
            close(fd);
            return 1;
        }
        printf("Created persistent TUN interface: %s\n", ifr.ifr_name);
    } else if (strcmp(cmd, "del") == 0) {
        if (ioctl(fd, TUNSETPERSIST, 0) < 0) {
            perror("ioctl TUNSETPERSIST del");
            close(fd);
            return 1;
        }
        printf("Removed persistent TUN interface: %s\n", ifr.ifr_name);
    } else if (strcmp(cmd, "status") == 0) {
        printf("Successfully opened /dev/net/tun and verified TUNSETIFF on %s\n", ifr.ifr_name);
    } else {
        fprintf(stderr, "Unknown command: %s (use add, del, or status)\n", cmd);
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
