/*
 * UIOMux: a conflict manager for system resources, including UIO devices.
 * Copyright (C) 2009 Renesas Technology Corp.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "uio.h"

/* #define DEBUG */

static int fgets_with_openclose(char *fname, char *buf, size_t maxlen)
{
	FILE *fp;
	char *s;

	if ((fp = fopen(fname, "r")) != NULL) {
		s = fgets(buf, maxlen, fp);
		fclose(fp);
		return (s != NULL) ? strlen(buf) : 0;
	} else {
		return -1;
	}
}


#define MAXUIOIDS  100
#define MAXNAMELEN 256

static int locate_uio_device(const char *name, struct uio_device *udp)
{
	char fname[MAXNAMELEN], buf[MAXNAMELEN];
	int uio_id, i;

	for (uio_id = 0; uio_id < MAXUIOIDS; uio_id++) {
		sprintf(fname, "/sys/class/uio/uio%d/name", uio_id);
		if (fgets_with_openclose(fname, buf, MAXNAMELEN) < 0)
			continue;
		if (strncmp(name, buf, strlen(name)) == 0)
			break;
	}

	if (uio_id >= MAXUIOIDS)
		return -1;

	udp->name = strdup(buf);
	udp->path = strdup(fname);
	udp->path[strlen(udp->path) - 4] = '\0';

	sprintf(buf, "/dev/uio%d", uio_id);
	udp->fd = open(buf, O_RDWR | O_SYNC /*| O_NONBLOCK */ );

	if (udp->fd < 0) {
		perror("open");
		return -1;
	}

	return 0;
}

static int setup_uio_map(struct uio_device *udp, int nr,
			 struct uio_map *ump)
{
	char fname[MAXNAMELEN], buf[MAXNAMELEN];

	sprintf(fname, "%s/maps/map%d/addr", udp->path, nr);
	if (fgets_with_openclose(fname, buf, MAXNAMELEN) <= 0)
		return -1;

	ump->address = strtoul(buf, NULL, 0);

	sprintf(fname, "%s/maps/map%d/size", udp->path, nr);
	if (fgets_with_openclose(fname, buf, MAXNAMELEN) <= 0)
		return -1;

	ump->size = strtoul(buf, NULL, 0);
	if (ump->size > UIO_BUFFER_MAX)
		return -1;

	ump->iomem = mmap(0, ump->size,
			  PROT_READ | PROT_WRITE, MAP_SHARED,
			  udp->fd, nr * sysconf(_SC_PAGESIZE));

	if (ump->iomem == MAP_FAILED)
		return -1;

	return 0;
}

/*
   mc_map is a map to mark the memory regions which are occupied by
   each process.  This is required because the fcntl()'s advisory
   lock is only valid between processes, not within a process.
 */
static pthread_mutex_t mc_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char (*mc_map)[UIO_DEVICE_MAX];

#define PAGE_FREE      0
#define PAGE_ALLOCATED 1
#define PAGE_SHARED    2

int uio_close(struct uio *uio)
{
	if (uio == NULL)
		return -1;

	if (uio->mem.iomem)
		munmap(uio->mem.iomem, uio->mem.size);

	if (uio->mmio.iomem)
		munmap(uio->mmio.iomem, uio->mmio.size);

	if (uio->dev.fd > 0)
		close(uio->dev.fd);
	if (uio->dev.path)
		free(uio->dev.path);
	if (uio->dev.name)
		free(uio->dev.name);

	free(uio);

	return 0;
}

struct uio *uio_open(const char *name)
{
	struct uio *uio;
	int ret;

	uio = (struct uio *) calloc(1, sizeof(struct uio));
	if (uio == NULL)
		return NULL;

	ret = locate_uio_device(name, &uio->dev);
	if (ret < 0) {
		uio_close(uio);
		return NULL;
	}
#ifdef DEBUG
	printf("uio_open: Found matching UIO device at %s\n",
	       uio->dev.path);
#endif

	ret = setup_uio_map(&uio->dev, 0, &uio->mmio);
	if (ret < 0) {
		uio_close(uio);
		return NULL;
	}

	ret = setup_uio_map(&uio->dev, 1, &uio->mem);
	if (ret < 0) {
		uio_close(uio);
		return NULL;
	}

	/* initialize uio memory usage map once in each process */
	pthread_mutex_lock(&mc_lock);
	if (mc_map == NULL) {
		const long pagesize = sysconf(_SC_PAGESIZE);
		size_t n_pages;

		n_pages = (UIO_BUFFER_MAX / pagesize) *
			UIO_DEVICE_MAX * sizeof(unsigned char);
		mc_map = (unsigned char (*)[UIO_DEVICE_MAX])malloc(n_pages);
		if (mc_map == NULL) {
			uio_close(uio);
			return NULL;
		}
		memset((void *)mc_map, PAGE_FREE, n_pages);
	}
	pthread_mutex_unlock(&mc_lock);

	return uio;
}

int uio_sleep(struct uio *uio)
{
	int fd, ret;

	fd = uio->dev.fd;

	/* Enable interrupt in UIO driver */
	{
		unsigned long enable = 1;

		ret = write(fd, &enable, sizeof(u_long));
		if (ret < 0)
			return ret;
	}

	/* Wait for an interrupt */
	{
		unsigned long n_pending;

		ret = read(fd, &n_pending, sizeof(u_long));
		if (ret < 0)
			return ret;
	}

	return 0;
}

