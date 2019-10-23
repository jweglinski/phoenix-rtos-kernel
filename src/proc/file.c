/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * File
 *
 * Copyright 2019 Phoenix Systems
 * Author: Jan Sikorski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include HAL
#include "../../include/types.h"
#include "../../include/errno.h"
#include "../../include/fcntl.h"
#include "../../include/ioctl.h"
#include "../../include/socket.h"
#include "../../include/event.h"
#include "../lib/lib.h"
#include "proc.h"
#include "socket.h"
#include "event.h"

#define FD_HARD_LIMIT 1024


static struct {
	lock_t lock;
	file_t *root;
} file_common;


static ssize_t generic_read(file_t *file, void *data, size_t size, off_t offset)
{
	ssize_t retval;

	if ((retval = proc_objectRead(file->port, file->oid.id, data, size, offset)) > 0)
		file->offset += retval;

	return retval;
}


static ssize_t generic_write(file_t *file, const void *data, size_t size, off_t offset)
{
	ssize_t retval;

	if ((retval = proc_objectWrite(file->port, file->oid.id, data, size, offset)) > 0)
		file->offset += retval;

	return retval;
}


static int generic_release(file_t *file)
{
	port_put(file->port);
	return proc_objectClose(file->port, file->oid.id);
}


static int generic_seek(file_t *file, off_t *offset, int whence)
{
	return EOK;
}


static int generic_setattr(file_t *file, int attr, const void *value, size_t size)
{
	return proc_objectSetAttr(file->port, file->oid.id, attr, value, size);
}


static ssize_t generic_getattr(file_t *file, int attr, void *value, size_t size)
{
	return proc_objectGetAttr(file->port, file->oid.id, attr, value, size);
}


static int generic_link(file_t *dir, const char *name, const oid_t *file)
{
	if (!file->port)
		return -EINVAL;

	return proc_objectLink(dir->port, file->id, name, file);
}


static int generic_unlink(file_t *dir, const char *name)
{
	return proc_objectUnlink(dir->port, dir->oid.id, name);
}


static int generic_ioctl(file_t *file, unsigned cmd, const void *in_buf, size_t in_size, void *out_buf, size_t out_size)
{
	return proc_objectControl(file->port, file->oid.id, cmd, in_buf, in_size, out_buf, out_size);
}


const file_ops_t generic_file_ops = {
	.read = generic_read,
	.write = generic_write,
	.release = generic_release,
	.seek = generic_seek,
	.setattr = generic_setattr,
	.getattr = generic_getattr,
	.link = generic_link,
	.unlink = generic_unlink,
	.ioctl = generic_ioctl,
};


/* file_t functions */

static void file_lock(file_t *f)
{
	proc_lockSet(&f->lock);
}


static void file_unlock(file_t *f)
{
	proc_lockClear(&f->lock);
}


static void file_destroy(file_t *f)
{
	if (f->ops != NULL)
		f->ops->release(f);
	proc_lockDone(&f->lock);
	vm_kfree(f);
}


void file_ref(file_t *f)
{
	lib_atomicIncrement(&f->refs);
}


int file_put(file_t *f)
{
	if (f && !lib_atomicDecrement(&f->refs))
		file_destroy(f);

	return EOK;
}


static file_t *file_alloc(void)
{
	file_t *file;

	if ((file = vm_kmalloc(sizeof(*file))) != NULL) {
		file->refs = 1;
		file->offset = 0;
		file->status = 0;
		proc_lockInit(&file->lock);
		file->ops = NULL;
		file->mode = 0;
		file->port = NULL;
	}

	return file;
}


static file_t *file_root(void)
{
	file_t *root;
	proc_lockSet(&file_common.lock);
	if ((root = file_common.root) != NULL)
		file_ref(root);
	proc_lockClear(&file_common.lock);
	return root;
}


/* File descriptor table functions */

