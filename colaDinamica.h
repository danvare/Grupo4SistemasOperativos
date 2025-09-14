#ifndef COLADINAMICA_HEADER_NOT_INCLUDED
#define COLADINAMICA_HEADER_NOT_INCLUDED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MIN(X,Y) (((X)<(Y))?(X):(Y))
#define ERROR 0
#define ERR_MEM 0
#define TODO_OK 1
#define NO_ESTA_LLENO 0


typedef struct sNodo{
  void * info;
  unsigned tamInfo;
  struct sNodo * sig;
}tNodo;

typedef struct { 
  tNodo * pri, * ult;
} tCola ;

void crear_cola(tCola * pc);
int poner_en_cola(tCola * pc, const void * pd,unsigned tamDato);
int sacar_de_cola(tCola * pc, void * pd, unsigned tamDato);
int cola_llena(tCola * pc);
int cola_vacia(tCola * pc);
void destruir_cola(tCola * pc);

#endif //PILADINAMICA_HEADER_NOT_INCLUDED