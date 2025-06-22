#include <stdio.h>       // Funciones estándar de entrada/salida (printf, fprintf, perror)
#include <stdlib.h>      // Funciones de utilidad general (malloc, free, exit, getenv)
#include <unistd.h>      // Funciones de sistema POSIX (fork, execvp, pipe, dup2, chdir, gethostname, geteuid)
#include <string.h>      // Funciones de manipulación de cadenas (strlen, strcmp, strncpy, strtok, strspn, strdup)
#include <sys/wait.h>    // Funciones para esperar cambios de estado en procesos hijos (wait, waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG)
#include <errno.h>       // Para manejar códigos de error del sistema (errno, EINTR)
#include <pwd.h>         // Para obtener información de usuario (getpwuid)
#include <limits.h>      // Para límites del sistema (PATH_MAX, HOST_NAME_MAX)
#include <fcntl.h>       // Para open, close, y flags como O_RDONLY, O_WRONLY, O_CREAT, O_APPEND, O_TRUNC
#include <signal.h>      // Para manejo de señales (signal, sigaction, kill, SIG_IGN, SIG_DFL, SIGCHLD, SIGINT, SIGQUIT, SIGTSTP)

// Incluir las bibliotecas de readline
#include <readline/readline.h> // Para leer líneas de entrada con edición y historial
#include <readline/history.h>  // Para gestionar el historial de comandos

// --- Definiciones de constantes ---
#define MAX_COMANDOS 10            // Número máximo de comandos que se pueden encadenar con tuberías o '&&'
#define MAX_ARGUMENTOS 40          // Número máximo de argumentos por comando (incluyendo el nombre del comando)
#define MAX_LONGITUD_ENTRADA 1024  // Tamaño máximo del buffer para la línea de entrada del usuario de readline
#define MAX_SEGMENTOS_AND 5        // Número máximo de segmentos separados por '&&' (ej: cmd1 && cmd2 && cmd3)

// --- ENUM para tipos de redirección ---
// Define los tipos de operaciones especiales que un comando puede tener
typedef enum {
    SIN_REDIR = 0,         // Sin redirección o operador especial
    REDIR_ENTRADA,         // '<' (redirección de entrada desde un archivo)
    REDIR_SALIDA_TRUNCAR,  // '>' (redirección de salida, trunca el archivo o lo crea)
    REDIR_SALIDA_ANEXAR    // '>>' (redirección de salida, añade al final del archivo o lo crea)
} TipoOperacion;

// --- Estructura para representar un comando parseado ---
// Almacena la información de un comando individual después de ser analizado
typedef struct {
    char *argv[MAX_ARGUMENTOS];  // Array de punteros a cadenas para los argumentos del comando (ej: {"ls", "-l", NULL})
    int argc;                    // Número de argumentos en argv
    char *archivo_entrada;       // Nombre del archivo para redirección de entrada (NULL si no hay)
    char *archivo_salida;        // Nombre del archivo para redirección de salida (NULL si no hay)
    TipoOperacion tipo_operacion; // Tipo de operación de redirección de salida (si aplica)
} ComandoParseado;

// Variable global para almacenar el PID del proceso hijo en primer plano.
// Es crucial para enviar SIGINT/SIGQUIT al proceso que está activo en el foreground.
// Se inicializa a 0 y se actualiza al forkear un proceso en primer plano.
volatile pid_t pid_proceso_en_primer_plano = 0; // 'volatile' para asegurar que el compilador no optimice el acceso a esta variable, ya que puede ser modificada por un manejador de señales.

// --- Prototipos de funciones auxiliares y de manejo de señales ---
void imprimir_error(const char *mensaje); // Imprime mensajes de error usando perror
int dividir_cadena(char *cadena, char *delimitador, char *tokens[]); // Divide una cadena en tokens por un delimitador
char *generar_prompt(); // Genera el string del prompt del shell (ej: usuario@host:~/current_dir$)
void imprimir_bienvenida(); // Imprime un mensaje de bienvenida con ASCII art
void deshabilitar_reporte_raton(); // Deshabilita el reporte del ratón en la terminal

// Prototipos de funciones de manejo de señales
void configurar_senales_padre(); // Configura los manejadores de señales para el proceso principal del shell
void restaurar_senales_hijo();   // Restaura los manejadores de señales a su comportamiento por defecto para los procesos hijos
void manejador_sigint_quit(int signo); // Manejador para las señales SIGINT (Ctrl+C) y SIGQUIT (Ctrl+\)
void manejador_sigchld(int signo);     // Manejador para la señal SIGCHLD (cuando un proceso hijo cambia de estado)

// Prototipos de funciones modularizadas del shell
int ejecutar_comando_interno(ComandoParseado *comando); // Ejecuta comandos built-in (cd, exit, history)
int ejecutar_tuberia(ComandoParseado comandos_parseados[], int num_comandos_tuberia); // Maneja la ejecución de tuberías de comandos
int parsear_argumentos_comando(char *cadena_comando, ComandoParseado *comando_parseado); // Analiza una cadena de comando para extraer argumentos y redirecciones (incluyendo comillas)
void liberar_comando_parseado(ComandoParseado *comando_parseado); // Libera la memoria de una estructura ComandoParseado


/**
 * @brief Función principal del minishell.
 *
 * Se encarga de la lectura de comandos, su parseo, el manejo de comandos internos
 * y la ejecución de comandos externos con soporte para tuberías, redirecciones y el
 * operador condicional '&&'.
 * Utiliza la librería readline para una interfaz de usuario mejorada (historial, edición de línea).
 */
