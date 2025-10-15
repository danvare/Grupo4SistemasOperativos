#ifndef LISTADINAMICA_HEADER_NOT_INCLUDED
#define LISTADINAMICA_HEADER_NOT_INCLUDED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MIN(X,Y) (((X)<(Y))?(X):(Y))
#define ERROR 0
#define ERR_MEM 0
#define TODO_OK 1
#define NO_ESTA_LLENO 0

typedef int(*Cmp)(void *,void *);
typedef void(*Accion)(void *);  

typedef struct sNodo{
  void * info;
  unsigned tamInfo;
  struct sNodo * sig;
}tNodo;

typedef tNodo * tLista ;

void crear_lista(tLista * pl);
int poner_en_lista(tLista * pl, const void * pd,unsigned tamDato);
int sacar_ultimo_lista(tLista * pl,void * pd, unsigned tamDato);
int lista_llena(tLista * pl);
int lista_vacia(tLista * pl);
void destruir_lista(tLista * pl);
int poner_ordenado_lista(tLista * pl, void * pd, unsigned tamDato, Cmp cmp);
void* buscar_en_lista(tLista * pl, void * pd, unsigned tamDato, Cmp cmp);
void recorrer_lista(tLista * pl, Accion accion);
void copiar_lista(tLista * dest, tLista * src);
int lista_contar(tLista * pl);
#endif //LISTADINAMICA_HEADER_NOT_INCLUDED