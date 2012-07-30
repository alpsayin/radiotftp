
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

#include "lock.h"

#define P_LOCK "/var/lock"

/*
 * Find out name to use for lockfile when locking tty.
 */
static char lockfile[128]; /* UUCP lock file of terminal */
static char username[16];
static int pid;
static int retry=6;

int getLockRetries(void)
{
    return retry;
}
int decrementLockRetries(void)
{
    retry--;
    return retry;
}

char *mbasename(char *s, char *res, int reslen)
{
    char *p;

    if(strncmp(s, "/dev/", 5) == 0)
    {
        /* In /dev */
        strncpy(res, s + 5, reslen - 1);
        res[reslen - 1]=0;
        for(p=res; *p; p++)
            if(*p == '/')
                *p='_';
    }
    else
    {
        /* Outside of /dev. Do something sensible. */
        if((p=strrchr(s, '/')) == NULL)
            p=s;
        else
            p++;
        strncpy(res, p, reslen - 1);
        res[reslen - 1]=0;
    }
    return res;
}

int lockfile_create(void)
{
    int fd, n;
    char buf[81];

    n=umask(022);
    /* Create lockfile compatible with UUCP-1.2  and minicom */
    if((fd=open(lockfile, O_WRONLY | O_CREAT | O_EXCL, 0666)) < 0)
    {
        return 0;
    }
    else
    {
        snprintf(buf, sizeof (buf), "%05d tty_talk %.20s\n", (int)getpid(),
                 username);

        if(write(fd, buf, strlen(buf)) <= 0)
        {
            fputs("couldn't write to lockfile\n", stderr);
        }
        close(fd);
    }
    umask(n);
    return 1;
}

void lockfile_remove(void)
{
    if(lockfile[0])
        unlink(lockfile);
}

int have_lock_dir(char* dial_tty)
{
    struct stat stt;
    char buf[128];

    if(stat(P_LOCK, &stt) == 0)
    {

        snprintf(lockfile, sizeof (lockfile),
                 "%s/LCK..%s",
                 P_LOCK, mbasename(dial_tty, buf, sizeof (buf)));
    }
    else
    {
        fprintf(stderr, "Lock directory %s does not exist\n", P_LOCK);
        exit(-1);
    }
    return 1;
}

int get_lock(char* dial_tty)
{
    char buf[128];
    int fd, n=0;

    have_lock_dir(dial_tty);

    if((fd=open(lockfile, O_RDONLY)) >= 0)
    {
        n=read(fd, buf, 127);
        close(fd);
        if(n > 0)
        {
            pid= -1;
            if(n == 4)
                /* Kermit-style lockfile. */
                pid= *((int *)buf);
            else
            {
                /* Ascii lockfile. */
                buf[n]=0;
                sscanf(buf, "%d", &pid);
            }
            if(pid > 0 && kill((pid_t)pid, 0) < 0 &&
               errno == ESRCH)
            {
                fprintf(stderr, "Lockfile is stale. Overriding it..\n");
                sleep(1);
                unlink(lockfile);
            }
            else
                n=0;
        }
        if(n == 0)
        {
            if(retry == 1) /* Last retry */
                fprintf(stderr, "Device %s is locked.\n", dial_tty);
            return 0;
        }
    }
    lockfile_create();
    return 1;
}


