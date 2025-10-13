// gcc -pthread server.c -o server
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include "../Producto.h"
#include "../listaDinamica.h"

#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// ====== Estado global (reutiliza tu loader) ======
tLista g_lista_productos = NULL;
pthread_mutex_t g_productos_mutex;   // protege memoria en COMMIT
sem_t g_sem_conc;                    // limita concurrencia real
const char* NOMBRE_ARCHIVO = "productos.csv";

// Helpers para manipular tLista localmente
static int lista_contar(tLista *pl) {
    int cnt = 0;
    tNodo *p = *pl;
    while (p) { cnt++; p = p->sig; }
    return cnt;
}

static Producto* lista_get(tLista *pl, int idx) {
    int i = 0;
    tNodo *p = *pl;
    while (p) {
        if (i == idx) return (Producto*)p->info;
        i++; p = p->sig;
    }
    return NULL;
}

// Use the list API from listaDinamica: poner_en_lista already inserts appropriately


// Cargar productos en una lista dinámica (tLista) usando las funciones del módulo listaDinamica
int cargar_productos_en_lista(tLista *pl) {
    FILE* f = fopen(NOMBRE_ARCHIVO, "r");
    if (!f) { perror("fopen productos.csv"); return 0; }

    crear_lista(pl);
    char linea[1024];
    Producto prod;

    while (fgets(linea, sizeof linea, f)) {
        if (leerProductoDeLineaCSV(linea, &prod)) {
            if (poner_en_lista(pl, &prod, sizeof(Producto)) != TODO_OK) {
                // fallo memoria: limpiar y salir
                destruir_lista(pl);
                fclose(f);
                return 0;
            }
        }
    }

    fclose(f);
    return 1;
}

// wrapper que usa la lista global
int cargar_productos_desde_csv(void) {
    return cargar_productos_en_lista(&g_lista_productos);
}

static int persistir_csv_bloqueado_exclusivo_lista(tLista *pl) {
    int fd = open(NOMBRE_ARCHIVO, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return -1; }
#ifdef __linux__
    if (flock(fd, LOCK_EX) < 0) { perror("flock LOCK_EX"); close(fd); return -1; }
#endif

    // Escribimos atómicamente todo
    FILE* f = fdopen(fd, "a");
    if (!f) { perror("fdopen");
#ifdef __linux__
        flock(fd, LOCK_UN);
#endif
        close(fd); return -1; }

    // iterar la lista y escribir cada producto
    tNodo *p = *pl;
    while (p) {
        Producto *pr = (Producto*)p->info;
        escribirProductoEnCSV(f, pr);
        p = p->sig;
    }

    fflush(f);
    fsync(fd);
    // Desbloqueo y cierre
#ifdef __linux__
    flock(fd, LOCK_UN);
#endif
    fclose(f); // cierra también fd
    return 0;
}

