/*
 * ipc.h - FR4 POSIX shared memory + named semaphores between processes.
 */
#ifndef CC_IPC_H
#define CC_IPC_H

#define IPC_SLOTS     8      /* ring-buffer size inside shared memory */
#define IPC_MAX_ITEMS 1000

int cmd_ipc(int items);

#endif