int uio_read_nonblocking(struct uio *uio)
{
	int fd, ret;
	unsigned long n_pending;

	fd = uio->dev.fd;

	/* Perform a non-blocking read. This is needed to allow multiple file
	   handles to work since UIO read returns when the number of interrupts
	   recieved is different to that stored in the file handle. The latter
	   is only updated in open and read. */
	ret = fcntl(fd, F_SETFL, O_SYNC | O_NONBLOCK);
	if (ret < 0)
		return ret;

	ret = read(fd, &n_pending, sizeof(u_long));
	fcntl(fd, F_SETFL, O_SYNC);

	if (ret < 0)
		return ret;

	return 0;
}

/* Returns index */
static int uio_mem_lock(int fd, int offset, int count, int shared)
{
	struct flock lck;
	int ret;

	if (shared)
		lck.l_type = F_RDLCK;
	else
		lck.l_type = F_WRLCK;
	lck.l_whence = SEEK_SET;
	lck.l_start = offset;
	lck.l_len = count;

	ret = fcntl(fd, F_SETLK, &lck);

	return ret;
}

static int uio_mem_unlock(int fd, int offset, int count)
{
	struct flock lck;
	int ret;

	lck.l_type = F_UNLCK;
	lck.l_whence = SEEK_SET;
	lck.l_start = offset;
	lck.l_len = count;

	ret = fcntl(fd, F_SETLK, &lck);

	return ret;
}

/* Find a region of unused memory by attempting to lock the file at successive
 * offsets. Obviously this will be quite slow if another process has already
 * allocated memory.
 */
static int uio_mem_find(int fd, int res, int max, int count, int shared)
{
	int s, l, c;

	pthread_mutex_lock(&mc_lock);
	for (s = 0; s < max; s++) {
		/* Find memory region not used by this process */
		for (l = s, c = count; (l < max) && (c > 0); l++, c--) {
			if (shared) {
				if (mc_map[l][res] == PAGE_ALLOCATED) break;
			}
			else {
				if (mc_map[l][res] != PAGE_FREE) break;
			}
		}

		if (c <= 0) {
			int ret;

			/* Attempt to lock the region */
			ret = uio_mem_lock(fd, s, count, shared);
			if (!ret)
				goto found;
		}
		s = l;	/* skip */
	}

	pthread_mutex_unlock(&mc_lock);
	return -1;

found:
#ifdef DEBUG
	fprintf(stderr, "%s: Found %d available pages at index %d\n",
		__func__, count, s);
#endif
	pthread_mutex_unlock(&mc_lock);
	return s;
}

static int uio_mem_alloc(int fd, int res, int offset, int count, int shared)
{
	unsigned char flag = (shared ? PAGE_SHARED : PAGE_ALLOCATED);
	pthread_mutex_lock(&mc_lock);
	while (count-- > 0)
		mc_map[offset++][res] = flag;
	pthread_mutex_unlock(&mc_lock);

	return 0;
}

static int uio_mem_free(int fd, int res, int offset, int count)
{
	struct flock lck;
	int ret;

	ret = uio_mem_unlock(fd, offset, count);

	if (ret == 0) {
		pthread_mutex_lock(&mc_lock);
		while (count-- > 0)
			mc_map[offset++][res] = PAGE_FREE;
		pthread_mutex_unlock(&mc_lock);
	}

	return ret;
}

void *uio_malloc(struct uio *uio, int resid, size_t size, int align, int shared)
{
	unsigned char * mem_base;
	int pagesize, pages_req, pages_max;
	int base;

	if (uio->mem.iomem == NULL) {
		fprintf(stderr,
			"%s: Allocation failed: uio->mem.iomem NULL\n",
			__func__);
		return NULL;
	}

	pagesize = sysconf(_SC_PAGESIZE);

	pages_max = (uio->mem.size + pagesize - 1) / pagesize;
	pages_req = (size + pagesize - 1) / pagesize;

	if ((base = uio_mem_find(uio->dev.fd, resid,
				 pages_max, pages_req, shared)) == -1)
		return NULL;
	uio_mem_alloc(uio->dev.fd, resid, base, pages_req, shared);

	mem_base = (void *)
		((unsigned long)uio->mem.iomem + (base * pagesize));

	return mem_base;
}

void uio_free(struct uio *uio, int resid, void *address, size_t size)
{
	int pagesize, base, pages_req;

	pagesize = sysconf(_SC_PAGESIZE);

	base = (int)(((unsigned long)address -
		      (unsigned long)uio->mem.iomem) / pagesize);
	pages_req = (size + pagesize - 1) / pagesize;
	uio_mem_free(uio->dev.fd, resid, base, pages_req);
}

static void print_usage(int pid, long base, long top)
{
	char fname[MAXNAMELEN], cmdline[MAXNAMELEN];

	printf("0x%08lx-0x%08lx : ", base, top);

	if (pid == 0) {
		printf("----\n");
	} else {
		sprintf(fname, "/proc/%d/cmdline", pid);
		if (fgets_with_openclose(fname, cmdline, MAXNAMELEN) < 0) {
			printf("%d\n", pid);
		} else {
			printf("%d %s\n", pid, cmdline);
		}
	}
}

void uio_meminfo(struct uio *uio)
{
	struct flock lck;
	const long pagesize = sysconf(_SC_PAGESIZE);
	const int pages_max = uio->mem.size / pagesize;
	long base;
	int count, ret;

	for (count = 0; count < pages_max; count++) {
		base = uio->mem.address + count * pagesize;
		lck.l_type = F_WRLCK;
		lck.l_whence = SEEK_SET;
		lck.l_start = count;
		lck.l_len = 1;
		lck.l_pid = 0;
		ret = fcntl(uio->dev.fd, F_GETLK, &lck);
		if (ret == 0)
			print_usage(lck.l_pid, base, base + pagesize - 1);
	}
}
