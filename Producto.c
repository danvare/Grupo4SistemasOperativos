#include "Producto.h"

char obtenerEstado(int cantidadProd)
{
    char estado[4] = "SNCR";
    return estado[rand() % 4];
}

int opcionRandomCantidad()
{
    int cantidadAElegir[] = {10,20,25,15,5,2,1,0};
    return cantidadAElegir[rand() % 8];
}

void opcionNombreRandom(char* destino)
{
    char nombres[][64] = {"Huevos\0","Manzanas\0","Aceite\0","Yerba Mate\0", "Agua\0"};

    strcpy(destino,nombres[rand() % 5]);
}

void escribirProductoEnCSV(FILE* archivo, Producto* prod)
{
    fprintf(archivo, "%d,%s,%c,%d\n", prod->id, prod->nombre, prod->Estado, prod->cantidad);
}

Producto* leerProductoDeLineaCSV(char* linea, Producto* prod)
{
    if (sscanf(linea, "%d,%63[^,],%c,%d\n", &prod->id, prod->nombre, &prod->Estado, &prod->cantidad) == 4)
    {
        return prod;
    }
    return NULL;
}

int cmpId(const void* a, const void* b)
{
    const Producto* prodA = (const Producto*)a;
    const Producto* prodB = (const Producto*)b;

    if (prodA->id < prodB->id) return -1;
    if (prodA->id > prodB->id) return 1;
    return 0;
}