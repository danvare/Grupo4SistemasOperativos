#include "listaDinamica.h"

void crear_lista(tLista * pl){
  *pl = NULL;
}

int poner_en_lista(tLista * pl, const void * pd,unsigned tamDato){
  tNodo * nue = NULL;
  if((nue = malloc(sizeof(tNodo))) == NULL || (nue->info = malloc(sizeof(tamDato))) == NULL){
    free(nue);
    return ERR_MEM;
  }
  mempcpy(nue->info,pd,sizeof(tamDato));
  nue->tamInfo = tamDato;
  nue->sig = *pl;
  *pl = nue;
  return TODO_OK;
}


int lista_llena(tLista * pl){
  void *aux;
  if((aux=malloc(sizeof(tNodo)))==NULL)
    return TODO_OK;
  return NO_ESTA_LLENO;
}

int lista_vacia(tLista * pl){
  return *pl == NULL;
}

void destruir_lista(tLista * pl){
  tNodo *aux;
  while(*pl){
    aux = *pl;
    *pl = (*pl)->sig;
    free(aux->info);
    free(aux);
  }
}

int sacar_ultimo_lista(tLista * pl,void * pd, unsigned tamDato){
  if(*pl == NULL)
    return ERROR;
  while((*pl)->sig){
    pl = &(*pl)->sig;
  }
  memcpy(pd,(*pl)->info,MIN((*pl)->tamInfo,tamDato));
  free((*pl)->info);
  free(*pl);
  *pl = NULL;
  return TODO_OK;
}

/*int ordenar_lista(tLista * pl, Cmp * cmp){
  tLista * q,* pri = pl;
  tNodo * aux = NULL;

  if(*pl == NULL)
    return ERROR;

  while(*pl){
    if(cmp((*pl)->info,(*pl)->sig->info)>0){
      q = pri;
      aux = (*pl)->sig;
      
      while(*q && cmp((*pl)->info,(*pl)->sig->info)>0)
        q = &(*q)->sig;
      aux->sig = *q;
      *q = aux;
    }
    else
      pl = &(*pl)->sig;
  }

}*/

int poner_ordenado_lista(tLista * pl, void * pd, unsigned tamDato, Cmp cmp){

  tNodo * nue = NULL;
  
  while(*pl && cmp((*pl)->info,pd)<0){
    pl = &(*pl)->sig;
  }

  
  if((nue = malloc(sizeof(tNodo))) == NULL || (nue->info = malloc(sizeof(tamDato))) == NULL){
    free(nue);
    return ERR_MEM;
  }

  mempcpy(nue->info,pd,sizeof(tamDato));
  nue->tamInfo = tamDato;
  nue->sig = *pl;
  *pl = nue;
  
  return TODO_OK;
}

void * buscar_en_lista(tLista * pl, void * pd, unsigned tamDato, Cmp cmp){
  while(*pl && cmp((*pl)->info,pd)!=0){
    pl = &(*pl)->sig;
  }
  if(*pl)
    return (*pl)->info;
  return NULL;
}

void recorrer_lista(tLista * pl, Accion accion){
  while(*pl){
    accion((*pl)->info);
    pl = &(*pl)->sig;
  }
}

void copiar_lista(tLista * dest, tLista * src){
  tNodo * aux = *src;
  while(aux){
    poner_en_lista(dest,aux->info,aux->tamInfo);
    aux = aux->sig;
  }
}

int lista_contar(tLista * pl){
  int cont = 0;
  while(*pl){
    cont++;
    pl = &(*pl)->sig;
  }
  return cont;
}