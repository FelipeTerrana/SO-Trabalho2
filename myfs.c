/*
*  myfs.c - Implementacao das funcoes relativas ao novo sistema de arquivos
*
*  Autores: Eduardo Pereira do Valle - 201665554AC
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

// 255 significa que os 8 bits sao iguais a 1. Se for diferente de 255 pelo menos um bit e 0, representando
// um bloco livre no disco
#define NON_ZERO_BYTE 255

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



// Retorna o primeiro bit igual a 0 no byte de entrada, procurando do bit menos significativo para o mais significativo.
// Os bits são considerados do 0 ao 7 e, caso todos os bits sejam 1, retorna -1
int __firstZeroBit(unsigned char byte)
{
    if(byte == NON_ZERO_BYTE) return -1;

    int i;
    unsigned char mask = 1;
    for(i=0; i < sizeof(unsigned char); i++)
    {
        if( (mask & byte) == 0 ) return i;
        mask <<= (unsigned char) 1;
    }

    return -1;
}


// Retorna o byte de entrada com o bit na posicao informada transformado em 1. Bits sao contados do menos significativo
// para o mais significativo, contando do 0 ao 7
unsigned char __setBitToOne(unsigned char byte, unsigned int bit)
{
    unsigned char mask = (unsigned char) 1 << bit;
    return byte | mask;
}


// Retorna o byte de entrada com o bit na posicao informada transformado em 0. Bits sao contados do menos significativo
// para o mais significativo, contando do 0 ao 7
unsigned char __setBitToZero(unsigned char byte, unsigned int bit)
{
    unsigned char mask = ((unsigned char) 1 << bit);
    mask = ~mask;
    return byte & mask;
}



// Encontra um bloco livre no disco e o marca como ocupado se este estiver em formato myfs. Retorna -1 se nao houver
// bloco livre ou se o disco nao estiver formatado corretamente
unsigned int __findFreeBlock(Disk *d)
{
    unsigned char buffer[DISK_SECTORDATASIZE];
    if(diskReadSector(d, 0, buffer) == -1) return -1; // Superbloco inicialmente carregado no buffer

    if(buffer[SUPERBLOCK_FSID] != myfsInfo.fsid) return -1;

    unsigned int sectorsPerBlock;
    char2ul(&buffer[SUPERBLOCK_BLOCKSIZE], &sectorsPerBlock);
    sectorsPerBlock /= DISK_SECTORDATASIZE;

    unsigned int numBlocks;
    char2ul(&buffer[SUPERBLOCK_NUM_BLOCKS], &numBlocks);

    unsigned int firstBlock;
    char2ul(&buffer[SUPERBLOCK_FIRST_BLOCK_SECTOR], &firstBlock);

    unsigned int freeSpaceSector;
    char2ul(&buffer[SUPERBLOCK_FREE_SPACE_SECTOR], &freeSpaceSector);

    unsigned int freeSpaceSize = firstBlock - freeSpaceSector;

    unsigned int i;
    for(i = freeSpaceSector; i < freeSpaceSector + freeSpaceSize; i++)
    {
        if(diskReadSector(d, i, buffer) == -1) return -1;

        unsigned int j;
        for(j=0; j < DISK_SECTORDATASIZE; j++)
        {
            int freeBit = __firstZeroBit(buffer[j]);
            if(freeBit != -1)
            {
                unsigned int freeBlock = firstBlock +
                             (i - freeSpaceSector) * DISK_SECTORDATASIZE * 8 * sectorsPerBlock +
                             j * 8 * sectorsPerBlock +
                             freeBit * sectorsPerBlock;

                // Bloco livre excede a regiao de blocos disponiveis, nenhum bloco livre valido foi encontrado
                if((freeBlock - firstBlock) / sectorsPerBlock >= numBlocks) return -1;

                buffer[j] = __setBitToOne(buffer[j], freeBit);
                if(diskWriteSector(d, i, buffer) == -1) return -1;

                return freeBlock;
            }
        }
    }

    return -1;
}



// Dado um bloco em um disco formatado em myfs, marca o bloco como livre para uso. Retorna true (!= 0) se a operacao
// foi bem sucedida e false (0) se algum erro ocorreu no processo
bool __setBlockFree(Disk *d, unsigned int block)
{
    unsigned char buffer[DISK_SECTORDATASIZE];
    if(diskReadSector(d, 0, buffer) == -1) return false;

    if(buffer[SUPERBLOCK_FSID] != myfsInfo.fsid) return false;

    unsigned int sectorsPerBlock;
    char2ul(&buffer[SUPERBLOCK_BLOCKSIZE], &sectorsPerBlock);
    sectorsPerBlock /= DISK_SECTORDATASIZE;

    unsigned int numBlocks;
    char2ul(&buffer[SUPERBLOCK_NUM_BLOCKS], &numBlocks);

    unsigned int firstBlock;
    char2ul(&buffer[SUPERBLOCK_FIRST_BLOCK_SECTOR], &firstBlock);

    unsigned int freeSpaceStartSector;
    char2ul(&buffer[SUPERBLOCK_FREE_SPACE_SECTOR], &freeSpaceStartSector);

    // Bloco de entrada excede a regiao de blocos disponiveis
    if((block - firstBlock) / sectorsPerBlock >= numBlocks) return false;

    unsigned int blockFreeSpaceSector = ((block - firstBlock) / sectorsPerBlock) / (DISK_SECTORDATASIZE * 8);
    if(diskReadSector(d, blockFreeSpaceSector, buffer) == -1) return false;

    unsigned int blockFreeSpaceBit = ((block - firstBlock) / sectorsPerBlock) % (DISK_SECTORDATASIZE * 8);
    buffer[blockFreeSpaceBit / 8] = __setBitToZero(buffer[blockFreeSpaceBit / 8], blockFreeSpaceBit % 8);

    if(diskWriteSector(d, blockFreeSpaceSector, buffer) == -1) return false;

    return true;
}



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

    unsigned int fileSize = inodeGetFileSize(file->inode);
    unsigned int bytesRead = 0;
    unsigned int currentInodeBlockNum = file->currentByte / file->diskBlockSize;
    unsigned int offset = file->currentByte % file->diskBlockSize; // offset em bytes a partir do início do bloco
    unsigned int currentBlock = inodeGetBlockAddr(file->inode, currentInodeBlockNum);
    unsigned char diskBuffer[DISK_SECTORDATASIZE];

    while(bytesRead < nbytes &&
          bytesRead + file->currentByte < fileSize &&
          currentBlock > 0)
    {
        unsigned int sectorsPerBlock = file->diskBlockSize / DISK_SECTORDATASIZE;
        unsigned int firstSector = offset / DISK_SECTORDATASIZE;
        unsigned int firstByteInSector = offset % DISK_SECTORDATASIZE;

        int i;
        for(i = firstSector; i < sectorsPerBlock; i++)
        {
            if(diskReadSector(file->disk, currentBlock + i, diskBuffer) == -1) return -1;

            int j;
            for(j = firstByteInSector;  j < DISK_SECTORDATASIZE &&
                                        bytesRead < nbytes &&
                                        bytesRead + file->currentByte < fileSize;  j++)
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
    FileInfo* file = openFiles[fd];

    if(file == NULL) return -1;

    // Libera apenas o ponteiro para o Inode pois o ponteiro para Disk ja existia antes da alocacao do FileInfo
    free(file->inode);

    free(file);
    openFiles[fd] = NULL;
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