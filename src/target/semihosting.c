/*
 * semihosting spec: https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "semihosting.h"
#include "semihosting_internal.h"

#include <rtthread.h>
#define DBG_TAG "SEMI"
#define DBG_LVL DBG_INFO
//#define DBG_LVL DBG_DBG
#include <rtdbg.h>

#include <dfs_file.h>
#include <unistd.h>
#include <stdio.h> /* rename() */
#include <sys/stat.h>
#include <msh.h>

#include "settings.h"
#include "serials.h"
#include "usb_cdc0.h"

#define STDIN_FILENO  0 /* standard input file descriptor */
#define STDOUT_FILENO 1 /* standard output file descriptor */
#define STDERR_FILENO 2 /* standard error file descriptor */

#define FEATURE_FILENO (INT32_MAX - 1)

/* This stores the current SYS_CLOCK epoch relative to the values from SYS_TIME */
uint32_t semihosting_wallclock_epoch = UINT32_MAX;

/* This stores the current :semihosting-features "file" access offset */
static uint8_t semihosting_features_offset = 0U;

/*
 * "SHFB" is the magic number header for the :semihosting-features "file"
 * Following that comes a byte of feature bits:
 * - bit 0 defines if we support extended exit
 * - bit 1 defines if we support both stdout and stderr via :tt
 * Given we support both, we set this to 0b00000011
 */
#define SEMIHOSTING_FEATURES_LENGTH 5U
static const char semihosting_features[SEMIHOSTING_FEATURES_LENGTH] = {'S', 'H', 'F', 'B', '\x03'};

static const char semihosting_tempname_template[] = "/sdcard/tempAA.tmp";
#define SEMIHOSTING_TEMPNAME_LENGTH ARRAY_LENGTH(semihosting_tempname_template)

#define SEMIHOSTING_PATH_MAX 64U

/* relative file names are placed on the sdcard */
#define SEMIHOSTING_FILE_PREFIX     "/sdcard/"
#define SEMIHOSTING_FILE_PREFIX_LEN (sizeof(SEMIHOSTING_FILE_PREFIX) - 1U)

/* semihosting_opened_fds is 32 bit */
#define SEMIHOSTING_MAX_FD 31U

/* returned by SYS_READC */
#define SEMIHOSTING_READC_EOF ((int32_t) - 1)

/* returned when the semihosting spec says 'undefined' */
#define SEMIHOSTING_UNDEFINED (0xDEADBEEF)

/* strings for debug output */
const char *const semihosting_names[] = {
	[SEMIHOSTING_SYS_OPEN] = "SYS_OPEN",
	[SEMIHOSTING_SYS_CLOSE] = "SYS_CLOSE",
	[SEMIHOSTING_SYS_WRITEC] = "SYS_WRITEC",
	[SEMIHOSTING_SYS_WRITE0] = "SYS_WRITE0",
	[SEMIHOSTING_SYS_WRITE] = "SYS_WRITE",
	[SEMIHOSTING_SYS_READ] = "SYS_READ",
	[SEMIHOSTING_SYS_READC] = "SYS_READC",
	[SEMIHOSTING_SYS_ISERROR] = "SYS_ISERROR",
	[SEMIHOSTING_SYS_ISTTY] = "SYS_ISTTY",
	[SEMIHOSTING_SYS_SEEK] = "SYS_SEEK",
	[SEMIHOSTING_SYS_FLEN] = "SYS_FLEN",
	[SEMIHOSTING_SYS_TMPNAM] = "SYS_TMPNAM",
	[SEMIHOSTING_SYS_REMOVE] = "SYS_REMOVE",
	[SEMIHOSTING_SYS_RENAME] = "SYS_RENAME",
	[SEMIHOSTING_SYS_CLOCK] = "SYS_CLOCK",
	[SEMIHOSTING_SYS_TIME] = "SYS_TIME",
	[SEMIHOSTING_SYS_SYSTEM] = "SYS_SYSTEM",
	[SEMIHOSTING_SYS_ERRNO] = "SYS_ERRNO",
	[SEMIHOSTING_SYS_GET_CMDLINE] = "SYS_GET_CMDLINE",
	[SEMIHOSTING_SYS_HEAPINFO] = "SYS_HEAPINFO",
	[SEMIHOSTING_SYS_EXIT] = "SYS_EXIT",
	[SEMIHOSTING_SYS_EXIT_EXTENDED] = "SYS_EXIT_EXTENDED",
	[SEMIHOSTING_SYS_ELAPSED] = "SYS_ELAPSED",
	[SEMIHOSTING_SYS_TICKFREQ] = "SYS_TICKFREQ",
};

