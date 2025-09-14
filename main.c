#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define BATCH 10

pthread_mutex_t printeo = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    int id;
    char Estado;
} Producto;


// S Stock
// N Sin stock
// C Critico
// R Reservado

char opcionRandom ()
{
    char estado[4] = "SNCR";
    return estado[(rand()%4)];
}

typedef struct
{
    long next_id;     // arranca en 1
    long max_id;      // total a generar
    pthread_mutex_t m;
} Coordinador;

typedef struct
{
    int id_hilo;
    Coordinador* coord;
    // acá podrías tener referencia a la cola de “registros” hacia el escritor CSV
} Args;

void coord_init(Coordinador* c, long total)
{
    c->next_id = 1;
    c->max_id  = total;
    pthread_mutex_init(&c->m, NULL);
}
// Devuelve cuántos IDs concedió (0 si no quedan). Llena out[0..n-1]
int pedir_bloque(Coordinador* c, long out[BATCH])
{
    pthread_mutex_lock(&c->m);
    if (c->next_id > c->max_id)
    {
        pthread_mutex_unlock(&c->m);
        return 0;
    }
    long start = c->next_id;
    long remaining = c->max_id - start + 1;
    int give = (remaining >= BATCH) ? BATCH : (int)remaining;
    for (int i = 0; i < give; ++i)
    {
        out[i] = c->next_id++;
    }
    pthread_mutex_unlock(&c->m);
    return give;
}


void* generador(void* p)
{
    Producto prod;
    Args* a = (Args*)p;
    long bloque[BATCH];
    for (;;)
    {
        int n = pedir_bloque(a->coord, bloque);
        if (n == 0) break; // no hay más IDs → terminar
        // Producís n registros con esos IDs (puede ser aleatorio el resto de campos)
        for (int i = 0; i < n; ++i)
        {
            long id = bloque[i];
            prod.id = id;
            prod.Estado = opcionRandom();
            // TODO: llenar Producto con 'id' y otros campos aleatorios
            // TODO: enviar registro al coordinador-escritor por otra cola/SHM
            // (por ahora, demostremos que no hay duplicados)
            // printf("[gen %d] ID=%ld\n", a->id_hilo, id);
            // crear mutex cuando se genera el csv (la razon viene en que cada proceso puede entrar a memoria compartida de manera aleatoria).
            //
            printf("ID: %d | ESTADO: %c\n", prod.id, prod.Estado);
        }
    }
    return NULL;
}

int main()
{
    int pg = 4;            // procesos/hilos generadores
    long total = 95;       // total de registros (IDs 1..95)
    pthread_t th[pg];
    Args args[pg];
    Coordinador coord;
    coord_init(&coord, total);
    for (int i = 0; i < pg; ++i)
    {
        args[i].id_hilo = i;
        args[i].coord = &coord;
        pthread_create(&th[i], NULL, generador, &args[i]);
    }
    for (int i = 0; i < pg; ++i) pthread_join(th[i], NULL);
    // acá te faltaría el lado escritor CSV, que va consumiendo registros
    // en el orden que lleguen (no hace falta que queden ordenados)
    return 0;
}