static int _fd_realloc(process_t *p)
{
	fildes_t *new;
	int fdcount;

	fdcount = p->fdcount ? p->fdcount * 2 : 4;

	if (fdcount > FD_HARD_LIMIT)
		return -ENFILE;

	if ((new = vm_kmalloc(fdcount * sizeof(fildes_t))) == NULL)
		return -ENOMEM;

	hal_memcpy(new, p->fds, p->fdcount * sizeof(fildes_t));
	hal_memset(new + p->fdcount, 0, (fdcount - p->fdcount) * sizeof(fildes_t));

	vm_kfree(p->fds);
	p->fds = new;
	p->fdcount = fdcount;

	return EOK;
}


static int _fd_alloc(process_t *p, int fd)
{
	int err = EOK;

	while (err == EOK) {
		while (fd < p->fdcount) {
			if (p->fds[fd].file == NULL)
				return fd;

			fd++;
		}

		err = _fd_realloc(p);
	}

	return err;
}


static int _fd_close(process_t *p, int fd)
{
	file_t *file;
	int error = EOK;

	if (fd < 0 || fd >= p->fdcount || (file = p->fds[fd].file) == NULL)
		return -EBADF;
	p->fds[fd].file = NULL;
	file_put(file);

	return error;
}


int fd_close(process_t *p, int fd)
{
	int error;
	process_lock(p);
	error = _fd_close(p, fd);
	process_unlock(p);
	return error;
}


static int _fd_new(process_t *p, int minfd, unsigned flags, file_t *file)
{
	int fd;

	if ((fd = _fd_alloc(p, minfd)) < 0)
		return fd;

	p->fds[fd].file = file;
	p->fds[fd].flags = flags;
	return fd;
}


static file_t *_file_get(process_t *p, int fd)
{
	file_t *f;

	if (fd < 0 || fd >= p->fdcount || (f = p->fds[fd].file) == NULL || f->ops == NULL)
		return NULL;

	file_ref(f);
	return f;
}


static int fd_new(process_t *p, int fd, int flags, file_t *file)
{
	int retval;
	process_lock(p);
	retval = _fd_new(p, fd, flags, file);
	process_unlock(p);
	return retval;
}


file_t *file_get(process_t *p, int fd)
{
	file_t *f;
	process_lock(p);
	f = _file_get(p, fd);
	process_unlock(p);
	return f;
}


int fd_create(process_t *p, int minfd, int flags, unsigned int status, const file_ops_t *ops, void *data)
{
	file_t *file;
	int error;

	if ((file = file_alloc()) == NULL)
		return -ENOMEM;

	file->data = data;
	file->status = status;

	if ((error = fd_new(p, minfd, flags, file)) != EOK)
		file_put(file);

	file->ops = ops;
	return error;
}


static int file_readLink(file_t *file)
{
	return -ENOSYS;
}


static int file_followOid(file_t *file)
{
	oid_t dest;
	int error;
	port_t *port;

	if ((error = proc_objectRead(file->port, file->oid.id, &dest, sizeof(oid_t), 0)) != EOK)
		return error;

	/* TODO: close after read oid is opened? */
	if ((error = proc_objectClose(file->port, file->oid.id)) != EOK)
		return error;

	if (dest.port != file->oid.port) {
		port_put(file->port);

		if ((port = port_get(dest.port)) == NULL) {
			/* Object is closed, don't close again when cleaning up */
			file->ops = NULL;
			return -ENXIO;
		}

		file->port = port;
		file->oid.port = dest.port;
	}

	file->oid.id = dest.id;

	if ((error = proc_objectOpen(file->port, file->oid.id)) != EOK)
		return error;

	file->ops = &generic_file_ops;
	return EOK;
}


