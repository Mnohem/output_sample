#pragma once
#include <ff.h>

/*
 *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
 *  in ffconf.h
 */
#define DISK_DRIVE_NAME "SD"

#define DISK_MOUNT_PT "/"DISK_DRIVE_NAME":"

/* mounting info */
extern struct fs_mount_t mp;

#define FS_RET_OK FR_OK

int init_sdhc();
int deinit_sdhc();
// static int lsdir(const char *path);

