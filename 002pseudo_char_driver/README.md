# Pseudo Character Driver (Single Device)

## Objective
This project is created for learning Linux Character Device Drivers.  
It demonstrates the basics of writing a **simple pseudo (virtual) character driver** with one device.

**Key Concepts Covered:**
- Character Device Registration
- Dynamic Major Number Allocation
- `file_operations` structure
- `open()`, `read()`, `write()`, `llseek()`, `release()`
- `cdev` structure
- `class_create()` and `device_create()`
- `copy_to_user()` and `copy_from_user()`
- Proper module init/exit with error cleanup
- VFS (Virtual File System) interaction

---

# Device Architecture
This driver creates **one virtual character device** that behaves like a small RAM-based file.

```
/dev/pcd
```

**Memory Layout:**
```
+-------------------+
| device_buff[512]  |   ← 512 bytes of kernel RAM
+-------------------+
```

All read/write operations happen on this internal buffer.

---

# Permissions
- The device is created with default permissions (`crw-------` owned by root).
- No custom permission checking in this basic version (anyone with root can read/write).
- In production drivers, you would add permission checks in `open()`.

---

# VFS Flow
When a user application accesses the device:

```c
fd = open("/dev/pcd", O_RDWR);
```

**Flow:**
```
User Application
        |
        v
      VFS (Virtual File System)
        |
        v
   pcd_open()
```

**Read Flow:**
```
read(fd, buf, size)
   |
   v
pcd_read()
```

**Write Flow:**
```
write(fd, buf, size)
   |
   v
pcd_write()
```

**Seek Flow:**
```
lseek(fd, offset, SEEK_SET)
   |
   v
pcd_llseek()
```

**Close Flow:**
```
close(fd)
   |
   v
pcd_release()
```

---

# Driver Registration Flow

**Module Init (`pcd_driver_init`):**
```
alloc_chrdev_region()
        |
        v
cdev_init() + cdev_add()
        |
        v
class_create()
        |
        v
device_create()
```

**Module Exit (`pcd_driver_exit`):**
```
device_destroy()
        |
        v
class_destroy()
        |
        v
cdev_del()
        |
        v
unregister_chrdev_region()
```

---

# Major and Minor Numbers
Linux identifies devices using `dev_t` (32-bit value):

```
Major Number (12 bits) : Minor Number (20 bits)
```

**Example Output:**
```
Device number <major>:<minor> = 239:0
```

- **Major Number**: Identifies the driver.
- **Minor Number**: Identifies the specific device (here only 0).

---

# Understanding `cdev`
Linux internally represents every character device using:

```c
struct cdev
```

**During registration:**
```c
cdev_init(&pcd_cdev, &pcd_fops);   // Link with file operations
cdev_add(&pcd_cdev, device_number, 1);
```

Whenever the kernel receives a system call (`open/read/write`), it uses the `cdev` to call the correct functions from `pcd_fops`.

---

# Understanding `filp->private_data`
In this simple driver we use global variables, but in advanced drivers:

- `open()` stores per-device or per-open context in `filp->private_data`.
- `read/write/llseek` retrieve it quickly without global variables.

This pattern becomes essential when supporting **multiple devices** (see multi-device version).

---

# User Space vs Kernel Space
Kernel memory and User memory are **separated** for security and stability.

**Unsafe (Never do this):**
```c
memcpy(user_ptr, kernel_ptr, size);
```

**Safe (Used in this driver):**
```c
copy_to_user()     // Kernel → User
copy_from_user()   // User → Kernel
```

**Why?**
- User pointers may be invalid or point to unmapped pages.
- Prevents kernel from crashing due to bad user input.
- Kernel validates address and handles page faults gracefully.

---

# Build

Create a `Makefile`:

```makefile
obj-m += pcdrv_single_device.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

**Compile:**
```bash
make
```

**Expected Output:**
```
pcdrv_single_device.ko
```

---

# Insert Module
```bash
sudo insmod pcdrv_single_device.ko
```

**Verify:**
```bash
dmesg | tail -20
```

---

# Check Device Node
```bash
ls -l /dev/pcd
```

Example output:
```bash
crw------- 1 root root 239,0 Jun 11 17:30 /dev/pcd
```

---

# Testing

**Write Data:**
```bash
echo "Hello from User Space!" > /dev/pcd
```

**Read Data:**
```bash
cat /dev/pcd
```

**Test with Seek:**
```bash
dd if=/dev/pcd bs=10 count=5 skip=2
```

**Check Logs:**
```bash
dmesg | tail
```

---

# Remove Module
```bash
sudo rmmod pcdrv_single_device
```

**Verify:**
```bash
dmesg | tail
ls -l /dev/pcd     # Should be gone
```

---