int main() {
    char linea_original[MAX_LONGITUD_ENTRADA]; // Buffer para la línea completa de readline
    char *segmentos_and[MAX_SEGMENTOS_AND];    // Array para almacenar segmentos separados por '&&'
    int num_segmentos_and;                     // Número de segmentos '&&'

    char *linea_entrada;      // Puntero a la línea leída por readline
    char *prompt_actual;      // Puntero al string del prompt actual
    int ultimo_estado_salida = 0; // Almacena el estado de salida del último comando ejecutado (0 para éxito, >0 para error)

    configurar_senales_padre(); // Configurar manejadores de señales para el shell padre (ignorar Ctrl+C, etc.)

    deshabilitar_reporte_raton(); // Deshabilita el reporte del ratón en la terminal
    imprimir_bienvenida();        // Muestra el mensaje de bienvenida

    while (1) { // Bucle principal del shell
        prompt_actual = generar_prompt(); // Genera el prompt dinámicamente
        linea_entrada = readline(prompt_actual); // Lee la entrada del usuario con readline
        free(prompt_actual); // Libera la memoria del prompt

        if (linea_entrada == NULL) { // Ctrl+D (EOF) fue presionado
            printf("Saliendo del MiniShell.\n");
            fflush(stdout); // Asegura que el mensaje se imprima antes de salir
            break; // Sale del bucle principal, terminando el shell
        }

        // Si la línea está vacía o contiene solo espacios en blanco, la ignora
        if (strlen(linea_entrada) == 0 || linea_entrada[strspn(linea_entrada, " \t\r\n")] == '\0') {
            free(linea_entrada); // Libera la memoria de la línea leída
            continue; // Vuelve al inicio del bucle para pedir otra entrada
        }

        add_history(linea_entrada); // Añade la línea leída al historial de readline

        // Copia la línea leída a un buffer modificable, ya que dividir_cadena la modificará
        strncpy(linea_original, linea_entrada, sizeof(linea_original) - 1);
        linea_original[sizeof(linea_original) - 1] = '\0'; // Asegura la terminación nula

        free(linea_entrada); // Libera la memoria de la línea original de readline

        // Dividir la línea por '&&' (operador AND condicional)
        num_segmentos_and = dividir_cadena(linea_original, "&&", segmentos_and);

        ultimo_estado_salida = 0; // Reiniciar estado de salida para cada nueva línea de entrada

        // Itera sobre cada segmento separado por '&&'
        for (int s = 0; s < num_segmentos_and; s++) {
            // Si el comando anterior falló (estado de salida distinto de 0) y estamos en un segmento '&&',
            // saltamos la ejecución del comando actual (comportamiento de '&&').
            if (s > 0 && ultimo_estado_salida != 0) {
                continue;
            }

            char *comandos_str[MAX_COMANDOS]; // Almacena las subcadenas de comandos separadas por '|'
            ComandoParseado comandos_parseados[MAX_COMANDOS]; // Array de estructuras para los comandos parseados
            int num_comandos_tuberia; // Número de comandos en la tubería actual

            // Cada segmento '&&' puede contener una tubería (comandos separados por '|')
            num_comandos_tuberia = dividir_cadena(segmentos_and[s], "|", comandos_str);

            // Verifica si el segmento '&&' está vacío o es inválido
            if (num_comandos_tuberia < 1 || comandos_str[0] == NULL || strlen(comandos_str[0]) == 0) {
                imprimir_error("Error: Comando inválido en segmento '&&'.");
                ultimo_estado_salida = 1; // Un comando inválido también se considera un fallo
                continue; // Pasa al siguiente segmento '&&'
            }

            int error_parseo = 0; // Bandera para detectar errores de parseo
            // Itera sobre cada comando en la tubería para parsear sus argumentos y redirecciones
            for (int i = 0; i < num_comandos_tuberia; i++) {
                // Inicializar la estructura ComandoParseado para cada comando
                comandos_parseados[i].argc = 0;
                comandos_parseados[i].archivo_entrada = NULL;
                comandos_parseados[i].archivo_salida = NULL;
                comandos_parseados[i].tipo_operacion = SIN_REDIR; // Ahora solo maneja redirección de salida si existe

                // Llama a la función de parseo que soporta comillas
                if (parsear_argumentos_comando(comandos_str[i], &comandos_parseados[i]) != 0) {
                    fprintf(stderr, "Error de sintaxis en el comando '%s'.\n", comandos_str[i]);
                    fflush(stderr);
                    error_parseo = 1; // Marca que hubo un error de parseo
                    // Libera la memoria de los comandos parseados hasta ahora en caso de error
                    for (int k = 0; k <= i; k++) {
                        liberar_comando_parseado(&comandos_parseados[k]);
                    }
                    ultimo_estado_salida = 1; // Fallo en parseo
                    break; // Sale del bucle de parseo de tuberías
                }
            }

            if (error_parseo) {
                continue; // Si hubo un error de parseo, pasa al siguiente segmento '&&'
            }

            // --- Manejo de comandos internos (built-ins) ---
            // Solo se ejecuta un built-in si es un solo comando, sin redirecciones.
            // Esto evita que built-ins se usen en tuberías o con redirecciones.
            if (num_comandos_tuberia == 1 &&
                comandos_parseados[0].archivo_entrada == NULL &&
                comandos_parseados[0].archivo_salida == NULL) {

                // Si ejecutar_comando_interno devuelve 1, significa que un built-in fue ejecutado
                if (ejecutar_comando_interno(&comandos_parseados[0])) {
                    ultimo_estado_salida = 0; // Considera la ejecución del built-in como exitosa para el '&&'
                    liberar_comando_parseado(&comandos_parseados[0]); // Libera la memoria del comando parseado
                    continue; // Pasa al siguiente segmento '&&'
                }
            }

            // --- Ejecución de tuberías (o comando único externo) ---
            // Si no fue un built-in o si es una tubería/redirección, se ejecuta como comando externo.
            // La función ejecutar_tuberia devolverá el estado de salida del último comando en la tubería.
            ultimo_estado_salida = ejecutar_tuberia(comandos_parseados, num_comandos_tuberia);

            // Liberar la memoria de los comandos parseados para esta tubería
            for (int i = 0; i < num_comandos_tuberia; i++) {
                liberar_comando_parseado(&comandos_parseados[i]);
            }
        }
    }
    return 0; // El shell termina exitosamente
}

// --- Implementación de funciones auxiliares ---