/* bitmask of all opened file descriptors */
static uint32_t semihosting_opened_fds = 0U;

/* true if file was opened by semihosting */
static bool is_semihosting_fd(uint32_t fd)
{
	return (fd < SEMIHOSTING_MAX_FD) && ((semihosting_opened_fds & (0x1 << fd)) != 0);
}

/* read filename from target memory */
static bool semihosting_read_path(
	target_s *const target, const target_addr_t path_taddr, const uint32_t path_length, char *const path)
{
	if (path_taddr == TARGET_NULL) {
		LOG_E("path: null pointer");
		target->tc->gdb_errno = TARGET_EINVAL;
		return false;
	}
	if (path_length == 0U) {
		LOG_E("path: length 0");
		target->tc->gdb_errno = TARGET_EINVAL;
		return false;
	}
	if (path_length >= SEMIHOSTING_PATH_MAX) {
		LOG_E("path: too long");
		target->tc->gdb_errno = TARGET_ENAMETOOLONG;
		return false;
	}
	target_mem32_read(target, path, path_taddr, path_length);
	if (target_check_error(target)) {
		LOG_E("path: memory read error");
		target->tc->gdb_errno = TARGET_EFAULT;
		return false;
	}
	path[path_length] = '\0';

	if (path[0] != '/' && path[0] != ':') {
		if (path_length + SEMIHOSTING_FILE_PREFIX_LEN >= SEMIHOSTING_PATH_MAX) {
			LOG_E("path: too long");
			target->tc->gdb_errno = TARGET_ENAMETOOLONG;
			return false;
		}
		memmove(path + SEMIHOSTING_FILE_PREFIX_LEN, path, path_length + 1U);
		memcpy(path, SEMIHOSTING_FILE_PREFIX, SEMIHOSTING_FILE_PREFIX_LEN);
	}

	return true;
}

/* convert rt-thread errno in gdb file i/o errno for SYS_ERRNO */
static semihosting_errno_e semihosting_errno(void)
{
	const int32_t error = errno;
	switch (error) {
	case 0:
		return TARGET_SUCCESS;
	case EPERM:
		return TARGET_EPERM;
	case ENOENT:
		return TARGET_ENOENT;
	case EINTR:
		return TARGET_EINTR;
	case EIO:
		return TARGET_EIO;
	case EBADF:
		return TARGET_EBADF;
	case EACCES:
		return TARGET_EACCES;
	case EFAULT:
		return TARGET_EFAULT;
	case EBUSY:
		return TARGET_EBUSY;
	case EEXIST:
		return TARGET_EEXIST;
	case ENODEV:
	case ENXIO:
		return TARGET_ENODEV;
	case ENOTDIR:
		return TARGET_ENOTDIR;
	case EISDIR:
		return TARGET_EISDIR;
	case EINVAL:
		return TARGET_EINVAL;
	case ENFILE:
		return TARGET_ENFILE;
	case EMFILE:
		return TARGET_EMFILE;
	case EFBIG:
		return TARGET_EFBIG;
	case ENOSPC:
		return TARGET_ENOSPC;
	case ESPIPE:
		return TARGET_ESPIPE;
	case EROFS:
		return TARGET_EROFS;
	case ENOSYS:
		return TARGET_ENOSYS;
	case ENAMETOOLONG:
		return TARGET_ENAMETOOLONG;
	default:
		return TARGET_EUNKNOWN;
	}
}

