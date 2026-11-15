/* Arcade unpacked runtime, the C twin of arcade_link::runtime::relaunch_unpacked
 * (RT-7, AUDIT/memory.md U1). Header-only; the Qt apps (Shelf, Wheel) and the
 * Flutter Linux runner (Clipboard) call arcade_relaunch_unpacked() first thing
 * in main, before QApplication / the Flutter engine.
 *
 * An app running from its AppImage's FUSE mount copies the image's contents
 * once to <data>/arcade/runtime/<app>-<version>-<key>/ and runs the same
 * command line from there, so no FUSE helper stays resident and file pages
 * come from the real disk. $APPIMAGE is kept, so everything that advertises or
 * restarts the app still uses the installed AppImage.
 *
 * Returns -1 when nothing is to be done and the app keeps running (not in an
 * AppImage, ARCADE_UNPACKED already set, the executable is outside $APPDIR,
 * copying or starting the copy failed). Otherwise the copy has been started
 * and the return value is the code the caller must exit with: with wait
 * (one-shot CLI calls) the copy's own exit status, without it (resident
 * starts) 0 at once, so the mount goes away.
 *
 * Differences from the Rust function: none in behaviour. The Rust side also
 * removes MALLOC_ARENA_MAX from the copy's environment, but only when
 * runtime::cap_malloc_arenas set it in that very process; no C app does, so
 * the variable is passed on untouched (a user's own setting included).
 *
 * Folder names are the ones Arcade Tools' runtime_gc recognises: key is 16
 * lowercase hex digits (FNV-1a 64 over the image's size and mtime), copies in
 * progress are .<name>.tmp-<pid>. Linux only; elsewhere it returns -1.
 *
 * Under strict -std=c11 include this header before any other (it defines
 * _GNU_SOURCE for POSIX declarations); C++ compilers define it already. */
#ifndef ARCADE_UNPACKED_H
#define ARCADE_UNPACKED_H

#define ARCADE_UNPACKED_ENV "ARCADE_UNPACKED"

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif

/* Next path component of *p (empty and "." components skipped), 0 at the end. */
static inline int arcade_unpacked_component(const char **p, const char **start, size_t *len)
{
    for (;;) {
        while (**p == '/')
            ++*p;
        if (!**p)
            return 0;
        *start = *p;
        while (**p && **p != '/')
            ++*p;
        *len = (size_t)(*p - *start);
        if (!(*len == 1 && (*start)[0] == '.'))
            return 1;
    }
}

/* Path::starts_with: whole components, not a string prefix. */
static inline int arcade_unpacked_inside(const char *path, const char *root)
{
    const char *a = path, *b = root;
    const char *as = 0, *bs = 0;
    size_t al = 0, bl = 0;
    int seen = 0;
    while (arcade_unpacked_component(&b, &bs, &bl)) {
        seen = 1;
        if (!arcade_unpacked_component(&a, &as, &al) || al != bl || memcmp(as, bs, al) != 0)
            return 0;
    }
    return seen || (root[0] == '/' && path[0] == '/');
}

static inline void arcade_unpacked_rm_rf(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return;
    if (!S_ISDIR(st.st_mode)) {
        unlink(path);
        return;
    }
    chmod(path, 0700);
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != 0) {
            char child[PATH_MAX];
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                continue;
            if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) < (int)sizeof child)
                arcade_unpacked_rm_rf(child);
        }
        closedir(dir);
    }
    rmdir(path);
}

/* create_dir_all: 0 or -1 with errno. */
static inline int arcade_unpacked_mkdir_p(const char *path)
{
    char buf[PATH_MAX];
    struct stat st;
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof buf) {
        errno = n ? ENAMETOOLONG : ENOENT;
        return -1;
    }
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i <= n; ++i) {
        if (buf[i] != '/' && buf[i] != '\0')
            continue;
        buf[i] = '\0';
        if (mkdir(buf, 0777) != 0 && errno != EEXIST)
            return -1;
        if (i < n)
            buf[i] = '/';
    }
    if (stat(path, &st) != 0)
        return -1;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