// ====== Utilidades protocolo ======
static ssize_t send_line(int sock, const char* s) {
    return send(sock, s, strlen(s), 0);
}
static int recv_line(int sock, char* buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c; ssize_t r = recv(sock, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = 0;
    return (int)i;
}

// ====== Thread por cliente ======
typedef struct {
    int sock;
} client_args_t;

void* client_thread(void* arg) {
    client_args_t* a = (client_args_t*)arg;
    int sock = a->sock;
    free(a);

    // Control de concurrencia real
    sem_wait(&g_sem_conc);

    // Estado de transacción por cliente (snapshot como tLista)
    bool in_tx = false;
    tLista shadow = NULL; // lista temporal de Producto

    send_line(sock, "Bienvenido. Comandos: BEGIN, GET idx, LIST, ADD texto, COMMIT TRANSACTION, ROLLBACK, QUIT\n");

    char line[2048];
    while (1) {
        int n = recv_line(sock, line, sizeof(line));
        if (n <= 0) break;

        // Normalizamos
        if (!strncmp(line, "QUIT", 4)) {
            send_line(sock, "OK Bye\n");
            break;
        } else if (!strncmp(line, "BEGIN", 5)) {
            if (in_tx) { send_line(sock, "ERR Ya en transaccion\n"); continue; }
            // Crear snapshot de trabajo (lecturas y cambios locales)
            pthread_mutex_lock(&g_productos_mutex);
            // copiar cada Producto de g_lista_productos a shadow
            tNodo *p = g_lista_productos;
            while (p) {
                Producto *orig = (Producto*)p->info;
                poner_en_lista(&shadow, orig, sizeof(Producto));
                p = p->sig;
            }
            pthread_mutex_unlock(&g_productos_mutex);
            in_tx = true;
            send_line(sock, "OK BEGIN\n");

        } else if (!strncmp(line, "LIST", 4)) {
            char msg[64];
            if (in_tx) {
                int cnt = lista_contar(&shadow);
                snprintf(msg, sizeof msg, "OK %d items (TX)\n", cnt); send_line(sock, msg);
                for (int i = 0; i < cnt; i++) {
                    Producto *pr = lista_get(&shadow, i);
                    if (pr) {
                        char buf[256];
                        // formatear como CSV (mismo formato que escribirProductoEnCSV)
                        snprintf(buf, sizeof buf, "%d,%s,%c,%d\n", pr->id, pr->nombre, pr->Estado, pr->cantidad);
                        send_line(sock, buf);
                    }
                }
            } else {
                send_line(sock, "ERR Usar BEGIN primero\n");
                continue;
            }

        } else if (!strncmp(line, "GET ", 4)) {
            int idx = atoi(line + 4);
            if (in_tx) {
                Producto *pr = lista_get(&shadow, idx);
                if (!pr) { send_line(sock, "ERR idx\n"); }
                else { char buf[256]; snprintf(buf, sizeof buf, "%d,%s,%c,%d\n", pr->id, pr->nombre, pr->Estado, pr->cantidad); send_line(sock, "OK "); send_line(sock, buf); }
            } else {
                send_line(sock, "ERR Usar BEGIN primero\n");
                continue;
            }

        } else if (!strncmp(line, "ADD ", 4)) {
            if (!in_tx) { send_line(sock, "ERR Usar BEGIN primero\n"); continue; }
            // txt expected as CSV line for a Producto
            char* txt = line + 4;
            Producto pnew;
            if (!leerProductoDeLineaCSV(txt, &pnew)) { send_line(sock, "ERR formato\n"); continue; }
            if (poner_en_lista(&shadow, &pnew, sizeof(Producto)) != TODO_OK) { send_line(sock, "ERR mem\n"); continue; }
            send_line(sock, "OK ADD\n");

        } else if (!strncmp(line, "COMMIT TRANSACTION", 18)) {
            if (!in_tx) { send_line(sock, "ERR No hay transaccion\n"); continue; }
            // Persistimos con bloqueo exclusivo y swap in-memory
            pthread_mutex_lock(&g_productos_mutex);
            if (persistir_csv_bloqueado_exclusivo_lista(&shadow) == 0) {
                // Reemplazar en memoria: destruir la lista anterior y asignar la nueva
                destruir_lista(&g_lista_productos);
                g_lista_productos = shadow;
                shadow = NULL;
                pthread_mutex_unlock(&g_productos_mutex);
                in_tx = false;
                send_line(sock, "OK COMMIT TRANSACTION\n");
            } else {
                pthread_mutex_unlock(&g_productos_mutex);
                send_line(sock, "ERR COMMIT TRANSACTION\n");
            }

        } else if (!strncmp(line, "ROLLBACK", 8)) {
            if (!in_tx) { send_line(sock, "ERR No hay transaccion\n"); continue; }
            destruir_lista(&shadow); shadow = NULL;
            in_tx = false;
            send_line(sock, "OK ROLLBACK\n");

        } else {
            send_line(sock, "ERR Comando\n");
        }
    }

    // Limpieza
    if (shadow) destruir_lista(&shadow);
    close(sock);
    sem_post(&g_sem_conc);
    return NULL;
}

// ====== Socket server ======
static int crear_listen_socket(uint16_t port, int backlog) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); exit(1); }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(s); exit(1); }
    if (listen(s, backlog) < 0) { perror("listen"); close(s); exit(1); }
    return s;
}

int main() {
    printf("Iniciando configuracion del servidor...\n");

    int max_concurrentes, max_espera, puerto;
    do { printf("Max usuarios concurrentes: "); scanf("%d", &max_concurrentes); } while (max_concurrentes <= 0);
    do { printf("Max usuarios en espera (backlog): "); scanf("%d", &max_espera); } while (max_espera < 0);
    do { printf("Puerto a escuchar (ej 5050): "); scanf("%d", &puerto); } while (puerto <= 0 || puerto > 65535);

    pthread_mutex_init(&g_productos_mutex, NULL);
    if (!cargar_productos_desde_csv()) return 1;

    // Semáforo de concurrencia real
    sem_init(&g_sem_conc, 0, (unsigned)max_concurrentes);

    int srv = crear_listen_socket((uint16_t)puerto, max_espera);
    printf("Servidor escuchando en 0.0.0.0:%d  (concurrencia=%d, backlog=%d)\n", puerto, max_concurrentes, max_espera);

    while (1) {
        struct sockaddr_in cli; socklen_t l = sizeof(cli);
        int cs = accept(srv, (struct sockaddr*)&cli, &l);
        if (cs < 0) { if (errno == EINTR) continue; perror("accept"); break; }

        client_args_t* a = malloc(sizeof *a);
        a->sock = cs;
        pthread_t th; pthread_create(&th, NULL, client_thread, a);
        pthread_detach(th); // no acumulamos joins
    }

    close(srv);
    // (Nunca llegamos acá en este demo)
    return 0;
}
