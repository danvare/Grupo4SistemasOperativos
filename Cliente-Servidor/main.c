// gcc -pthread server.c -o server
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// ====== Estado global (reutiliza tu loader) ======
char** g_lista_productos = NULL;
int g_num_productos = 0;
pthread_mutex_t g_productos_mutex;   // protege memoria en COMMIT
sem_t g_sem_conc;                    // limita concurrencia real
const char* NOMBRE_ARCHIVO = "productos.csv";

// ====== Loader (igual al tuyo) ======
int cargar_productos_desde_csv(void) {
    FILE* archivo = fopen(NOMBRE_ARCHIVO, "r+");
    if (!archivo) { printf("Archivo '%s' no encontrado\n", NOMBRE_ARCHIVO); return 0; }
    char linea[1024];
    while (fgets(linea, sizeof(linea), archivo)) {
        linea[strcspn(linea, "\r\n")] = 0;
        if (!*linea) continue;
        char** nuevo = realloc(g_lista_productos, sizeof(char*) * (g_num_productos + 1));
        if (!nuevo) { perror("realloc"); exit(1); }
        g_lista_productos = nuevo;
        g_lista_productos[g_num_productos] = strdup(linea);
        if (!g_lista_productos[g_num_productos]) { perror("strdup"); exit(1); }
        g_num_productos++;
    }
    fclose(archivo);
    printf("Se cargaron %d productos desde '%s'.\n", g_num_productos, NOMBRE_ARCHIVO);
    return 1;
}

static int persistir_csv_bloqueado_exclusivo(char** lista, int n) {
    int fd = open(NOMBRE_ARCHIVO, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return -1; }
    if (flock(fd, LOCK_EX) < 0) { perror("flock LOCK_EX"); close(fd); return -1; }

    // Escribimos atómicamente todo
    FILE* f = fdopen(fd, "a");
    if (!f) { perror("fdopen"); flock(fd, LOCK_UN); close(fd); return -1; }
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", lista[i]);
    fflush(f);
    fsync(fd);
    // Desbloqueo y cierre
    flock(fd, LOCK_UN);
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

    // Estado de transacción por cliente
    bool in_tx = false;
    char** shadow = NULL; // snapshot/buffer de trabajo
    int shadow_n = 0;

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
            shadow_n = g_num_productos;
            shadow = malloc(sizeof(char*) * shadow_n);
            for (int i = 0; i < shadow_n; i++) shadow[i] = strdup(g_lista_productos[i]);
            pthread_mutex_unlock(&g_productos_mutex);
            in_tx = true;
            send_line(sock, "OK BEGIN\n");

        } else if (!strncmp(line, "LIST", 4)) {
            char msg[64];
            if (in_tx) {
                snprintf(msg, sizeof msg, "OK %d items (TX)\n", shadow_n); send_line(sock, msg);
                for (int i = 0; i < shadow_n; i++) { send_line(sock, shadow[i]); send_line(sock, "\n"); }
            } else {
                send_line(sock, "ERR Usar BEGIN primero\n");
                continue;
            }

        } else if (!strncmp(line, "GET ", 4)) {
            int idx = atoi(line + 4);
            if (in_tx) {
                if (idx < 0 || idx >= shadow_n) { send_line(sock, "ERR idx\n"); }
                else { send_line(sock, "OK "); send_line(sock, shadow[idx]); send_line(sock, "\n"); }
            } else {
                send_line(sock, "ERR Usar BEGIN primero\n");
                continue;
            }

        } else if (!strncmp(line, "ADD ", 4)) {
            if (!in_tx) { send_line(sock, "ERR Usar BEGIN primero\n"); continue; }
            char* txt = line + 4;
            char** nuevo = realloc(shadow, sizeof(char*) * (shadow_n + 1));
            if (!nuevo) { send_line(sock, "ERR mem\n"); continue; }
            shadow = nuevo;
            shadow[shadow_n++] = strdup(txt);
            send_line(sock, "OK ADD\n");

        } else if (!strncmp(line, "COMMIT TRANSACTION", 18)) {
            if (!in_tx) { send_line(sock, "ERR No hay transaccion\n"); continue; }
            // Persistimos con bloqueo exclusivo y swap in-memory
            pthread_mutex_lock(&g_productos_mutex);
            if (persistir_csv_bloqueado_exclusivo(shadow, shadow_n) == 0) {
                // Reemplazar en memoria
                for (int i = 0; i < g_num_productos; i++) free(g_lista_productos[i]);
                free(g_lista_productos);
                g_lista_productos = shadow;
                g_num_productos = shadow_n;
                shadow = NULL; shadow_n = 0;
                pthread_mutex_unlock(&g_productos_mutex);
                in_tx = false;
                send_line(sock, "OK COMMIT TRANSACTION\n");
            } else {
                pthread_mutex_unlock(&g_productos_mutex);
                send_line(sock, "ERR COMMIT TRANSACTION\n");
            }

        } else if (!strncmp(line, "ROLLBACK", 8)) {
            if (!in_tx) { send_line(sock, "ERR No hay transaccion\n"); continue; }
            for (int i = 0; i < shadow_n; i++) free(shadow[i]);
            free(shadow); shadow = NULL; shadow_n = 0;
            in_tx = false;
            send_line(sock, "OK ROLLBACK\n");

        } else {
            send_line(sock, "ERR Comando\n");
        }
    }

    // Limpieza
    if (shadow) { for (int i = 0; i < shadow_n; i++) free(shadow[i]); free(shadow); }
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
