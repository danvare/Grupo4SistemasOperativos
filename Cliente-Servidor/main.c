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

// ====== Estado global (reutiliza tu loader) ======
tLista g_lista_productos = NULL;
pthread_mutex_t g_productos_mutex;   // protege memoria en COMMIT
sem_t g_sem_conc,g_sem_trans;                    // limita concurrencia real
unsigned max_transacciones = 1; // solo 1 transaccion a la vez
const char* NOMBRE_ARCHIVO = "productos.csv";

// Use the list API from listaDinamica: poner_en_lista already inserts appropriately


// Cargar productos en una lista dinámica (tLista) usando las funciones del módulo listaDinamica
int cargar_productos_en_lista(tLista *pl) {
    int conteoLineas = 0;
    char linea[1024];
    Producto prod;

    FILE* f = fopen(NOMBRE_ARCHIVO, "r");
    if (!f) { perror("fopen productos.csv"); return 0; }

    crear_lista(pl);


    while (fgets(linea, sizeof linea, f)) {
        if (leerProductoDeLineaCSV(linea, &prod)) {
            prod.linea = conteoLineas;
            conteoLineas++;
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

static int persistir_csv_bloqueado_exclusivo_lista(tLista *pl) {
    int fd = open(NOMBRE_ARCHIVO, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return -1; }
#ifdef __linux__
    if (flock(fd, LOCK_EX) < 0) { perror("flock LOCK_EX"); close(fd); return -1; }
#endif

    // Escribimos atómicamente todo
    FILE* f = fdopen(fd, "w");
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

static int persistir_prod_csv_bloqueado_exclusivo(Producto *pr) {
    int fd = open(NOMBRE_ARCHIVO, O_APPEND, 0644);
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

        escribirProductoEnCSV(f, pr);

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

static int sem_wait_timeout(sem_t *sem,int sock, bool in_tx) {
    int cont=0; 
    if(in_tx) return 1; // ya tiene el semáforo
    while (sem_trywait(sem) != 0 && cont<=5) {
        cont++;
        sleep(1); // esperar a que haya un token disponible
    }
    // si pasaron más de 5 segundos, rechazamos la operación
    if(cont>5){
        send_line(sock, "ERR Servidor ocupado, intente luego\n");
        return 0;
    }else{
        sem_post(&g_sem_trans);
        return 1; // ok
    }

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
typedef struct {
    int sock;
} client_args_t;

void* client_thread(void* arg) {
    client_args_t* a = (client_args_t*)arg;
    int sock = a->sock;
    int sem_val,cont=0;
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
            pthread_mutex_lock(&g_productos_mutex);
            // Crear snapshot de trabajo (lecturas y cambios locales)
            crear_lista(&shadow);
            copiar_lista(&shadow, &g_lista_productos);
            sem_wait(&g_sem_trans);
            send_line(sock, "OK BEGIN\n");
            in_tx = true;
        } else if (!strncmp(line, "COMMIT TRANSACTION", 18)) {
            if (!in_tx) { send_line(sock, "ERR No hay transaccion\n"); continue; }
            // Persistimos con bloqueo exclusivo y swap in-memory
            if (persistir_csv_bloqueado_exclusivo_lista(&shadow) == 0) {
                // Reemplazar en memoria: destruir la lista anterior y asignar la nueva
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
            if (sem_wait_timeout(&g_sem_trans, sock, in_tx) == 0) {
                continue; // no pudo obtener el semáforo
            }

            if (!strncmp(line, "LIST", 4)) {
                char msg[64];
                int cnt;
                tNodo *p;

                if(in_tx){
                    cnt = lista_contar(&shadow);
                    p = shadow;
                }else{
                    cnt = lista_contar(&g_lista_productos);
                    p = g_lista_productos;
                }

                snprintf(msg, sizeof msg, "OK %d items \n", cnt); send_line(sock, msg);

                for (p; p != NULL; p = p->sig) {
                    Producto *pr = (Producto*)p->info;
                    if (pr) {
                        char buf[256];
                        // formatear como CSV (mismo formato que escribirProductoEnCSV)
                        snprintf(buf, sizeof buf, "%d,%s,%c,%d\n", pr->id, pr->nombre, pr->Estado, pr->cantidad);
                        send_line(sock, buf);
                    }
                }
            } else if (!strncmp(line, "GET ", 4)) {
                int idx = atoi(line + 4);
                Producto buscar,*pr;
                buscar.id = idx;
                if(in_tx){
                    pr = buscar_en_lista(&shadow, &buscar, sizeof(Producto), (Cmp)cmpId);
                }else{
                    pr = buscar_en_lista(&g_lista_productos, &buscar, sizeof(Producto), (Cmp)cmpId);
                }
                if (!pr) { send_line(sock, "ERR idx\n"); }
                else { char buf[256]; snprintf(buf, sizeof buf, "%d,%s,%c,%d\n", pr->id, pr->nombre, pr->Estado, pr->cantidad); send_line(sock, "OK "); send_line(sock, buf); }
            } else if (!strncmp(line, "ADD ", 4)) {
                char* txt = line + 4;
                Producto pnew;
                if (!in_tx) {
                    if (mutex_lock_timeout(&g_productos_mutex, sock) == 0) {
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
            } else if (!strncmp(line, "UPDATE ", 7)) {
                char* txt = line + 7;
                Producto pnew;
                if (!in_tx) {
                    if (mutex_lock_timeout(&g_productos_mutex, sock) == 0) {
                        continue;
                    }
                    leerProductoDeLineaCSV(txt, &pnew);
                    Producto *exist = buscar_en_lista(&g_lista_productos, &pnew, sizeof(Producto), (Cmp)cmpId);
                    if (!exist) {
                        pthread_mutex_unlock(&g_productos_mutex);
                        send_line(sock, "ERR No existe ID\n");
                        continue;
                    }
                    // Actualizar campos
                    exist->cantidad = pnew.cantidad;
                    exist->Estado = pnew.Estado;
                    persistir_csv_bloqueado_exclusivo_lista(&g_lista_productos);
                    pthread_mutex_unlock(&g_productos_mutex);
                } else {
                    leerProductoDeLineaCSV(txt, &pnew);
                    Producto *exist = buscar_en_lista(&shadow, &pnew, sizeof(Producto), (Cmp)cmpId);
                    if (!exist) {
                        send_line(sock, "ERR No existe ID\n");
                        continue;
                    }
                    // Actualizar campos
                    exist->cantidad = pnew.cantidad;
                    exist->Estado = pnew.Estado;
                }
                send_line(sock, "OK UPDATE\n");
            } else if (!strncmp(line, "DELETE ", 7)) {
                int idx = atoi(line + 7);
                Producto pr;
                pr.id = idx;
                if(!in_tx){
                    if (mutex_lock_timeout(&g_productos_mutex, sock) == 0) {
                        continue;
                    }
                    // Eliminar de la lista
                    // (implementar función lista_eliminar por id)
                    if (lista_buscar_y_eliminar(&g_lista_productos, &pr, (Cmp)cmpId) == TODO_OK) {
                        persistir_csv_bloqueado_exclusivo_lista(&g_lista_productos);
                        send_line(sock, "OK DELETE\n");
                    } else {
                        send_line(sock, "ERR No se pudo eliminar, no se encontro el producto en la lista\n");
                    }
                }else{
                    if (lista_buscar_y_eliminar(&shadow, &pr, (Cmp)cmpId) == TODO_OK) {
                        send_line(sock, "OK DELETE\n");
                    } else {
                        send_line(sock, "ERR No se pudo eliminar, no se encontro el producto en la lista\n");
                    }
                }
            }else {
                send_line(sock, "ERR Comando\n");
            }
        }
    }

    if(in_tx){
        pthread_mutex_unlock(&g_productos_mutex);
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
    if (!cargar_productos_en_lista(&g_lista_productos)) return 1;

    // Semáforo de concurrencia real
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
        pthread_detach(th); // no acumulamos joins
    }

    sem_destroy(&g_sem_conc);
    sem_destroy(&g_sem_trans);
    close(srv);
    // (Nunca llegamos acá en este demo) Ja
    return 0;
}
