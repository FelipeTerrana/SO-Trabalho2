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
DirectoryEntry* Files[MAX_FDS] = {NULL}; // acho que poderiamos usar esse para armazenar numero de inode dos arquivos/diretorios e seus caminhos



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
    numeroInodes = numInodes; // preciso do umero de inodes para abrir arquivo
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
    // TODO Verificar se esta certo
    int i;
    int booleano;
    int n;
    int j;
    int idDir;
    int numeroInode;
    unsigned char dirpath [MAX_FILENAME_LENGTH + 1];
    int numerosInodesUsados [MAX_FDS] = {0};
    int cont = 0;
    int fdLivre = -1;
    for(i=0;i<MAX_FDS;i++)
    {
        DirectoryEntry *arquivo = Files[i];
        if(arquivo != NULL )
        {
            if(arquivo->filename == path) // verificar se o arquivo existe
            {
                if (openFiles[i]==NULL) // verifica se n esta aberto
                {
                    //openFiles[i]->currentByte  =;
                    openFiles[i]->disk = d;
                    //openFiles[i]->diskBlockSize =;
                    openFiles[i]->inode = inodeLoad(arquivo->inumber,d);
                }
                return i;
            }
            else // determina quais inodes ja foram usados
            {
                numerosInodesUsados[cont] = arquivo->inumber;
                cont++;
            }
        }
        else //para encontrar fd livre
        {
            if(fdLivre == -1)
                fdLivre = i;
        }

    }
    if(cont==numeroInodes) // verificar se tem inode disponivel
    {
        return -1;
    }
    for(int i = 1; i <= cont; i++) // descobrir numero de inode livre
    {
        booleano = 1;
        for(j=0;j<cont;j++)
        {
            if (numerosInodesUsados[j]==i)
            {
                booleano = 0;
            }
        }
        if(booleano)
        {
            numeroInode = i;
            break;
        }
    }

    Inode* inode = inodeCreate(numeroInode,d);
    inodeSetFileType(inode,FILETYPE_REGULAR);
    if(inodeSave(inode)!=0)
    {
        return -1;
    }
    for(i=0;path[i] != '\0';i++);
    n = i-1;
    for(i=n;i>=0;i--)  // para encontrar diretorio (se tiver do arquivo), acho melhor fazemos opendir recursivo abrindo diretorios ate chegar a raiz quando for abrir um diretorio  , ou dependendo usar o vetor files
    {
        if(path[i]=='/' && i!=0)
        {
            for(j=0;j>=i-1;j++)
            {
                dirpath[j]=path[j];
            }
            dirpath[j]='\0';
            idDir = myfsOpendir(d,dirpath); // a partir do comentario acima acredito que passando um caminho do diretorio principal
            if(idDir>-1)
            {
                myfsLink(idDir,path,numeroInode);
            }
            else
            {
                return -1;
            }
            break;
        }
    }
    // TODO setar ref count se o arquivo estiver sendo construido na raiz
//    Files[fdLivre]->filename = *path;
    Files[fdLivre]->inumber = numeroInode; // LINHA NÃO compila
    //openFiles[fdLivre]->currentByte  =; TODO
    openFiles[fdLivre]->disk = d;
    //openFiles[fdLivre]->diskBlockSize =; TODO
    openFiles[fdLivre]->inode = inodeLoad(numeroInode,d);
    return fdLivre;
}

int myfsOpendir(Disk *d, const char *path)
{
    // TODO myfsOpendir
    int newFd = -1; //fd da pasta a ser aberta
    int fd = -1; //fd auxiliar
    int numeroInode;
    int numerosInodesUsados [MAX_FDS] = {0};
    int cont = 0;
    char *aux = (char*)malloc(sizeof(char) * sizeof(path));
    char *aux2 = (char*)malloc(sizeof(char) * sizeof(path));
    int i = 0;
    while(path[i] != "/" ) { // Dir Atual fica em aux1
        aux[i] = path[i];
        i++;
    }
    int j=0;
    for(;path[i] != '\0'; i++ ){ //Resto do path fica em aux2
            aux2[j++] = path[i];
    }
    //Ver ser o diretório está no caminho atual, caso contrário chama dnv
   // bool isHere = false;
    bool dirAlreadyExists = false;
    for(i=0;i<MAX_FDS;i++) {
        DirectoryEntry *arquivo = Files[i];
        if(arquivo != NULL ) {
            if(arquivo->filename == aux) { // verificar se o arquivo existe
                if (openFiles[i]==NULL)  { // verifica se n esta aberto
                    //openFiles[i]->currentByte  =;
                    openFiles[i]->disk = d;
                    //openFiles[i]->diskBlockSize =;
                    openFiles[i]->inode = inodeLoad(arquivo->inumber,d);
                }
                dirAlreadyExists = true;
                fd = i;
                break;

            }
            else // determina quais inodes ja foram usados
            {
                numerosInodesUsados[cont] = arquivo->inumber;
                cont++;
            }
        }
        else //para encontrar fd livre para criar diretório
        {
            if(newFd == -1)
                newFd = i;
        }
    }
    if(cont==numeroInodes) // verificar se tem inode disponivel
    {
        return -1;
    }
    if(!dirAlreadyExists) {
        Inode* inode = inodeCreate(numeroInode,d);
        inodeSetFileType(inode,FILETYPE_DIR);
        if(inodeSave(inode)!=0)
        {
            return -1;
        }
        myfsLink(newFd,aux,numeroInode);
     //   Files[newFd]->filename = path; // LINHA NÃO compila
        Files[newFd]->inumber = numeroInode;
        //openFiles[newFd]->currentByte  =; TODO
        openFiles[newFd]->disk = d;
        //openFiles[newFd]->diskBlockSize =; TODO
        openFiles[newFd]->inode = inodeLoad(numeroInode,d);
        fd = newFd;
    }

    if(aux != '\0')
          fd = myfsOpendir(d,aux2);
    else
        return fd;

    /*
     char entryname[MAX_FILENAME_LENGTH+1];
    unsigned int inumber;
    int res = myfsReaddir(fd,entryname,inumber);
    int fd aux;
    while ( res > 0 ) {
       DirectoryEntry *arquivo = Files[inumber];
       if(arquivo->filename = aux2) {
            isHere = true;
            o
            break;
        }
       res = myfsReaddir (fd, entryname, &inumber);


    }
    if(isHere) {
        return inumber;
    }
    else{ */


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

            if(currentBlock == -1) break; // Disco cheio

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
    // TODO myfsClosedir
    return 0;
}
