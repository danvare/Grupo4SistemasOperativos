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
#include <sys/types.h>
#include <unistd.h>
#include "../Producto.h"
#include "../listaDinamica.h"

#ifdef __linux__
#include <sys/file.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// ====== Estado global ======
tLista g_lista_productos = NULL;
pthread_mutex_t g_productos_mutex;   // protege memoria en COMMIT
sem_t g_sem_conc, g_sem_trans;      // limita concurrencia real
unsigned max_transacciones = 1;     // solo 1 transaccion a la vez
const char* NOMBRE_ARCHIVO = "productos.csv";

// ====== Carga inicial ======
int cargar_productos_en_lista(tLista *pl) {
    int conteoLineas = 0;
    char linea[1024];
    Producto prod;

    FILE* f = fopen(NOMBRE_ARCHIVO, "r");
    if (!f) { perror("fopen productos.csv"); return 0; }

    crear_lista(pl);

    while (fgets(linea, sizeof linea, f)) {
        if (leerProductoDeLineaCSV(linea, &prod)) {
            prod.linea = conteoLineas++;
            if (poner_en_lista(pl, &prod, sizeof(Producto)) != TODO_OK) {
                destruir_lista(pl);
                fclose(f);
                return 0;
            }
        }
    }

    fclose(f);
    return 1;
}

// ====== Persistencia ======
static int persistir_csv_bloqueado_exclusivo_lista(tLista *pl) {
    int fd = open(NOMBRE_ARCHIVO, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return -1; }
#ifdef __linux__
    if (flock(fd, LOCK_EX) < 0) { perror("flock LOCK_EX"); close(fd); return -1; }
#endif
    FILE* f = fdopen(fd, "w");
    if (!f) {
        perror("fdopen");
#ifdef __linux__
        flock(fd, LOCK_UN);
#endif
        close(fd);
        return -1;
    }

    tNodo *p = *pl;
    while (p) {
        Producto *pr = (Producto*)p->info;
        escribirProductoEnCSV(f, pr);
        p = p->sig;
    }

    fflush(f);
    fsync(fd);
#ifdef __linux__
    flock(fd, LOCK_UN);
#endif
    fclose(f);
    return 0;
}