int32_t semihosting_open(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t file_name_taddr = request->params[0];
	const int32_t open_mode_raw = request->params[1];
	const uint32_t file_name_length = request->params[2];

	if (open_mode_raw > 11U) {
		LOG_E("open: invalid mode %d", open_mode_raw);
		target->tc->gdb_errno = TARGET_EINVAL;
		return -1;
	}

	static const int32_t open_mode_flags[] = {
		O_RDONLY,                      /* r, rb */
		O_RDWR,                        /* r+, r+b */
		O_WRONLY | O_CREAT | O_TRUNC,  /* w, wb */
		O_RDWR | O_CREAT | O_TRUNC,    /* w+, w+b */
		O_WRONLY | O_CREAT | O_APPEND, /* a, ab */
		O_RDWR | O_CREAT | O_APPEND,   /* a+, a+b */
	};

	const int32_t open_mode = open_mode_flags[open_mode_raw >> 1U];
	char file_name[SEMIHOSTING_PATH_MAX + 1] = {0};

	if (!semihosting_read_path(target, file_name_taddr, file_name_length, file_name)) {
		LOG_E("open: file name read error");
		return -1;
	}

	/* console i/o */
	if (!strncmp(file_name, ":tt", 4U)) {
		int32_t result = -1;
		if (open_mode == O_RDONLY)
			result = STDIN_FILENO;
		else if (open_mode & O_TRUNC)
			result = STDOUT_FILENO;
		else
			result = STDERR_FILENO;
		return result + 1;
	}

	/* known limitation: one access to ":semihosting-features" at a time */
	if (!strncmp(file_name, ":semihosting-features", 22U)) {
		/* only let the firmware open features in read-only mode */
		if (open_mode == O_RDONLY) {
			semihosting_features_offset = 0U;
			return FEATURE_FILENO + 1;
		}
		LOG_E("open: :semihosting-features is read only");
		target->tc->gdb_errno = TARGET_EINVAL;
		return -1;
	}

	/* a real file */
	if (!settings.fileio_enable) {
		LOG_E("open: '%s' file i/o disabled in settings", file_name);
		target->tc->gdb_errno = TARGET_EACCES;
		return -1;
	}

	const int32_t fd = open(file_name, open_mode);

	if (fd < 0) {
		LOG_E("open: '%s' fail", file_name);
		target->tc->gdb_errno = semihosting_errno();
		return -1;
	}

	if (fd >= SEMIHOSTING_MAX_FD) {
		/* can't track */
		LOG_E("open: '%s' too many open files", file_name);
		close(fd);
		target->tc->gdb_errno = TARGET_EMFILE;
		return -1;
	}

	semihosting_opened_fds |= 0x1 << fd;
	LOG_I("open: '%s' fd %" PRId32, file_name, fd);

	return fd + 1;
}

int32_t semihosting_close(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;

	if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO || fd == FEATURE_FILENO)
		return 0;

	if (!is_semihosting_fd(fd))
		return -1;

	int32_t result = close(fd);

	semihosting_opened_fds &= ~(0x1 << fd);

	if (result < 0) {
		LOG_E("close: fd %" PRId32 " error", fd);
		target->tc->gdb_errno = semihosting_errno();
		return -1;
	}

	return 0;
}