/**
 * @brief Imprime un mensaje de bienvenida ASCII art con colores.
 */
void imprimir_bienvenida() {
    printf("\033[1;36m"); // Establece el color cian brillante para el texto
    printf("┏┳┓╻┏┓╻╻┏━┓╻ ╻┏━╸╻  ╻      ┏━╸┏━┓╻ ╻╻┏━┓┏━┓      ┏━┓\n");
    printf("┃┃┃┃┃┗┫┃┗━┓┣━┫┣╸ ┃  ┃      ┣╸ ┃┓┃┃ ┃┃┣━┛┃ ┃      ┗━┫\n");
    printf("╹ ╹╹╹ ╹╹┗━┛╹ ╹┗━╸┗━╸┗━╸    ┗━╸┗┻┛┗━┛╹╹  ┗━┛     #┗━┛\n");
    printf("\033[0m"); // Restablece el color de la terminal a su valor por defecto
    printf("\n");       // Imprime una nueva línea
    fflush(stdout);   // Asegura que el mensaje se imprima inmediatamente
}

/**
 * @brief Genera y devuelve el string del prompt de la terminal.
 * El prompt incluye el usuario actual, el nombre del host y el directorio de trabajo actual.
 * El directorio de trabajo se abrevia con '~' si está dentro del directorio home del usuario.
 *
 * @return Un puntero a una cadena de caracteres que contiene el prompt generado.
 * Esta cadena se asigna dinámicamente y debe ser liberada con `free()` por el llamador.
 */
char *generar_prompt() {
    char nombre_host[HOST_NAME_MAX + 1]; // Buffer para el nombre del host
    char cwd[PATH_MAX + 1];              // Buffer para el directorio de trabajo actual
    struct passwd *pw;                   // Estructura para almacenar información del usuario
    char *nombre_usuario = "desconocido"; // Nombre de usuario por defecto
    char *dir_casa = NULL;               // Directorio home del usuario

    // Obtener información del usuario actual
    pw = getpwuid(geteuid());
    if (pw != NULL) {
        nombre_usuario = pw->pw_name; // Obtiene el nombre de usuario
        dir_casa = pw->pw_dir;       // Obtiene el directorio home
    }

    // Obtener el nombre del host
    if (gethostname(nombre_host, sizeof(nombre_host)) == -1) {
        strcpy(nombre_host, "host_desconocido"); // Si falla, usa un nombre por defecto
    }
    nombre_host[sizeof(nombre_host) - 1] = '\0'; // Asegura terminación nula

    // Obtener el directorio de trabajo actual
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        imprimir_error("getcwd"); // Imprime un error si getcwd falla
        strcpy(cwd, "ruta_desconocida"); // Si falla, usa una ruta por defecto
    }

    char display_cwd[PATH_MAX + 1]; // Buffer para la ruta que se mostrará en el prompt
    // Si el directorio actual comienza con el directorio home, lo abrevia con '~'
    if (dir_casa != NULL && strncmp(cwd, dir_casa, strlen(dir_casa)) == 0) {
        // Verifica si la ruta actual es exactamente el directorio home o un subdirectorio
        if (strlen(cwd) == strlen(dir_casa) || cwd[strlen(dir_casa)] == '/') {
            snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(dir_casa));
        } else { // Si es un directorio que empieza igual pero no es subdirectorio, muestra la ruta completa
            strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
            display_cwd[sizeof(display_cwd) - 1] = '\0';
        }
    } else { // Si no está dentro del directorio home, muestra la ruta completa
        strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);
        display_cwd[sizeof(display_cwd) - 1] = '\0';
    }

    // Asigna memoria para el string del prompt
    char *prompt_str = (char *)malloc(MAX_LONGITUD_ENTRADA + HOST_NAME_MAX + PATH_MAX + 10);
    if (prompt_str == NULL) {
        imprimir_error("malloc para prompt"); // Error si falla la asignación de memoria
        return strdup("> "); // Devuelve un prompt simple en caso de error
    }

    // Formatea el string del prompt con colores ANSI
    // \033[7;32m: fondo verde, texto blanco
    // \033[0m: resetea los colores
    // \033[7;34m: fondo azul, texto blanco
    snprintf(prompt_str, MAX_LONGITUD_ENTRADA + HOST_NAME_MAX + PATH_MAX + 10, "\033[7;32m%s@%s\033[0m:\033[7;34m%s\033[0m$ ", nombre_usuario, nombre_host, display_cwd);

    return prompt_str; // Devuelve el prompt generado
}

/**
 * @brief Imprime un mensaje de error en la salida de error estándar (stderr).
 * Utiliza `perror` para imprimir el mensaje de error del sistema asociado a `errno`.
 * @param mensaje El mensaje de error descriptivo a imprimir antes del error del sistema.
 */
void imprimir_error(const char *mensaje) {
    perror(mensaje); // Imprime el mensaje proporcionado seguido del error de sistema (ej. "Error al abrir archivo: No such file or directory")
    fflush(stderr);  // Asegura que el mensaje se imprima inmediatamente
}

/**
 * @brief Divide una cadena de caracteres en tokens basándose en un delimitador.
 * Elimina espacios en blanco al inicio y final de cada token.
 * Utiliza `strtok_r` para ser reentrante y seguro en entornos multihilo, aunque en este shell de un solo hilo no es estrictamente necesario, es una buena práctica.
 *
 * @param cadena La cadena a dividir (se modifica durante el proceso de tokenización).
 * @param delimitador El delimitador a usar para la división (ej. "|", "&&").
 * @param tokens[] Un arreglo de punteros a `char` donde se almacenarán los tokens encontrados.
 * El último elemento será NULL.
 * @return El número de tokens encontrados.
 */
