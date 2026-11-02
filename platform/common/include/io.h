#pragma once
#include <dirent.h>
#include <unistd.h>
#include <string.h>

#define MAX_FILENAME_LEN 256

struct FileHandle;

#ifdef C_IO_FUNCTIONS

#define DirStruct DIR

struct FileHandle {
    FILE* file;
    char* filename;
    char flags[10];
};

#else
struct DirStruct;
#endif

void        fs_init();
FileHandle* file_open(const char*, const char*);    // Returns NULL if error occurs
void        file_close(FileHandle*);
void        file_read(void*, int, int, FileHandle*);
void        file_write(const void*, int, int, FileHandle*);

/*
 * A write that does not flush, and a flush of its own.
 *
 * file_write() asks the card to put the bytes on the card, and on a slow
 * card that costs tens of milliseconds - longer than a frame. A file that
 * is written in small pieces, a log of a running program for instance,
 * should not pay that on every piece: it can write what it has and flush
 * when it wants the bytes to be there.
 *
 * The file system still holds the bytes until the flush, so a file closed
 * without one is not lost - the card only gets them when the file is
 * closed. A program that is killed does lose them.
 */
void        file_write_buffered(const void*, int, int, FileHandle*);
void        file_flush(FileHandle*);

void        file_gets(char*, int, FileHandle*);
void        file_putc(char, FileHandle*);

int         file_tell(FileHandle*);
void        file_seek(FileHandle*, int, int);
int         file_getSize(FileHandle*);
void        file_setSize(FileHandle*, size_t);

void        file_printf(FileHandle*, const char*, ...);

bool        file_exists(const char* filename);

void        fs_deleteFile(const char* filename);

struct dirent* fs_readdir(); // reads from current working directory

void        fs_getcwd(char*, size_t);
void        fs_chdir(const char*);
