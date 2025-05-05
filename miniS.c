/*
 PROGRAMA: Mini-Shell
 INTEGRANTES: Espina Ramirez Ariadna Betsabe
							Ramirez Gallardo Mario Enrique
							Perez Zurita Irving Tristan
 COMPILACION: gcc -Wall -o "miniS" "miniS.c"
 FECHA: 05-05-2025
 */

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
    int n_comandos; // Número de comandos separados

    printf("\t\t==Bienvenido este es un MiniShell.==\n\n");

    while (1) {
        printf(" > ");
        //Leemos la linea
        if (fgets(linea, sizeof(linea), stdin) == NULL) {
            printf("\nSaliendo del MiniShell.\n");
            break;
        }
        //quitar los saltos y cambiarlos por valor nulo
        linea[strcspn(linea, "\n")] = '\0';

        if (strlen(linea) == 0) { //Si el usuario no escribe nada, vuelve a pedir
            continue;
        }

		//dividimos cada linea por "|" y le enviamos el arrglo comandos
        n_comandos = dividir(linea, "|", comandos); 

        //Si no se detecto almenos un comando 
        if (n_comandos < 1) {
            fprintf(stderr, "Error: No hay comandos válidos.\n");
            continue;
        }

		//Imprimir la primera tubería
        imprimir_tuberia(comandos, n_comandos);
        
		//Aquí se separa cada comando en palabras
        for (int i = 0; i < n_comandos; i++) {
            int argc = 0;
            //dividimos la cadena en tokens con el delimitador, en este caso el delimitador " "
            char *arg = strtok(comandos[i], " ");
            
            while (arg && argc < MAX_ARG - 1) {
                argv[i][argc++] = arg;
                //Al final le agregamos un null
                arg = strtok(NULL, "  ");
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
			//Con fork creamos el proceso hijo
            pid = fork();
            //Comprobamos que no exista algun error
            if (pid == -1) {
                perror("Error al crear el proceso hijo");
                return 1;
            }

            if (pid == 0) { // Si ha funcionado seguimos:
				
                // Redirigir la entrada si no es el primer comando
                if (i > 0) {
					//con dup2  el extremo escritur [0] se redirecciona a la entrada estandar
                    dup2(tubes[i - 1][0], STDIN_FILENO);
                }

                
                if (i < n_comandos - 1) {
					//Con la tuberia en su extremo de lectura [1] se redirecciona a la salida estandar
                    dup2(tubes[i][1], STDOUT_FILENO);
                }

                // Cerrar los extremos  que no se ocuparan ya 
                for (int j = 0; j < n_comandos - 1; j++) {
                    close (tubes[j][0]);
                    close (tubes[j][1]);
                }

               //Enviar el comando y el puntero donde termina cada uno 
                execvp(argv[i][0], argv[i]);
                perror("Error al ejecutar el comando");
            }
        }

        // El padre debe cerrar las tuberias
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


int dividir(char *cadena, char *lim, char *tokens[]) {
    int conta = 0;
    //cada "palabra" se guardara en una posicion del arreglo token
    //el lim es el limitante que se ha enviado "|"
    char *token = strtok(cadena, lim);
    while (token != NULL && conta < MAX_PROCESOS) {
        tokens[conta++] = token; //Guarda el token en el arreglo tokens
        token = strtok(NULL, lim);//Se llama de nuevo a strtok, pero con NULL para continuar donde quedó
    }
    //terminar el ultimo arreglo con null
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
     