/* fs::copy: contents and permission bits of one regular file. */
static inline int arcade_unpacked_copy_file(const char *from, const char *to, const struct stat *st)
{
    if (!S_ISREG(st->st_mode)) {
        errno = EINVAL;
        return -1;
    }
    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) {
        int e = errno;
        close(in);
        errno = e;
        return -1;
    }
    char buf[65536];
    int err = 0;
    for (;;) {
        ssize_t got = read(in, buf, sizeof buf);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            err = got < 0 ? errno : 0;
            break;
        }
        for (ssize_t done = 0; done < got;) {
            ssize_t put = write(out, buf + done, (size_t)(got - done));
            if (put < 0 && errno == EINTR)
                continue;
            if (put < 0) {
                err = errno;
                break;
            }
            done += put;
        }
        if (err)
            break;
    }
    if (!err && fchmod(out, st->st_mode & 07777) != 0)
        err = errno;
    close(in);
    if (close(out) != 0 && !err)
        err = errno;
    errno = err;
    return err ? -1 : 0;
}

/* Directories and file permissions are kept (the directory's own mode is set
 * once it is filled, so a read-only source directory can still be written),
 * symlinks are recreated as symlinks, regular files are copied. */
static inline int arcade_unpacked_copy_tree(const char *from, const char *to)
{
    struct stat top;
    if (stat(from, &top) != 0)
        return -1;
    if (arcade_unpacked_mkdir_p(to) != 0)
        return -1;
    DIR *dir = opendir(from);
    if (!dir)
        return -1;
    struct dirent *e;
    int err = 0;
    while (!err && (e = readdir(dir)) != 0) {
        char src[PATH_MAX], dst[PATH_MAX];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (snprintf(src, sizeof src, "%s/%s", from, e->d_name) >= (int)sizeof src
            || snprintf(dst, sizeof dst, "%s/%s", to, e->d_name) >= (int)sizeof dst) {
            err = ENAMETOOLONG;
        } else if (lstat(src, &st) != 0) {
            err = errno;
        } else if (S_ISLNK(st.st_mode)) {
            char target[PATH_MAX];
            ssize_t n = readlink(src, target, sizeof target - 1);
            if (n < 0) {
                err = errno;
            } else {
                target[n] = '\0';
                if (symlink(target, dst) != 0)
                    err = errno;
            }
        } else if (S_ISDIR(st.st_mode)) {
            if (arcade_unpacked_copy_tree(src, dst) != 0)
                err = errno;
        } else if (arcade_unpacked_copy_file(src, dst, &st) != 0) {
            err = errno;
        }
    }
    closedir(dir);
    if (!err && chmod(to, top.st_mode & 07777) != 0)
        err = errno;
    errno = err;
    return err ? -1 : 0;
}

static inline int arcade_unpacked_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Copies appdir to dir once: into a temporary sibling, then renamed, with a
 * .complete marker, so a crash mid-copy never leaves a half copy in use.
 * 0 or -1 with errno. */