int dividir_cadena(char *cadena, char *delimitador, char *tokens[]) {
    int contador = 0; // Contador de tokens
    char *token;      // Puntero para el token actual
    char *saveptr;    // Contexto para strtok_r

    token = strtok_r(cadena, delimitador, &saveptr); // Obtiene el primer token
    while (token != NULL && contador < MAX_COMANDOS) { // Itera mientras haya tokens y no se exceda el máximo de comandos
        // Eliminar espacios en blanco al principio del token
        while (*token == ' ' || *token == '\t' || *token == '\n') token++;
        // Eliminar espacios en blanco al final del token
        char *fin = token + strlen(token) - 1;
        while (fin > token && (*fin == ' ' || *fin == '\t' || *fin == '\n')) fin--;
        *(fin + 1) = '\0'; // Termina el token en el nuevo final

        if (strlen(token) > 0) { // Asegurarse de que el token no esté vacío después de recortar espacios
            tokens[contador++] = token; // Almacena el token y incrementa el contador
        }
        token = strtok_r(NULL, delimitador, &saveptr); // Obtiene el siguiente token
    }
    tokens[contador] = NULL; // Termina el array de tokens con NULL
    return contador;         // Devuelve el número de tokens encontrados
}

/**
 * @brief Deshabilita el reporte de eventos del ratón en la terminal para permitir el scroll normal.
 * Envía secuencias de escape ANSI a la terminal para desactivar los modos de reporte del ratón.
 * Esto es común en terminales para asegurar que las acciones del ratón no interfieran con la interacción del shell.
 */
void deshabilitar_reporte_raton() {
    printf("\033[?1000l"); // Deshabilita el reporte de clic del ratón
    printf("\033[?1002l"); // Deshabilita el reporte de movimiento del ratón
    printf("\033[?1003l"); // Deshabilita el reporte de arrastre del ratón
    fflush(stdout);       // Asegura que los códigos de escape se envíen inmediatamente
}

/**
 * @brief Libera la memoria asignada dinámicamente para los campos de una estructura ComandoParseado.
 * Esto es crucial para evitar fugas de memoria, ya que los argumentos y nombres de archivo se duplican con `strdup`.
 * @param comando_parseado Puntero a la estructura ComandoParseado a liberar.
 */
void liberar_comando_parseado(ComandoParseado *comando_parseado) {
    // Libera la memoria de cada argumento en argv
    for (int i = 0; i < comando_parseado->argc; i++) {
        free(comando_parseado->argv[i]);
        comando_parseado->argv[i] = NULL; // Establece el puntero a NULL para evitar usos posteriores
    }
    // Libera la memoria del nombre del archivo de entrada si existe
    if (comando_parseado->archivo_entrada) {
        free(comando_parseado->archivo_entrada);
        comando_parseado->archivo_entrada = NULL;
    }
    // Libera la memoria del nombre del archivo de salida si existe
    if (comando_parseado->archivo_salida) {
        free(comando_parseado->archivo_salida);
        comando_parseado->archivo_salida = NULL;
    }
}

/**
 * @brief Parsea una cadena de comando individual (sin tuberías) para extraer argumentos y
 * redirecciones de entrada/salida.
 * Modifica una estructura ComandoParseado para almacenar los resultados.
 * Esta función es clave ya que maneja argumentos entre comillas simples o dobles,
 * permitiendo que los espacios y caracteres especiales sean tratados como parte de un solo argumento.
 *
 * @param cadena_comando La cadena de comando a parsear (ej. "ls -l > output.txt" o 'grep "hello world" file.txt').
 * Esta cadena se modifica durante el parseo (es decir, es destruida).
 * @param comando_parseado Puntero a la estructura ComandoParseado donde se almacenarán los resultados.
 * @return 0 si el parseo fue exitoso, -1 si hubo un error de sintaxis o fallo de memoria.
 */
