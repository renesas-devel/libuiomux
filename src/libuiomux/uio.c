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
#include <sys/param.h>

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

#define MAXNAMELEN 256

static int get_uio_device_list(struct uio_device **list, int *count)
{
	static int uio_device_count = -1;
	static struct uio_device uio_device[UIO_DEVICE_MAX];
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	int n;
	char path[MAXPATHLEN];

	pthread_mutex_lock(&lock);

	/* do we already know all available UIOs? */
	if (uio_device_count != -1) {
		*list  = uio_device;
		*count = uio_device_count;
		pthread_mutex_unlock(&lock);
		return 0;
	}

	uio_device_count = 0;
	for (n = 0; n < UIO_DEVICE_MAX; n++) {
		snprintf(path, MAXPATHLEN, "/sys/class/uio/uio%d/name", n);
		if (fgets_with_openclose(path,
					 uio_device[uio_device_count].name,
					 UIO_DEVICE_NAME_MAX) < 0)
			break;	/* There can be no gaps
				   in the uio device numbering. */

		path[strlen(path) - 4] = '\0';
		strcpy(uio_device[uio_device_count].path, path);
		uio_device_count++;
	}

	*list  = uio_device;
	*count = uio_device_count;

	pthread_mutex_unlock(&lock);

	return 0;
err:
	memset(&uio_device, 0, sizeof(uio_device));
	uio_device_count = -1;

unlock:
	pthread_mutex_unlock(&lock);
	return -1;
}

static int locate_uio_device(const char *name, struct uio_device *udp, int *device_index)
{
	struct uio_device *list;
	int uio_id, i, count;
	char buf[MAXNAMELEN];

	/* get list of UIO devices */
	if (get_uio_device_list(&list, &count) < 0)
		return -1;

	for (i = 0; i < count; i++) {
		if (strncmp(name, list[i].name, strlen(name)) == 0)
			break;
	}

	if (i >= count)
		return -1;

	strncpy(udp->name, list[i].name, UIO_DEVICE_NAME_MAX);
	strncpy(udp->path, list[i].path, UIO_DEVICE_PATH_MAX);

	sscanf(udp->path, "/sys/class/uio/uio%i", &uio_id);
	sprintf(buf, "/dev/uio%d", uio_id);
	udp->fd = open(buf, O_RDWR | O_SYNC /*| O_NONBLOCK */ );

	if (udp->fd < 0) {
		perror("open");
		return -1;
	}

	*device_index = i;

	return 0;
}

int uio_list_device(char ***names, int *n)
{
	struct uio_device *list;
	int i;
	static char *_names[UIO_DEVICE_MAX + 1];
	static int count = -1;
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&lock);
	if (count != -1) {
		*n = count;
		*names = _names;
		pthread_mutex_unlock(&lock);
		return 0;
	}

	/* get list of UIO devices */
	if (get_uio_device_list(&list, &count) < 0) {
		pthread_mutex_unlock(&lock);
		return -1;
	}

	memset(_names, 0, sizeof(_names));
	for (i = 0; i < count; i++)
		_names[i] = list[i].name;

	*n = count;
	*names = _names;
	pthread_mutex_unlock(&lock);

	return 0;
}

int uio_device_index (struct uio * uio)
{
	if (!uio)
		return -1;

	return uio->device_index;
}

static int setup_uio_map(struct uio_device *udp, int nr,
			 struct uio_map *ump)
{
	char fname[MAXNAMELEN], buf[MAXNAMELEN];

	ump->iomem = NULL;

	sprintf(fname, "%s/maps/map%d/addr", udp->path, nr);
	if (fgets_with_openclose(fname, buf, MAXNAMELEN) <= 0)
		return -1;

	ump->address = strtoul(buf, NULL, 0);

	sprintf(fname, "%s/maps/map%d/size", udp->path, nr);
	if (fgets_with_openclose(fname, buf, MAXNAMELEN) <= 0)
		return -1;

	ump->size = strtoul(buf, NULL, 0);
	ump->iomem = mmap(0, ump->size,
			  PROT_READ | PROT_WRITE, MAP_SHARED,
			  udp->fd, nr * sysconf(_SC_PAGESIZE));

