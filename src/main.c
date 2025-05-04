#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "process.h"
#include "utils.h"

#define MAX_LINE 1024

int main() {
    char line[MAX_LINE];
    char*** commands;
    int num_commands;
	
	system("clear");
	printf("\033[1;37;44m");//colores ANSI
	printf("Bienvenido este es un MiniShell\n\n");
	
    while (1) {
        printf("(=^･ｪ･^=) > ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\nSaliendo del mini-shell...\n");
            break;
        }

        // Comando exit
        if (strncmp(line, "exit", 4) == 0) {
            break;
        }

        // Parsear la línea de entrada
        commands = malloc(MAX_CMDS * sizeof(char**));
        if (commands == NULL) {
            error_exit("Error al asignar memoria para los comandos");
        }
        parse_pipeline(line, commands, &num_commands);

        // Ejecutar la tubería de comandos
        if (num_commands > 0) {
            execute_pipeline(commands, num_commands);
        }

        // Liberar memoria
        free_string_array(commands, num_commands);
        free(commands);
    }

    return 0;
}