static int persistir_prod_csv_bloqueado_exclusivo(Producto *pr) {
    int fd = open(NOMBRE_ARCHIVO, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) { perror("open"); return -1; }
#ifdef __linux__
    if (flock(fd, LOCK_EX) < 0) { perror("flock LOCK_EX"); close(fd); return -1; }
#endif
    FILE* f = fdopen(fd, "a");
    if (!f) {
        perror("fdopen");
#ifdef __linux__
        flock(fd, LOCK_UN);
#endif
        close(fd);
        return -1;
    }

    escribirProductoEnCSV(f, pr);
    fflush(f);
    fsync(fd);
#ifdef __linux__
    flock(fd, LOCK_UN);
#endif
    fclose(f);
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

// Toma el semáforo con timeout simple (5s). Si está en TX, ya tiene el token.
static int sem_wait_timeout(sem_t *sem, int sock, bool in_tx) {
    if (in_tx) return 1;
    int cont = 0;
    while (sem_trywait(sem) != 0 && cont < 5) {
        cont++;
        sleep(1);
    }
    if (cont == 5) {
        send_line(sock, "ERR Servidor ocupado, intente luego\n");
        return 0; // no adquirido
    }
    return 1; // adquirido (el caller debe soltarlo si !in_tx)
}

static int mutex_lock_timeout(pthread_mutex_t *mutex, int sock) {
    int cont = 0;
    while (pthread_mutex_trylock(mutex) != 0 && cont < 5) {
        cont++;
        sleep(1);
    }
    if (cont == 5){
        send_line(sock, "ERR Servidor ocupado, intente luego\n");
        return 0; // timeout
    }
    return 1; // ok
}

// ====== Thread por cliente ======
typedef struct { int sock; } client_args_t;

void* client_thread(void* arg) {
    client_args_t* a = (client_args_t*)arg;
    int sock = a->sock;
    free(a);

    // limitar número real de clientes
    sem_wait(&g_sem_conc);

    bool in_tx = false;
    tLista shadow = NULL;

    send_line(sock, "Bienvenido. Comandos: BEGIN, GET idx, LIST, ADD texto, COMMIT TRANSACTION, ROLLBACK, DELETE id, QUIT\n");

    // helper para liberar el token de transacción si no estamos en TX
    #define RELEASE_IF_NONTX() do { if (!in_tx) sem_post(&g_sem_trans); } while(0)

    char line[2048];
    while (1) {
        int n = recv_line(sock, line, sizeof(line));
        if (n <= 0) break;

        if (!strncmp(line, "QUIT", 4)) {
            send_line(sock, "OK Bye\n");
            break;

        } else if (!strncmp(line, "BEGIN", 5)) {
            if (in_tx) { send_line(sock, "ERR Ya en transaccion\n"); continue; }
            // 1) tomar el token de transacción
            if (sem_wait_timeout(&g_sem_trans, sock, false) == 0) { continue; }
            // 2) tomar el mutex y preparar snapshot
            pthread_mutex_lock(&g_productos_mutex);
            crear_lista(&shadow);
            copiar_lista(&shadow, &g_lista_productos);
            send_line(sock, "OK BEGIN\n");
            in_tx = true;

        } else if (!strncmp(line, "COMMIT TRANSACTION", 18)) {
            if (!in_tx) { send_line(sock, "ERR No hay transaccion\n"); continue; }
            if (persistir_csv_bloqueado_exclusivo_lista(&shadow) == 0) {
                destruir_lista(&g_lista_productos);
                g_lista_productos = shadow;
                shadow = NULL;
                in_tx = false;
                send_line(sock, "OK COMMIT TRANSACTION\n");
            } else {
                send_line(sock, "ERR COMMIT TRANSACTION\n");
            }
            pthread_mutex_unlock(&g_productos_mutex);
            sem_post(&g_sem_trans);

        } else if (!strncmp(line, "ROLLBACK", 8)) {
            if (!in_tx) { send_line(sock, "ERR No hay transaccion\n"); continue; }
            destruir_lista(&shadow); shadow = NULL;
            in_tx = false;
            pthread_mutex_unlock(&g_productos_mutex);
            sem_post(&g_sem_trans);
            send_line(sock, "OK ROLLBACK\n");

        } else {
            // comandos fuera de TX toman token con timeout
            if (sem_wait_timeout(&g_sem_trans, sock, in_tx) == 0) continue;

            if (!strncmp(line, "LIST", 4)) {
                int cnt = 0;
                tNodo *p = in_tx ? shadow : g_lista_productos;

                for (tNodo *q = p; q; q = q->sig) cnt++;  // conteo NO destructivo

                char msg[64];
                snprintf(msg, sizeof msg, "OK %d items \n", cnt);
                send_line(sock, msg);

                for (; p != NULL; p = p->sig) {
                    Producto *pr = (Producto*)p->info;
                    if (pr) {
                        char buf[256];
                        snprintf(buf, sizeof buf, "%d,%s,%c,%d\n",
                                 pr->id, pr->nombre, pr->Estado, pr->cantidad);
                        send_line(sock, buf);
                    }
                }
                RELEASE_IF_NONTX();

            } else if (!strncmp(line, "GET ", 4)) {
                int idx = atoi(line + 4);
                Producto buscar, *pr;
                buscar.id = idx;
                if (in_tx)
                    pr = buscar_en_lista(&shadow, &buscar, sizeof(Producto), (Cmp)cmpId);
                else
                    pr = buscar_en_lista(&g_lista_productos, &buscar, sizeof(Producto), (Cmp)cmpId);

                if (!pr) send_line(sock, "ERR idx\n");
                else {
                    char buf[256];
                    snprintf(buf, sizeof buf, "%d,%s,%c,%d\n",
                             pr->id, pr->nombre, pr->Estado, pr->cantidad);
                    send_line(sock, "OK "); send_line(sock, buf);
                }
                RELEASE_IF_NONTX();

            } else if (!strncmp(line, "ADD ", 4)) {
                char* txt = line + 4;
                Producto pnew;
                if (!in_tx) {
                    if (mutex_lock_timeout(&g_productos_mutex, sock) == 0) {
                        RELEASE_IF_NONTX();
                        continue;
                    }
                    leerProductoDeLineaCSV(txt, &pnew);
                    persistir_prod_csv_bloqueado_exclusivo(&pnew);
                    poner_en_lista(&g_lista_productos, &pnew, sizeof(Producto));
                    pthread_mutex_unlock(&g_productos_mutex);
                } else {
                    leerProductoDeLineaCSV(txt, &pnew);
                    poner_en_lista(&shadow, &pnew, sizeof(Producto));
                }
                send_line(sock, "OK ADD\n");
                RELEASE_IF_NONTX();

            } else if (!strncmp(line, "UPDATE ", 7)) {
                char* txt = line + 7;
                Producto pnew;
                if (!in_tx) {
                    if (mutex_lock_timeout(&g_productos_mutex, sock) == 0) {
                        RELEASE_IF_NONTX();
                        continue;
                    }
                    leerProductoDeLineaCSV(txt, &pnew);
                    Producto *exist = buscar_en_lista(&g_lista_productos, &pnew, sizeof(Producto), (Cmp)cmpId);
                    if (!exist) {
                        pthread_mutex_unlock(&g_productos_mutex);
                        send_line(sock, "ERR No existe ID\n");
                        RELEASE_IF_NONTX();
                        continue;
                    }
                    exist->cantidad = pnew.cantidad;
                    exist->Estado = pnew.Estado;
                    persistir_csv_bloqueado_exclusivo_lista(&g_lista_productos);
                    pthread_mutex_unlock(&g_productos_mutex);
                } else {
                    leerProductoDeLineaCSV(txt, &pnew);
                    Producto *exist = buscar_en_lista(&shadow, &pnew, sizeof(Producto), (Cmp)cmpId);
                    if (!exist) {
                        send_line(sock, "ERR No existe ID\n");
                        RELEASE_IF_NONTX();
                        continue;
                    }
                    exist->cantidad = pnew.cantidad;
                    exist->Estado = pnew.Estado;
                }
                send_line(sock, "OK UPDATE\n");
                RELEASE_IF_NONTX();

            } else if (!strncmp(line, "DELETE ", 7)) {
                int idx = atoi(line + 7);
                Producto pr; pr.id = idx;
                if (!in_tx) {
                    if (mutex_lock_timeout(&g_productos_mutex, sock) == 0) {
                        RELEASE_IF_NONTX();
                        continue;
                    }
                    if (lista_buscar_y_eliminar(&g_lista_productos, &pr, (Cmp)cmpId) == TODO_OK) {
                        persistir_csv_bloqueado_exclusivo_lista(&g_lista_productos);
                        send_line(sock, "OK DELETE\n");
                    } else {
                        send_line(sock, "ERR No se pudo eliminar, no se encontro el producto en la lista\n");
                    }
                    pthread_mutex_unlock(&g_productos_mutex);
                    RELEASE_IF_NONTX();
                } else {
                    if (lista_buscar_y_eliminar(&shadow, &pr, (Cmp)cmpId) == TODO_OK)
                        send_line(sock, "OK DELETE\n");
                    else
                        send_line(sock, "ERR No se pudo eliminar, no se encontro el producto en la lista\n");
                }

            } else {
                send_line(sock, "ERR Comando\n");
                RELEASE_IF_NONTX();
            }
        }
    }

    // Limpieza robusta si el cliente se va en mitad de TX
    if (in_tx) {
        pthread_mutex_unlock(&g_productos_mutex);
        sem_post(&g_sem_trans);
        if (shadow) destruir_lista(&shadow);
        shadow = NULL;
        in_tx = false;
    } else {
        if (shadow) destruir_lista(&shadow);
    }

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
    if (!cargar_productos_en_lista(&g_lista_productos)) return 1;

    sem_init(&g_sem_conc, 0, (unsigned)max_concurrentes);
    sem_init(&g_sem_trans, 0, max_transacciones);

    int srv = crear_listen_socket((uint16_t)puerto, max_espera);
    printf("Servidor escuchando en 0.0.0.0:%d  (concurrencia=%d, backlog=%d)\n", puerto, max_concurrentes, max_espera);

    while (1) {
        struct sockaddr_in cli; socklen_t l = sizeof(cli);
        int cs = accept(srv, (struct sockaddr*)&cli, &l);
        if (cs < 0) { if (errno == EINTR) continue; perror("accept"); break; }

        client_args_t* a = malloc(sizeof *a);
        a->sock = cs;
        pthread_t th; pthread_create(&th, NULL, client_thread, a);
        pthread_detach(th);
    }

    sem_destroy(&g_sem_conc);
    sem_destroy(&g_sem_trans);
    close(srv);
    return 0;
}