int parsear_argumentos_comando(char *cadena_comando, ComandoParseado *comando_parseado) {
    comando_parseado->argc = 0;                // Inicializa el contador de argumentos
    comando_parseado->archivo_entrada = NULL;  // Inicializa el archivo de entrada
    comando_parseado->archivo_salida = NULL;   // Inicializa el archivo de salida
    comando_parseado->tipo_operacion = SIN_REDIR; // Inicializa el tipo de operación (solo para redirección de salida)

    char *current_char = cadena_comando; // Puntero para recorrer la cadena de comando
    char *arg_start = NULL;             // Puntero para marcar el inicio de un argumento
    char quote_char = '\0';              // Carácter de comilla actual ('\0' si no estamos dentro de comillas)
    int i = 0;                           // Índice para construir el argumento en el buffer temporal

    // Crear un buffer temporal para construir los argumentos sin comillas
    char temp_arg_buffer[MAX_LONGITUD_ENTRADA];

    // Itera a través de la cadena de comando carácter por carácter
    while (*current_char != '\0') {
        // Si no estamos dentro de comillas y encontramos un espacio, tabulador o nueva línea:
        // Es el fin de un argumento (o el inicio de uno nuevo si hay varios espacios).
        if (quote_char == '\0' && (*current_char == ' ' || *current_char == '\t' || *current_char == '\n')) {
            if (arg_start != NULL) { // Si ya habíamos empezado a construir un argumento
                temp_arg_buffer[i] = '\0'; // Termina el string del argumento en el buffer temporal
                // Añade el argumento parseado al array argv del comando
                if (comando_parseado->argc < MAX_ARGUMENTOS - 1) {
                    comando_parseado->argv[comando_parseado->argc++] = strdup(temp_arg_buffer); // Duplica el string
                    if (comando_parseado->argv[comando_parseado->argc - 1] == NULL) {
                        imprimir_error("strdup");
                        return -1;
                    }
                } else {
                    fprintf(stderr, "Demasiados argumentos para un comando.\n");
                    return -1;
                }
                arg_start = NULL; // Resetea el inicio del argumento
                i = 0;            // Resetea el índice del buffer temporal
            }
        }
        // Si encontramos una comilla simple o doble
        else if (*current_char == '\'' || *current_char == '"') {
            if (quote_char == '\0') {
                // Si no estamos dentro de ninguna comilla, es el inicio de una nueva comilla
                quote_char = *current_char; // Guarda el tipo de comilla
                if (arg_start == NULL) {    // Si no estamos ya construyendo un argumento
                    arg_start = current_char; // Marca el inicio del argumento (la comilla no se copia al argumento)
                }
            } else if (quote_char == *current_char) {
                // Si encontramos la misma comilla que abrió la sección, es el cierre de la comilla
                quote_char = '\0'; // Resetea el estado de comillas
            } else {
                // Si encontramos una comilla diferente mientras estamos dentro de una comilla (comilla anidada)
                // Se considera parte del argumento y se añade al buffer temporal.
                if (arg_start == NULL) {
                    arg_start = current_char;
                }
                temp_arg_buffer[i++] = *current_char;
            }
        }
        // Manejo básico de caracteres escapados (ej: \" para una comilla literal dentro de una cadena)
        // Esto es un ejemplo simple, un parser completo de escapes es más complejo.
        else if (*current_char == '\\' && (current_char[1] == '\'' || current_char[1] == '"' || current_char[1] == ' ' || current_char[1] == '\t')) {
            if (arg_start == NULL) {
                arg_start = current_char;
            }
            current_char++; // Avanza más allá del carácter de escape ('\'), para tomar el siguiente carácter literal
            temp_arg_buffer[i++] = *current_char; // Añade el carácter escapado al buffer
        }
        // Manejo de redirección de entrada '<'
        // Solo se procesa si no estamos dentro de comillas
        else if (*current_char == '<' && quote_char == '\0') {
            if (arg_start != NULL) { // Si hay un argumento en construcción, lo finaliza y lo añade
                temp_arg_buffer[i] = '\0';
                if (comando_parseado->argc < MAX_ARGUMENTOS - 1) {
                    comando_parseado->argv[comando_parseado->argc++] = strdup(temp_arg_buffer);
                    if (comando_parseado->argv[comando_parseado->argc - 1] == NULL) {
                        imprimir_error("strdup");
                        return -1;
                    }
                } else {
                    fprintf(stderr, "Demasiados argumentos para un comando.\n");
                    return -1;
                }
                arg_start = NULL;
                i = 0;
            }
            current_char++; // Avanza más allá del '<'
            // Ignora espacios en blanco después del '<'
            while (*current_char == ' ' || *current_char == '\t') current_char++;
            char *file_start = current_char; // Marca el inicio del nombre del archivo
            // Avanza hasta encontrar un espacio, tabulador, nueva línea, otro operador de redirección o el final de la cadena
            while (*current_char != '\0' && *current_char != ' ' && *current_char != '\t' && *current_char != '>' && *current_char != '<') {
                current_char++;
            }
            if (file_start == current_char) { // Si no se encontró un nombre de archivo
                fprintf(stderr, "Error de sintaxis: se esperaba nombre de archivo después de '<'.\n");
                return -1;
            }
            char temp_file_name[PATH_MAX]; // Buffer temporal para el nombre del archivo
            // Copia el nombre del archivo al buffer temporal
            strncpy(temp_file_name, file_start, current_char - file_start);
            temp_file_name[current_char - file_start] = '\0'; // Asegura terminación nula
            if (comando_parseado->archivo_entrada != NULL) { // Error si ya hay una redirección de entrada
                fprintf(stderr, "Error de sintaxis: múltiples redirecciones de entrada.\n");
                return -1;
            }
            comando_parseado->archivo_entrada = strdup(temp_file_name); // Duplica el nombre del archivo
            if (comando_parseado->archivo_entrada == NULL) {
                imprimir_error("strdup");
                return -1;
            }
            current_char--; // Decrementa para que el bucle procese correctamente el carácter que detuvo el avance
        }
        // Manejo de redirección de salida '>' o '>>'
        // Solo se procesa si no estamos dentro de comillas
        else if (*current_char == '>' && quote_char == '\0') {
            if (arg_start != NULL) { // Si hay un argumento en construcción, lo finaliza y lo añade
                temp_arg_buffer[i] = '\0';
                if (comando_parseado->argc < MAX_ARGUMENTOS - 1) {
                    comando_parseado->argv[comando_parseado->argc++] = strdup(temp_arg_buffer);
                    if (comando_parseado->argv[comando_parseado->argc - 1] == NULL) {
                        imprimir_error("strdup");
                        return -1;
                    }
                } else {
                    fprintf(stderr, "Demasiados argumentos para un comando.\n");
                    return -1;
                }
                arg_start = NULL;
                i = 0;
            }
            current_char++; // Avanza más allá del '>'
            TipoOperacion op = REDIR_SALIDA_TRUNCAR; // Por defecto, es redirección de truncado (>)
            if (*current_char == '>') { // Si el siguiente carácter también es '>', entonces es anexar (>>)
                op = REDIR_SALIDA_ANEXAR;
                current_char++; // Avanza más allá del segundo '>'
            }
            // Ignora espacios en blanco después del operador de redirección
            while (*current_char == ' ' || *current_char == '\t') current_char++;
            char *file_start = current_char; // Marca el inicio del nombre del archivo
            // Avanza hasta encontrar un espacio, tabulador, nueva línea, otro operador de redirección o el final de la cadena
            while (*current_char != '\0' && *current_char != ' ' && *current_char != '\t' && *current_char != '>' && *current_char != '<') {
                current_char++;
            }
            if (file_start == current_char) { // Si no se encontró un nombre de archivo
                fprintf(stderr, "Error de sintaxis: se esperaba nombre de archivo después de redirección de salida.\n");
                return -1;
            }
            char temp_file_name[PATH_MAX]; // Buffer temporal para el nombre del archivo
            // Copia el nombre del archivo al buffer temporal
            strncpy(temp_file_name, file_start, current_char - file_start);
            temp_file_name[current_char - file_start] = '\0'; // Asegura terminación nula
            if (comando_parseado->archivo_salida != NULL) { // Error si ya hay una redirección de salida
                fprintf(stderr, "Error de sintaxis: múltiples redirecciones de salida.\n");
                return -1;
            }
            comando_parseado->archivo_salida = strdup(temp_file_name); // Duplica el nombre del archivo
            if (comando_parseado->archivo_salida == NULL) {
                imprimir_error("strdup");
                return -1;
            }
            comando_parseado->tipo_operacion = op; // Establece el tipo de operación de redirección
            current_char--; // Decrementa para que el bucle procese correctamente el carácter que detuvo el avance
        }
        else {
            // Si el carácter no es un delimitador, una comilla o un operador especial:
            // Es parte del argumento actual.
            if (arg_start == NULL) {
                arg_start = current_char; // Marca el inicio de un nuevo argumento
            }
            temp_arg_buffer[i++] = *current_char; // Añade el carácter al buffer temporal
            if (i >= MAX_LONGITUD_ENTRADA) { // Previene desbordamiento del buffer para un solo argumento
                fprintf(stderr, "Argumento demasiado largo.\n");
                return -1;
            }
        }
        current_char++; // Avanza al siguiente carácter
    }

    // Después de que el bucle ha terminado, verifica si queda un argumento sin finalizar
    if (arg_start != NULL && i > 0) {
        if (quote_char != '\0') { // Si la cadena termina con una comilla sin cerrar
            fprintf(stderr, "Error de sintaxis: comilla sin cerrar.\n");
            return -1;
        }
        temp_arg_buffer[i] = '\0'; // Termina el string del último argumento
        // Añade el último argumento al array argv
        if (comando_parseado->argc < MAX_ARGUMENTOS - 1) {
            comando_parseado->argv[comando_parseado->argc++] = strdup(temp_arg_buffer);
            if (comando_parseado->argv[comando_parseado->argc - 1] == NULL) {
                imprimir_error("strdup");
                return -1;
            }
        } else {
            fprintf(stderr, "Demasiados argumentos para un comando.\n");
            return -1;
        }
    } else if (quote_char != '\0') { // Si la cadena termina con una comilla sin cerrar, sin argumento en proceso
        fprintf(stderr, "Error de sintaxis: comilla sin cerrar al final del comando.\n");
        return -1;
    }

    comando_parseado->argv[comando_parseado->argc] = NULL; // El array de argumentos debe terminar con NULL para execvp

    // Validación final: si no se encontró ningún argumento o redirección, el comando es inválido
    if (comando_parseado->argc == 0 && comando_parseado->archivo_entrada == NULL && comando_parseado->archivo_salida == NULL) {
        fprintf(stderr, "Error de sintaxis: comando vacío o solo con operadores.\n");
        return -1;
    }

    return 0; // Parseo exitoso
}


