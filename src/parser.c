#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "utils.h"

void parse_pipeline(char* line, char*** commands, int* num_commands) {
    char* token;
    int i = 0;

    // Eliminar salto de línea
    line[strcspn(line, "\n")] = '\0';

    token = strtok(line, "|");
    while (token != NULL && i < MAX_CMDS) {
        // Eliminar espacios al inicio y final
        char* start = token;
        while (*start == ' ') start++;
        char* end = start + strlen(start) - 1;
        while (end > start && *end == ' ') end--;
        *(end + 1) = '\0';

        // Dividir en argumentos
        char** args = malloc(MAX_ARGS * sizeof(char*));
        if (args == NULL) {
            error_exit("Error al asignar memoria para los argumentos");
        }
        int j = 0;
        char* arg = strtok(start, " \t");
        while (arg != NULL && j < MAX_ARGS - 1) {
            args[j++] = strdup(arg); // Duplicar la cadena para evitar problemas con strtok
            if (args[j-1] == NULL)
                error_exit("Error al asignar memoria para un argumento");
            arg = strtok(NULL, " \t");
        }
        args[j] = NULL; // NULL para execvp
        commands[i++] = args;

        token = strtok(NULL, "|");
    }

    *num_commands = i;
}
