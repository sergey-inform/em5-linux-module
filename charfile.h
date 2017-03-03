#ifndef EM5_charfile_H
#define EM5_charfile_H

int em5_charfile_init( int major, int minor);
void em5_charfile_free( void);
void notify_readers( void);
void kill_readers( void);

#endif /*EM5_charfile_H*/
