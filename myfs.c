/*
*  myfs.c - Implementacao das funcoes relativas ao novo sistema de arquivos
*
*  Autores: Eduardo Pereira do Valle -
*           Felipe Terrana Cazetta - 201635026
*           Matheus Brinati Altomar -
*           Vinicius Alberto Alves da Silva -
*
*  Projeto: Trabalho Pratico II - Sistemas Operacionais
*  Organizacao: Universidade Federal de Juiz de Fora
*  Departamento: Dep. Ciencia da Computacao
*
*/

#include "myfs.h"
#include "vfs.h"



FSInfo myfsInfo =
{
    'm',
    "myfs",
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



int installMyFS()
{
    return 0;
}