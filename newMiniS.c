#include <stdio.h>      
#include <stdlib.h>     
#include <unistd.h>     // Funciones de sistema POSIX (fork, execvp, pipe, dup2, chdir, gethostname, geteuid)
#include <string.h>     // Funciones de manipulación de cadenas (strlen, strcmp, strncpy, strtok, strspn, strdup)
#include <sys/wait.h>   // Funciones para esperar cambios de estado en procesos hijos (wait, waitpid)
#include <errno.h>      // Para manejar códigos de error del sistema (errno, EINTR)
#include <pwd.h>        // Para obtener información de usuario (getpwuid)
#include <limits.h>     // Para límites del sistema (PATH_MAX, HOST_NAME_MAX)
#include <fcntl.h>      // Para open, close, y flags como O_RDONLY, O_WRONLY, O_CREAT, O_APPEND
#include <signal.h>     // Para manejo de señales (signal, sigaction, kill)

// Incluir las bibliotecas de readline
#include <readline/readline.h> // Para leer líneas de entrada con edición y historial
#include <readline/history.h>  // Para gestionar el historial de comandos

// --- Definiciones de constantes ---
#define MAX_COMANDOS 10         // Número máximo de comandos que se pueden encadenar con tuberías o &&
#define MAX_ARGUMENTOS 40              // Número máximo de argumentos por comando
#define MAX_LONGITUD_ENTRADA 1024  // Tamaño máximo del buffer para la línea de entrada del usuario
#define MAX_SEGMENTOS_AND 5        // Número máximo de segmentos separados por '&&'

// --- ENUM para tipos de redirección/operación ---
typedef enum {
    SIN_REDIR = 0,         // Sin redirección o operador especial
    REDIR_ENTRADA,          // < (redirección de entrada)
    REDIR_SALIDA_TRUNCAR,   // > (redirección de salida, trunca o crea)
    REDIR_SALIDA_ANEXAR,  // >> (redirección de salida, añade o crea)
    EJEC_SEGUNDO_PLANO       // & (ejecución en segundo plano)
} TipoOperacion;

// --- Estructura para representar un comando parseado ---
typedef struct {
    char *argv[MAX_ARGUMENTOS];            // Argumentos del comando
    int argc;                       // Número de argumentos
    char *archivo_entrada;               // Archivo para redirección de entrada (NULL si no hay)
    char *archivo_salida;              // Archivo para redirección de salida (NULL si no hay)
    TipoOperacion tipo_operacion;           // Tipo de operación (para el último comando en la tubería, o si es un solo comando)
} ComandoParseado;

// Variable global para almacenar el PID del proceso hijo en primer plano (para manejo de señales)
// Es crucial para enviar SIGINT/SIGQUIT solo al proceso que está activo en el foreground.
// Se inicializa a 0 y se actualiza al forkear un proceso en primer plano.
volatile pid_t pid_proceso_en_primer_plano = 0; // 'volatile' para manejo de señales

// --- Prototipos de funciones auxiliares y de manejo de señales ---
void imprimir_error(const char *mensaje);
int dividir_cadena(char *cadena, char *delimitador, char *tokens[]);
char *generar_prompt();
void imprimir_bienvenida();
void deshabilitar_reporte_raton();

// Prototipos de funciones de manejo de señales
void configurar_senales_padre();
void restaurar_senales_hijo();
void manejador_sigint_quit(int signo);
void manejador_sigchld(int signo);

// Prototipos de funciones modularizadas del shell
int ejecutar_comando_interno(ComandoParseado *comando);
int ejecutar_tuberia(ComandoParseado comandos_parseados[], int num_comandos_tuberia);
int parsear_argumentos_comando(char *cadena_comando, ComandoParseado *comando_parseado);
void liberar_comando_parseado(ComandoParseado *comando_parseado);


/**
 * @brief Función principal del minishell.
 *
 * Se encarga de la lectura de comandos, su parseo, el manejo de comandos internos
 * y la ejecución de comandos externos con soporte para tuberías, redirecciones,
 * ejecución en segundo plano y el operador condicional '&&'.
 * Utiliza la librería readline para una interfaz de usuario mejorada.
 */