int file_lookup(const file_t *dir, file_t *file, const char *name, int flags, mode_t cmode)
{
	int err = EOK, sflags, ret = EOK;
	const char *delim = name, *path;
	id_t id, nextid;
	mode_t mode;
	port_t *port;

	cmode |= ((cmode & S_IFMT) == 0) * S_IFREG;

	port = port_get(dir->port->id); /* TODO: add port_ref? */
	id = dir->oid.id;
	mode = dir->mode;

	do {
		path = delim;

		while (*path && *path == '/')
			++path;

		delim = path;

		while (*delim && *delim != '/')
			delim++;

		if (path == delim)
			continue;

		if (!*delim && (flags & O_PARENT)) {
			ret = path - name;
			break;
		}

		if (S_ISLNK(mode)) {
			if ((err = file_readLink(file)) != EOK)
				break;
		}
		else if (S_ISMNT(mode)) {
			if ((err = file_followOid(file)) != EOK)
				break;
		}
		else if (!S_ISDIR(mode)) {
			err = -ENOTDIR;
			break;
		}

		mode = cmode;
		sflags = *delim ? 0 : flags;

		if ((err = proc_objectLookup(port, id, path, delim - path, sflags, &nextid, &mode)) < 0)
			break;

		proc_objectClose(port, id);
		id = nextid;
	} while (*delim);

	if (err < 0) {
		ret = err;
		proc_objectClose(port, id);
		port_put(port);
	}
	else {
		file->port = port;
		file->oid.port = port->id;
		file->oid.id = id;
		file->ops = &generic_file_ops;
	}

	return ret;
}


static int _file_dup(process_t *p, int fd, int fd2, int flags)
{
	file_t *f, *f2;

	if (fd == fd2)
		return -EINVAL;

	if (fd2 < 0 || (f = _file_get(p, fd)) == NULL)
		return -EBADF;

	if (flags & FD_ALLOC || fd2 >= p->fdcount) {
		if ((fd2 = _fd_alloc(p, fd2)) < 0) {
			file_put(f);
			return fd2;
		}

		flags &= ~FD_ALLOC;
	}
	else if ((f2 = p->fds[fd2].file) != NULL) {
		file_put(f2);
	}

	p->fds[fd2].file = f;
	p->fds[fd2].flags = flags;
	return fd2;
}


int proc_filesSetRoot(int fd, id_t id, mode_t mode)
{
	file_t *root, *port;
	process_t *process = proc_current()->process;

	if ((port = file_get(process, fd)) == NULL)
		return -EBADF;

	if ((root = file_alloc()) == NULL) {
		file_put(port);
		return -ENOMEM;
	}

	/* TODO: check type */
	root->oid.port = port->port->id;
	root->oid.id = id;
	root->port = port_get(port->port->id); /* XXX */
	root->ops = &generic_file_ops;
	root->mode = mode;

	proc_lockSet(&file_common.lock);
	if (file_common.root != NULL)
		file_put(file_common.root);
	file_common.root = root;
	proc_lockClear(&file_common.lock);

	return EOK;
}


int file_open(file_t **result, process_t *process, int dirfd, const char *path, int flags, mode_t mode)
{
	int error = EOK;
	file_t *dir = NULL, *file;

	if (path == NULL)
		return -EINVAL;

	if (path[0] != '/') {
		if (dirfd == AT_FDCWD) {
			if ((dir = process->cwd) == NULL)
				/* Current directory not set */
				return -ENOENT;
			file_ref(process->cwd);
		}
		else if ((dir = file_get(process, dirfd)) == NULL) {
			return -EBADF;
		}

		if (!S_ISDIR(dir->mode)) {
			file_put(dir);
			return -ENOTDIR;
		}
	}
	else {
		if ((dir = file_root()) == NULL)
			/* Rootfs not mounted yet */
			return -ENOENT;
	}

	if ((file = file_alloc()) == NULL) {
		file_put(dir);
		return -ENOMEM;
	}

	error = file_lookup(dir, file, path, flags, mode);
	file_put(dir);

	if (error != EOK) {
		file_put(file);
		return error;
	}

	if (S_ISMNT(file->mode) || S_ISCHR(file->mode) || S_ISBLK(file->mode)) {
		error = file_followOid(file);
	}
	else if (S_ISLNK(file->mode)) {
		error = file_readLink(file);
	}
	else if (S_ISFIFO(file->mode)) {
		error = npipe_open(file, flags);
	}
	else if (S_ISSOCK(file->mode)) {
		error = -ENOSYS;
	}
	else if (S_ISREG(file->mode) || S_ISDIR(file->mode)) {
		error = EOK;
	}
	else {
		error = -ENXIO;
	}

	if (error == EOK)
		*result = file;
	else
		file_put(file);

	return error;
}


int proc_fileOpen(int dirfd, const char *path, int flags, mode_t mode)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file;
	int error = EOK;

	if ((error = file_open(&file, process, dirfd, path, flags, mode)) < 0)
		return error;

	return fd_new(process, 0, 0, file);
}