	if (ump->iomem == MAP_FAILED) {
		ump->iomem = NULL;
		return -1;
	}

	return 0;
}

/*
   mc_map is a map to mark the memory regions which are occupied by
   each process.  This is required because the fcntl()'s advisory
   lock is only valid between processes, not within a process.
 */
static pthread_mutex_t mc_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *mc_map[UIO_DEVICE_MAX];
static int mc_refcount[UIO_DEVICE_MAX];

#define PAGE_FREE      0
#define PAGE_ALLOCATED 1
#define PAGE_SHARED    2

int uio_close(struct uio *uio)
{
	int res;

	if (uio == NULL)
		return -1;

	if (uio->mem.iomem)
		munmap(uio->mem.iomem, uio->mem.size);

	if (uio->mmio.iomem)
		munmap(uio->mmio.iomem, uio->mmio.size);

	if (uio->dev.fd > 0)
		close(uio->dev.fd);

	if (uio->exit_sleep_pipe[0] > 0) {
		close(uio->exit_sleep_pipe[0]);
		close(uio->exit_sleep_pipe[1]);
	}

	res = uio->device_index;
	pthread_mutex_lock(&mc_lock);
	mc_refcount[res]--;
	if ((mc_refcount[res] == 0) && mc_map[res]) {
		free(mc_map[res]);
		mc_map[res] = NULL;
	}
	pthread_mutex_unlock(&mc_lock);

	free(uio);

	return 0;
}

struct uio *uio_open(const char *name)
{
	struct uio *uio;
	int ret;
	int res;

	uio = (struct uio *) calloc(1, sizeof(struct uio));
	if (uio == NULL)
		return NULL;

	ret = locate_uio_device(name, &uio->dev, &uio->device_index);
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

	/* contiguous memory may not be available */
	ret = setup_uio_map(&uio->dev, 1, &uio->mem);

	pipe(uio->exit_sleep_pipe);

	/* initialize uio memory usage map once in each process */
	res = uio->device_index;
	pthread_mutex_lock(&mc_lock);
	if (mc_map[res] == NULL && uio->mem.iomem) {
		const long pagesize = sysconf(_SC_PAGESIZE);
		size_t n_pages;

		n_pages = ((uio->mem.size + pagesize - 1) / pagesize) *
			sizeof(unsigned char);
		mc_map[res] = (unsigned char *)malloc(n_pages);
		if (mc_map[res] == NULL) {
			pthread_mutex_unlock(&mc_lock);
			uio_close(uio);
			return NULL;
		}
		memset((void *)mc_map[res], PAGE_FREE, n_pages);
	}
	mc_refcount[res]++;
	pthread_mutex_unlock(&mc_lock);

	return uio;
}

