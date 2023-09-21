#ifndef FAT32_H
#define FAT32_H

class CFat32 {
  public:
    bool  begin();
    void  listDir(const char * dirname, uint8_t levels);
    void  createDir(const char * path);
    void  removeDir(const char * path);
    void  readFile(const char * path);
    void  writeFile(const char * path, const char * message);
    void  appendFile(const char * path, const char * message);
    void  renameFile(const char * path1, const char * path2);
    void  deleteFile(const char * path);
    void  testFileIO(const char * path);
};

extern CFat32 Fat32;

#endif
