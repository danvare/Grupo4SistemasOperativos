#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// ---- Declaración de Variables Globales ----
char** g_lista_productos = NULL;
int g_num_productos = 0;
pthread_mutex_t g_productos_mutex;
const char* NOMBRE_ARCHIVO = "productos.csv";

// ---- Prototipo de la función para cargar datos ----
int cargar_productos_desde_csv();


// ======================================================
// FUNCIÓN PRINCIPAL DEL SERVIDOR
// ======================================================
int main() {
    printf("Iniciando configuracion del servidor...\n");

    // ---- 1. OBTENER PARÁMETROS DEL USUARIO ----
    int max_concurrentes, max_espera;

    do {
        printf("Ingrese el numero maximo de usuarios concurrentes (ej: 5): ");
        scanf("%d", &max_concurrentes);
        if (max_concurrentes <= 0) {
            printf("El valor debe ser positivo.\n");
        }
    } while (max_concurrentes <= 0);

    do {
        printf("Ingrese el numero maximo de usuarios en espera (ej: 10): ");
        scanf("%d", &max_espera);
        if (max_espera < 0) {
            printf("El valor no puede ser negativo.\n");
        }
    } while (max_espera < 0);

    printf("Configuracion recibida: %d usuarios concurrentes, %d en espera.\n", max_concurrentes, max_espera);

    // ---- 2. INICIALIZAR HERRAMIENTAS DE CONCURRENCIA ----
    if (pthread_mutex_init(&g_productos_mutex, NULL) != 0) {
        perror("Error al inicializar el mutex");
        return 1; // Salir si no se puede crear el mutex
    }
    printf("Mutex inicializado correctamente.\n");

    // ---- 3. CARGAR DATOS DESDE EL ARCHIVO CSV ----
    if(!cargar_productos_desde_csv()) {
        return 1; //Por si no encuentra el archivo. Siempre debe existir
    }

    printf("Configuracion inicial completada. El servidor esta listo para el siguiente paso.\n");
    printf("======================================================\n");

    // Aquí comenzaría el Paso 2: Crear y Configurar el Socket...
    // iniciar_servidor(max_concurrentes, max_espera);

    // Al final del programa, no olvides liberar la memoria
    // ...
    // for(int i = 0; i < g_num_productos; i++) {
    //     free(g_lista_productos[i]);
    // }
    // free(g_lista_productos);
    // pthread_mutex_destroy(&g_productos_mutex);

    return 0;
}


// ======================================================
// FUNCIÓN PARA CARGAR LOS PRODUCTOS DESDE EL CSV
// ======================================================
int cargar_productos_desde_csv() {
    FILE* archivo = fopen(NOMBRE_ARCHIVO, "r");

    if (archivo == NULL) {
        printf("Archivo '%s' no encontrado\n", NOMBRE_ARCHIVO);
        return 0;
    }

    char linea[128]; // Un búfer para leer cada línea

    // Leer el archivo línea por línea
    while (fgets(linea, sizeof(linea), archivo) != NULL) {
        // Quitar el salto de línea '\n' que fgets() incluye al final
        linea[strcspn(linea, "\n")] = 0;

        // Ignorar líneas vacías
        if(strlen(linea) == 0) continue;

        // Agrandar nuestro arreglo dinámico de productos en 1
        g_lista_productos = realloc(g_lista_productos, sizeof(char*) * (g_num_productos + 1));
        if (g_lista_productos == NULL) {
            perror("Fallo al redimensionar la memoria para la lista de productos");
            exit(EXIT_FAILURE);
        }

        // Reservar memoria para la nueva cadena y copiarla
        g_lista_productos[g_num_productos] = malloc(strlen(linea) + 1);
        if (g_lista_productos[g_num_productos] == NULL) {
            perror("Fallo al reservar memoria para el producto");
            exit(EXIT_FAILURE);
        }
        strcpy(g_lista_productos[g_num_productos], linea);

        // Incrementar nuestro contador de productos
        g_num_productos++;
    }

    fclose(archivo);
    printf("Se cargaron %d productos desde '%s'.\n", g_num_productos, NOMBRE_ARCHIVO);
    return 1;
}
