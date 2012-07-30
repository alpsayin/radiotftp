/* 
 * File:   lock.h
 * Author: alpsayin
 *
 * Created on April 1, 2012, 12:18 AM
 */

#ifndef LOCK_H
#define	LOCK_H

#ifdef	__cplusplus
extern "C" {
#endif

char *mbasename(char *s, char *res, int reslen);
int lockfile_create(void);
void lockfile_remove(void);
int have_lock_dir(char* dial_tty);
int get_lock(char* dial_tty);
int getLockRetries(void);
int decrementLockRetries(void);


#ifdef	__cplusplus
}
#endif

#endif	/* LOCK_H */

