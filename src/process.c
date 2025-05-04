#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "process.h"
#include "utils.h"

void execute_pipeline(char*** commands, int num_commands) {
    int i;
    int pipefd[2 * (num_commands - 1)];
    pid_t pid;

    // Crear tuberías
    for (i = 0; i < num_commands - 1; i++) {
        if (pipe(pipefd + i * 2) == -1) {
            error_exit("Error al crear la tubería");
        }
    }

    // Crear procesos y ejecutar comandos
    for (i = 0; i < num_commands; i++) {
        pid = fork();
        if (pid == -1) {
            error_exit("Error al hacer fork");
        }

        if (pid == 0) { // Proceso hijo
            // Redirección de entrada
            if (i > 0) {
                if (dup2(pipefd[(i - 1) * 2], STDIN_FILENO) == -1) {
                    error_exit("Error al redirigir la entrada");
                }
            }

            // Redirección de salida
            if (i < num_commands - 1) {
                if (dup2(pipefd[i * 2 + 1], STDOUT_FILENO) == -1) {
                    error_exit("Error al redirigir la salida");
                }
            }

            // Cerrar descriptores de tubería innecesarios
            for (int j = 0; j < 2 * (num_commands - 1); j++) {
                close(pipefd[j]);
            }

            // Ejecutar comando
            execvp(commands[i][0], commands[i]);
            perror("Error al ejecutar el comando"); // perror para el mensaje de error de execvp
            exit(EXIT_FAILURE);
        }
    }

    // Proceso padre: cerrar descriptores de tubería
    for (i = 0; i < 2 * (num_commands - 1); i++) {
        close(pipefd[i]);
    }

    // Esperar a los hijos
    for (i = 0; i < num_commands; i++) {
        wait(NULL);
    }
}
