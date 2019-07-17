/*
*  myfsInternalFunctions.c - Implementacao das funcoes auxiliares usadas por myfs
*
*  Autores: Eduardo Pereira do Valle - 201665554AC
*           Felipe Terrana Cazetta - 201635026
*           Matheus Brinati Altomar - 201665564C
*           Vinicius Alberto Alves da Silva - 201665558AC
*
*  Projeto: Trabalho Pratico II - Sistemas Operacionais
*  Organizacao: Universidade Federal de Juiz de Fora
*  Departamento: Dep. Ciencia da Computacao
*
*/

#include "myfsInternalFunctions.h"

#include <stdlib.h>
#include <string.h>
#include "util.h"

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




// Encontra um bloco livre no disco e o marca como ocupado se este estiver em formato myfs. Retorna 0 se nao houver
// bloco livre ou se o disco nao estiver formatado corretamente
unsigned int __findFreeBlock(Disk *d)
{
    unsigned char buffer[DISK_SECTORDATASIZE];
    if(diskReadSector(d, 0, buffer) == -1) return 0; // Superbloco inicialmente carregado no buffer

    if(buffer[SUPERBLOCK_FSID] != myfsInfo.fsid) return 0;

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
        if(diskReadSector(d, i, buffer) == -1) return 0;

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
                if((freeBlock - firstBlock) / sectorsPerBlock >= numBlocks) return 0;

                buffer[j] = __setBitToOne(buffer[j], freeBit);
                if(diskWriteSector(d, i, buffer) == -1) return 0;

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




// Le e retorna o tamanho do bloco de um disco em bytes, assumindo que ele esteja formatado em myfs.
// Retorna 0 em caso de erro
unsigned int __getBlockSize(Disk *d)
{
    unsigned char superblock[DISK_SECTORDATASIZE];
    if(diskReadSector(d, 0, superblock) == -1) return 0;

    if(superblock[SUPERBLOCK_FSID] != myfsInfo.fsid) return 0;

    unsigned int blockSize;
    char2ul(&superblock[SUPERBLOCK_BLOCKSIZE], &blockSize);

    return blockSize;
}




// Funciona como um openDir para o diretorio raiz de um disco. Pode ser fechado normalmnte atraves de myfsClosedir.
// Retorna um descritor de arquivo em caso de sucesso e -1 em caso de erro
int __openRoot(Disk *d)
{
    int fd;
    for(fd = 1; fd <= MAX_FDS; fd++)
    {
        if(openFiles[fd-1] == NULL) break;
    }

    if(fd > MAX_FDS) return -1;

    FileInfo* root = openFiles[fd-1] = malloc(sizeof(FileInfo));
    root->disk = d;
    root->diskBlockSize = __getBlockSize(d);
    root->inode = inodeLoad(ROOT_DIRECTORY_INODE, d);
    root->currentByte = 0;

    if(root->diskBlockSize == 0 || root->inode == NULL)
    {
        free(root);
        openFiles[fd-1] = NULL;
        return -1;
    }

    return fd;
}




bool __deleteFile(Disk *d, Inode *inode)
{
    unsigned int blockCount = 0;
    unsigned int currentBlock = inodeGetBlockAddr(inode, blockCount);

    while (currentBlock > 0)
    {
        __setBlockFree(d, currentBlock);
        blockCount++;
        currentBlock = inodeGetBlockAddr(inode, blockCount);
    }

    return inodeClear(inode) == -1 ? false : true;
}




bool __deleteDir(Disk *d, Inode *inode, Inode *parent)
{
    // Existem outras entradas no diretorio alem de . e ..
    if(inodeGetFileSize(inode) > 2 * sizeof(DirectoryEntry)) return false;

    unsigned int parentPreviousRefCount = inodeGetRefCount(parent);
    inodeSetRefCount(parent, parentPreviousRefCount - 1);
    inodeSave(parent);

    return __deleteFile(d, inode);
}




bool __autoLink(int fd)
{
    if(fd <= 0 || fd > MAX_FDS) return false;
    FileInfo* dir = openFiles[fd-1];

    if(dir == NULL || inodeGetFileType(dir->inode) != FILETYPE_DIR) return false;

    unsigned int dirInumber = inodeGetNumber(dir->inode); // Atualiza inode do diretorio na memoria
    free(dir->inode);
    dir->inode = inodeLoad(dirInumber, dir->disk);

    DirectoryEntry entry;
    strcpy(entry.filename, ".");
    entry.inumber = inodeGetNumber(dir->inode);

    inodeSetRefCount(dir->inode, 1);

    dir->currentByte = 0;
    int bytesWritten = myfsWrite(fd, (const char*) &entry, sizeof(DirectoryEntry));
    dir->currentByte = 0;

    if(bytesWritten != sizeof(DirectoryEntry)) // Falha na insercao da entrada
    {
        // Diretorio volta ao tamanho original, caso a escrita tenha sido valida mas incompleta
        inodeSetFileSize(dir->inode, 0);
        inodeSave(dir->inode);

        return false;
    }

    inodeSave(dir->inode);
    return true;
}