int file_resolve(process_t *process, int fildes, const char *path, int flags, file_t **result)
{
	if (flags & O_CREAT)
		return -EINVAL;

	if (path == NULL) {
		if (flags)
			return -EINVAL;

		if ((*result = file_get(process, fildes)) == NULL)
			return -ENOENT;
	}
	return file_open(result, process, fildes, path, flags, 0);
}


int proc_fileOid(process_t *process, int fd, oid_t *oid)
{
	int retval = -EBADF;
	file_t *file;

	process_lock(process);
	if ((file = _file_get(process, fd)) != NULL) {
		hal_memcpy(oid, &file->oid, sizeof(oid_t));
		file_put(file);
		retval = EOK;
	}
	process_unlock(process);
	return retval;
}


int proc_fileClose(int fildes)
{
	thread_t *current = proc_current();
	process_t *process = current->process;

	return fd_close(process, fildes);
}


ssize_t proc_fileRead(int fildes, char *buf, size_t nbyte)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file;
	ssize_t retval;

	if ((file = file_get(process, fildes)) == NULL)
		return -EBADF;

	file_lock(file);
	if ((retval = file->ops->read(file, buf, nbyte, file->offset)) > 0)
		file->offset += retval;
	file_unlock(file);

	file_put(file);
	return retval;
}


ssize_t proc_fileWrite(int fildes, const char *buf, size_t nbyte)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file;
	ssize_t retval;

	if ((file = file_get(process, fildes)) == NULL)
		return -EBADF;

	file_lock(file);
	if ((file->status & O_APPEND) && (file->ops->getattr(file, atSize, &file->offset, sizeof(file->offset)) != sizeof(file->offset)))
		retval = -EIO;
	else if ((retval = file->ops->write(file, buf, nbyte, file->offset)) > 0)
		file->offset += retval;
	file_unlock(file);

	file_put(file);
	return retval;
}


int proc_fileSeek(int fildes, off_t *offset, int whence)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file;
	int retval;

	if ((file = file_get(process, fildes)) == NULL)
		return -EBADF;

	retval = file->ops->seek(file, offset, whence);
	file_put(file);
	return retval;
}


int proc_fileTruncate(int fildes, off_t length)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file;
	ssize_t retval;

	if ((file = file_get(process, fildes)) == NULL)
		return -EBADF;

	if ((retval = file->ops->setattr(file, atSize, &length, sizeof(length))) >= 0)
		retval = EOK;

	file_put(file);
	return retval;
}


int proc_fileIoctl(int fildes, unsigned long request, const char *indata, size_t insz, char *outdata, size_t outsz)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file;
	int retval;

	if ((file = file_get(process, fildes)) == NULL)
		return -EBADF;

	retval = file->ops->ioctl(file, request, indata, insz, outdata, outsz);
	file_put(file);
	return retval;
}


int proc_fileDup(int fildes, int fildes2, int flags)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	int retval;

	process_lock(process);
	retval = _file_dup(process, fildes, fildes2, flags);
	process_unlock(process);
	return retval;
}


int proc_fileLink(int fildes, const char *path, int dirfd, const char *name, int flags)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file, *dir;
	int retval;
	const char *linkname;

	if ((retval = file_resolve(process, dirfd, name, O_DIRECTORY | O_PARENT, &dir)) < 0)
		return retval;

	linkname = name + retval;

	if ((retval = file_resolve(process, fildes, path, 0, &file)) < 0) {
		file_put(dir);
		return retval;
	}

	retval = dir->ops->link(dir, linkname, &file->oid);

	file_put(file);
	file_put(dir);
	return retval;
}


int proc_fileUnlink(int dirfd, const char *path, int flags)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *dir;
	int retval;
	const char *name;

	if ((retval = file_resolve(process, dirfd, path, O_PARENT | O_DIRECTORY, &dir)) < 0)
		return retval;

	name = path + retval;

	retval = dir->ops->unlink(dir, name);
	file_put(dir);
	return retval;
}


