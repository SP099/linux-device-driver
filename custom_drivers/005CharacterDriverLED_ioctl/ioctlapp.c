/*
 * User space test application for GPIO ioctl driver
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define LED_ON      _IO('f', 1)
#define LED_OFF     _IO('f', 2)
#define LED_TOGGLE  _IO('f', 3)
#define LED_STATUS  _IOR('f', 4, int)

int main(int argc, char *argv[])
{
    int fd, cmd, status = 0;

    if (argc < 2) {
        printf("Usage: %s <command>\n", argv[0]);
        printf("  1 : LED ON\n");
        printf("  0 : LED OFF\n");
        printf("  2 : LED Toggle\n");
        printf("  3 : Get LED Status\n");
        return -1;
    }

    fd = open("/dev/testchar", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/testchar");
        return -1;
    }

    cmd = atoi(argv[1]);

    switch (cmd) {
    case 1:
        ioctl(fd, LED_ON, 0);
        printf("Command: LED ON sent\n");
        break;
    case 0:
        ioctl(fd, LED_OFF, 0);
        printf("Command: LED OFF sent\n");
        break;
    case 2:
        ioctl(fd, LED_TOGGLE, 0);
        printf("Command: LED Toggle sent\n");
        break;
    case 3:
        ioctl(fd, LED_STATUS, &status);
        printf("LED Status = %d\n", status);
        break;
    default:
        printf("Invalid command!\n");
    }

    close(fd);
    return 0;
}
