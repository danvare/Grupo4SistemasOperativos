#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h> // Necesario para srand
#include "Producto.h"

#define BATCH 10

// Mutex para proteger la escritura en el archivo CSV
pthread_mutex_t csv_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *csv_file; // Puntero al archivo, será global para que los hilos lo vean

typedef struct
{
    long next_id;   // arranca en 1
    long max_id;    // total a generar
    pthread_mutex_t m;
} Coordinador;

typedef struct
{
    int id_hilo;
    Coordinador* coord;
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
    pthread_mutex_lock(&c->m); // Se aplica el mutex para que no se repitan IDs en otros hilos
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

        // Producís n registros con esos IDs
        for (int i = 0; i < n; ++i)
        {
            long id = bloque[i];
            prod.id = id;
            prod.cantidad = opcionRandomCantidad();
            prod.Estado =
            (prod.cantidad);
            opcionNombreRandom(prod.nombre);


            // --- Inicio de la sección crítica para escribir en el archivo ---
            pthread_mutex_lock(&csv_mutex);

            // Escribir la línea en el archivo CSV
            fprintf(csv_file, "%d, %s, %c, %d\n", prod.id, prod.nombre, prod.Estado, prod.cantidad);

            // Liberar el mutex para que otro hilo pueda escribir
            pthread_mutex_unlock(&csv_mutex);
            // --- Fin de la sección crítica ---
        }
    }
    return NULL;
}

int main() {
    srand(time(NULL)); // Inicializar la semilla para números aleatorios

    int pg;      // procesos/hilos generadores
    long total;  // total de registros (IDs 1..95)

    puts("Ingresar numero de procesos generadores");
    scanf("%d",&pg);

    puts("Ingresar numero de registros totales");
    scanf("%ld",&total);

    pthread_t th[pg];
    Args args[pg];
    Coordinador coord;

    // Abrir el archivo en modo escritura ("w")
    csv_file = fopen("productos.csv", "w");
    if (csv_file == NULL)
    {
        perror("Error al abrir el archivo CSV");
        return 1;
    }

    // Escribir la cabecera del CSV
    fprintf(csv_file, "ID,Nombre,Estado,Cantidad\n");

    coord_init(&coord, total);
    for (int i = 0; i < pg; ++i)
    {
        args[i].id_hilo = i;
        args[i].coord = &coord;
        pthread_create(&th[i], NULL, generador, &args[i]);
    }

    for (int i = 0; i < pg; ++i)
    {
        pthread_join(th[i], NULL);
    }

    // Cerrar el archivo después de que todos los hilos hayan terminado
    fclose(csv_file);
    pthread_mutex_destroy(&csv_mutex); // Buena práctica destruir el mutex

    printf("Archivo 'productos.csv' generado exitosamente con %ld registros.\n", total);

    return 0;
}
