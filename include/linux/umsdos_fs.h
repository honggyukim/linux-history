#ifndef LINUX_UMSDOS_FS_H
#define LINUX_UMSDOS_FS_H

#define UMSDOS_VERSION	0
#define UMSDOS_RELEASE	3

#ifndef LINUX_FS_H
#include <linux/fs.h>
#endif

/* This is the file acting as a directory extension */
#define UMSDOS_EMD_FILE		"--linux-.---"
#define UMSDOS_EMD_NAMELEN	12
#define UMSDOS_PSDROOT_NAME	"linux"
#define UMSDOS_PSDROOT_LEN	5

struct umsdos_fake_info {
	char fname[13];
	int  len;
};

#define UMSDOS_MAXNAME	220
/* This structure is 256 bytes large, depending on the name, only part */
/* of it is written to disk */
struct umsdos_dirent {
	unsigned char name_len;	/* if == 0, then this entry is not used */
	unsigned char  flags;	/* UMSDOS_xxxx */
	unsigned short nlink;	/* How many hard links point to this entry */
	uid_t		 uid;		/* Owner user id */
	gid_t		 gid;		/* Group id */
	time_t		atime;		/* Access time */
	time_t		mtime;		/* Last modification time */
	time_t		ctime;		/* Creation time */
	dev_t		rdev;		/* major and minor number of a device */
							/* special file */
	umode_t		mode;		/* Standard UNIX permissions bits + type of */
	char	spare[12];		/* unused bytes for future extensions */
							/* file, see linux/stat.h */
	char name[UMSDOS_MAXNAME];	/* Not '\0' terminated */
							/* but '\0' padded, so it will allow */
							/* for adding news fields in this record */
							/* by reducing the size of name[] */
};
#define UMSDOS_HIDDEN	1	/* Never show this entry in directory search */
#define UMSDOS_HLINK	2	/* It is a (pseudo) hard link */

/* #Specification: EMD file / record size
	Entry are 64 bytes wide in the EMD file. It allows for a 30 characters
	name. If a name is longer, contiguous entries are allocated. So a
	umsdos_dirent may span multiple records.
*/
#define UMSDOS_REC_SIZE		64

/* Translation between MSDOS name and UMSDOS name */
struct umsdos_info{
	int msdos_reject;	/* Tell if the file name is invalid for MSDOS */
						/* See umsdos_parse */
	struct umsdos_fake_info fake;
	struct umsdos_dirent entry;
	off_t f_pos;		/* offset of the entry in the EMD file */
						/* or offset where the entry may be store */
						/* if it is a new entry */
	int recsize;		/* Record size needed to store entry */
};

/* Definitions for ioctl (number randomly chosen) */
/* The next ioctl commands operate only on the DOS directory */
/* The file umsdos_progs/umsdosio.c contain a string table */
/* based on the order of those definition. Keep it in sync */
#define UMSDOS_READDIR_DOS	1234	/* Do a readdir of the DOS directory */
#define UMSDOS_UNLINK_DOS	1235	/* Erase in the DOS directory only */
#define UMSDOS_RMDIR_DOS	1236	/* rmdir in the DOS directory only */
#define UMSDOS_STAT_DOS		1237	/* Get info about a file */
/* The next ioctl commands operate only on the EMD file */
#define UMSDOS_CREAT_EMD	1238	/* Create a file */
#define UMSDOS_UNLINK_EMD	1239	/* unlink (rmdir) a file */
#define UMSDOS_READDIR_EMD	1240	/* read the EMD file only. */
#define UMSDOS_GETVERSION	1241	/* Get the release number of UMSDOS */
#define UMSDOS_INIT_EMD		1242	/* Create the EMD file if not there */
#define UMSDOS_DOS_SETUP	1243	/* Set the defaults of the MsDOS driver */

struct umsdos_ioctl{
	struct dirent dos_dirent;
	struct umsdos_dirent umsdos_dirent;
	/* The following structure is used to exchange some data */
	/* with utilities (umsdos_progs/util/umsdosio.c). The first */
	/* releases were using struct stat from "sys/stat.h". This was */
	/* causing some problem for cross compilation of the kernel */
	/* Since I am not really using the structure stat, but only some field */
	/* of it, I have decided to replicate the structure here */
	/* for compatibility with the binaries out there */
	struct {
		dev_t		st_dev;
		unsigned short	__pad1;
		ino_t		st_ino;
		umode_t		st_mode;
		nlink_t		st_nlink;
		uid_t		st_uid;
		gid_t		st_gid;
		dev_t		st_rdev;
		unsigned short	__pad2;
		off_t		st_size;
		unsigned long	st_blksize;
		unsigned long	st_blocks;
		time_t		st_atime;
		unsigned long	__unused1;
		time_t		st_mtime;
		unsigned long	__unused2;
		time_t		st_ctime;
		unsigned long	__unused3;
		unsigned long	__unused4;
		unsigned long	__unused5;
	}stat;
	char version,release;
};

/* Different macros to access struct umsdos_dirent */
#define EDM_ENTRY_ISUSED(e) ((e)->name_len!=0)

#ifdef __KERNEL__

extern struct inode_operations umsdos_dir_inode_operations;
extern struct file_operations  umsdos_file_operations;
extern struct inode_operations umsdos_file_inode_operations;
extern struct inode_operations umsdos_file_inode_operations_no_bmap;
extern struct inode_operations umsdos_symlink_inode_operations;

#include <linux/umsdos_fs.p>

#endif /* __KERNEL__ */

#endif
