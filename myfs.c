/*
*  myfs.c - Implementacao das funcoes relativas ao novo sistema de arquivos
*
*  Autores: Eduardo Pereira do Valle - TODO colocar matricula
*           Felipe Terrana Cazetta - 201635026
*           Matheus Brinati Altomar - TODO colocar matricula
*           Vinicius Alberto Alves da Silva - TODO colocar matricula
*
*  Projeto: Trabalho Pratico II - Sistemas Operacionais
*  Organizacao: Universidade Federal de Juiz de Fora
*  Departamento: Dep. Ciencia da Computacao
*
*/

#include "myfs.h"

#include <stdlib.h>
#include <stdbool.h>
#include "disk.h"
#include "inode.h"
#include "util.h"
#include "vfs.h"

/// Posicoes das informacoes do disco no superbloco. O superbloco sempre ocupa o setor 0
#define SUPERBLOCK_BLOCKSIZE 0
#define SUPERBLOCK_FSID sizeof(unsigned int)
#define SUPERBLOCK_FREE_SPACE_SECTOR (sizeof(unsigned int) + sizeof(char))
#define SUPERBLOCK_FIRST_BLOCK_SECTOR (2 * sizeof(unsigned int) + sizeof(char))
#define SUPERBLOCK_NUM_BLOCKS (3 * sizeof(unsigned int) + sizeof(char))

int myfsSlot = -1;

FSInfo myfsInfo =
{
    0,      // fsid
    "myfs", // fsname
    myfsIsIdle,
    myfsFormat,
    myfsOpen,
    myfsRead,
    myfsWrite,
    myfsClose,
    myfsOpendir,
    myfsReaddir,
    myfsLink,
    myfsUnlink,
    myfsClosedir
};

FileInfo* openFiles[MAX_FDS] = {NULL};



int installMyFS()
{
    myfsSlot = vfsRegisterFS(&myfsInfo);
    return 0;
}



int myfsIsIdle(Disk *d)
{
    int i;

    for(i=0; i < MAX_FDS; i++)
    {
        FileInfo* file = openFiles[i];
        if(file != NULL && diskGetId(d) == diskGetId(file->disk)) return false;
    }

    return true;
}




int myfsFormat(Disk *d, unsigned int blockSize)
{
    unsigned char superblock[DISK_SECTORDATASIZE] = {0};

    ul2char(blockSize, &superblock[SUPERBLOCK_BLOCKSIZE]);
    superblock[SUPERBLOCK_FSID] = myfsInfo.fsid;

    unsigned int numInodes = (diskGetSize(d) / blockSize) / 8;

    unsigned int i;
    for(i=1; i <= numInodes; i++)
    {
        Inode* inode = inodeCreate(i, d);
        if(inode == NULL) return -1;
        free(inode);
    }

    // Espaco livre e representado por um mapa de bits, em que um bit 0 significa que o bloco correspondente e livre
    // e um bit 1 significa em uso
    unsigned int freeSpaceSector = inodeAreaBeginSector() + numInodes / inodeNumInodesPerSector();
    unsigned int freeSpaceSize   = (diskGetSize(d) / blockSize) / (sizeof(unsigned char) * 8 * DISK_SECTORDATASIZE);

    ul2char(freeSpaceSector, &superblock[SUPERBLOCK_FREE_SPACE_SECTOR]);

    unsigned int firstBlockSector = freeSpaceSector + freeSpaceSize;
    unsigned int numBlocks        = (diskGetNumSectors(d) - firstBlockSector) / (blockSize / DISK_SECTORDATASIZE);

    ul2char(firstBlockSector, &superblock[SUPERBLOCK_FIRST_BLOCK_SECTOR]);
    ul2char(numBlocks, &superblock[SUPERBLOCK_NUM_BLOCKS]);

    if(diskWriteSector(d, 0, superblock) == -1 ) return -1;

    unsigned char freeSpace[DISK_SECTORDATASIZE] = {0};
    for(i=0; i < freeSpaceSize; i++)
    {
        if(diskWriteSector(d, freeSpaceSector + i, freeSpace) == -1) return -1;
    }

    return numBlocks > 0 ? numBlocks : -1;
}




int myfsOpen(Disk *d, const char *path)
{
    // TODO myfsOpen
    return 0;
}




int myfsRead(int fd, char *buf, unsigned int nbytes)
{
    FileInfo* file = openFiles[fd];
    if(file == NULL) return -1;

    unsigned int bytesRead = 0;
    unsigned int currentInodeBlockNum = file->currentByte / file->diskBlockSize;
    unsigned int offset = file->currentByte % file->diskBlockSize; // offset em bytes a partir do início do bloco
    unsigned int currentBlock = inodeGetBlockAddr(file->inode, currentInodeBlockNum);
    unsigned char diskBuffer[DISK_SECTORDATASIZE];

    while(bytesRead < nbytes && currentBlock > 0)
    {
        unsigned int sectorsPerBlock = file->diskBlockSize / DISK_SECTORDATASIZE;
        unsigned int firstSector = offset / DISK_SECTORDATASIZE;
        unsigned int firstByteInSector = offset % DISK_SECTORDATASIZE;

        int i;
        for(i = firstSector; i < sectorsPerBlock; i++)
        {
            if(diskReadSector(file->disk, currentBlock + i, diskBuffer) == -1) return -1;

            int j;
            for(j = firstByteInSector; j < DISK_SECTORDATASIZE && bytesRead < nbytes; j++)
            {
                buf[bytesRead] = diskBuffer[j];
                bytesRead++;
            }

            firstByteInSector = 0;
        }

        offset = 0;
        currentInodeBlockNum++;
        currentBlock = inodeGetBlockAddr(file->inode, currentInodeBlockNum);
    }

    file->currentByte += bytesRead;

    return bytesRead;
}




int myfsWrite(int fd, const char *buf, unsigned int nbytes)
{
    // TODO myfsWrite
    return 0;
}




int myfsClose(int fd)
{
    // TODO myfsClose
    return 0;
}




int myfsOpendir(Disk *d, const char *path)
{
    // TODO myfsOpendir
    return 0;
}




int myfsReaddir(int fd, char *filename, unsigned int *inumber)
{
    // TODO myfsReaddir
    return 0;
}




int myfsLink(int fd, const char *filename, unsigned int inumber)
{
    // TODO myfsLink
    return 0;
}




int myfsUnlink(int fd, const char *filename)
{
    // TODO myfsUnlink
    return 0;
}




int myfsClosedir(int fd)
{
    // TODO myfsClosedir
    return 0;
}