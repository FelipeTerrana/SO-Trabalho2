/*
*  myfsInternalFunctions.h - API que define headers de funcoes auxiliares usadas nas operacoes do sistema de arquivos
 *                           myfs e tambem macros uteis no processo de formatacao e leitura de disco
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

#ifndef SO_TRABALHO2_MYFSINTERNALFUNCTIONS_H
#define SO_TRABALHO2_MYFSINTERNALFUNCTIONS_H


#include <stdbool.h>
#include "disk.h"
#include "myfs.h"
#include "vfs.h"

/// Posicoes das informacoes do disco no superbloco. O superbloco sempre ocupa o setor 0
#define SUPERBLOCK_BLOCKSIZE 0
#define SUPERBLOCK_FSID sizeof(unsigned int)
#define SUPERBLOCK_FREE_SPACE_SECTOR (sizeof(unsigned int) + sizeof(char))
#define SUPERBLOCK_FIRST_BLOCK_SECTOR (2 * sizeof(unsigned int) + sizeof(char))
#define SUPERBLOCK_NUM_BLOCKS (3 * sizeof(unsigned int) + sizeof(char))

#define ROOT_DIRECTORY_INODE 1

extern int myfsSlot;
extern FSInfo myfsInfo;
extern FileInfo* openFiles[MAX_FDS];


// Retorna o primeiro bit igual a 0 no byte de entrada, procurando do bit menos significativo para o mais significativo.
// Os bits são considerados do 0 ao 7 e, caso todos os bits sejam 1, retorna -1
int __firstZeroBit(unsigned char byte);


// Retorna o byte de entrada com o bit na posicao informada transformado em 1. Bits sao contados do menos significativo
// para o mais significativo, contando do 0 ao 7
unsigned char __setBitToOne(unsigned char byte, unsigned int bit);


// Retorna o byte de entrada com o bit na posicao informada transformado em 0. Bits sao contados do menos significativo
// para o mais significativo, contando do 0 ao 7
unsigned char __setBitToZero(unsigned char byte, unsigned int bit);


// Encontra um bloco livre no disco e o marca como ocupado se este estiver em formato myfs. Retorna 0 se nao houver
// bloco livre ou se o disco nao estiver formatado corretamente
unsigned int __findFreeBlock(Disk *d);


// Dado um bloco em um disco formatado em myfs, marca o bloco como livre para uso. Retorna true (!= 0) se a operacao
// foi bem sucedida e false (0) se algum erro ocorreu no processo
bool __setBlockFree(Disk *d, unsigned int block);


// Le e retorna o tamanho do bloco de um disco em bytes, assumindo que ele esteja formatado em myfs.
// Retorna 0 em caso de erro
unsigned int __getBlockSize(Disk *d);


// Funciona como um openDir para o diretorio raiz de um disco. Pode ser fechado normalmnte atraves de myfsClosedir.
// Retorna um descritor de arquivo em caso de sucesso e -1 em caso de erro
int __openRoot(Disk *d);


// Desocupa os blocos usados pelo arquivo referente ao inode e libera seus inodes em disco. Nenhuma verificacao e feita
// em relacao ao numero de referencias do inode. Essa funcao nao deve ser usada para diretorios, use __deleteDir para
// isso.
// em caso de erro
bool __deleteFile(Disk *d, Inode *inode);


// Desocupa os blocos usados pelo diretorio vazio referente ao inode e libera seus inodes em disco. Nenhuma verificacao
// e feita em relacao ao numero de referencias do inode. Essa funcao nao deve ser usada para arquivos comuns, use
// __deleteFile para isso. Nenhuma verificacao e feita em relacao ao tipo de arquivo. Retorna true (!= 0) em caso de
// sucesso e false (0) caso o diretorio nao esteja vazio ou algum erro tenha ocorrido
bool __deleteDir(Disk *d, Inode *inode, Inode *parent);


// Faz o link do diretorio para ele mesmo ("."). Retorna true (!= 0) se o link foi bem sucedido e false (0) em caso
// de erro. Esta funcao so deve ser usada se o diretorio estiver vazio e apenas uma vez, nenhuma verificacao e feita
// nesse sentido
bool __autoLink(int fd);


#endif //SO_TRABALHO2_MYFSINTERNALFUNCTIONS_H