int32_t semihosting_writec(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t ch_taddr = request->r1;
	uint8_t ch = '\0';

	if (ch_taddr == TARGET_NULL) {
		LOG_E("writec: null pointer");
		target->tc->gdb_errno = TARGET_EFAULT;
		return SEMIHOSTING_UNDEFINED;
	}
	ch = target_mem32_read8(target, ch_taddr);
	if (target_check_error(target)) {
		LOG_E("writec: memory read error");
		target->tc->gdb_errno = TARGET_EFAULT;
		return SEMIHOSTING_UNDEFINED;
	}
	cdc0_write(&ch, 1);
	return SEMIHOSTING_UNDEFINED;
}

int32_t semihosting_write0(target_s *const target, const semihosting_s *const request)
{
	target_addr_t str = request->r1;
	uint8_t ch = '\0';

	if (str == TARGET_NULL) {
		LOG_E("write0: null pointer");
		target->tc->gdb_errno = TARGET_EFAULT;
		return SEMIHOSTING_UNDEFINED;
	}
	/* memory access is only guaranteed up to the terminating NUL */
	while ((ch = target_mem32_read8(target, str++)) != '\0') {
		if (target_check_error(target)) {
			LOG_E("write0: memory read error");
			target->tc->gdb_errno = TARGET_EFAULT;
			return SEMIHOSTING_UNDEFINED;
		}
		cdc0_write(&ch, 1);
	}
	return SEMIHOSTING_UNDEFINED;
}

int32_t semihosting_write(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
	const target_addr_t buf_taddr = request->params[1];
	const uint32_t buf_len = request->params[2];
	uint8_t buffer[STDOUT_READ_BUF_SIZE];

	/* write requests to stdin and :semihosting-features always fail */
	if ((fd == STDIN_FILENO) || (fd == FEATURE_FILENO)) {
		LOG_E("write: can't write stdin/features");
		target->tc->gdb_errno = TARGET_EINVAL;
		return buf_len;
	}

	/* a real file: check the handle before moving any data */
	const bool is_console = (fd == STDOUT_FILENO) || (fd == STDERR_FILENO);
	if (!is_console && !is_semihosting_fd(fd)) {
		LOG_E("write: file not open");
		target->tc->gdb_errno = TARGET_EBADF;
		return buf_len;
	}

	/* chunked write */
	uint32_t total_written = 0U;
	while (total_written < buf_len) {
		uint32_t len = buf_len - total_written;
		if (len > STDOUT_READ_BUF_SIZE)
			len = STDOUT_READ_BUF_SIZE;

		target_mem32_read(target, buffer, buf_taddr + total_written, len);
		if (target_check_error(target)) {
			LOG_E("write: memory read error");
			target->tc->gdb_errno = TARGET_EFAULT;
			break;
		}

		if (is_console) {
			cdc0_write(buffer, len);
			total_written += len;
			continue;
		}

		const ssize_t bytes_written = write(fd, buffer, len);
		if (bytes_written < 0) {
			LOG_E("write: file write error");
			target->tc->gdb_errno = semihosting_errno();
			break;
		}

		total_written += (uint32_t)bytes_written;

		if ((uint32_t)bytes_written < len) {
			LOG_E("write: short write");
			target->tc->gdb_errno = semihosting_errno();
			break;
		}
	}

	/* SYS_WRITE returns the number of bytes NOT written; 0 = complete success */
	return (int32_t)(buf_len - total_written);
}

