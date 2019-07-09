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

#include <stdbool.h>
#include "disk.h"
#include "vfs.h"

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
    int diskId = diskGetId(d);
    int i;

    for(i=0; i < MAX_FDS; i++)
    {
        FileInfo* fi = openFiles[i];
        if(fi != NULL && fi->diskId == diskId) return false;
    }

    return true;
}




int myfsFormat(Disk *d, unsigned int blockSize)
{
    // TODO myfsFormat
    return 0;
}




int myfsOpen(Disk *d, const char *path)
{
    // TODO myfsOpen
    return 0;
}




int myfsRead(int fd, char *buf, unsigned int nbytes)
{
    // TODO myfsRead
    return 0;
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