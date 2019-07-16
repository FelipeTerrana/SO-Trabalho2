/*
*  myfs.c - Implementacao das funcoes relativas ao novo sistema de arquivos
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

#include "myfs.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
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

#define ROOT_DIRECTORY_INODE 1

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
    unsigned int freeSpaceSize   = 1 + (diskGetSize(d) / blockSize) / (sizeof(unsigned char) * 8 * DISK_SECTORDATASIZE);

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

    // Define um inode fixo como diretorio raiz
    Inode* root = inodeLoad(ROOT_DIRECTORY_INODE, d);
    if(root == NULL) return -1;

    inodeSetFileSize(root, 2 * sizeof(DirectoryEntry));
    inodeSetRefCount(root, 3);
    inodeSetFileType(root, FILETYPE_DIR);

    unsigned int rootBlock = __findFreeBlock(d);

    if(rootBlock == 0 || inodeAddBlock(root, rootBlock) == -1)
    {
        free(root);
        return -1;
    }

    numBlocks--; // Desconsidera bloco ocupado pela raiz

    DirectoryEntry current, parent;
    current.inumber = parent.inumber = ROOT_DIRECTORY_INODE;
    strcpy(current.filename, ".");
    strcpy(parent.filename, "..");

    unsigned char rootBuffer[DISK_SECTORDATASIZE]; // Transfere os dados das entradas . e .. direto para o setor da raiz
    for(i=0; i < sizeof(DirectoryEntry); i++) rootBuffer[i] = ((char*) &current) [i];
    for(i=0; i < sizeof(DirectoryEntry); i++) rootBuffer[i + sizeof(DirectoryEntry)] = ((char*) &parent) [i];

    if(diskWriteSector(d, rootBlock, rootBuffer) == -1 )
    {
        free(root);
        return -1;
    }

    inodeSave(root);
    free(root);
    return numBlocks > 0 ? numBlocks : -1;
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
    for(fd = 0; fd < MAX_FDS; fd++)
    {
        if(openFiles[fd] == NULL) break;
    }

    if(fd == MAX_FDS) return -1;

    FileInfo* root = openFiles[fd] = malloc(sizeof(FileInfo));
    root->disk = d;
    root->diskBlockSize = __getBlockSize(d);
    root->inode = inodeLoad(ROOT_DIRECTORY_INODE, d);
    root->currentByte = 0;

    if(root->diskBlockSize == 0 || root->inode == NULL)
    {
        free(root);
        openFiles[fd] = NULL;
        return -1;
    }

    return fd;
}




int myfsOpen(Disk *d, const char *path)
{
    int lastBar;
    for(lastBar = strlen(path) - 1; lastBar >= 0 && path[lastBar] != '/'; lastBar--);

    if(lastBar < 0) return -1;

    // dirpath representa o caminho da pasta pai do arquivo a ser aberto
    char* dirPath = malloc( (strlen(path) + 1) * sizeof(char) );
    strcpy(dirPath, path);
    dirPath[lastBar] = '\0';

    path += lastBar + 1; // path passa a apontar para o inicio do nome do arquivo dentro do diretorio pai

    int fd = myfsOpendir(d, dirPath);
    if(fd == -1)
    {
        free(dirPath);
        return -1;
    }

    DirectoryEntry entry;
    while(myfsReaddir(fd, entry.filename, &entry.inumber) == 1)
    {
        if(strcmp(entry.filename, path) == 0)
        {
            Inode* inode = inodeLoad(entry.inumber, d);
            unsigned int blockSize = __getBlockSize(d);
            myfsClosedir(fd);

            if(inode == NULL || blockSize == 0)
            {
                free(dirPath);
                return -1;
            }

            openFiles[fd] = malloc(sizeof(FileInfo));
            openFiles[fd]->disk = d;
            openFiles[fd]->diskBlockSize = blockSize;
            openFiles[fd]->inode = inode;
            openFiles[fd]->currentByte = 0;

            return fd;
        }
    }

    // Arquivo nao encontrado, cria um novo
    unsigned int inumber = inodeFindFreeInode(ROOT_DIRECTORY_INODE + 1, d);
    if(inumber == 0)
    {
        myfsClosedir(fd);
        free(dirPath);
        return -1;
    }

    Inode* inode = inodeLoad(inumber, d);
    if(inode == NULL)
    {
        myfsClosedir(fd);
        free(dirPath);
        return -1;
    }

    inodeSetFileType(inode, FILETYPE_DIR);
    inodeSetRefCount(inode, 0);
    inodeSetFileSize(inode, 0);

    unsigned int newFileFirstBlock = __findFreeBlock(d);
    if(newFileFirstBlock == 0)
    {
        free(inode);
        myfsClosedir(fd);
        free(dirPath);
        return -1;
    }

    if(inodeAddBlock(inode, newFileFirstBlock) == -1)
    {
        free(inode);
        myfsClosedir(fd);
        free(dirPath);
        __setBlockFree(d, newFileFirstBlock);
        return -1;
    }

    inodeSetFileSize(inode, 0);
    inodeSetRefCount(inode, 0);
    inodeSetFileType(inode, FILETYPE_REGULAR);

    myfsLink(fd, path, inumber); // TODO tratar erro nos links

    myfsClosedir(fd);

    unsigned int blockSize = __getBlockSize(d); // TODO tratar erro

    openFiles[fd] = malloc(sizeof(FileInfo));
    openFiles[fd]->disk = d;
    openFiles[fd]->diskBlockSize = blockSize;
    openFiles[fd]->inode = inode;
    openFiles[fd]->currentByte = 0;

    free(dirPath);
    return fd;
}




int myfsRead(int fd, char *buf, unsigned int nbytes)
{
    if(fd < 0 || fd >= MAX_FDS) return -1;
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
        for(i = firstSector; i < sectorsPerBlock && bytesRead < nbytes; i++)
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
    if(fd < 0 || fd >= MAX_FDS) return -1;
    FileInfo* file = openFiles[fd];
    if(file == NULL) return -1;

    unsigned int fileSize = inodeGetFileSize(file->inode);
    unsigned int bytesWritten = 0;
    unsigned int currentInodeBlockNum = file->currentByte / file->diskBlockSize;
    unsigned int offset = file->currentByte % file->diskBlockSize; // offset em bytes a partir do início do bloco
    unsigned int currentBlock = inodeGetBlockAddr(file->inode, currentInodeBlockNum);
    unsigned char diskBuffer[DISK_SECTORDATASIZE];

    while(bytesWritten < nbytes)
    {
        unsigned int sectorsPerBlock = file->diskBlockSize / DISK_SECTORDATASIZE;
        unsigned int firstSector = offset / DISK_SECTORDATASIZE;
        unsigned int firstByteInSector = offset % DISK_SECTORDATASIZE;

        if(currentBlock == 0)
        {
            currentBlock = __findFreeBlock(file->disk);

            if(currentBlock == 0) break; // Disco cheio

            if(inodeAddBlock(file->inode, currentBlock) == -1) // Erro na associacao do bloco livre ao inode
            {
                __setBlockFree(file->disk, currentBlock);
                break;
            }
        }

        int i;
        for(i = firstSector; i < sectorsPerBlock && bytesWritten < nbytes; i++)
        {
            if(diskReadSector(file->disk, currentBlock + i, diskBuffer) == -1) return -1;

            int j;
            for(j = firstByteInSector; j < DISK_SECTORDATASIZE && bytesWritten < nbytes; j++)
            {
                diskBuffer[j] = buf[bytesWritten];
                bytesWritten++;
            }

            if(diskWriteSector(file->disk, currentBlock + i, diskBuffer) == -1) return -1;
            firstByteInSector = 0;
        }

        offset = 0;
        currentInodeBlockNum++;
        currentBlock = inodeGetBlockAddr(file->inode, currentInodeBlockNum);
    }

    file->currentByte += bytesWritten;
    if(file->currentByte >= fileSize)
    {
        inodeSetFileSize(file->inode, file->currentByte);
        inodeSave(file->inode);
    }

    return bytesWritten;
}




int myfsClose(int fd)
{
    if(fd < 0 || fd >= MAX_FDS) return -1;
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
    int currentDirFd = __openRoot(d);
    if(currentDirFd == -1) return -1;

    unsigned int blockSize = openFiles[currentDirFd]->diskBlockSize;

    if(path[0] == '/') path++; // Desconsidera a primeira barra, ja que todos os caminhos se iniciam na raiz

    while(path[0] != '\0')
    {
        char nextDirname[MAX_FILENAME_LENGTH + 1];
        int i;
        for(i=0; path[i] != '/' && path[i] != '\0'; i++) nextDirname[i] = path[i];
        nextDirname[i] = '\0';
        path += i;

        if(path[0] == '/') path++;

        DirectoryEntry entry;
        bool foundEntry = false;
        while(myfsReaddir(currentDirFd, entry.filename, &entry.inumber) == 1)
        {
            if(strcmp(entry.filename, nextDirname) == 0)
            {
                Inode* nextDirInode = inodeLoad(entry.inumber, d);
                if(nextDirInode == NULL || inodeGetFileType(nextDirInode) != FILETYPE_DIR)
                {
                    free(nextDirInode);
                    return -1;
                }

                foundEntry = true;
                myfsClosedir(currentDirFd);

                openFiles[currentDirFd] = malloc(sizeof(FileInfo));
                openFiles[currentDirFd]->disk = d;
                openFiles[currentDirFd]->diskBlockSize = blockSize;
                openFiles[currentDirFd]->inode = nextDirInode;
                openFiles[currentDirFd]->currentByte = 0;

                break;
            }
        }

        // Nao foi encontrado o proximo diretorio a ser aberto mas ele e o ultimo a ser visitado,
        // cria um novo diretorio vazio dentro do atual
        if(!foundEntry && path[0] == '\0')
        {
            unsigned int newDirInumber = inodeFindFreeInode(ROOT_DIRECTORY_INODE + 1, d);
            if(newDirInumber == 0)
            {
                myfsClosedir(currentDirFd);
                return -1;
            }

            Inode* newDirInode = inodeLoad(newDirInumber, d);
            if(newDirInode == NULL)
            {
                myfsClosedir(currentDirFd);
                return -1;
            }

            inodeSetFileType(newDirInode, FILETYPE_DIR);
            inodeSetRefCount(newDirInode, 0);
            inodeSetFileSize(newDirInode, 0);

            unsigned int newDirFirstBlock = __findFreeBlock(d);
            if(newDirFirstBlock == 0)
            {
                free(newDirInode);
                myfsClosedir(currentDirFd);
                return -1;
            }

            if(inodeAddBlock(newDirInode, newDirFirstBlock) == -1)
            {
                free(newDirInode);
                myfsClosedir(currentDirFd);
                __setBlockFree(d, newDirFirstBlock);
                return -1;
            }

            myfsLink(currentDirFd, nextDirname, newDirInumber);

            DirectoryEntry current;
            current.inumber = newDirInumber;
            strcpy(current.filename, ".");

            DirectoryEntry parent;
            parent.inumber = inodeGetNumber(openFiles[currentDirFd]->inode);
            strcpy(parent.filename, "..");

            myfsClosedir(currentDirFd);
            openFiles[currentDirFd] = malloc(sizeof(FileInfo));
            openFiles[currentDirFd]->disk = d;
            openFiles[currentDirFd]->diskBlockSize = blockSize;
            openFiles[currentDirFd]->inode = newDirInode;
            openFiles[currentDirFd]->currentByte = 0;

            myfsLink(currentDirFd, current.filename, current.inumber);
            myfsLink(currentDirFd, parent.filename, parent.inumber);

            inodeSave(newDirInode);
            return currentDirFd;
        }

        else if(!foundEntry) // Nao foi encontrado o proximo diretorio e ainda ha outros a serem percorridos, erro
        {
            myfsClosedir(currentDirFd);
            return -1;
        }
    }

    return currentDirFd;
}




int myfsReaddir(int fd, char *filename, unsigned int *inumber)
{
    if(fd < 0 || fd >= MAX_FDS) return -1;
    FileInfo* file = openFiles[fd];

    if(file == NULL || inodeGetFileType(file->inode) != FILETYPE_DIR) return -1;

    DirectoryEntry entry;
    int bytesRead = myfsRead(fd, (char*) &entry, sizeof(DirectoryEntry));

    if(bytesRead == -1) return -1; // Erro na leitura
    if(bytesRead < sizeof(DirectoryEntry)) return 0; // Fim do diretorio

    strcpy(filename, entry.filename);
    *inumber = entry.inumber;
    return 1;
}




int myfsLink(int fd, const char *filename, unsigned int inumber) // TODO conferir se ja existe entrada de nome filename
{
    if(fd < 0 || fd >= MAX_FDS) return -1;
    FileInfo* dir = openFiles[fd];

    if(dir == NULL || inodeGetFileType(dir->inode) != FILETYPE_DIR) return -1;

    Inode* inodeToLink = inodeLoad(inumber, dir->disk);
    if(inodeToLink == NULL) return -1;

    DirectoryEntry entry;
    strcpy(entry.filename, filename);
    entry.inumber = inumber;

    unsigned int previousCurrentByte = dir->currentByte;
    unsigned int previousDirSize = inodeGetFileSize(dir->inode);
    dir->currentByte = previousDirSize; // Para que a entrada seja inserida sempre no final do diretorio

    int bytesWritten = myfsWrite(fd, (const char*) &entry, sizeof(DirectoryEntry));
    dir->currentByte = previousCurrentByte;

    if(bytesWritten != sizeof(DirectoryEntry)) // Falha na insercao de uma nova entrada
    {
        // Diretorio volta ao tamanho original, caso a escrita tenha sido valida mas incompleta
        inodeSetFileSize(dir->inode, previousDirSize);
        inodeSave(dir->inode);

        free(inodeToLink);
        return -1;
    }

    unsigned int previousRefCount = inodeGetRefCount(inodeToLink);
    inodeSetRefCount(inodeToLink, previousRefCount + 1);

    inodeSave(inodeToLink);
    free(inodeToLink);
    return 0;
}




int myfsUnlink(int fd, const char *filename)
{
    if(fd < 0 || fd >= MAX_FDS) return -1;
    FileInfo* dir = openFiles[fd];

    if(dir == NULL || inodeGetFileType(dir->inode) != FILETYPE_DIR) return -1;

    unsigned int previousCurrentByte = dir->currentByte;
    dir->currentByte = 0; // Para percorrer as entradas de dir desde o inicio

    DirectoryEntry entry;
    unsigned int inumber = 0;
    while(myfsRead(fd, (char*) &entry, sizeof(DirectoryEntry)) == sizeof(DirectoryEntry))
    {
        if(strcmp(entry.filename, filename) == 0)
        {
            inumber = entry.inumber;

            // Para remover a entrada encontrada, percorre-se o diretorio lendo as entradas da frente e escrevendo sobre
            // as anteriores, "arrastando" as entradas para tras
            unsigned int currentEntryByte = dir->currentByte;
            unsigned int nextEntryByte = dir->currentByte + sizeof(DirectoryEntry);

            dir->currentByte = nextEntryByte;
            while(myfsRead(fd, (char*) &entry, sizeof(DirectoryEntry)) == sizeof(DirectoryEntry))
            {
                dir->currentByte = currentEntryByte;
                myfsWrite(fd, (char*) &entry, sizeof(DirectoryEntry));

                currentEntryByte += sizeof(DirectoryEntry);
                nextEntryByte += sizeof(DirectoryEntry);

                dir->currentByte = nextEntryByte;
            }

            unsigned int previousDirSize = inodeGetFileSize(dir->inode);
            inodeSetFileSize(dir->inode, previousDirSize - sizeof(DirectoryEntry));
            break;
        }
    }

    dir->currentByte = previousCurrentByte;
    if(inumber == 0) return -1;

    Inode* inodeToUnlink = inodeLoad(inumber, dir->disk);
    if(inodeToUnlink == NULL) return -1;

    unsigned int previousRefCount = inodeGetRefCount(inodeToUnlink);
    inodeSetRefCount(inodeToUnlink, previousRefCount - 1);

    // TODO tratar diretorios de forma diferente
    if(previousRefCount == 1) // Ultima referencia do inode foi removida, libera os blocos ocupados por ele
    {
        unsigned int blockCount = 0;
        unsigned int currentBlock = inodeGetBlockAddr(inodeToUnlink, blockCount);

        while (currentBlock > 0)
        {
            __setBlockFree(dir->disk, currentBlock);
            blockCount++;
            currentBlock = inodeGetBlockAddr(inodeToUnlink, blockCount);
        }

        inodeClear(inodeToUnlink);
    }

    inodeSave(inodeToUnlink);
    free(inodeToUnlink);
    return 0;
}




int myfsClosedir(int fd)
{
    return myfsClose(fd);
}