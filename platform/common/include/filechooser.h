#pragma once
#include <string>

/* What an entry of the file list is. */
#define FLAG_DIRECTORY  1
#define FLAG_SUSPENDED  2
#define FLAG_ROM        4

struct FileChooserState {
    int selection;
    std::string directory;
};

char* startFileChooser(const char* extensions[], bool romExtensions, bool canQuit=false);
bool isFileChooserOn();
void setFileChooserMatchFile(const char* filename); // Auto-select this file in the directory

void saveFileChooserState(FileChooserState* state);
void loadFileChooserState(FileChooserState* state);

extern FileChooserState romChooserState, borderChooserState;
