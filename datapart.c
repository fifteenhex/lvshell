/*
 * datapart - create the user data partition in the free space at the end of
 * the SD card, then format and mount it.
 *
 * The kernel won't re-read the whole partition table while the rootfs is
 * mounted from the same disk, so instead of relying on that we write the new
 * MBR entry ourselves and use BLKPG_ADD_PARTITION to make the kernel pick up
 * just the new partition.
 *
 * Usage:
 *   datapart check    exit 0 if a data partition can be created (and none yet)
 *   datapart create   create + format + mount it
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

#define DISK       "/dev/mmcblk0"
#define PART       "/dev/mmcblk0p4"
#define DATA_DIR   "/data"
#define DATA_PNO   4
#define ALIGN_SECT 2048          /* 1MiB alignment */
#define MIN_FREE   (128u * 2048) /* don't bother for < 128MiB free */

struct mbr_part {
	uint8_t  status;
	uint8_t  chs_first[3];
	uint8_t  type;
	uint8_t  chs_last[3];
	uint32_t lba_start;
	uint32_t lba_size;
} __attribute__((packed));

/* Work out where a new data partition would go. Returns false if there's no
 * usable free space (or the partition already exists). */
static bool plan_partition(int fd, uint32_t *start, uint32_t *size)
{
	uint8_t mbr[512];
	struct mbr_part *p;
	uint64_t bytes = 0;
	uint32_t disk_sectors, used_end = ALIGN_SECT;
	int i;

	if (pread(fd, mbr, sizeof(mbr), 0) != sizeof(mbr))
		return false;
	if (mbr[510] != 0x55 || mbr[511] != 0xaa)
		return false;

	p = (struct mbr_part *)(mbr + 446);

	/* The data slot must be free. */
	if (p[DATA_PNO - 1].type != 0)
		return false;

	for (i = 0; i < 4; i++) {
		if (p[i].type && p[i].lba_size) {
			uint32_t end = p[i].lba_start + p[i].lba_size;
			if (end > used_end)
				used_end = end;
		}
	}

	if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
		return false;
	disk_sectors = (uint32_t)(bytes / 512);

	*start = (used_end + ALIGN_SECT - 1) & ~(ALIGN_SECT - 1);
	if (*start >= disk_sectors || disk_sectors - *start < MIN_FREE)
		return false;
	*size = disk_sectors - *start;
	return true;
}

static int write_mbr_entry(int fd, uint32_t start, uint32_t size)
{
	uint8_t mbr[512];
	struct mbr_part *p;

	if (pread(fd, mbr, sizeof(mbr), 0) != sizeof(mbr))
		return -1;

	p = (struct mbr_part *)(mbr + 446);
	memset(&p[DATA_PNO - 1], 0, sizeof(p[0]));
	p[DATA_PNO - 1].type = 0x83;                 /* Linux */
	/* CHS is ignored by Linux; mark as "use LBA". */
	memset(p[DATA_PNO - 1].chs_first, 0xff, 3);
	memset(p[DATA_PNO - 1].chs_last, 0xff, 3);
	p[DATA_PNO - 1].lba_start = start;
	p[DATA_PNO - 1].lba_size = size;

	if (pwrite(fd, mbr, sizeof(mbr), 0) != sizeof(mbr))
		return -1;
	fsync(fd);
	return 0;
}

static int blkpg_add(int fd, uint32_t start, uint32_t size)
{
	struct blkpg_partition part;
	struct blkpg_ioctl_arg arg;

	memset(&part, 0, sizeof(part));
	part.start  = (long long)start * 512;
	part.length = (long long)size * 512;
	part.pno    = DATA_PNO;

	memset(&arg, 0, sizeof(arg));
	arg.op      = BLKPG_ADD_PARTITION;
	arg.datalen = sizeof(part);
	arg.data    = &part;

	return ioctl(fd, BLKPG, &arg);
}

static int do_create(void)
{
	uint32_t start, size;
	int fd, ret;
	bool fresh = false;

	fd = open(DISK, O_RDWR);
	if (fd < 0) {
		perror("open " DISK);
		return 1;
	}

	if (access(PART, F_OK) != 0) {
		/* The partition device isn't registered yet. */
		if (!plan_partition(fd, &start, &size)) {
			fprintf(stderr, "datapart: no free space / already partitioned\n");
			close(fd);
			return 1;
		}

		printf("datapart: creating %s at sector %u (%u sectors, %u MiB)\n",
		       PART, start, size, size / 2048);

		if (write_mbr_entry(fd, start, size) != 0) {
			perror("write partition table");
			close(fd);
			return 1;
		}
		if (blkpg_add(fd, start, size) != 0 && errno != EBUSY)
			perror("BLKPG_ADD_PARTITION");
		fresh = true;
	}

	close(fd);

	/* Give devtmpfs a moment to create the node. */
	for (ret = 0; ret < 50 && access(PART, F_OK) != 0; ret++)
		usleep(100 * 1000);
	if (access(PART, F_OK) != 0) {
		fprintf(stderr, "datapart: %s did not appear\n", PART);
		return 1;
	}

	mkdir(DATA_DIR, 0755);

	/* If it already holds a filesystem, just mount it; only format fresh ones. */
	if (!fresh && mount(PART, DATA_DIR, "exfat", 0, NULL) == 0) {
		printf("datapart: mounted existing %s at %s\n", PART, DATA_DIR);
		return 0;
	}

	printf("datapart: formatting %s (exfat)\n", PART);
	if (system("mkfs.exfat -L data " PART) != 0) {
		fprintf(stderr, "datapart: mkfs failed\n");
		return 1;
	}

	if (mount(PART, DATA_DIR, "exfat", 0, NULL) != 0 && errno != EBUSY) {
		perror("mount " PART);
		return 1;
	}

	printf("datapart: mounted at %s\n", DATA_DIR);
	return 0;
}

static int do_check(void)
{
	uint32_t start, size;
	int fd, ok;

	if (access(PART, F_OK) == 0)
		return 1;   /* already exists */

	fd = open(DISK, O_RDONLY);
	if (fd < 0)
		return 1;
	ok = plan_partition(fd, &start, &size);
	close(fd);
	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "check") == 0)
		return do_check();
	if (argc >= 2 && strcmp(argv[1], "create") == 0)
		return do_create();

	fprintf(stderr, "usage: %s check|create\n", argv[0]);
	return 2;
}