int main() {
    char linea_original[MAX_LONGITUD_ENTRADA]; // Para la línea completa de readline
    char *segmentos_and[MAX_SEGMENTOS_AND]; // Segmentos separados por '&&'
    int num_segmentos_and;

    char *linea_entrada;
    char *prompt_actual;
    int ultimo_estado_salida = 0; // Almacena el estado de salida del último comando ejecutado

    configurar_senales_padre(); // Configurar manejadores de señales para el shell padre

    deshabilitar_reporte_raton();
    imprimir_bienvenida();

    while (1) {
        prompt_actual = generar_prompt();
        linea_entrada = readline(prompt_actual);
        free(prompt_actual);

        if (linea_entrada == NULL) { // Ctrl+D
            printf("Saliendo del MiniShell.\n");
            fflush(stdout);
            break;
        }

        if (strlen(linea_entrada) == 0 || linea_entrada[strspn(linea_entrada, " \t\r\n")] == '\0') {
            free(linea_entrada);
            continue;
        }

        add_history(linea_entrada);

        strncpy(linea_original, linea_entrada, sizeof(linea_original) - 1);
        linea_original[sizeof(linea_original) - 1] = '\0';

        free(linea_entrada);

        // Dividir la línea por '&&'
        num_segmentos_and = dividir_cadena(linea_original, "&&", segmentos_and);

        ultimo_estado_salida = 0; // Reiniciar estado de salida para cada nueva línea de entrada

        for (int s = 0; s < num_segmentos_and; s++) {
            // Si el comando anterior falló y estamos en un segmento '&&', saltar el actual
            if (s > 0 && ultimo_estado_salida != 0) {
                // printf("Comando anterior falló, saltando '%s'\n", segmentos_and[s]); // Para depuración
                continue;
            }

            char *comandos_str[MAX_COMANDOS]; // Almacena las subcadenas de comandos separadas por '|'
            ComandoParseado comandos_parseados[MAX_COMANDOS]; // Array de estructuras para los comandos parseados
            int num_comandos_tuberia; // Número de comandos en la tubería

            // Cada segmento '&&' puede contener una tubería (separada por '|')
            num_comandos_tuberia = dividir_cadena(segmentos_and[s], "|", comandos_str);

            if (num_comandos_tuberia < 1 || comandos_str[0] == NULL || strlen(comandos_str[0]) == 0) {
                imprimir_error("Error: Comando inválido en segmento '&&'.");
                ultimo_estado_salida = 1; // Un comando inválido también es un fallo
                continue;
            }

            int error_parseo = 0;
            for (int i = 0; i < num_comandos_tuberia; i++) {
                // Inicializar la estructura para cada comando
                comandos_parseados[i].argc = 0;
                comandos_parseados[i].archivo_entrada = NULL;
                comandos_parseados[i].archivo_salida = NULL;
                comandos_parseados[i].tipo_operacion = SIN_REDIR;

                if (parsear_argumentos_comando(comandos_str[i], &comandos_parseados[i]) != 0) {
                    fprintf(stderr, "Error de sintaxis en el comando '%s'.\n", comandos_str[i]);
                    fflush(stderr);
                    error_parseo = 1;
                    // Liberar memoria de los comandos parseados hasta ahora
                    for (int k = 0; k <= i; k++) {
                        liberar_comando_parseado(&comandos_parseados[k]);
                    }
                    ultimo_estado_salida = 1; // Fallo en parseo
                    break;
                }
            }

            if (error_parseo) {
                continue;
            }

            // --- Manejo de comandos internos (built-ins) ---
            // Solo se ejecuta si es un solo comando, sin redirecciones y sin estar en segundo plano.
            if (num_comandos_tuberia == 1 &&
                comandos_parseados[0].archivo_entrada == NULL &&
                comandos_parseados[0].archivo_salida == NULL &&
                comandos_parseados[0].tipo_operacion != EJEC_SEGUNDO_PLANO) {

                if (ejecutar_comando_interno(&comandos_parseados[0])) {
                    // Built-in ejecutado, su éxito/fracaso no afecta el '&&' directamente en este punto
                    // (ej. 'cd' no tiene un estado de salida explícito para el shell como lo tendría un execvp)
                    // Para simplificar, asumimos éxito para built-ins que no sean exit/quit.
                    // Si 'cd' falla, su mensaje de error ya se imprime.
                    ultimo_estado_salida = 0;
                    liberar_comando_parseado(&comandos_parseados[0]);
                    continue;
                }
            }

            // --- Ejecución de tuberías (o comando único externo) ---
            // La función ejecutar_tuberia devolverá el estado de salida del último comando en la tubería.
            ultimo_estado_salida = ejecutar_tuberia(comandos_parseados, num_comandos_tuberia);

            // Liberar la memoria de los comandos parseados para esta tubería
            for (int i = 0; i < num_comandos_tuberia; i++) {
                liberar_comando_parseado(&comandos_parseados[i]);
            }
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
    printf("┏┳┓╻┏┓╻╻┏━┓╻ ╻┏━╸╻  ╻     ┏━╸┏━┓╻ ╻╻┏━┓┏━┓     ┏━┓\n");
    printf("┃┃┃┃┃┗┫┃┗━┓┣━┫┣╸ ┃  ┃     ┣╸ ┃┓┃┃ ┃┃┣━┛┃ ┃     ┗━┫\n");
    printf("╹ ╹╹╹ ╹╹┗━┛╹ ╹┗━╸┗━╸┗━╸   ┗━╸┗┻┛┗━┛╹╹  ┗━┛    #┗━┛\n");
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
    char nombre_host[HOST_NAME_MAX + 1];
    char cwd[PATH_MAX + 1];
    struct passwd *pw;
    char *nombre_usuario = "desconocido";
    char *dir_casa = NULL;

    pw = getpwuid(geteuid());
    if (pw != NULL) {
        nombre_usuario = pw->pw_name;
        dir_casa = pw->pw_dir;
    }

    if (gethostname(nombre_host, sizeof(nombre_host)) == -1) {
        strcpy(nombre_host, "host_desconocido");
    }
    nombre_host[sizeof(nombre_host) - 1] = '\0';

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        imprimir_error("getcwd");
        strcpy(cwd, "ruta_desconocida");
    }

    char display_cwd[PATH_MAX + 1];
    if (dir_casa != NULL && strncmp(cwd, dir_casa, strlen(dir_casa)) == 0) {
        if (strlen(cwd) == strlen(dir_casa) || cwd[strlen(dir_casa)] == '/') {
            snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(dir_casa));
        } else {
            strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
            display_cwd[sizeof(display_cwd) - 1] = '\0';
        }
    } else {
        strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
        display_cwd[sizeof(display_cwd) - 1] = '\0';
    }

    char *prompt_str = (char *)malloc(MAX_LONGITUD_ENTRADA + HOST_NAME_MAX + PATH_MAX + 10);
    if (prompt_str == NULL) {
        imprimir_error("malloc para prompt");
        return strdup("> ");
    }

    snprintf(prompt_str, MAX_LONGITUD_ENTRADA + HOST_NAME_MAX + PATH_MAX + 10, "\033[7;32m%s@%s\033[0m:\033[7;34m%s\033[0m$ ", nombre_usuario, nombre_host, display_cwd);

    return prompt_str;
}