/**
 * @brief Maneja la ejecución de comandos internos (built-ins) como 'exit', 'quit', 'history' y 'cd'.
 * Esta función es llamada solo si el comando es el primero en una tubería y no tiene redirecciones.
 * Los built-ins se ejecutan directamente en el proceso del shell padre.
 *
 * @param comando Puntero a la estructura ComandoParseado que contiene el comando a ejecutar.
 * @return 1 si el comando era un built-in y fue ejecutado, 0 en caso contrario.
 */
int ejecutar_comando_interno(ComandoParseado *comando) {
    if (comando->argc == 0) return 0; // Si no hay argumentos, no es un built-in válido

    // Comando 'exit' o 'quit'
    if (strcmp(comando->argv[0], "exit") == 0 || strcmp(comando->argv[0], "quit") == 0) {
        printf("Saliendo del MiniShell.\n");
        fflush(stdout);
        exit(0); // Termina el proceso del shell
    }
    // Comando 'history'
    else if (strcmp(comando->argv[0], "history") == 0) {
        history_set_pos(0); // Establece la posición de lectura del historial al principio
        HIST_ENTRY *h_entry; // Puntero a una entrada del historial
        int i = 0;           // Contador para numerar las entradas
        // Itera y imprime cada entrada del historial
        while ((h_entry = history_get(i++)) != NULL) {
            printf("%d: %s\n", i, h_entry->line);
        }
        fflush(stdout); // Asegura que el historial se imprima
        return 1;       // Indica que se ejecutó un built-in
    }
    // Comando 'cd' (change directory)
    else if (strcmp(comando->argv[0], "cd") == 0) {
        if (comando->argv[1] == NULL) { // Si no se proporciona un directorio
            fprintf(stderr, "Uso: cd <directorio>\n"); // Mensaje de uso
            fflush(stderr);
        } else {
            if (chdir(comando->argv[1]) == -1) { // Intenta cambiar el directorio
                imprimir_error("Error al cambiar de directorio"); // Error si chdir falla
            }
        }
        return 1; // Indica que se ejecutó un built-in
    }
    return 0; // Si no es ninguno de los built-ins reconocidos, devuelve 0
}

/**
 * @brief Ejecuta una tubería de comandos externos, incluyendo redirecciones.
 * Crea los procesos hijos necesarios, maneja las tuberías (pipes) para la comunicación entre ellos,
 * redirige la entrada/salida estándar y espera por la terminación de los procesos hijos.
 *
 * @param comandos_parseados Array de estructuras ComandoParseado que representan los comandos en la tubería.
 * @param num_comandos_tuberia Número de comandos en el array.
 * @return El estado de salida del último comando en la tubería (0 para éxito, >0 para fallo).
 */
