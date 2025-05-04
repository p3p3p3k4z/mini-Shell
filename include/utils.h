#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Imprime un mensaje de error y sale del programa.
 *
 * @param msg El mensaje de error a imprimir.
 */
void error_exit(const char* msg);

/**
 * @brief Libera la memoria asignada a un arreglo de arreglos de cadenas.
 *
 * @param arr El arreglo a liberar.
 * @param size El número de elementos en el arreglo.
 */
void free_string_array(char*** arr, int size);

#endif // UTILS_H