int32_t semihosting_read(target_s *const target, const semihosting_s *const request)
{
	int32_t fd = request->params[0] - 1;
	target_addr_t buf_taddr = request->params[1];
	uint32_t buf_len = request->params[2];
	uint8_t buffer[STDOUT_READ_BUF_SIZE];

	if (fd == FEATURE_FILENO) {
		uint32_t len = buf_len;
		if (len > SEMIHOSTING_FEATURES_LENGTH - semihosting_features_offset)
			len = SEMIHOSTING_FEATURES_LENGTH - semihosting_features_offset;
		target_mem32_write(target, buf_taddr, semihosting_features + semihosting_features_offset, len);
		if (target_check_error(target)) {
			LOG_E("read: semihosting_features memory write error");
			target->tc->gdb_errno = TARGET_EFAULT;
			return buf_len;
		}
		semihosting_features_offset += len;
		return (int32_t)(buf_len - len);
	}

	if ((fd == STDOUT_FILENO) || (fd == STDERR_FILENO)) {
		LOG_E("read: can't read stdout/stderr");
		target->tc->gdb_errno = TARGET_EINVAL;
		return buf_len;
	}

	if (fd == STDIN_FILENO) {
		return buf_len; /* nothing read, no error */
	}

	/* real file */
	if (!is_semihosting_fd(fd)) {
		LOG_E("read: file not open");
		target->tc->gdb_errno = TARGET_EBADF;
		return buf_len;
	}

	/* chunked read */
	uint32_t total_read = 0U;
	while (total_read < buf_len) {
		uint32_t len = buf_len - total_read;
		if (len > STDOUT_READ_BUF_SIZE)
			len = STDOUT_READ_BUF_SIZE;

		const ssize_t bytes_read = read(fd, buffer, len);
		if (bytes_read < 0) {
			LOG_E("read: file read error");
			target->tc->gdb_errno = semihosting_errno();
			if (total_read == 0U)
				return buf_len; /* fail */
			break;              /* report partial transfer */
		}

		if (bytes_read == 0)
			break; /* end of file */

		target_mem32_write(target, buf_taddr + total_read, buffer, (size_t)bytes_read);
		if (target_check_error(target)) {
			LOG_E("read: memory write error");
			target->tc->gdb_errno = TARGET_EFAULT;
			break; /* report partial transfer */
		}

		total_read += (uint32_t)bytes_read;
		/* keep reading */
	}

	/* return number of bytes not read; 0 = all read */
	return (int32_t)(buf_len - total_read);
}

/* readc: always returns EOF */
int32_t semihosting_readc(target_s *const target)
{
	return SEMIHOSTING_READC_EOF;
}

int32_t semihosting_is_error(const semihosting_errno_e code)
{
	const bool is_error = code == TARGET_EPERM || code == TARGET_ENOENT || code == TARGET_EINTR || code == TARGET_EIO ||
		code == TARGET_EBADF || code == TARGET_EACCES || code == TARGET_EFAULT || code == TARGET_EBUSY ||
		code == TARGET_EEXIST || code == TARGET_ENODEV || code == TARGET_ENOTDIR || code == TARGET_EISDIR ||
		code == TARGET_EINVAL || code == TARGET_ENFILE || code == TARGET_EMFILE || code == TARGET_EFBIG ||
		code == TARGET_ENOSPC || code == TARGET_ESPIPE || code == TARGET_EROFS || code == TARGET_ENOSYS ||
		code == TARGET_ENAMETOOLONG || code == TARGET_EUNKNOWN;
	return is_error;
}

int32_t semihosting_isatty(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;

	if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
		return 1; /* console is tty */

	if (fd == FEATURE_FILENO || is_semihosting_fd(fd))
		return 0; /* fd valid, but is no tty */

	LOG_E("isatty: invalid fd");
	target->tc->gdb_errno = TARGET_EBADF;
	return -1; /* fd invalid */
}

int32_t semihosting_seek(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
	const off_t offset = (off_t)request->params[1];

	if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO) {
		LOG_E("seek: seek on console");
		target->tc->gdb_errno = TARGET_ESPIPE;
		return -1;
	}

	if (fd == FEATURE_FILENO) {
		if (offset >= 0 && offset < (off_t)SEMIHOSTING_FEATURES_LENGTH)
			semihosting_features_offset = (uint8_t)offset;
		else
			semihosting_features_offset = SEMIHOSTING_FEATURES_LENGTH;
		return 0;
	}

	if (!is_semihosting_fd(fd)) {
		LOG_E("seek: no access");
		target->tc->gdb_errno = TARGET_EACCES;
		return -1;
	}

	const off_t result = lseek(fd, offset, SEEK_SET);

	if (result != offset) {
		LOG_E("seek: seek failed");
		target->tc->gdb_errno = semihosting_errno();
		return -1;
	}

	return 0;
}

