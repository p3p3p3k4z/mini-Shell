#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <limits.h>

// Incluir las bibliotecas de readline
#include <readline/readline.h>
#include <readline/history.h>

// --- Definiciones de constantes ---
#define MAX_PROCESOS 10
#define MAX_ARG 40
#define MAX_LINEA_ENTRADA 1024

// --- Prototipos de funciones auxiliares ---
void error(const char *msg);
int dividir(char *cadena, char *lim, char *tokens[]);
void imprimir_tuberia(char *tokens[], int num_tokens);
char *generar_prompt();
void imprimir_bienvenida();

// ********************************************************************************
// Función para deshabilitar el modo de ratón en la terminal
void disable_mouse_reporting() {
    // Escape sequence para deshabilitar el reporte de eventos del ratón
    // "\033[?1000h" - Habilita el reporte del ratón (modo por defecto de muchos programas)
    // "\033[?1000l" - Deshabilita el reporte del ratón
    // "\033[?1002h" - Habilita el reporte del ratón con eventos de movimiento
    // "\033[?1003h" - Habilita el reporte del ratón con eventos de arrastre
    // Queremos asegurarnos de que esté deshabilitado.
    printf("\033[?1000l"); // Deshabilita el reporte básico del ratón (clics y scroll)
    printf("\033[?1002l"); // Deshabilita el reporte extendido (movimiento con clic)
    printf("\033[?1003l"); // Deshabilita el reporte extendido (arrastre)
    fflush(stdout); // Asegura que las secuencias se envíen inmediatamente
}
// ********************************************************************************


/**
 * @brief Función principal del minishell.
 *
 * Se encarga de la lectura de comandos, su parseo, el manejo de comandos internos
 * y la ejecución de comandos externos con soporte para tuberías.
 * Utiliza la librería readline para una interfaz de usuario mejorada.
 */
