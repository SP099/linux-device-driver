#ifndef PLATFORM_H
#define PLATFORM_H

struct pcdev_platform_data
{
    int size;
    int perm;
    const char *serial_number;
};

#define RDWR 0x11
#define RDONLY 0x01
#define WRONLY 0x10

#endif /* PLATFORM_H */
