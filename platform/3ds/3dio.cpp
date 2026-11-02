/*
 * The file system layer.
 *
 * The old port was written against libctru v0.6, which had FS_makePath,
 * PATH_CHAR and FS_archive. None of those exist in current libctru: the
 * paths are now FS_Path values, made with fsMakePath, and the archive is
 * a u64 handle rather than a struct. This is the same interface, written
 * against the current library.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <3ds/types.h>
#include <3ds/services/fs.h>
#include "io.h"
#include "error.h"

static FS_Archive sdmcArchive;

struct FileHandle {
    Handle handle;
    size_t head;
};

struct DirStruct {
    Handle handle;
    struct dirent activeEntry;
};

static char fs_cwd[MAX_FILENAME_LEN];
static DirStruct dir;

/* private functions */

static void fs_relativePath(char* dest, const char* src) {
    bool back = false;
    if (strcmp(src, "..") == 0 || strcmp(src, "../") == 0) {
        if (dest != fs_cwd)
            strcpy(dest, fs_cwd);
        back = true;
    }
    else if (src[0] == '/')
        strcpy(dest, src);
    else {
        if (dest != fs_cwd)
            strcpy(dest, fs_cwd);
        if (dest[strlen(dest)-1] != '/')
            strcat(dest, "/");
        strcat(dest, src);
    }

    while (strlen(dest) > 1 &&
            strrchr(dest, '/') != 0 &&
            strrchr(dest, '/') == dest+strlen(dest)-1)
        *strrchr(dest, '/') = '\0';

    if (back) {
        if (strrchr(dest, '/') != 0) {
            *(strrchr(dest, '/')) = '\0';
            if (strcmp(dest, "") == 0)
                strcpy(dest, "/");
        }
    }
}

/* public functions */

void fs_init() {
    sdmcArchive = 0;
    FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));

    strcpy(fs_cwd, "");
    dir.handle = 0;
    fs_chdir("/");
}

FileHandle* file_open(const char* filename, const char* flags) {
#ifdef EMBEDDED_ROM
    return NULL;
#endif

    u32 openFlags = 0;

    for (uint i=0; i<strlen(flags); i++) {
        char c = flags[i];
        switch(c) {
            case 'r':
                openFlags |= FS_OPEN_READ;
                break;
            case 'w':
                openFlags |= FS_OPEN_WRITE | FS_OPEN_CREATE;
                break;
            case '+':
                if (openFlags & FS_OPEN_READ)
                    openFlags |= FS_OPEN_WRITE;
                else if (openFlags & FS_OPEN_WRITE)
                    openFlags |= FS_OPEN_READ | FS_OPEN_CREATE;
                break;
        }
    }

    char buffer[MAX_FILENAME_LEN];
    fs_relativePath(buffer, filename);

    FileHandle* fileHandle = (FileHandle*)malloc(sizeof(FileHandle));
    if (!fileHandle)
        return NULL;

    Result res = FSUSER_OpenFile(&fileHandle->handle, sdmcArchive,
            fsMakePath(PATH_ASCII, buffer), openFlags, 0);

    if (R_FAILED(res)) {
        free(fileHandle);
        return NULL;
    }

    fileHandle->head = 0;
    return fileHandle;
}

void file_close(FileHandle* fileHandle) {
    FSFILE_Close(fileHandle->handle);
    free(fileHandle);
}

void file_read(void* dest, int bs, int size, FileHandle* fileHandle) {
    u32 bytesRead;
    FSFILE_Read(fileHandle->handle, &bytesRead, fileHandle->head, dest, bs*size);
    fileHandle->head += bytesRead;
}

void file_write(const void* src, int bs, int size, FileHandle* fileHandle) {
    u32 bytesWritten;
    FSFILE_Write(fileHandle->handle, &bytesWritten, fileHandle->head, src, bs*size,
            FS_WRITE_FLUSH);
    fileHandle->head += bytesWritten;
}

/* A write that leaves the bytes in the file system for now. See io.h for
 * why a program that writes often wants this one. */
