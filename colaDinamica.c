#include "colaDinamica.h"

void crear_cola(tCola * pc){
  pc->pri = NULL;
  pc->ult = NULL;
}

int poner_en_cola(tCola * pc, const void * dato,unsigned tamDato){
  tNodo * nue = NULL;

  if((nue = malloc(sizeof(tNodo))) == NULL || (nue->info = malloc(tamDato)) == NULL ){
    free(nue->info);
    return ERR_MEM;
  }

  memcpy(nue->info,dato,tamDato);
  nue->sig = NULL;
  nue->tamInfo = tamDato;

  if(pc->ult)
    pc->ult->sig = nue;
  else
    pc->pri = nue;
  pc->ult = nue;
  return TODO_OK;
}

int sacar_de_cola(tCola * pc, void * pd, unsigned tamDato){
  tNodo * aux = pc->pri;
  if(pc->pri == NULL)
    return ERROR;

  memcpy(pd,pc->pri->info,MIN(tamDato,pc->pri->tamInfo));

  pc->pri = pc->pri->sig;
  free(aux->info);
  free(aux);
  return TODO_OK;
}

int cola_llena(tCola * pc){
  void * aux = malloc(sizeof(tNodo));
  if(aux == NULL)
    return ERR_MEM;
  free(aux);
  return TODO_OK;
}

int cola_vacia(tCola * pc){
  return pc->pri == NULL;
}

void destruir_cola(tCola * pc){
  tNodo * aux;
  while(pc->pri){
    aux = pc->pri;
    pc->pri = pc->pri->sig;
    
    free(aux->info);
    free(aux);
  }
    
}