int main() {
    char linea[MAX_LINEA_ENTRADA];
    char *comandos[MAX_PROCESOS];
    char *argv[MAX_PROCESOS][MAX_ARG];
    int tubes[MAX_PROCESOS - 1][2];
    pid_t pid;
    int n_comandos;

    char *input_line;
    char *current_prompt;

    // ********************************************************************************
    // Llama a la función para deshabilitar el reporte de eventos del ratón
    // Esto debe hacerse antes de que readline tome el control de la terminal.
    disable_mouse_reporting();
    // ********************************************************************************

    // Imprime el mensaje de bienvenida al inicio del minishell.
    imprimir_bienvenida();

    // Bucle principal del minishell: Continúa ejecutándose hasta que el usuario decida salir.
    while (1) {
        // Genera y muestra el prompt dinámicamente.
        current_prompt = generar_prompt();
        input_line = readline(current_prompt); // Lee la línea de entrada con readline
        free(current_prompt); // Libera la memoria del prompt

        // Manejo de Ctrl+D (EOF): Si readline devuelve NULL.
        if (input_line == NULL) {
            printf("Saliendo del MiniShell.\n");
            fflush(stdout);
            break;
        }

        // Si la línea está vacía o solo contiene espacios en blanco, la ignora.
        if (strlen(input_line) == 0 || input_line[strspn(input_line, " \t\r\n")] == '\0') {
            free(input_line);
            continue;
        }

        // Añade la línea de comando al historial de readline.
        add_history(input_line);

        // Copia la línea leída por readline al buffer 'linea' para parseo.
        strncpy(linea, input_line, sizeof(linea) - 1);
        linea[sizeof(linea) - 1] = '\0'; // Asegura la terminación nula

        free(input_line); // Libera la memoria asignada por readline

        // Divide la línea de comando en comandos individuales por '|'.
        n_comandos = dividir(linea, "|", comandos);

        // Verifica si hay comandos válidos después de la división.
        if (n_comandos < 1 || comandos[0] == NULL || strlen(comandos[0]) == 0) {
            fprintf(stderr, "Error: No hay comandos válidos.\n");
            fflush(stderr);
            continue;
        }
        
        // Para cada comando individual, lo divide en argumentos.
        for (int i = 0; i < n_comandos; i++) {
            int argc = 0; // Contador de argumentos
            char *arg = strtok(comandos[i], " "); // Obtiene el primer argumento

            while (arg && argc < MAX_ARG - 1) {
                argv[i][argc++] = arg;
                arg = strtok(NULL, " ");
            }
            argv[i][argc] = NULL; // Termina el arreglo de argumentos con NULL
        }

        // --- Manejo de comandos internos (built-ins) ---
        if (strcmp(argv[0][0], "exit") == 0 || strcmp(argv[0][0], "quit") == 0) {
            printf("Saliendo del MiniShell.\n");
            fflush(stdout);
            break;
        } else if (strcmp(argv[0][0], "history") == 0) {
            // Muestra el historial de comandos de readline.
            history_set_pos(0);
            HIST_ENTRY *h_entry;
            int i = 0;
            while ((h_entry = history_get(i++)) != NULL) {
                printf("%d: %s\n", i, h_entry->line);
            }
            fflush(stdout);
            continue;
        } else if (strcmp(argv[0][0], "cd") == 0) { // Comando 'cd'
            if (argv[0][1] == NULL) {
                fprintf(stderr, "Uso: cd <directorio>\n");
                fflush(stderr);
            } else {
                if (chdir(argv[0][1]) == -1) { // chdir cambia el directorio de trabajo
                    perror("Error al cambiar de directorio");
                    fflush(stderr);
                }
            }
            continue;
        }
        // --- Fin manejo de comandos internos ---

        // Si hay más de un comando (tuberías), crea las tuberías necesarias.
        for (int i = 0; i < n_comandos - 1; i++) {
            if (pipe(tubes[i]) == -1) { // Crea un par de descriptores para la tubería
                perror("Error al crear la tubería");
                fflush(stderr);
                return 1;
            }
        }

        // Ejecuta cada comando en un proceso hijo separado.
        for (int i = 0; i < n_comandos; i++) {
            pid = fork(); // Crea un nuevo proceso hijo
            if (pid == -1) { // Error al crear el proceso hijo
                perror("Error al crear el proceso hijo");
                fflush(stderr);
                for (int j = 0; j < n_comandos - 1; j++) {
                    close(tubes[j][0]);
                    close(tubes[j][1]);
                }
                return 1;
            }

            if (pid == 0) { // CÓDIGO DEL PROCESO HIJO
                // Redirecciona la entrada/salida para tuberías.
                if (i > 0) { // Si no es el primer comando, redirige la entrada
                    dup2(tubes[i - 1][0], STDIN_FILENO);
                }
                if (i < n_comandos - 1) { // Si no es el último comando, redirige la salida
                    dup2(tubes[i][1], STDOUT_FILENO);
                }

                // Cierra todos los extremos de las tuberías en el proceso hijo.
                for (int j = 0; j < n_comandos - 1; j++) {
                    close(tubes[j][0]);
                    close(tubes[j][1]);
                }

                execvp(argv[i][0], argv[i]); // Ejecuta el comando
                perror("Error al ejecutar el comando");
                fflush(stderr);
                exit(EXIT_FAILURE); // El hijo termina con error si execvp falla
            }
        }

        // CÓDIGO DEL PROCESO PADRE
        // El padre cierra todos los descriptores de archivo de las tuberías.
        for (int i = 0; i < n_comandos - 1; i++) {
            close(tubes[i][0]);
            close(tubes[i][1]);
        }

        // El padre espera a que todos los procesos hijos terminen.
        for (int i = 0; i < n_comandos; i++) {
            wait(NULL);
        }
    }
    return 0; // El shell termina
}

// --- Implementación de funciones auxiliares ---

/**
 * @brief Imprime un mensaje de bienvenida ASCII art con colores.
 */
void imprimir_bienvenida() {
    printf("\033[1;36m"); // Color cian brillante
    printf("┏┳┓╻┏┓╻╻┏━┓╻ ╻┏━╸╻   ╻     ┏━╸┏━┓╻ ╻╻┏━┓┏━┓     ┏━┓\n");
    printf("┃┃┃┃┃┗┫┃┗━┓┣━┫┣╸ ┃   ┃     ┣╸ ┃┓┃┃ ┃┃┣━┛┃ ┃     ┗━┫\n");
    printf("╹ ╹╹╹ ╹╹┗━┛╹ ╹┗━╸┗━╸ ┗━╸   ┗━╸┗┻┛┗━┛╹╹  ┗━┛    #┗━┛\n");
    printf("\033[0m"); // Resetear color
    printf("\n");
    fflush(stdout); // Asegura que el mensaje se imprima inmediatamente
}

