#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

void* tarea(void* args){
    int id = *(int*)args;
    printf("Tarea hilo ID: %d\n", id);
    return args;
}

int main() {
    pthread_t* vthread;
    int pg = 0, i, *ids;

    printf("Cuantos procesos generadores quiere inicializar?\n");
    scanf("%d", &pg);

    vthread = malloc(sizeof(pthread_t)* pg);
    ids = malloc(pg* sizeof(int));
    if(!vthread || !ids){
        return 0;
    }
    for(i = 0; i < pg; i++){
        ids[i] = i;
        if(pthread_create(&vthread[i], NULL, tarea, &ids[i]) != 0 ){
            perror("thread create");
            return 1;
        }
    }

    for(int j = 0; j < pg; j++){
        pthread_join(vthread[j], NULL);
    }
    free(ids);
    free(vthread);
    //registro coordinador -> padre //COLA -> ?
    //generar los procesos con pid 0 - 10 que producen datos de prueba // Cantidad procesos generadores
    //funciones asignan y escriben archivos csv
    return 0;
}