static inline int arcade_unpacked_ensure(const char *appdir, const char *dir)
{
    char complete[PATH_MAX], parent[PATH_MAX], tmp[PATH_MAX], marker[PATH_MAX];
    if (snprintf(complete, sizeof complete, "%s/.complete", dir) >= (int)sizeof complete) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (arcade_unpacked_is_file(complete))
        return 0;
    const char *slash = strrchr(dir, '/');
    size_t plen = slash ? (size_t)(slash - dir) : 0;
    if (!slash || plen == 0 || plen >= sizeof parent) {
        errno = slash ? ENAMETOOLONG : EINVAL;
        return -1;
    }
    memcpy(parent, dir, plen);
    parent[plen] = '\0';
    if (arcade_unpacked_mkdir_p(parent) != 0)
        return -1;
    if (snprintf(tmp, sizeof tmp, "%s/.%s.tmp-%ld", parent, slash + 1, (long)getpid()) >= (int)sizeof tmp
        || snprintf(marker, sizeof marker, "%s/.complete", tmp) >= (int)sizeof marker) {
        errno = ENAMETOOLONG;
        return -1;
    }
    arcade_unpacked_rm_rf(tmp);
    if (arcade_unpacked_copy_tree(appdir, tmp) != 0) {
        int e = errno;
        arcade_unpacked_rm_rf(tmp);
        errno = e;
        return -1;
    }
    int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0 || close(fd) != 0) {
        int e = errno;
        arcade_unpacked_rm_rf(tmp);
        errno = e;
        return -1;
    }
    if (rename(tmp, dir) == 0)
        return 0;
    int e = errno;
    arcade_unpacked_rm_rf(tmp);
    if (arcade_unpacked_is_file(complete))
        return 0; /* another process won the race: use its copy */
    errno = e;
    return -1;
}

/* A non-empty absolute directory from the environment, as paths.rs env_dir. */
static inline const char *arcade_unpacked_env_dir(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] == '/' ? v : 0;
}

/* <data>/arcade/runtime as UserDirs::discover().data.join("arcade/runtime"):
 * XDG_DATA_HOME, else <home>/.local/share, home being HOME or, when that is not
 * an absolute path, the temp dir (TMPDIR or /tmp). ARCADE_HOME does not move it. */
static inline int arcade_unpacked_root(char *out, size_t cap)
{
    const char *data = arcade_unpacked_env_dir("XDG_DATA_HOME");
    int n;
    if (data) {
        n = snprintf(out, cap, "%s/arcade/runtime", data);
    } else {
        const char *home = arcade_unpacked_env_dir("HOME");
        if (!home) {
            home = getenv("TMPDIR");
            if (!home)
                home = "/tmp";
        }
        n = snprintf(out, cap, "%s/.local/share/arcade/runtime", home);
    }
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static inline void arcade_unpacked_fnv(uint64_t *h, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        *h ^= (v >> (8 * i)) & 0xff;
        *h *= 0x100000001b3ULL;
    }
}

/* Identifies one AppImage file: its size and modification time. A new version
 * or an update in place gets a new folder. 0 or -1. */
static inline int arcade_unpacked_key(const char *appimage, char out[17])
{
    struct stat st;
    if (stat(appimage, &st) != 0)
        return -1;
    uint64_t h = 0xcbf29ce484222325ULL;
    arcade_unpacked_fnv(&h, (uint64_t)st.st_size);
    arcade_unpacked_fnv(&h, (uint64_t)st.st_mtim.tv_sec);
    arcade_unpacked_fnv(&h, (uint64_t)st.st_mtim.tv_nsec);
    snprintf(out, 17, "%016llx", (unsigned long long)h);
    return 0;
}

/* Runs dir/AppRun with argv[1..] and the unpacked environment. The child makes
 * every descriptor above stdio close-on-exec first (childenv::detach_from_image).
 * Returns the pid, or -1 when the copy could not be started. */
