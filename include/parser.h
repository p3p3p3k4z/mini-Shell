#ifndef PARSER_H
#define PARSER_H

#define MAX_CMDS 10
#define MAX_ARGS 64

/**
 * @brief Parsea la línea de entrada del usuario.
 *
 * @param line La línea de entrada.
 * @param commands Un arreglo de arreglos de cadenas para guardar los comandos y argumentos.
 * @param num_commands Puntero para guardar el número de comandos.
 */
void parse_pipeline(char* line, char*** commands, int* num_commands);

#endif // PARSER_H
