Multi-Container Runtime (OS Jackfruit Project)
1. Team Information
Team Member 1: KULAKARNI VIJAYEENDRA

SRN: PES2UG24CS244

Team Member 2: KRISHNA PRAJOTH G

SRN: PES2UG24CS237

2. Build, Load, and Run Instructions
Build the Project
make
Load Kernel Module
sudo insmod monitor.ko
Verify Device Creation
ls /dev/container_monitor
Start Supervisor
sudo ./engine supervisor ./rootfs-base
Create Root Filesystems
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta
Start Containers
sudo ./engine start alpha ./rootfs-alpha /bin/sh
sudo ./engine start beta ./rootfs-beta /bin/sh
View Running Containers
sudo ./engine ps
Run Workloads
cp cpu_hog ./rootfs-alpha/
sudo ./engine start alpha ./rootfs-alpha /cpu_hog
View Logs
cat logs/alpha.log
Stop Containers
sudo ./engine stop alpha
sudo ./engine stop beta
Unload Kernel Module
sudo rmmod monitor
3. Demo with Screenshots

## Screenshots
### Supervisor Running

![img2](images/2.png)

### CONTAINERS

![img1](images/1.png)

### LOGS OF CONTAINER

![img3](images/3.png)

### KERNAL MONITORING

![img4](images/4.png)

### CPU HOG

![img5](images/5.png)

### MEMORY HOG

![img6](images/6.png)

### I/O LOADS

![img7](images/7.png)

### CONTAINER TERMINATION AND CLEANING

![img8](images/8.png)


4. Engineering Analysis
Isolation Mechanisms
The runtime uses Linux namespaces such as PID, UTS, and mount namespaces to isolate containers. Each container has its own process space and filesystem view using chroot. However, all containers share the same underlying host kernel.

Supervisor and Process Lifecycle
A long-running supervisor process manages all containers. It creates containers using clone(), tracks metadata such as PID and state, and handles SIGCHLD signals to properly reap child processes and avoid zombies.

IPC, Threads, and Synchronization
Two IPC mechanisms are used:

FIFO/socket for CLI → supervisor communication
Pipes for container → supervisor logging
A bounded buffer is used between producer and consumer threads. Mutex locks and condition variables ensure synchronization, preventing race conditions, data loss, and deadlocks.

Memory Management and Enforcement
RSS (Resident Set Size) represents the actual physical memory used by a process. A soft limit generates warnings, while a hard limit enforces termination. This enforcement is implemented in kernel space to ensure reliability and prevent bypassing.

Scheduling Behavior
Experiments show that processes with lower nice values receive more CPU time. CPU-bound processes with higher priority complete faster, while I/O-bound processes demonstrate better responsiveness due to scheduler design.

5. Design Decisions and Tradeoffs
Used FIFO/socket IPC instead of shared memory for simplicity and reliability
Used chroot instead of pivot_root for easier implementation
Used mutex-based synchronization since operations may block
Used threads for logging to avoid blocking the supervisor
Tradeoff: Simpler design reduces complexity but may not be as secure or optimized as production container runtimes.

6. Scheduler Experiment Results
Two CPU-bound containers were run with different nice values:

alpha → nice = 10 (low priority)
beta → nice = -5 (high priority)
Observation:

The higher priority container completed faster
It received more CPU time from the scheduler
Conclusion: Linux Completely Fair Scheduler (CFS) allocates CPU time based on process priority, ensuring fairness while favoring higher-priority processes.