int32_t semihosting_file_length(target_s *const target, const semihosting_s *const request)
{
	const int32_t fd = request->params[0] - 1;
	struct stat file_stat;

	if (fd == FEATURE_FILENO)
		return SEMIHOSTING_FEATURES_LENGTH;

	if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
		return 0;

	if (!is_semihosting_fd(fd)) {
		LOG_E("flen: no access");
		target->tc->gdb_errno = TARGET_EACCES;
		return -1;
	}

	if (fstat(fd, &file_stat) < 0) {
		LOG_E("flen: fstat error");
		target->tc->gdb_errno = semihosting_errno();
		return -1;
	}

	if (file_stat.st_size > INT32_MAX) {
		LOG_E("flen: file too big");
		target->tc->gdb_errno = TARGET_EFBIG;
		return -1;
	}

	return (int32_t)file_stat.st_size;
}

int32_t semihosting_temp_name(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t buffer_taddr = request->params[0];
	const uint8_t target_id = request->params[1];
	const size_t buffer_length = request->params[2];

	char file_name[SEMIHOSTING_TEMPNAME_LENGTH];
	memcpy(file_name, semihosting_tempname_template, SEMIHOSTING_TEMPNAME_LENGTH);

	file_name[12] += (target_id >> 4U) & 0xfU;
	file_name[13] += target_id & 0xfU;

	if (buffer_length < sizeof(file_name)) {
		target->tc->gdb_errno = TARGET_EINVAL;
		LOG_E("tmpnam: buffer too small");
		return -1;
	}

	target_mem32_write(target, buffer_taddr, file_name, SEMIHOSTING_TEMPNAME_LENGTH);

	if (target_check_error(target)) {
		LOG_E("tmpnam: memory write error");
		target->tc->gdb_errno = TARGET_EFAULT;
		return -1;
	}

	return 0;
}

int32_t semihosting_remove(target_s *const target, const semihosting_s *const request)
{
	char path[SEMIHOSTING_PATH_MAX];

	if (!settings.fileio_enable) {
		LOG_E("remove: file i/o disabled in settings");
		target->tc->gdb_errno = TARGET_EACCES;
		return -1;
	}

	if (!semihosting_read_path(target, request->params[0], request->params[1], path))
		return -1;

	if (unlink(path) < 0) {
		LOG_E("remove: '%s' failed", path);
		const semihosting_errno_e host_errno = semihosting_errno();
		target->tc->gdb_errno = host_errno;
		return host_errno;
	}

	return 0;
}

int32_t semihosting_rename(target_s *const target, const semihosting_s *const request)
{
	char old_path[SEMIHOSTING_PATH_MAX];
	char new_path[SEMIHOSTING_PATH_MAX];

	if (!settings.fileio_enable) {
		LOG_E("rename: file i/o disabled in settings");
		target->tc->gdb_errno = TARGET_EACCES;
		return -1;
	}

	if (!semihosting_read_path(target, request->params[0], request->params[1], old_path) ||
		!semihosting_read_path(target, request->params[2], request->params[3], new_path))
		return -1;

	if (rename(old_path, new_path) < 0) {
		LOG_E("rename: '%s' '%s' failed", old_path, new_path);
		const semihosting_errno_e host_errno = semihosting_errno();
		target->tc->gdb_errno = host_errno;
		return host_errno;
	}

	return 0;
}

int32_t semihosting_clock(target_s *const target)
{
	(void)target;

	uint32_t centiseconds = (uint32_t)(rt_tick_get_millisecond() / 10U);
	if (semihosting_wallclock_epoch > centiseconds)
		semihosting_wallclock_epoch = centiseconds;
	centiseconds -= semihosting_wallclock_epoch;
	centiseconds = (int32_t)(centiseconds & 0x7fffffff); /* truncate */

	return (int32_t)centiseconds;
}