void file_write_buffered(const void* src, int bs, int size, FileHandle* fileHandle) {
    u32 bytesWritten;
    FSFILE_Write(fileHandle->handle, &bytesWritten, fileHandle->head, src, bs*size, 0);
    fileHandle->head += bytesWritten;
}

/* Put what has been written on the card. */
void file_flush(FileHandle* fileHandle) {
    FSFILE_Flush(fileHandle->handle);
}

void file_gets(char* buffer, int bufferSize, FileHandle* fileHandle) {
    u32 bytesRead;
    char* ptr = buffer;

    while (bufferSize > 1) {
        char c;
        FSFILE_Read(fileHandle->handle, &bytesRead, fileHandle->head, &c, 1);
        fileHandle->head += bytesRead;
        if (bytesRead != 1)
            break;

        *(ptr++) = c;
        bufferSize--;

        if (c == '\n' || c == '\0')
            break;
    }

    // bufferSize >= 1
    if (*ptr != '\0') {
        *(ptr++) = '\0';
        bufferSize--;
    }
}

void file_putc(char c, FileHandle* fileHandle) {
    file_write(&c, 1, 1, fileHandle);
}

int file_tell(FileHandle* fileHandle) {
    return fileHandle->head;
}

void file_seek(FileHandle* fileHandle, int pos, int origin) {
    switch (origin) {
        case SEEK_SET:
            fileHandle->head = pos;
            break;
        case SEEK_CUR:
            fileHandle->head += pos;
            break;
        case SEEK_END:
            fileHandle->head = file_getSize(fileHandle) + pos;
            break;
    }
}

int file_getSize(FileHandle* fileHandle) {
    u64 size;
    FSFILE_GetSize(fileHandle->handle, &size);
    return size;
}

void file_setSize(FileHandle* fileHandle, size_t size) {
    FSFILE_SetSize(fileHandle->handle, size);
}

void file_printf(FileHandle* fileHandle, const char* format, ...) {
    char buffer[512];

    va_list args;
    va_start(args, format);

    vsnprintf(buffer, 512, format, args);
    va_end(args);

    file_write(buffer, 1, strlen(buffer), fileHandle);
}

bool file_exists(const char* filename) {
    FileHandle* h = file_open(filename, "r");
    if (h == NULL)
        return false;
    else {
        file_close(h);
        return true;
    }
}

void fs_deleteFile(const char* filename) {
    char buffer[MAX_FILENAME_LEN];
    fs_relativePath(buffer, filename);
    FSUSER_DeleteFile(sdmcArchive, fsMakePath(PATH_ASCII, buffer));
}

struct dirent* fs_readdir() {
    u32 numEntries = 0;
    FS_DirectoryEntry entry;
    FSDIR_Read(dir.handle, &numEntries, 1, &entry);

    if (numEntries == 0)
        return 0;

    /* The name comes back as UTF-16. The rest of the emulator works in
     * bytes, so it is narrowed here; a name that is not ASCII becomes a
     * question mark rather than nonsense. */
    for (int i=0; i<MAX_FILENAME_LEN; i++) {
        u16 c = entry.name[i];
        dir.activeEntry.d_name[i] = (c < 0x80) ? (char)c : '?';
        if (c == '\0')
            break;
    }
    dir.activeEntry.d_type = 0;
    if (entry.attributes & FS_ATTRIBUTE_DIRECTORY)
        dir.activeEntry.d_type |= DT_DIR;

    return &dir.activeEntry;
}

void fs_getcwd(char* dest, size_t maxLen) {
    strncpy(dest, fs_cwd, maxLen);
}

void fs_chdir(const char* s) {
    char buffer[MAX_FILENAME_LEN];
    fs_relativePath(buffer, s);

    if (dir.handle != 0)
        FSDIR_Close(dir.handle);

    Handle handle = 0;
    Result res = FSUSER_OpenDirectory(&handle, sdmcArchive,
            fsMakePath(PATH_ASCII, buffer));

    if (R_FAILED(res)) {
        FSUSER_OpenDirectory(&dir.handle, sdmcArchive,
                fsMakePath(PATH_ASCII, fs_cwd));
        return;
    }

    dir.handle = handle;
    strcpy(fs_cwd, buffer);
}
