/*
*  myfs.h - Header da API do novo sistema de arquivos
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

#ifndef SO_TRABALHO2_MYFS_H
#define SO_TRABALHO2_MYFS_H

#include "disk.h"
#include "inode.h"

int installMyFS();

int myfsIsIdle(Disk *d);
int myfsFormat(Disk *d, unsigned int blockSize);
int myfsOpen(Disk *d, const char *path);
int myfsRead(int fd, char *buf, unsigned int nbytes);
int myfsWrite(int fd, const char *buf, unsigned int nbytes);
int myfsClose(int fd);
int myfsOpendir(Disk *d, const char *path);
int myfsReaddir(int fd, char *filename, unsigned int *inumber);
int myfsLink(int fd, const char *filename, unsigned int inumber);
int myfsUnlink(int fd, const char *filename);
int myfsClosedir(int fd);

typedef struct
{
    Disk* disk;
    unsigned int diskBlockSize;
    Inode* inode;
    unsigned int currentByte;
} FileInfo;

#endif //SO_TRABALHO2_MYFS_H