int32_t semihosting_time(target_s *const target)
{
	(void)target;

	return (int32_t)time(NULL);
}

int32_t semihosting_system(target_s *const target, const semihosting_s *const request)
{
	char cmd[SEMIHOSTING_PATH_MAX + 1];
	const target_addr_t buffer_taddr = request->params[0];
	const size_t buffer_length = request->params[1];

	if (!settings.shell_enable) {
		LOG_E("system: shell disabled in settings");
		target->tc->gdb_errno = TARGET_EACCES;
		return -1;
	}

	if (buffer_taddr == TARGET_NULL || buffer_length == 0U || buffer_length >= SEMIHOSTING_PATH_MAX) {
		LOG_E("system: invalid string");
		target->tc->gdb_errno = TARGET_EINVAL;
		return -1;
	}

	target_mem32_read(target, cmd, buffer_taddr, buffer_length);

	if (target_check_error(target)) {
		LOG_E("system: memory read error");
		target->tc->gdb_errno = TARGET_EFAULT;
		return -1;
	}

	cmd[buffer_length] = '\0';

	/* Warning: stack overflow risk */
	int32_t result = msh_exec(cmd, buffer_length);

	LOG_I("system: command '%s' return status %d", cmd, result);

	return result;
}

int32_t semihosting_get_command_line(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t buffer_taddr = request->params[0];
	const size_t buffer_length = request->params[1];
	const size_t command_line_length = strlen(target->cmdline) + 1U;

	if (command_line_length > buffer_length) {
		LOG_E("cmdline: buffer too small");
		target->tc->gdb_errno = TARGET_EFAULT;
		return -1;
	}

	target_mem32_write(target, buffer_taddr, target->cmdline, command_line_length);
	if (target_check_error(target)) {
		LOG_E("cmdline: memory write error");
		target->tc->gdb_errno = TARGET_EFAULT;
		return -1;
	}

	target_mem32_write32(target, request->r1 + 4U, command_line_length);
	if (target_check_error(target)) {
		LOG_E("cmdline: memory write error");
		target->tc->gdb_errno = TARGET_EFAULT;
		return -1;
	}

	return 0;
}

int32_t semihosting_heap_info(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t block_taddr = request->r1;

	target_mem32_write(target, block_taddr, target->heapinfo, sizeof(target->heapinfo));

	if (target_check_error(target)) {
		LOG_E("heapinfo: memory write error");
		target->tc->gdb_errno = TARGET_EFAULT;
		return -1;
	}

	return 0;
}

int32_t semihosting_exit(target_s *const target, const semihosting_exit_reason_e reason, const uint32_t status_code)
{
	if (reason == EXIT_REASON_APPLICATION_EXIT)
		LOG_I("exit(%" PRIu32 ")\n", status_code);
	else
		LOG_I("Exception trapped: %x (%" PRIu32 ")\n", reason, status_code);
	target_halt_resume(target, true);

	return 0;
}

int32_t semihosting_elapsed(target_s *const target, const semihosting_s *const request)
{
	const target_addr_t block_taddr = request->r1;

	const uint64_t elapsed = rt_tick_get();
	if (target_mem32_write(target, block_taddr, &elapsed, sizeof(elapsed))) {
		LOG_E("elapsed: memory write error");
		target->tc->gdb_errno = TARGET_EFAULT;
		/* spec: on failure the RETURN REGISTER contains -1, and the PARAMETER REGISTER contains -1 */
		uint32_t result = -1;
		target_reg_write(target, 1, &result, sizeof(result));
		return -1;
	}
	return 0;
}

