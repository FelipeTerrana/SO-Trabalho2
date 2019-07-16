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
#include "myfsInternalFunctions.h"
#include "util.h"
#include "vfs.h"



int installMyFS()
{
    myfsSlot = vfsRegisterFS(&myfsInfo);
    return 0;
}



int myfsIsIdle(Disk *d)
{
    int i;

    for(i=1; i <= MAX_FDS; i++)
    {
        FileInfo* file = openFiles[i-1];
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

    inodeSetFileSize(root, 0);
    inodeSetRefCount(root, 1);
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

    // Preserva um descritor de arquivo para poder usar sua posicao temporariamente
    // Nao chama a funcao __openRoot para que nao exista a possibilidade de falha
    FileInfo* previousFirstFD = openFiles[1-1];
    openFiles[1-1] = malloc(sizeof(FileInfo));
    openFiles[1-1]->disk = d;
    openFiles[1-1]->diskBlockSize = blockSize;
    openFiles[1-1]->inode = root;
    openFiles[1-1]->currentByte = 0;

    myfsLink(1, current.filename, current.inumber);
    myfsLink(1, parent.filename, parent.inumber);

    free(openFiles[1-1]);
    openFiles[1-1] = previousFirstFD;

    inodeSave(root);
    free(root);
    return numBlocks > 0 ? numBlocks : -1;
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

            openFiles[fd-1] = malloc(sizeof(FileInfo));
            openFiles[fd-1]->disk = d;
            openFiles[fd-1]->diskBlockSize = blockSize;
            openFiles[fd-1]->inode = inode;
            openFiles[fd-1]->currentByte = 0;

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
    inodeSave(inode);
    free(inode);

    myfsLink(fd, path, inumber); // TODO tratar erro nos links

    myfsClosedir(fd);

    unsigned int blockSize = __getBlockSize(d); // TODO tratar erro

    openFiles[fd-1] = malloc(sizeof(FileInfo));
    openFiles[fd-1]->disk = d;
    openFiles[fd-1]->diskBlockSize = blockSize;
    openFiles[fd-1]->inode = inodeLoad(inumber, d); // Carrega o inode novamente do disco para que o link tenha efeito
    openFiles[fd-1]->currentByte = 0;

    free(dirPath);
    return fd;
}




int myfsRead(int fd, char *buf, unsigned int nbytes)
{
    if(fd <= 0 || fd > MAX_FDS) return -1;
    FileInfo* file = openFiles[fd-1];
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
    if(fd <= 0 || fd > MAX_FDS) return -1;
    FileInfo* file = openFiles[fd-1];
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

        currentBlock = (file->currentByte + bytesWritten < fileSize) ?
                        inodeGetBlockAddr(file->inode, currentInodeBlockNum) :
                        0;
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
    if(fd <= 0 || fd > MAX_FDS) return -1;
    FileInfo* file = openFiles[fd-1];

    if(file == NULL) return -1;

    // Libera apenas o ponteiro para o Inode pois o ponteiro para Disk ja existia antes da alocacao do FileInfo
    free(file->inode);

    free(file);
    openFiles[fd-1] = NULL;
    return 0;
}




int myfsOpendir(Disk *d, const char *path)
{
    int currentDirFd = __openRoot(d);
    if(currentDirFd == -1) return -1;

    unsigned int blockSize = openFiles[currentDirFd-1]->diskBlockSize;

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

                openFiles[currentDirFd-1] = malloc(sizeof(FileInfo));
                openFiles[currentDirFd-1]->disk = d;
                openFiles[currentDirFd-1]->diskBlockSize = blockSize;
                openFiles[currentDirFd-1]->inode = nextDirInode;
                openFiles[currentDirFd-1]->currentByte = 0;

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
            parent.inumber = inodeGetNumber(openFiles[currentDirFd-1]->inode);
            strcpy(parent.filename, "..");

            myfsClosedir(currentDirFd);
            openFiles[currentDirFd-1] = malloc(sizeof(FileInfo));
            openFiles[currentDirFd-1]->disk = d;
            openFiles[currentDirFd-1]->diskBlockSize = blockSize;
            openFiles[currentDirFd-1]->inode = newDirInode;
            openFiles[currentDirFd-1]->currentByte = 0;

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
    if(fd <= 0 || fd > MAX_FDS) return -1;
    FileInfo* file = openFiles[fd-1];

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
    if(fd <= 0 || fd > MAX_FDS) return -1;
    FileInfo* dir = openFiles[fd-1];

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
    if(fd <= 0 || fd > MAX_FDS) return -1;
    FileInfo* dir = openFiles[fd-1];

    if(dir == NULL || inodeGetFileType(dir->inode) != FILETYPE_DIR) return -1;

    if(strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) return -1;

    unsigned int previousCurrentByte = dir->currentByte;
    dir->currentByte = 0; // Para percorrer as entradas de dir desde o inicio

    Inode* inodeToUnlink = NULL;
    unsigned int previousRefCount = 0;
    DirectoryEntry entry;
    unsigned int inumber = 0;
    while(myfsRead(fd, (char*) &entry, sizeof(DirectoryEntry)) == sizeof(DirectoryEntry))
    {
        if(strcmp(entry.filename, filename) == 0)
        {
            inumber = entry.inumber;
            inodeToUnlink = inodeLoad(inumber, dir->disk);
            if(inodeToUnlink == NULL) return -1;

            previousRefCount = inodeGetRefCount(inodeToUnlink);

            // Se esta for a ultima referencia ao inode, ele deve ser liberado. Para que isso aconteca, e necessario
            // confirmar se o inode nao esta aberto em algum arquivo e, caso esteja aberto, o unlink nao e realizado
            if( (inodeGetFileType(inodeToUnlink) == FILETYPE_REGULAR && previousRefCount == 1) ||
                (inodeGetFileType(inodeToUnlink) == FILETYPE_DIR && previousRefCount == 2) )
            {
                int i;
                for(i=1; i <= MAX_FDS; i++)
                {
                    if(openFiles[i-1] != NULL && inodeGetNumber(openFiles[i-1]->inode) == inumber)
                    {
                        free(inodeToUnlink);
                        return -1;
                    }
                }
            }

            inodeSetRefCount(inodeToUnlink, previousRefCount - 1);

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
            inodeSave(dir->inode);
            break;
        }
    }

    if(inumber == 0) return -1; // Entrada nao encontrada
    dir->currentByte = previousCurrentByte;

    if(inodeGetFileType(inodeToUnlink) == FILETYPE_REGULAR && previousRefCount == 1)
        __deleteFile(dir->disk, inodeToUnlink);

    else if(inodeGetFileType(inodeToUnlink) == FILETYPE_DIR && previousRefCount == 2)
        __deleteDir(dir->disk, inodeToUnlink, dir->inode);

    inodeSave(inodeToUnlink);
    free(inodeToUnlink);
    return 0;
}




int myfsClosedir(int fd)
{
    return myfsClose(fd);
}