/**
 * @brief Imprime un mensaje de error en la salida de error estándar (stderr).
 * @param mensaje El mensaje de error a imprimir.
 */
void imprimir_error(const char *mensaje) {
    perror(mensaje);
    fflush(stderr);
}

/**
 * @brief Divide una cadena de caracteres en tokens basándose en un delimitador.
 * Recorta espacios en blanco al inicio y final de cada token.
 *
 * @param cadena La cadena a dividir (se modifica).
 * @param delimitador El delimitador a usar para la división.
 * @param tokens[] Un arreglo de punteros donde se almacenarán los tokens encontrados.
 * @return El número de tokens encontrados.
 */
int dividir_cadena(char *cadena, char *delimitador, char *tokens[]) {
    int contador = 0;
    char *token;
    char *saveptr; // Para strtok_r

    token = strtok_r(cadena, delimitador, &saveptr);
    while (token != NULL && contador < MAX_COMANDOS) {
        // Eliminar espacios en blanco al principio y al final del token
        while (*token == ' ' || *token == '\t' || *token == '\n') token++;
        char *fin = token + strlen(token) - 1;
        while (fin > token && (*fin == ' ' || *fin == '\t' || *fin == '\n')) fin--;
        *(fin + 1) = '\0';

        if (strlen(token) > 0) { // Asegurarse de que el token no esté vacío después de recortar espacios
            tokens[contador++] = token;
        }
        token = strtok_r(NULL, delimitador, &saveptr);
    }
    tokens[contador] = NULL;
    return contador;
}

