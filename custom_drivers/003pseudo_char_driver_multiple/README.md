# Pseudo Character Driver

## Objective

This project is created for learning Linux Character Device Drivers.

It demonstrates:

- Character Device Registration
- Dynamic Major Number Allocation
- Multiple Device Support
- Device Permissions
- VFS Interaction
- file_operations
- open()
- read()
- write()
- llseek()
- release()
- cdev
- class_create()
- device_create()
- container_of()
- filp->private_data
- copy_to_user()
- copy_from_user()

---

# Device Architecture

This driver creates 4 virtual character devices.

```
/dev/pcd-0
/dev/pcd-1
/dev/pcd-2
/dev/pcd-3
```

Each device owns its own memory buffer.

```
+----------------+
| pcd-0 Buffer   |
+----------------+

+----------------+
| pcd-1 Buffer   |
+----------------+

+----------------+
| pcd-2 Buffer   |
+----------------+

+----------------+
| pcd-3 Buffer   |
+----------------+
```

---

# Permissions

| Device | Permission |
|----------|----------|
| pcd-0 | Read Only |
| pcd-1 | Write Only |
| pcd-2 | Read Write |
| pcd-3 | Read Write |

---

# VFS Flow

When user opens a device:

```c
fd = open("/dev/pcd-0", O_RDONLY);
```

Flow:

```
User Application
        |
        v
      VFS
        |
        v
   pcd_open()
```

Read Flow:

```
read()
   |
   v
pcd_read()
```

Write Flow:

```
write()
   |
   v
pcd_write()
```

Seek Flow:

```
lseek()
   |
   v
pcd_llseek()
```

Close Flow:

```
close()
   |
   v
pcd_release()
```

---

# Driver Registration Flow

Module Init:

```
alloc_chrdev_region()
        |
        v
class_create()
        |
        v
cdev_init()
        |
        v
cdev_add()
        |
        v
device_create()
```

Module Exit:

```
device_destroy()
        |
        v
cdev_del()
        |
        v
class_destroy()
        |
        v
unregister_chrdev_region()
```

---

# Major and Minor Numbers

Linux identifies devices using:

```
dev_t
```

which contains:

```
Major Number
Minor Number
```

Example:

```
239:0
239:1
239:2
239:3
```

Major:

```
Driver Identifier
```

Minor:

```
Device Identifier
```

---

# Understanding cdev

Linux internally represents character devices using:

```c
struct cdev
```

During registration:

```c
cdev_init()
cdev_add()
```

Kernel associates:

```c
file_operations
```

with:

```c
struct cdev
```

Whenever user accesses device:

```c
open()
read()
write()
```

kernel reaches our callbacks through cdev.

---

# Understanding container_of()

One of the most important Linux macros.

We embed:

```c
struct cdev cdev;
```

inside:

```c
struct pcdev_private_data
```

Layout:

```
+-----------------------------+
| pcdev_private_data          |
|-----------------------------|
| buffer                      |
| size                        |
| permission                  |
| serial_number               |
| cdev                        |
+-----------------------------+
```

Kernel gives:

```c
inode->i_cdev
```

Using:

```c
container_of()
```

we get:

```c
struct pcdev_private_data *
```

back.

---

# Understanding private_data

Every open() creates:

```c
struct file
```

Driver can store information inside:

```c
filp->private_data
```

Example:

```c
filp->private_data = pcdev_data;
```

Later:

```c
read()
write()
llseek()
```

retrieve device information using:

```c
filp->private_data
```

without recalculating container_of().

---

# User Space vs Kernel Space

Kernel memory and User memory are different.

Unsafe:

```c
memcpy(user_ptr, kernel_ptr, size);
```

Safe:

```c
copy_to_user()
copy_from_user()
```

Used because:

- User pointer may be invalid
- Page may not be mapped
- Access may fault

Kernel validates everything.

---

# Build

```bash
make
```

Expected Output:

```bash
pseudo_char_driver_study.ko
```

---

# Insert Module

```bash
sudo insmod pseudo_char_driver_study.ko
```

Verify:

```bash
dmesg | tail
```

---

# Check Devices

```bash
ls -l /dev/pcd-*
```

Example:

```bash
crw------- 1 root root 239,0 /dev/pcd-0
crw------- 1 root root 239,1 /dev/pcd-1
crw------- 1 root root 239,2 /dev/pcd-2
crw------- 1 root root 239,3 /dev/pcd-3
```

---

# Testing Read

```bash
cat /dev/pcd-2
```

---

# Testing Write

```bash
echo "Hello Driver" > /dev/pcd-2
```

---

# Remove Module

```bash
sudo rmmod pseudo_char_driver_study
```

Verify:

```bash
dmesg | tail
```

---