static inline pid_t arcade_unpacked_spawn(const char *dir, char *const argv[])
{
    char apprun[PATH_MAX];
    if (snprintf(apprun, sizeof apprun, "%s/AppRun", dir) >= (int)sizeof apprun)
        return -1;
    size_t nargs = 1, nenv = 0;
    if (argv && argv[0])
        while (argv[nargs])
            ++nargs;
    while (environ && environ[nenv])
        ++nenv;
    char **args = (char **)malloc((nargs + 2) * sizeof *args);
    char **env = (char **)malloc((nenv + 3) * sizeof *env);
    char *appdir_var = (char *)malloc(strlen(dir) + 8);
    pid_t pid = -1;
    int errpipe[2] = {-1, -1};
    if (!args || !env || !appdir_var)
        goto done;
    sprintf(appdir_var, "APPDIR=%s", dir);
    args[0] = apprun;
    for (size_t i = 1; i < nargs; ++i)
        args[i] = argv[i];
    args[nargs] = 0;
    {
        size_t k = 0;
        for (size_t i = 0; i < nenv; ++i) {
            const char *v = environ[i];
            if (strncmp(v, "APPDIR=", 7) == 0 || strncmp(v, ARCADE_UNPACKED_ENV "=", sizeof ARCADE_UNPACKED_ENV) == 0)
                continue;
            env[k++] = environ[i];
        }
        env[k++] = appdir_var;
        env[k++] = (char *)(ARCADE_UNPACKED_ENV "=1");
        env[k] = 0;
    }
    if (pipe2(errpipe, O_CLOEXEC) != 0)
        goto done;
    pid = fork();
    if (pid == 0) {
        int e;
        if (syscall(SYS_close_range, 3U, ~0U, CLOSE_RANGE_CLOEXEC) != 0) {
            /* Kernels before 5.11: one descriptor at a time. */
            long max = sysconf(_SC_OPEN_MAX);
            max = max < 1024 ? 1024 : (max > 65536 ? 65536 : max);
            for (int fd = 3; fd < (int)max; ++fd) {
                int flags = fcntl(fd, F_GETFD);
                if (flags >= 0 && !(flags & FD_CLOEXEC))
                    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
            }
        }
        execve(apprun, args, env);
        e = errno;
        if (write(errpipe[1], &e, sizeof e) < 0) {}
        _exit(127);
    }
    if (pid > 0) {
        int e = 0;
        ssize_t got;
        close(errpipe[1]);
        errpipe[1] = -1;
        do {
            got = read(errpipe[0], &e, sizeof e);
        } while (got < 0 && errno == EINTR);
        if (got > 0) { /* exec failed: reap the child, nothing was started */
            while (waitpid(pid, 0, 0) < 0 && errno == EINTR) {}
            pid = -1;
        }
    }
done:
    if (errpipe[0] >= 0)
        close(errpipe[0]);
    if (errpipe[1] >= 0)
        close(errpipe[1]);
    free(args);
    free(env);
    free(appdir_var);
    return pid > 0 ? pid : -1;
}
#endif /* __linux__ */

static inline int arcade_relaunch_unpacked(const char *app_id, const char *version, int wait, char *const argv[])
{
#if defined(__linux__)
    char exe[PATH_MAX], root[PATH_MAX], dir[PATH_MAX], key[17];
    const char *appimage = getenv("APPIMAGE");
    const char *appdir = getenv("APPDIR");
    ssize_t n;
    pid_t pid;
    if (getenv(ARCADE_UNPACKED_ENV))
        return -1;
    if (!appimage || !*appimage || !appdir || !*appdir)
        return -1;
    n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return -1;
    exe[n] = '\0';
    if (!arcade_unpacked_inside(exe, appdir))
        return -1;
    if (arcade_unpacked_key(appimage, key) != 0 || arcade_unpacked_root(root, sizeof root) != 0)
        return -1;
    while (strncmp(app_id, "arcade.", 7) == 0)
        app_id += 7;
    if (snprintf(dir, sizeof dir, "%s/%s-%s-%s", root, app_id, version, key) >= (int)sizeof dir)
        return -1;
    if (arcade_unpacked_ensure(appdir, dir) != 0) {
        fprintf(stderr, "arcade-link: running from the AppImage mount (%s: %s (os error %d))\n", dir, strerror(errno), errno);
        return -1;
    }
    pid = arcade_unpacked_spawn(dir, argv);
    if (pid < 0)
        return -1;
    if (wait) {
        int status = 0;
        pid_t r;
        do {
            r = waitpid(pid, &status, 0);
        } while (r < 0 && errno == EINTR);
        return r == pid && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    return 0;
#else
    (void)app_id;
    (void)version;
    (void)wait;
    (void)argv;
    return -1;
#endif
}

#endif