int32_t semihosting_handle_request(target_s *const target, const semihosting_s *const request, const uint32_t syscall)
{
	switch (syscall) {
	case SEMIHOSTING_SYS_OPEN:
		return semihosting_open(target, request);

	case SEMIHOSTING_SYS_CLOSE:
		return semihosting_close(target, request);

	case SEMIHOSTING_SYS_WRITEC:
		return semihosting_writec(target, request);

	case SEMIHOSTING_SYS_WRITE0:
		return semihosting_write0(target, request);

	case SEMIHOSTING_SYS_WRITE:
		return semihosting_write(target, request);

	case SEMIHOSTING_SYS_READ:
		return semihosting_read(target, request);

	case SEMIHOSTING_SYS_READC:
		return semihosting_readc(target);

	case SEMIHOSTING_SYS_ISERROR:
		return semihosting_is_error(request->params[0]);

	case SEMIHOSTING_SYS_ISTTY:
		return semihosting_isatty(target, request);

	case SEMIHOSTING_SYS_SEEK:
		return semihosting_seek(target, request);

	case SEMIHOSTING_SYS_FLEN:
		return semihosting_file_length(target, request);

	case SEMIHOSTING_SYS_TMPNAM:
		return semihosting_temp_name(target, request);

	case SEMIHOSTING_SYS_REMOVE:
		return semihosting_remove(target, request);

	case SEMIHOSTING_SYS_RENAME:
		return semihosting_rename(target, request);

	case SEMIHOSTING_SYS_CLOCK:
		return semihosting_clock(target);

	case SEMIHOSTING_SYS_TIME:
		return semihosting_time(target);

	case SEMIHOSTING_SYS_SYSTEM:
		return semihosting_system(target, request);

	case SEMIHOSTING_SYS_ERRNO:
		/* Return the last errno we got from GDB */
		return target->tc->gdb_errno;

	case SEMIHOSTING_SYS_GET_CMDLINE:
		return semihosting_get_command_line(target, request);

	case SEMIHOSTING_SYS_HEAPINFO:
		return semihosting_heap_info(target, request);

	case SEMIHOSTING_SYS_EXIT:
		return semihosting_exit(target, request->r1, 0);

	case SEMIHOSTING_SYS_EXIT_EXTENDED:
		return semihosting_exit(target, request->params[0], request->params[1]);

	case SEMIHOSTING_SYS_ELAPSED:
		return semihosting_elapsed(target, request);

	case SEMIHOSTING_SYS_TICKFREQ:
		return RT_TICK_PER_SECOND;

	default:
		LOG_E("semihosting: unimplemented syscall 0x%02" PRIx32, syscall);
		return -1;
	}
}

int32_t semihosting_request(target_s *const target, const uint32_t syscall, const uint32_t r1)
{
	/* Reset the interruption state so we can tell if it was this request that was interrupted */
	target->tc->interrupted = false;

	/* Set up the request block appropriately */
	semihosting_s request = {r1, {0U}};
	if (syscall != SEMIHOSTING_SYS_EXIT)
		target_mem32_read(target, request.params, r1, sizeof(request.params));

	if (syscall != SEMIHOSTING_SYS_ERRNO)
		target->tc->gdb_errno = TARGET_SUCCESS;

	const char *syscall_descr = "";
	if (syscall < ARRAY_LENGTH(semihosting_names) && semihosting_names[syscall] != NULL)
		syscall_descr = semihosting_names[syscall];

	LOG_D("syscall 0x%02x %-12s (0x%" PRIx32 " 0x%" PRIx32 " 0x%" PRIx32 " 0x%" PRIx32 ")", syscall, syscall_descr,
		request.params[0], request.params[1], request.params[2], request.params[3]);

	return semihosting_handle_request(target, &request, syscall);
}

int32_t semihosting_reply(target_controller_s *const tc, const char *const pbuf)
{
	(void)pbuf;
	tc->interrupted = false;
	tc->gdb_errno = TARGET_EUNKNOWN;
	return -1;
}
