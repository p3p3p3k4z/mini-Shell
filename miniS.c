#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_PROCESOS 5
#define MAX_ARG 40

void error(const char *msg);
int dividir(char *cadena, char *lim, char *tokens[]);
void imprimir_tuberia(char *tokens[], int num_tokens);

int main() {
    char linea[1024];  // Guarda lo que escribe el usuario
    char *comandos[MAX_PROCESOS]; // Guarda cada comando separado por '|'
    char *argv[MAX_PROCESOS][MAX_ARG]; //Un puntero doble para guardar los argumentos de cada comando
    int tubes[MAX_PROCESOS - 1][2]; // Matriz de tuberías para comunicar procesos
    pid_t pid;
    int n_comandos; // Número de comandos separados por '|'

    printf("Hola este es un MiniShell.\n");

    while (1) {
        printf("> ");
        if (fgets(linea, sizeof(linea), stdin) == NULL) {
            printf("\nSaliendo del MiniShell.\n");
            break;
        }
        linea[strcspn(linea, "\n")] = '\0';

        if (strlen(linea) == 0) { //Si el usuario no escribe nada, vuelve a pedir
            continue;
        }

        n_comandos = dividir(linea, "|", comandos); //mochar 
        
        if (n_comandos < 1) {
            fprintf(stderr, "Error: No hay comandos válidos.\n");
            continue;
        }

        imprimir_tuberia(comandos, n_comandos);
	 //Aquí se separa cada comando en palabras
        for (int i = 0; i < n_comandos; i++) {
            int argc = 0;
            char *arg = strtok(comandos[i], " ");
            while (arg && argc < MAX_ARG - 1) {
                argv[i][argc++] = arg;
                arg = strtok(NULL, " ");
            }
            argv[i][argc] = NULL;
        }

        // Crear las tuberías necesarias para conectar los comandos
        for (int i = 0; i < n_comandos - 1; i++) {
            if (pipe(tubes[i]) == -1) {
                perror("Error al crear la tubería");
                return 1;
            }
        }

        // Ejecutar cada comando en un proceso hijo para cada comando
        for (int i = 0; i < n_comandos; i++) {
            pid = fork();
            if (pid == -1) {
                perror("Error al crear el proceso hijo");
                return 1;
            }

            if (pid == 0) { // Proceso hijo
                // Redirigir la entrada si no es el primer comando
                if (i > 0) {
                    dup2(tubes[i - 1][0], STDIN_FILENO);//recibe
                }

                // Redirigir la salida si no es el último comando
                if (i < n_comandos - 1) {
                    dup2(tubes[i][1], STDOUT_FILENO);//sale
                }

                // Cerrar los extremos INNECESARIOS de TODAS las tuberías en el hijo
                for (int j = 0; j < n_comandos - 1; j++) {
                    if (i == 0) { // Primer comando: solo cierra los extremos de lectura
                        close(tubes[j][0]);
                    } else if (i == n_comandos - 1) { // Último comando: solo cierra los extremos de escritura
                        close(tubes[j][1]);
                    } else { // Comando intermedio: cierra el extremo de lectura de la tubería anterior y el extremo de escritura de la tubería siguiente
                        if (j != i - 1) close(tubes[j][0]);
                        if (j != i) close(tubes[j][1]);
                    }
                }

                execvp(argv[i][0], argv[i]);
                perror("Error al ejecutar el comando");
                exit(EXIT_FAILURE);
            }
        }

        // El padre debe cerrar todos los descriptores de archivo de las tuberías
        for (int i = 0; i < n_comandos - 1; i++) {
            close(tubes[i][0]);
            close(tubes[i][1]);
        }

        // El padre debe esperar a todos los procesos hijos
        for (int i = 0; i < n_comandos; i++) {
            wait(NULL);
        }
    }

    return 0;
}

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int dividir(char *cadena, char *lim, char *tokens[]) {
    int conta = 0;
    char *token = strtok(cadena, lim);
    while (token != NULL && conta < MAX_PROCESOS) {
        tokens[conta++] = token; //Guarda el token en el arreglo tokens
        token = strtok(NULL, lim);//Se llama de nuevo a strtok, pero con NULL para continuar donde quedó
    }
    tokens[conta] = NULL;
    return conta;
}

void imprimir_tuberia(char *tokens[], int num_tokens) {
    printf("Comandos en la tubería: ");
    for (int i = 0; i < num_tokens; i++) {
        printf("%s", tokens[i]);
        if (i < num_tokens - 1) {
            printf(" | ");
        }
    }
    printf("\n");
}