/**
 * @brief Deshabilita el reporte de eventos del ratón en la terminal para permitir el scroll normal.
 * Envía secuencias de escape ANSI a la terminal para desactivar los modos de reporte del ratón.
 */
void deshabilitar_reporte_raton() {
    printf("\033[?1000l");
    printf("\033[?1002l");
    printf("\033[?1003l");
    fflush(stdout);
}

/**
 * @brief Libera la memoria asignada dinámicamente para los campos de una estructura ComandoParseado.
 * @param comando_parseado Puntero a la estructura ComandoParseado a liberar.
 */
void liberar_comando_parseado(ComandoParseado *comando_parseado) {
    for (int i = 0; i < comando_parseado->argc; i++) {
        free(comando_parseado->argv[i]);
        comando_parseado->argv[i] = NULL;
    }
    if (comando_parseado->archivo_entrada) {
        free(comando_parseado->archivo_entrada);
        comando_parseado->archivo_entrada = NULL;
    }
    if (comando_parseado->archivo_salida) {
        free(comando_parseado->archivo_salida);
        comando_parseado->archivo_salida = NULL;
    }
}

/**
 * @brief Parsea una cadena de comando individual (sin tuberías) para extraer argumentos,
 * redirecciones de entrada/salida y el operador de ejecución en segundo plano '&'.
 * Modifica una estructura ComandoParseado para almacenar los resultados.
 *
 * @param cadena_comando La cadena de comando a parsear (ej. "ls -l > output.txt &"). Se modifica.
 * @param comando_parseado Puntero a la estructura ComandoParseado donde se almacenarán los resultados.
 * @return 0 si el parseo fue exitoso, -1 si hubo un error de sintaxis o fallo de memoria.
 */