int ejecutar_tuberia(ComandoParseado comandos_parseados[], int num_comandos_tuberia) {
    int tuberias[MAX_COMANDOS - 1][2]; // Array para almacenar los descriptores de archivo de las tuberías (pipe[0] = lectura, pipe[1] = escritura)
    pid_t pids[MAX_COMANDOS];         // Array para almacenar los PIDs de los procesos hijos
    int estado_salida_final = 1;      // Por defecto, se asume fallo (estado de salida distinto de 0)

    // Crear tuberías si hay más de un comando en la tubería
    for (int i = 0; i < num_comandos_tuberia - 1; i++) {
        if (pipe(tuberias[i]) == -1) {
            imprimir_error("Error al crear la tubería");
            // Cierra las tuberías ya creadas en caso de error
            for (int k = 0; k < i; k++) {
                close(tuberias[k][0]);
                close(tuberias[k][1]);
            }
            return 1; // Retorna un código de error
        }
    }

    // Bucle para forkear y ejecutar cada comando en la tubería
    for (int i = 0; i < num_comandos_tuberia; i++) {
        pids[i] = fork(); // Crea un nuevo proceso hijo
        if (pids[i] == -1) { // Error al forkear
            imprimir_error("Error al crear el proceso hijo");
            // Cierra todas las tuberías abiertas en caso de error
            for (int j = 0; j < num_comandos_tuberia - 1; j++) {
                close(tuberias[j][0]);
                close(tuberias[j][1]);
            }
            // Mata los procesos hijos que ya pudieron haber sido creados
            for (int k = 0; k < i; k++) {
                if (pids[k] > 0) kill(pids[k], SIGKILL);
            }
            return 1; // Retorna un código de error
        }

        if (pids[i] == 0) { // CÓDIGO DEL PROCESO HIJO
            restaurar_senales_hijo(); // Restaura los manejadores de señales a su comportamiento por defecto

            // --- Manejo de redirección de entrada ---
            if (comandos_parseados[i].archivo_entrada != NULL) {
                // Abre el archivo de entrada en modo lectura
                int fd_in = open(comandos_parseados[i].archivo_entrada, O_RDONLY);
                if (fd_in == -1) {
                    imprimir_error("Error al abrir archivo de entrada");
                    exit(EXIT_FAILURE); // El hijo termina con fallo
                }
                dup2(fd_in, STDIN_FILENO); // Redirige la entrada estándar (stdin) al archivo
                close(fd_in);              // Cierra el descriptor de archivo original
            } else if (i > 0) { // Si no hay redirección de entrada y no es el primer comando, usa la tubería anterior como entrada
                dup2(tuberias[i - 1][0], STDIN_FILENO); // Redirige stdin al extremo de lectura de la tubería anterior
            }

            // --- Manejo de redirección de salida ---
            if (comandos_parseados[i].archivo_salida != NULL) {
                int flags = O_WRONLY | O_CREAT; // Abrir en modo escritura, crear si no existe
                if (comandos_parseados[i].tipo_operacion == REDIR_SALIDA_ANEXAR) {
                    flags |= O_APPEND; // Añadir al final (para '>>')
                } else { // REDIR_SALIDA_TRUNCAR (para '>')
                    flags |= O_TRUNC;  // Truncar el archivo si ya existe
                }
                // Abre el archivo de salida con los permisos 0644 (lectura/escritura para el dueño, lectura para grupo/otros)
                int fd_out = open(comandos_parseados[i].archivo_salida, flags, 0644);
                if (fd_out == -1) {
                    imprimir_error("Error al abrir archivo de salida");
                    exit(EXIT_FAILURE); // El hijo termina con fallo
                }
                dup2(fd_out, STDOUT_FILENO); // Redirige la salida estándar (stdout) al archivo
                close(fd_out);               // Cierra el descriptor de archivo original
            } else if (i < num_comandos_tuberia - 1) { // Si no hay redirección de salida y no es el último comando, usa la tubería para el siguiente comando
                dup2(tuberias[i][1], STDOUT_FILENO); // Redirige stdout al extremo de escritura de la tubería actual
            }

            // Cierra todos los extremos de las tuberías en el proceso hijo.
            // Esto es crucial para que los comandos se comporten correctamente (ej. `cat | wc -l` no se quede colgado)
            for (int j = 0; j < num_comandos_tuberia - 1; j++) {
                close(tuberias[j][0]); // Cierra los extremos de lectura de todas las tuberías
                close(tuberias[j][1]); // Cierra los extremos de escritura de todas las tuberías
            }

            // Ejecuta el comando usando execvp
            // execvp reemplaza el proceso actual con el nuevo programa.
            // Si execvp tiene éxito, nunca regresa; si falla, regresa -1.
            execvp(comandos_parseados[i].argv[0], comandos_parseados[i].argv);
            imprimir_error("Error al ejecutar el comando"); // Solo se ejecuta si execvp falla
            exit(EXIT_FAILURE); // El hijo termina con fallo si el comando no se pudo ejecutar
        }
    }

    // CÓDIGO DEL PROCESO PADRE
    // Cierra todos los descriptores de archivo de las tuberías en el proceso padre.
    // Esto es importante para que los comandos hijos sepan cuándo la entrada de la tubería se ha cerrado.
    for (int i = 0; i < num_comandos_tuberia - 1; i++) {
        close(tuberias[i][0]);
        close(tuberias[i][1]);
    }

    // El último PID de la tubería es el proceso en primer plano
    pid_proceso_en_primer_plano = pids[num_comandos_tuberia - 1];

    // Restaurar manejadores de SIGINT/SIGQUIT para que el padre pueda reenviar la señal al hijo en foreground.
    // Se usa `signal` aquí temporalmente para que el manejador se active mientras se espera.
    signal(SIGINT, manejador_sigint_quit);
    signal(SIGQUIT, manejador_sigint_quit);

    int status;
    // El padre espera a que cada uno de sus hijos en la tubería termine
    for (int i = 0; i < num_comandos_tuberia; i++) {
        waitpid(pids[i], &status, 0); // Espera por el PID específico (0 significa esperar hasta que termine)
        if (i == num_comandos_tuberia - 1) { // Captura el estado de salida del ÚLTIMO comando de la tubería
            if (WIFEXITED(status)) { // Si el proceso terminó normalmente
                estado_salida_final = WEXITSTATUS(status); // Obtiene el código de salida
            } else if (WIFSIGNALED(status)) { // Si el proceso fue terminado por una señal
                estado_salida_final = 128 + WTERMSIG(status); // Convención para terminación por señal
            }
        }
    }
    pid_proceso_en_primer_plano = 0; // Resetea el PID en primer plano cuando la tubería ha terminado

    // Restaurar manejadores de SIGINT/SIGQUIT a SIG_IGN (ignorar) para el prompt del shell
    // Esto previene que Ctrl+C termine el shell cuando no hay un proceso en foreground.
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    return estado_salida_final; // Devuelve el estado de salida del último comando ejecutado
}