/**
 * @brief Genera y devuelve el string del prompt de la terminal.
 *
 * @return Un puntero a una cadena de caracteres que contiene el prompt generado.
 * Esta cadena debe ser liberada con `free()` por el llamador.
 */
char *generar_prompt() {
    char hostname[HOST_NAME_MAX + 1];
    char cwd[PATH_MAX + 1];
    struct passwd *pw;
    char *username = "unknown";
    char *home_dir = NULL;

    pw = getpwuid(geteuid());
    if (pw != NULL) {
        username = pw->pw_name;
        home_dir = pw->pw_dir;
    }

    if (gethostname(hostname, sizeof(hostname)) == -1) {
        strcpy(hostname, "unknown_host");
    }
    hostname[sizeof(hostname) - 1] = '\0';

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        strcpy(cwd, "unknown_path");
    }

    char display_cwd[PATH_MAX + 1];
    if (home_dir != NULL && strncmp(cwd, home_dir, strlen(home_dir)) == 0) {
        if (strlen(cwd) == strlen(home_dir) || cwd[strlen(home_dir)] == '/') {
            snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home_dir));
        } else {
            strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
            display_cwd[sizeof(display_cwd) - 1] = '\0';
        }
    } else {
        strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
        display_cwd[sizeof(display_cwd) - 1] = '\0';
    }

    char *prompt_str = (char *)malloc(MAX_LINEA_ENTRADA + HOST_NAME_MAX + PATH_MAX + 10);
    if (prompt_str == NULL) {
        perror("malloc para prompt");
        return strdup("> ");
    }

    snprintf(prompt_str, MAX_LINEA_ENTRADA + HOST_NAME_MAX + PATH_MAX + 10, "\033[7;32m%s@%s\033[0m:\033[7;34m%s\033[0m$ ", username, hostname, display_cwd);

    return prompt_str;
}

/**
 * @brief Imprime un mensaje de error en la salida de error estándar (stderr).
 * @param msg El mensaje de error a imprimir.
 */
void error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    fflush(stderr);
}

/**
 * @brief Divide una cadena de caracteres en tokens basándose en un delimitador.
 * @param cadena La cadena a dividir (se modifica).
 * @param lim El delimitador a usar para la división.
 * @param tokens[] Un arreglo de punteros donde se almacenarán los tokens encontrados.
 * @return El número de tokens encontrados.
 */
int dividir(char *cadena, char *lim, char *tokens[]) {
    int conta = 0;
    char *token = strtok(cadena, lim);
    while (token != NULL && conta < MAX_PROCESOS) {
        if (strlen(token) > 0) {
            tokens[conta++] = token;
        }
        token = strtok(NULL, lim);
    }
    tokens[conta] = NULL;
    return conta;
}

/**
 * @brief (Función de depuración) Imprime los comandos que forman una tubería.
 * @param tokens[] Un arreglo de punteros a los comandos.
 * @param num_tokens El número de comandos en el arreglo.
 */
void imprimir_tuberia(char *tokens[], int num_tokens) {
    printf("Comandos en la tubería: ");
    for (int i = 0; i < num_tokens; i++) {
        printf("%s", tokens[i]);
        if (i < num_tokens - 1) {
            printf(" | ");
        }
    }
    printf("\n");
    fflush(stdout);
}


// --- GLOSARIO DE FUNCIONES DE SISTEMA CLAVE ---

/*
 * fork(): Crea un nuevo proceso hijo.
 */

/*
 * execvp(const char *file, char *const argv[]): Reemplaza la imagen del proceso actual con un nuevo programa.
 */

/*
 * pipe(int fd[2]): Crea un canal de comunicación unidireccional (tubería).
 */

/*
 * dup2(int oldfd, int newfd): Duplica un descriptor de archivo existente a un nuevo descriptor de archivo.
 */

/*
 * wait(int *status): Suspende la ejecución del proceso padre hasta que uno de sus procesos hijos termine.
 */

/*
 * geteuid(): Retorna el ID de usuario efectivo del proceso que llama.
 */

/*
 * getpwuid(uid_t uid): Retorna un puntero a una estructura `passwd` con información del usuario por ID.
 */

/*
 * gethostname(char *name, size_t len): Obtiene el nombre del host del sistema.
 */

/*
 * getcwd(char *buf, size_t size): Obtiene el directorio de trabajo actual.
 */

/*
 * chdir(const char *path): Cambia el directorio de trabajo actual del proceso.
 */