int parsear_argumentos_comando(char *cadena_comando, ComandoParseado *comando_parseado) {
    comando_parseado->argc = 0;
    comando_parseado->archivo_entrada = NULL;
    comando_parseado->archivo_salida = NULL;
    comando_parseado->tipo_operacion = SIN_REDIR;

    char *copia_cadena_comando = strdup(cadena_comando);
    if (copia_cadena_comando == NULL) {
        imprimir_error("strdup");
        return -1;
    }
    char *original_copia_cadena_comando = copia_cadena_comando; // Guardamos para free al final

    char *token;
    char *saveptr; // Para strtok_r

    token = strtok_r(copia_cadena_comando, " \t\n", &saveptr);
    while (token != NULL) {
        // Recortar espacios en blanco del token
        while (*token == ' ' || *token == '\t' || *token == '\n') token++;
        char *fin = token + strlen(token) - 1;
        while (fin > token && (*fin == ' ' || *fin == '\t' || *fin == '\n')) fin--;
        *(fin + 1) = '\0';

        if (strlen(token) == 0) { // Saltar tokens vacíos después de recortar
            token = strtok_r(NULL, " \t\n", &saveptr);
            continue;
        }

        if (strcmp(token, "<") == 0) {
            token = strtok_r(NULL, " \t\n", &saveptr); // Siguiente token debe ser el archivo
            if (token == NULL || strlen(token) == 0) {
                fprintf(stderr, "Error de sintaxis: se esperaba nombre de archivo después de '<'.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            if (comando_parseado->archivo_entrada != NULL) {
                fprintf(stderr, "Error de sintaxis: múltiples redirecciones de entrada.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            comando_parseado->archivo_entrada = strdup(token);
            if (comando_parseado->archivo_entrada == NULL) {
                imprimir_error("strdup");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
        } else if (strcmp(token, ">") == 0) {
            token = strtok_r(NULL, " \t\n", &saveptr); // Siguiente token debe ser el archivo
            if (token == NULL || strlen(token) == 0) {
                fprintf(stderr, "Error de sintaxis: se esperaba nombre de archivo después de '>'.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            if (comando_parseado->archivo_salida != NULL) {
                fprintf(stderr, "Error de sintaxis: múltiples redirecciones de salida.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            comando_parseado->archivo_salida = strdup(token);
            if (comando_parseado->archivo_salida == NULL) {
                imprimir_error("strdup");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            comando_parseado->tipo_operacion = REDIR_SALIDA_TRUNCAR;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok_r(NULL, " \t\n", &saveptr); // Siguiente token debe ser el archivo
            if (token == NULL || strlen(token) == 0) {
                fprintf(stderr, "Error de sintaxis: se esperaba nombre de archivo después de '>>'.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            if (comando_parseado->archivo_salida != NULL) {
                fprintf(stderr, "Error de sintaxis: múltiples redirecciones de salida.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            comando_parseado->archivo_salida = strdup(token);
            if (comando_parseado->archivo_salida == NULL) {
                imprimir_error("strdup");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            comando_parseado->tipo_operacion = REDIR_SALIDA_ANEXAR;
        } else if (strcmp(token, "&") == 0) {
            if (strtok_r(NULL, " \t\n", &saveptr) != NULL) {
                fprintf(stderr, "Error de sintaxis: '&' debe ser el último argumento del comando.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
            comando_parseado->tipo_operacion = EJEC_SEGUNDO_PLANO;
            break;
        } else {
            if (comando_parseado->argc < MAX_ARGUMENTOS - 1) {
                comando_parseado->argv[comando_parseado->argc++] = strdup(token);
                if (comando_parseado->argv[comando_parseado->argc - 1] == NULL) {
                    imprimir_error("strdup");
                    liberar_comando_parseado(comando_parseado);
                    free(original_copia_cadena_comando);
                    return -1;
                }
            } else {
                fprintf(stderr, "Demasiados argumentos para un comando.\n");
                liberar_comando_parseado(comando_parseado);
                free(original_copia_cadena_comando);
                return -1;
            }
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
    comando_parseado->argv[comando_parseado->argc] = NULL;

    if (comando_parseado->argc == 0 && comando_parseado->archivo_entrada == NULL && comando_parseado->archivo_salida == NULL) {
        fprintf(stderr, "Error de sintaxis: comando vacío o solo con operadores.\n");
        free(original_copia_cadena_comando);
        return -1;
    }

    free(original_copia_cadena_comando);
    return 0;
}

/**
 * @brief Maneja la ejecución de comandos internos (built-ins) como 'exit', 'quit', 'history' y 'cd'.
 * Esta función es llamada solo si el comando es el primero en una tubería, no tiene redirecciones
 * y no se ejecuta en segundo plano.
 *
 * @param comando Puntero a la estructura ComandoParseado que contiene el comando a ejecutar.
 * @return 1 si el comando era un built-in y fue ejecutado, 0 en caso contrario.
 */
int ejecutar_comando_interno(ComandoParseado *comando) {
    if (comando->argc == 0) return 0;

    if (strcmp(comando->argv[0], "exit") == 0 || strcmp(comando->argv[0], "quit") == 0) {
        printf("Saliendo del MiniShell.\n");
        fflush(stdout);
        exit(0);
    } else if (strcmp(comando->argv[0], "history") == 0) {
        history_set_pos(0);
        HIST_ENTRY *h_entry;
        int i = 0;
        while ((h_entry = history_get(i++)) != NULL) {
            printf("%d: %s\n", i, h_entry->line);
        }
        fflush(stdout);
        return 1;
    } else if (strcmp(comando->argv[0], "cd") == 0) {
        if (comando->argv[1] == NULL) {
            fprintf(stderr, "Uso: cd <directorio>\n");
            fflush(stderr);
        } else {
            if (chdir(comando->argv[1]) == -1) {
                imprimir_error("Error al cambiar de directorio");
            }
        }
        return 1;
    }
    return 0;
}

/**
 * @brief Ejecuta una tubería de comandos externos, incluyendo redirecciones y ejecución en segundo plano.
 * Crea los procesos hijos, maneja las tuberías (pipes), redirecciona I/O y espera por los hijos.
 *
 * @param comandos_parseados Array de estructuras ComandoParseado que representan los comandos en la tubería.
 * @param num_comandos_tuberia Número de comandos en el array.
 * @return El estado de salida del último comando en la tubería (0 para éxito, >0 para fallo).
 */
int ejecutar_tuberia(ComandoParseado comandos_parseados[], int num_comandos_tuberia) {
    int tuberias[MAX_COMANDOS - 1][2];
    pid_t pids[MAX_COMANDOS];
    int es_segundo_plano = 0;
    int estado_salida_final = 1; // Por defecto, se asume fallo

    // Crear tuberías si hay más de un comando
    for (int i = 0; i < num_comandos_tuberia - 1; i++) {
        if (pipe(tuberias[i]) == -1) {
            imprimir_error("Error al crear la tubería");
            for (int k = 0; k < i; k++) {
                close(tuberias[k][0]);
                close(tuberias[k][1]);
            }
            return 1;
        }
    }

    // Determinar si la tubería completa se ejecuta en segundo plano
    if (num_comandos_tuberia > 0 && comandos_parseados[num_comandos_tuberia - 1].tipo_operacion == EJEC_SEGUNDO_PLANO) {
        es_segundo_plano = 1;
    }

    for (int i = 0; i < num_comandos_tuberia; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            imprimir_error("Error al crear el proceso hijo");
            for (int j = 0; j < num_comandos_tuberia - 1; j++) {
                close(tuberias[j][0]);
                close(tuberias[j][1]);
            }
            for (int k = 0; k < i; k++) {
                if (pids[k] > 0) kill(pids[k], SIGKILL);
            }
            return 1;
        }

        if (pids[i] == 0) { // CÓDIGO DEL PROCESO HIJO
            restaurar_senales_hijo(); // Restaurar manejadores a por defecto

            // Redirección de entrada
            if (comandos_parseados[i].archivo_entrada != NULL) {
                int fd_in = open(comandos_parseados[i].archivo_entrada, O_RDONLY);
                if (fd_in == -1) {
                    imprimir_error("Error al abrir archivo de entrada");
                    exit(EXIT_FAILURE);
                }
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            } else if (i > 0) { // Desde tubería anterior si no hay archivo de entrada
                dup2(tuberias[i - 1][0], STDIN_FILENO);
            }

            // Redirección de salida
            if (comandos_parseados[i].archivo_salida != NULL) {
                int flags = O_WRONLY | O_CREAT;
                if (comandos_parseados[i].tipo_operacion == REDIR_SALIDA_ANEXAR) {
                    flags |= O_APPEND;
                } else { // REDIR_SALIDA_TRUNCAR
                    flags |= O_TRUNC;
                }
                int fd_out = open(comandos_parseados[i].archivo_salida, flags, 0644);
                if (fd_out == -1) {
                    imprimir_error("Error al abrir archivo de salida");
                    exit(EXIT_FAILURE);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            } else if (i < num_comandos_tuberia - 1) { // A siguiente tubería si no hay archivo de salida
                dup2(tuberias[i][1], STDOUT_FILENO);
            }

            // Cierra todos los extremos de las tuberías en el proceso hijo.
            for (int j = 0; j < num_comandos_tuberia - 1; j++) {
                close(tuberias[j][0]);
                close(tuberias[j][1]);
            }
            
            execvp(comandos_parseados[i].argv[0], comandos_parseados[i].argv);
            imprimir_error("Error al ejecutar el comando");
            exit(EXIT_FAILURE); // El hijo termina si execvp falla
        }
    }

    // CÓDIGO DEL PROCESO PADRE
    // Cierra todos los descriptores de archivo de las tuberías.
    for (int i = 0; i < num_comandos_tuberia - 1; i++) {
        close(tuberias[i][0]);
        close(tuberias[i][1]);
    }

    if (!es_segundo_plano) {
        pid_proceso_en_primer_plano = pids[num_comandos_tuberia - 1]; // El último PID de la tubería

        // Restaurar manejadores de SIGINT/SIGQUIT para que el padre pueda reenviar al hijo en foreground
        // Se usa `signal` aquí temporalmente, se podría usar `sigaction` para más robustez si fuera necesario
        signal(SIGINT, manejador_sigint_quit);
        signal(SIGQUIT, manejador_sigint_quit);

        int status;
        for (int i = 0; i < num_comandos_tuberia; i++) {
            // Esperar por cada PID específico
            waitpid(pids[i], &status, 0); // No WNOHANG, esperamos que terminen
            if (i == num_comandos_tuberia - 1) { // Capturar el estado de salida del último comando
                if (WIFEXITED(status)) {
                    estado_salida_final = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    estado_salida_final = 128 + WTERMSIG(status); // Convención para terminación por señal
                }
            }
        }
        pid_proceso_en_primer_plano = 0; // Resetear cuando el foreground termina

        // Restaurar manejadores de SIGINT/SIGQUIT a ignorar para el prompt del shell
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);

    } else {
        printf("Proceso en segundo plano lanzado: [PID %d]\n", pids[0]);
        fflush(stdout);
        estado_salida_final = 0; // Se considera "exitoso" el lanzamiento en segundo plano
    }
    return estado_salida_final;
}

// --- Implementación de funciones de manejo de señales ---

/**
 * @brief Configura los manejadores de señales para el proceso padre del shell.
 * Ignora SIGINT, SIGQUIT, SIGTSTP y configura un manejador para SIGCHLD.
 */
void configurar_senales_padre() {
    // Ignorar SIGINT (Ctrl+C), SIGQUIT (Ctrl+\), SIGTSTP (Ctrl+Z) para el shell padre
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    // Configurar manejador para SIGCHLD para evitar zombies
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejador_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // SA_RESTART para reanudar llamadas al sistema, SA_NOCLDSTOP para no recibir SIGCHLD cuando un hijo se detiene
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        imprimir_error("Error al configurar el manejador de SIGCHLD");
        exit(EXIT_FAILURE); // Error crítico, el shell no debería continuar
    }
}

/**
 * @brief Restaura los manejadores de señales a su comportamiento por defecto para los procesos hijos.
 * Esto es crucial para que los hijos respondan a Ctrl+C, Ctrl+\, etc., como programas normales.
 */
void restaurar_senales_hijo() {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCHLD, SIG_DFL); // Los hijos no necesitan manejar SIGCHLD
}

/**
 * @brief Manejador de la señal SIGINT (Ctrl+C) y SIGQUIT (Ctrl+\) para el shell padre.
 * Si hay un proceso hijo en primer plano, reenvía la señal a ese proceso.
 * Si no, simplemente imprime un mensaje o una nueva línea.
 * @param signo El número de la señal recibida (SIGINT o SIGQUIT).
 */
void manejador_sigint_quit(int signo) {
    if (pid_proceso_en_primer_plano > 0) {
        // Si hay un proceso en primer plano, enviarle la señal
        if (kill(pid_proceso_en_primer_plano, signo) == -1) {
            if (errno != ESRCH) { // ESRCH significa que el proceso no existe
                imprimir_error("Error al enviar señal al proceso hijo");
            }
        }
        // Para SIGINT y SIGQUIT, también imprimimos un mensaje en el shell
        if (signo == SIGINT) {
            printf("\nPrograma cancelado (Ctrl+C).\n");
            fflush(stdout);
        } else if (signo == SIGQUIT) {
            printf("\nPrograma terminado (Ctrl+\\).\n");
            fflush(stdout);
        }
    } else {
        // Si no hay proceso en primer plano (o si el shell es el de foreground),
        // simplemente ignora la señal o imprime un mensaje.
        if (signo == SIGINT) {
            printf("\n"); // Nueva línea para un prompt limpio después de Ctrl+C
            fflush(stdout);
        } else if (signo == SIGQUIT) {
            printf("\n"); // Nueva línea para un prompt limpio
            fflush(stdout);
        }
    }
}

/**
 * @brief Manejador de la señal SIGCHLD para el shell padre.
 * Se encarga de recolectar el estado de los procesos hijos que han terminado
 * (evitando procesos "zombie") de forma no bloqueante.
 * @param signo El número de la señal (SIGCHLD).
 */
void manejador_sigchld(int signo) {
    (void)signo; // Evitar advertencia de "unused parameter"

    pid_t pid;
    int status;
    // Recolectar todos los hijos zombies de forma no bloqueante
    // WNOHANG: no bloquea si no hay hijos terminados
    // WUNTRACED: también reporta hijos que se han detenido
    // WCONTINUED: reporta hijos que se han reanudado (e.g., con fg)
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        // Opcional: imprimir mensajes sobre el estado de los hijos.
        // Se comentan para evitar saturar la salida en shells normales.
        // if (WIFEXITED(status)) {
        //     printf("Proceso hijo [PID %d] terminado con estado %d.\n", pid, WEXITSTATUS(status));
        // } else if (WIFSIGNALED(status)) {
        //     printf("Proceso hijo [PID %d] terminado por señal %d.\n", pid, WTERMSIG(status));
        // } else if (WIFSTOPPED(status)) {
        //     printf("Proceso hijo [PID %d] detenido por señal %d.\n", pid, WSTOPSIG(status));
        // } else if (WIFCONTINUED(status)) {
        //     printf("Proceso hijo [PID %d] reanudado.\n", pid);
        // }
        // fflush(stdout);
    }
}