static int fcntl_getFd(int fd)
{
	process_t *p = proc_current()->process;
	file_t *file;
	int flags;

	process_lock(p);
	if ((file = _file_get(p, fd)) == NULL) {
		process_unlock(p);
		return -EBADF;
	}

	flags = p->fds[fd].flags;
	process_unlock(p);
	file_put(file);

	return flags;
}


static int fcntl_setFd(int fd, int flags)
{
	process_t *p = proc_current()->process;
	file_t *file;

	process_lock(p);
	if ((file = _file_get(p, fd)) == NULL) {
		process_unlock(p);
		return -EBADF;
	}

	p->fds[fd].flags = flags;
	process_unlock(p);
	file_put(file);

	return EOK;
}


static int fcntl_getFl(int fd)
{
	process_t *p = proc_current()->process;
	file_t *file;
	int status;

	if ((file = file_get(p, fd)) == NULL)
		return -EBADF;

	status = file->status;
	file_put(file);

	return status;
}


static int fcntl_setFl(int fd, int val)
{
	process_t *p = proc_current()->process;
	file_t *file;
	int ignored = O_CREAT|O_EXCL|O_NOCTTY|O_TRUNC|O_RDONLY|O_RDWR|O_WRONLY;

	if ((file = file_get(p, fd)) == NULL)
		return -EBADF;

	file_lock(file);
	file->status = (val & ~ignored) | (file->status & ignored);
	file_unlock(file);
	file_put(file);

	return 0;
}


int proc_fileControl(int fildes, int cmd, long arg)
{
	int err, flags = 0;

	switch (cmd) {
	case F_DUPFD_CLOEXEC:
		flags = FD_CLOEXEC;
		/* fallthrough */
	case F_DUPFD:
		err = proc_fileDup(fildes, arg, flags | FD_ALLOC);
		break;

	case F_GETFD:
		err = fcntl_getFd(fildes);
		break;

	case F_SETFD:
		err = fcntl_setFd(fildes, arg);
		break;

	case F_GETFL:
		err = fcntl_getFl(fildes);
		break;

	case F_SETFL:
		err = fcntl_setFl(fildes, arg);
		break;

	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		err = EOK;
		break;

	case F_GETOWN:
	case F_SETOWN:
	default:
		err = -EINVAL;
		break;
	}

	return err;
}


int proc_fileStat(int fildes, const char *path, file_stat_t *buf, int flags)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	file_t *file;
	int err;

	if ((err = file_resolve(process, fildes, path, flags, &file)) != EOK)
		return err;

	if ((err = file->ops->getattr(file, atStatStruct, (char *)buf, sizeof(*buf))) >= 0)
		err = EOK;

	file_put(file);
	return err;
}


int proc_fileChmod(int fildes, mode_t mode)
{
	return 0;
}


int proc_filesDestroy(process_t *process)
{
	int fd;

	process_lock(process);
	for (fd = 0; fd < process->fdcount; ++fd)
		_fd_close(process, fd);
	process_unlock(process);

	return EOK;
}


static int _proc_filesCopy(process_t *parent)
{
	thread_t *current = proc_current();
	process_t *process = current->process;
	int fd;

	if (process->fdcount)
		return -EINVAL;

	if ((process->fds = vm_kmalloc(parent->fdcount * sizeof(fildes_t))) == NULL)
		return -ENOMEM;

	process->fdcount = parent->fdcount;
	hal_memcpy(process->fds, parent->fds, parent->fdcount * sizeof(fildes_t));

	for (fd = 0; fd < process->fdcount; ++fd) {
		if (process->fds[fd].file != NULL)
			file_ref(process->fds[fd].file);
	}

	if (parent->cwd != NULL) {
		process->cwd = parent->cwd;
		file_ref(process->cwd);
	}
	else {
		process->cwd = file_root();
	}
	return EOK;
}


int proc_filesCopy(process_t *parent)
{
	int rv;
	process_lock(parent);
	rv = _proc_filesCopy(parent);
	process_unlock(parent);
	return rv;
}


int proc_filesCloseExec(process_t *process)
{
	int fd;

	process_lock(process);
	for (fd = 0; fd < process->fdcount; ++fd) {
		if (process->fds[fd].file != NULL && (process->fds[fd].flags & FD_CLOEXEC))
			_fd_close(process, fd);
	}
	process_unlock(process);

	return EOK;
}


