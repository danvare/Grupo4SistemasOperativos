#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <semaphore.h>


// Definición de la estructura del nodo
typedef struct Nodo {
    int dato;
    struct Nodo *siguiente;
} Nodo;

// Definición de la estructura de la cola
typedef struct Cola {
    Nodo *frente;
    Nodo *final;
} Cola;

Cola* crear_cola() {
    Cola *q = (Cola*) malloc(sizeof(Cola));
    if (q == NULL) {
        printf("Error: No se pudo asignar memoria para la cola.\n");
        exit(1);
    }
    q->frente = NULL;
    q->final = NULL;
    return q;
}

void encolar(Cola *q, void *valor, size_t tamano_dato) {
    Nodo *nuevo_nodo = (Nodo*) malloc(sizeof(Nodo));
    if (nuevo_nodo == NULL) {
        perror("Error: No se pudo asignar memoria para el nuevo nodo");
        exit(1);
    }

    (nuevo_nodo)->dato = malloc(tamano_dato);
    if (nuevo_nodo->dato == NULL) {
        perror("Error: No se pudo asignar memoria para el dato");
        free(nuevo_nodo);
        exit(1);
    }

    // Copia el dato al nuevo nodo
    memcpy(nuevo_nodo->dato, valor, tamano_dato);
    nuevo_nodo->siguiente = NULL;

    if (q->final == NULL) {
        q->frente = nuevo_nodo;
        q->final = nuevo_nodo;
    } else {
        q->final->siguiente = nuevo_nodo;
        q->final = nuevo_nodo;
    }
}

void* desencolar(Cola *q) {
    if (q->frente == NULL) {
        return NULL; // Devuelve NULL si la cola está vacía
    }

    Nodo *nodo_a_eliminar = q->frente;
    void *dato_a_devolver = nodo_a_eliminar->dato;
    q->frente = q->frente->siguiente;

    if (q->frente == NULL) {
        q->final = NULL;
    }

    free(nodo_a_eliminar);
    return dato_a_devolver;
}

int esta_vacia(Cola *q) {
    return (q->frente == NULL);
}


typedef struct 
{
    int id;
    char Estado;
} Producto;


// S Stock
// N Sin stock
// C Critico
// R Reservado

char opcionRandom (){
  char estado[4] = "SNCR";
  return estado[(rand()%4)];
}


void* tarea(void* args){
    // int id = *(int*)args;
    // printf("Tarea hilo ID: %d\n", id);

    Cola* cola = (Cola*)args;

    Producto* vector = (Producto*)desencolar(cola);
    int cant = *(int*)desencolar(cola);

    for (int i = cant; i < cant+10; i++)
    {
        vector[i].id = i;
        vector[i].Estado = opcionRandom();
    }
    
    encolar(cola, &vector, sizeof(Producto)*10);
    encolar(cola, &cant,sizeof(int));

    return args;
}

int main() {
    pthread_t* vthread; //Hilo Coordinador
    int pg = 0, i, *ids;
    Producto vectorProd[10];
    int cantidadRegistros = 0;

    Cola* cola = crear_cola();

    encolar(cola, &vectorProd, sizeof(Producto)*10);
    encolar(cola, &cantidadRegistros, sizeof(int));

    printf("Cuantos procesos generadores quiere inicializar?\n");
    scanf("%d", &pg);

    vthread = malloc(sizeof(pthread_t)* pg);
    ids = malloc(pg* sizeof(int));
    if(!vthread || !ids){
        free(vthread);
        free(ids);
        return 0;
    }

    for(i = 0; i < pg; i++){
        ids[i] = i;
        if(pthread_create(&vthread[i], NULL, tarea, &cola) != 0 ){
            perror("thread create");
            free(vthread);
            free(ids);
            return 1;
        }

        Producto* vectorProdAux = (Producto*)desencolar(cola);
        cantidadRegistros = *(int*)desencolar(cola);

        cantidadRegistros+=10;

        encolar(cola,&vectorProdAux,sizeof(Producto)*10);
        encolar(cola, &cantidadRegistros, sizeof(int));
    }

    for(int j = 0; j < pg; j++){
        pthread_join(vthread[j], NULL);
    }
    free(ids);
    free(vthread);
    //registro coordinador -> padre //COLA -> ?
    //generar los procesos con pid 0 - 10 que producen datos de prueba // Cantidad procesos generadores
    //funciones asignan y escriben archivos csv

    Producto* vectorProdAux = (Producto*)desencolar(cola);
    cantidadRegistros = *(int*)desencolar(cola);

    for (int i = 0; i < cantidadRegistros; i++)
    {
        printf("VectorProdAux id = %d",vectorProdAux->id);
        printf("VectoProdAux estado = %c", vectorProdAux->Estado);
    }
    
    scanf("%d", &pg);

    return 0;
}
