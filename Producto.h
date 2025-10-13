#ifndef PRODUCTO_H_INCLUDED
#define PRODUCTO_H_INCLUDED
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  int id;
  char Estado;
  int cantidad;
  char nombre[64];
} Producto;

// S: Stock
// N: Sin stock
// C: Critico
// R: Reservado

char obtenerEstado(int cantidadProd);
int opcionRandomCantidad();
void opcionNombreRandom(char* destino);
void escribirProductoEnCSV(FILE* archivo, Producto* prod);
Producto* leerProductoDeLineaCSV(char* linea, Producto* prod);
int cmp(const void* a, const void* b);

#endif