int uio_sleep(struct uio *uio, struct timeval *timeout)
{
	int fd, ret;
	fd_set readset;
	int nfds;
	int exit_fd;

	fd = uio->dev.fd;

	exit_fd = uio->exit_sleep_pipe[0];

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
		FD_ZERO(&readset);
		FD_SET(fd, &readset);
		FD_SET(exit_fd, &readset);

		nfds = fd > exit_fd ? fd + 1 : exit_fd + 1;

		ret = select(nfds, &readset, NULL, NULL, timeout);
		if (!ret)
			return -1;
		if (FD_ISSET(exit_fd, &readset)) {
			read(exit_fd, &n_pending, sizeof(u_long));
			return -1;
		}
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

static int uio_mem_lock_wait(int fd, int offset, int count, int shared)
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

	ret = fcntl(fd, F_SETLKW, &lck);

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
static int uio_mem_find(int fd, int res, int max, int count, int align, int shared)
{
	int s, l, c;

	if (mc_map[res] == NULL)
		return -1;
	s = 0;
	while (s < max) {
		/* Find memory region not used by this process */
		for (l = s, c = count; (l < max) && (c > 0); l++, c--) {
			if (shared) {
				if (mc_map[res][l] == PAGE_ALLOCATED) break;
			}
			else {
				if (mc_map[res][l] != PAGE_FREE) break;
			}
		}

		if (c <= 0) {
			int ret;

			/* Attempt to lock the region */
			ret = uio_mem_lock(fd, s, count, shared);
			if (!ret)
				goto found;
		}
		s = l + 1;	/* skip */
		if (align > 1)
			s = ((s / align) + 1) * align;
	}

	return -1;

found:
#ifdef DEBUG
	fprintf(stderr, "%s: Found %d available pages at index %d\n",
		__func__, count, s);
#endif
	return s;
}

static int uio_mem_alloc(int fd, int res, int offset, int count, int shared)
{
	unsigned char flag = (shared ? PAGE_SHARED : PAGE_ALLOCATED);

	while (count-- > 0)
		mc_map[res][offset++] = flag;

	return 0;
}

static pthread_cond_t mc_cond = PTHREAD_COND_INITIALIZER;

static int uio_mem_free(int fd, int res, int offset, int count)
{
	struct flock lck;
	int ret;

	ret = uio_mem_unlock(fd, offset, count);

	if (ret == 0) {
		pthread_mutex_lock(&mc_lock);
		while (count-- > 0)
			mc_map[res][offset++] = PAGE_FREE;
		pthread_mutex_unlock(&mc_lock);
		pthread_cond_broadcast(&mc_cond);
	}

	return ret;
}

void *uio_malloc(struct uio *uio, size_t size, int align, int shared)
{
	unsigned char * mem_base;
	int pagesize, pages_req, pages_max, pages_align;
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
	pages_align = (align + pagesize - 1) / pagesize;

	pthread_mutex_lock(&mc_lock);
	if ((base = uio_mem_find(uio->dev.fd, uio->device_index,
				 pages_max, pages_req,
				 pages_align, shared)) == -1) {
		pthread_mutex_unlock(&mc_lock);
		return NULL;
	}
	uio_mem_alloc(uio->dev.fd, uio->device_index, base, pages_req, shared);
	pthread_mutex_unlock(&mc_lock);

	mem_base = (void *)
		((unsigned long)uio->mem.iomem + (base * pagesize));

	return mem_base;
}

int uio_mlock(struct uio *uio, void *address, size_t size, int wait)
{
	int i, n, res;
	int pagesize, count, base;
	int ret = 0;

	if (uio == NULL || uio->mem.iomem == NULL) {
		fprintf(stderr,
			"%s: Allocation failed: uio->mem.iomem NULL\n",
			__func__);
		return -ENODEV;
	}

	pagesize = sysconf(_SC_PAGESIZE);
	count = (size + pagesize - 1) / pagesize;
	base = (int)(((unsigned long)address -
		      (unsigned long)uio->mem.iomem) / pagesize);

	res = uio->device_index;

	pthread_mutex_lock(&mc_lock);

	if (mc_map[res] == NULL) {
		pthread_mutex_unlock(&mc_lock);
		fprintf(stderr,
			"%s: Allocation failed: mc_map[res] NULL\n",
			__func__);
		return -ENODEV;
	}

	/* wait for available */
	if (wait)
		uio_mem_lock_wait(uio->dev.fd, base, count, 0);
	else
		uio_mem_lock(uio->dev.fd, base, count, 0);
retry:
	for (i=count, n=base; i>0; i--,n++) {
		if (mc_map[res][n] == PAGE_ALLOCATED) {
			ret = wait ? pthread_cond_wait(&mc_cond, &mc_lock) : -EBUSY;
			if (ret != 0)
				break;
			goto retry;
		}
	}

	/* lock */
	if (ret == 0) {
		for (i=count, n=base; i>0; i--,n++)
			mc_map[res][n] = PAGE_ALLOCATED;
	}

	pthread_mutex_unlock(&mc_lock);

	return ret;
}

void uio_free(struct uio *uio, void *address, size_t size)
{
	int pagesize, base, pages_req;

	pagesize = sysconf(_SC_PAGESIZE);

	base = (int)(((unsigned long)address -
		      (unsigned long)uio->mem.iomem) / pagesize);
	pages_req = (size + pagesize - 1) / pagesize;
	uio_mem_free(uio->dev.fd, uio->device_index, base, pages_req);
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