int proc_fifoCreate(int dirfd, const char *path, mode_t mode)
{
	process_t *process = proc_current()->process;
	int err;
	file_t *dir;
	const char *fifoname;
	id_t id;

	/* only permission bits allowed */
	if (mode & ~(S_IRWXU | S_IRWXG | S_IRWXO))
		return -EINVAL;

	if ((err = file_resolve(process, dirfd, path, O_PARENT | O_DIRECTORY, &dir)) < 0)
		return err;

	fifoname = path + err;

	mode |= S_IFIFO;
	err = proc_objectLookup(dir->port, dir->oid.id, fifoname, hal_strlen(fifoname), O_CREAT | O_EXCL, &id, &mode);
	file_put(dir);
	return err;
}


/* Sockets */

static int socket_get(process_t *process, int socket, file_t **file)
{
	if ((*file = file_get(process, socket)) == NULL)
		return -EBADF;

	if (!S_ISSOCK((*file)->mode)) {
		file_put(*file);
		return -ENOTSOCK;
	}

	return EOK;
}


int proc_netAccept4(int socket, struct sockaddr *address, socklen_t *address_len, int flags)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_accept(&file->oid, address, address_len, flags);
	file_put(file);
	return retval;
}


int proc_netBind(int socket, const struct sockaddr *address, socklen_t address_len)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_bind(&file->oid, address, address_len);
	file_put(file);
	return retval;
}


int proc_netConnect(int socket, const struct sockaddr *address, socklen_t address_len)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_connect(&file->oid, address, address_len);
	file_put(file);
	return retval;
}


int proc_netGetpeername(int socket, struct sockaddr *address, socklen_t *address_len)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_getpeername(&file->oid, address, address_len);
	file_put(file);
	return retval;
}


int proc_netGetsockname(int socket, struct sockaddr *address, socklen_t *address_len)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_getsockname(&file->oid, address, address_len);
	file_put(file);
	return retval;
}


int proc_netGetsockopt(int socket, int level, int optname, void *optval, socklen_t *optlen)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_getsockopt(&file->oid, level, optname, optval, optlen);
	file_put(file);
	return retval;
}


int proc_netListen(int socket, int backlog)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_listen(&file->oid, backlog);
	file_put(file);
	return retval;
}


ssize_t proc_netRecvfrom(int socket, void *message, size_t length, int flags, struct sockaddr *src_addr, socklen_t *src_len)
{
	file_t *file;
	process_t *process = proc_current()->process;
	ssize_t retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_recvfrom(&file->oid, message, length, flags, src_addr, src_len);
	file_put(file);
	return retval;
}


ssize_t proc_netSendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len)
{
	file_t *file;
	process_t *process = proc_current()->process;
	ssize_t retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_sendto(&file->oid, message, length, flags, dest_addr, dest_len);
	file_put(file);
	return retval;
}


int proc_netShutdown(int socket, int how)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_shutdown(&file->oid, how);
	file_put(file);
	return retval;
}


int proc_netSetsockopt(int socket, int level, int optname, const void *optval, socklen_t optlen)
{
	file_t *file;
	process_t *process = proc_current()->process;
	int retval;

	if ((retval = socket_get(process, socket, &file)) < 0)
		return retval;

	retval = socket_setsockopt(&file->oid, level, optname, optval, optlen);
	file_put(file);
	return retval;
}


int proc_netSocket(int domain, int type, int protocol)
{
	process_t *process = proc_current()->process;
	file_t *file;
	int err;

	if ((file = file_alloc()) == NULL)
		return -ENOMEM;

	file->mode = S_IFSOCK;

	switch (domain) {
	case AF_INET:
		if ((err = socket_create(&file->oid, domain, type, protocol)) >= 0) {
			file->ops = &generic_file_ops;
		}
		break;
	default:
		err = -EAFNOSUPPORT;
		break;
	}

	if (err != EOK || (err = fd_new(process, 0, 0, file)) < 0)
		file_put(file);

	return err;
}


void _file_init()
{
	file_common.root = NULL;
	proc_lockInit(&file_common.lock);
}