// --- Implementación de funciones de manejo de señales ---

/**
 * @brief Configura los manejadores de señales para el proceso padre del shell.
 * Ignora SIGINT (Ctrl+C), SIGQUIT (Ctrl+\) y SIGTSTP (Ctrl+Z) para que el shell no termine
 * inesperadamente por estas señales.
 * Configura un manejador para SIGCHLD para evitar procesos "zombie" recolectando
 * el estado de los hijos que terminan.
 */
void configurar_senales_padre() {
    // Ignorar SIGINT (Ctrl+C), SIGQUIT (Ctrl+\), SIGTSTP (Ctrl+Z) para el shell padre
    // Esto es para que el shell no se cierre cuando se presiona Ctrl+C, etc.
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    // Configurar manejador para SIGCHLD para evitar zombies
    struct sigaction sa;         // Estructura para configurar el manejador de señal
    memset(&sa, 0, sizeof(sa));  // Inicializa la estructura a ceros
    sa.sa_handler = manejador_sigchld; // Asigna la función manejadora
    sigemptyset(&sa.sa_mask);    // Limpia la máscara de señales (no bloquea ninguna señal adicional)
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // SA_RESTART: reanuda llamadas al sistema interrumpidas por la señal
                                              // SA_NOCLDSTOP: no recibe SIGCHLD cuando un hijo se detiene (solo cuando termina)
    // Instala el manejador de SIGCHLD
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        imprimir_error("Error al configurar el manejador de SIGCHLD");
        exit(EXIT_FAILURE); // Error crítico, el shell no debería continuar si no puede manejar SIGCHLD
    }
}

/**
 * @brief Restaura los manejadores de señales a su comportamiento por defecto para los procesos hijos.
 * Esto es crucial para que los hijos respondan a Ctrl+C, Ctrl+\, etc., como programas normales.
 * Los hijos deben tener sus propios manejadores de señal, generalmente los predeterminados.
 */
void restaurar_senales_hijo() {
    signal(SIGINT, SIG_DFL);  // SIG_DFL: comportamiento por defecto (terminar el proceso)
    signal(SIGQUIT, SIG_DFL); // SIG_DFL: comportamiento por defecto (terminar y generar un coredump)
    signal(SIGTSTP, SIG_DFL); // SIG_DFL: comportamiento por defecto (detener el proceso)
    signal(SIGCHLD, SIG_DFL); // Los hijos no necesitan manejar SIGCHLD, por lo que se restaura a por defecto
}

/**
 * @brief Manejador de la señal SIGINT (Ctrl+C) y SIGQUIT (Ctrl+\) para el shell padre.
 * Si hay un proceso hijo en primer plano (foreground), reenvía la señal a ese proceso.
 * Si no hay un proceso en primer plano, el shell simplemente ignora la señal o imprime un mensaje
 * para limpiar el prompt, manteniendo el shell en ejecución.
 * @param signo El número de la señal recibida (SIGINT o SIGQUIT).
 */
void manejador_sigint_quit(int signo) {
    // Si hay un PID de un proceso en primer plano almacenado en la variable global
    if (pid_proceso_en_primer_plano > 0) {
        // Enviar la señal al proceso hijo en primer plano
        if (kill(pid_proceso_en_primer_plano, signo) == -1) {
            if (errno != ESRCH) { // ESRCH significa que el proceso no existe (puede haber terminado justo antes)
                imprimir_error("Error al enviar señal al proceso hijo");
            }
        }
        // Imprimir un mensaje informativo sobre la cancelación/terminación del programa
        // Se añade un '\n' antes para que el mensaje no se mezcle con el prompt actual
        if (signo == SIGINT) {
            printf("\nPrograma cancelado (Ctrl+C).\n");
            fflush(stdout);
        } else if (signo == SIGQUIT) {
            printf("\nPrograma terminado (Ctrl+\\).\n");
            fflush(stdout);
        }
    } else {
        // Si no hay proceso en primer plano, el shell ignora la señal para no cerrarse
        // y simplemente imprime una nueva línea para limpiar el prompt.
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
 * Utiliza `waitpid` con `WNOHANG` para no bloquear al shell mientras espera por hijos.
 * También captura hijos que se detienen o se reanudan.
 * @param signo El número de la señal (SIGCHLD).
 */
void manejador_sigchld(int signo) {
    (void)signo; // Evita la advertencia de "unused parameter" ya que 'signo' no se usa directamente en el cuerpo.

    pid_t pid;    // Variable para almacenar el PID del hijo que termina
    int status;   // Variable para almacenar el estado de salida del hijo

    // Bucle para recolectar todos los hijos zombies de forma no bloqueante
    // waitpid(-1, ...): espera a cualquier proceso hijo
    // WNOHANG: no bloquea si no hay hijos terminados inmediatamente
    // WUNTRACED: también reporta hijos que se han detenido (ej. con Ctrl+Z)
    // WCONTINUED: reporta hijos que se han reanudado (ej. con 'fg')
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        // Opcional: imprimir mensajes sobre el estado de los hijos.
        // Se comentan por defecto para evitar saturar la salida en shells normales,
        // pero pueden ser útiles para depuración.
        // if (WIFEXITED(status)) {
        //    printf("Proceso hijo [PID %d] terminado con estado %d.\n", pid, WEXITSTATUS(status));
        // } else if (WIFSIGNALED(status)) {
        //    printf("Proceso hijo [PID %d] terminado por señal %d.\n", pid, WTERMSIG(status));
        // } else if (WIFSTOPPED(status)) {
        //    printf("Proceso hijo [PID %d] detenido por señal %d.\n", pid, WSTOPSIG(status));
        // } else if (WIFCONTINUED(status)) {
        //    printf("Proceso hijo [PID %d] reanudado.\n", pid);
        // }
        // fflush(stdout); // Asegura que los mensajes se impriman
    }
}
