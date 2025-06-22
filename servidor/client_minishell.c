#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include <readline/readline.h>
#include <readline/history.h>

// --- Constantes de configuración de red ---
#define SERVER_IP "127.0.0.1" // Cambia esto a la IP de tu servidor si no es local
#define SERVER_PORT 1666      // Puerto del servidor
#define CONNECT_RETRIES 5     // Número de reintentos de conexión
#define RETRY_DELAY_SEC 2     // Retardo entre reintentos en segundos

// --- Definiciones de constantes del shell ---
#define MAX_COMANDOS 10
#define MAX_ARGUMENTOS 40
#define MAX_LONGITUD_ENTRADA 1024
#define MAX_SEGMENTOS_AND 5
#define BUFFER_SIZE 4096 // Tamaño del buffer para comunicación de socket

typedef enum {
    SIN_REDIR = 0,
    REDIR_ENTRADA,
    REDIR_SALIDA_TRUNCAR,
    REDIR_SALIDA_ANEXAR
} TipoOperacion;

typedef struct {
    char *argv[MAX_ARGUMENTOS];
    int argc;
    char *archivo_entrada;
    char *archivo_salida;
    TipoOperacion tipo_operacion;
} ComandoParseado;

volatile pid_t pid_proceso_en_primer_plano = 0;

// --- Prototipos de funciones auxiliares y de manejo de señales ---
void imprimir_error(const char *mensaje);
int dividir_cadena(char *cadena, char *delimitador, char *tokens[]);
char *generar_prompt();
void imprimir_bienvenida();
void deshabilitar_reporte_raton();
void configurar_senales_padre();
void restaurar_senales_hijo();
void manejador_sigint_quit(int signo);
void manejador_sigchld(int signo);
// Modificamos estos prototipos para incluir el client_sockfd
int ejecutar_comando_interno(ComandoParseado *comando, int client_sockfd);
int ejecutar_tuberia(ComandoParseado comandos_parseados[], int num_comandos_tuberia, int client_sockfd);
int parsear_argumentos_comando(char *cadena_comando, ComandoParseado *comando_parseado);
void liberar_comando_parseado(ComandoParseado *comando_parseado);
void get_os_name(char *os_name, size_t size);
void build_command_string(char *dest, size_t dest_size, ComandoParseado *comando); // Agregado el prototipo

int main() {
    int client_sockfd;
    struct sockaddr_in server_addr;
    char client_os[256];
    char server_response[BUFFER_SIZE];
    ssize_t bytes_received;

    // 1. Configuración de la conexión al servidor
    client_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sockfd == -1) {
        perror("Error al crear el socket del cliente");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Dirección IP inválida/no soportada");
        close(client_sockfd);
        return 1;
    }

    int connected = 0;
    for (int i = 0; i < CONNECT_RETRIES; i++) {
        printf("Intentando conectar con el servidor en %s:%d (Intento %d/%d)...\n", SERVER_IP, SERVER_PORT, i + 1, CONNECT_RETRIES);
        if (connect(client_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            printf("Conexión establecida con el servidor.\n");
            connected = 1;
            break;
        }
        perror("Error al conectar");
        sleep(RETRY_DELAY_SEC); // Espera antes de reintentar
    }

    if (!connected) {
        fprintf(stderr, "No se pudo conectar al servidor después de %d intentos.\n", CONNECT_RETRIES);
        close(client_sockfd);
        return 1;
    }

// 2. Intercambio de saludos con el servidor
    get_os_name(client_os, sizeof(client_os));
    char client_hello[BUFFER_SIZE];
    snprintf(client_hello, sizeof(client_hello), "HOLA_CLIENTE:%s", client_os);

    // --- AGREGAR ESTO PARA EL TIMEOUT ---
    struct timeval timeout;
    timeout.tv_sec = 3;  // 3 segundos de timeout para el saludo
    timeout.tv_usec = 0;

    if (setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("Error al establecer timeout de recepción");
        // No es crítico, el programa puede continuar, pero es bueno reportarlo
    }
    // --- FIN DE AGREGADO ---

    if (send(client_sockfd, client_hello, strlen(client_hello), 0) == -1) {
        perror("Error al enviar saludo al servidor");
        close(client_sockfd);
        return 1;
    }

    bytes_received = recv(client_sockfd, server_response, sizeof(server_response) - 1, 0);

    // --- MODIFICAR LA LÓGICA DE MANEJO DE LA RESPUESTA ---
    if (bytes_received > 0) {
        server_response[bytes_received] = '\0';
        if (strncmp(server_response, "HOLA_SERVIDOR:", 14) == 0) {
            printf("Servidor dice: %s\n", server_response + 14);
        } else {
            // Recibió algo, pero no el saludo esperado
            printf("Respuesta inesperada del servidor (%s). Asumiendo servidor sin saludo.\n", server_response);
            // No se cierra la conexión, se asume que se puede continuar.
        }
    } else if (bytes_received == 0) {
        // Servidor cerró la conexión durante el saludo (o justo después de enviarlo)
        printf("Servidor desconectado o no respondió el saludo. Asumiendo servidor sin saludo.\n");
        // Aquí podrías decidir si quieres cerrar y salir, o intentar continuar.
        // Por la descripción, parece que quieres continuar.
    } else { // bytes_received < 0
        // Error en recv o timeout
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("Timeout al recibir saludo del servidor. Asumiendo servidor sin saludo.\n");
        } else {
            perror("Error al recibir saludo del servidor. Asumiendo servidor sin saludo.");
        }
        // No se cierra la conexión, se asume que se puede continuar.
    }

    // --- RESTAURAR EL TIMEOUT SI ES NECESARIO (o establecer uno más largo para comandos) ---
    // Si quieres que las operaciones de recv posteriores (para comandos) también tengan un timeout
    // o que sean bloqueantes nuevamente, podrías restaurarlo aquí.
    // Para ahora, lo dejaremos sin restaurar, lo que significa que el timeout de 3 segundos
    // aplicará a todas las operaciones de recv en este socket. Si prefieres que solo
    // el saludo tenga timeout, podrías poner timeout.tv_sec = 0; para hacerlo bloqueante de nuevo.
    // O un timeout más largo para comandos:
    timeout.tv_sec = 60; // Por ejemplo, 60 segundos para los comandos normales
    if (setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        perror("Error al restaurar timeout de recepción para comandos");
    }
    // --- FIN DE RESTAURACIÓN ---


    // 3. Bucle principal del minishell, enviando salida al socket
    char linea_original[MAX_LONGITUD_ENTRADA];
    char *segmentos_and[MAX_SEGMENTOS_AND];
    int num_segmentos_and;
    char *linea_entrada;
    char *prompt_actual;
    int ultimo_estado_salida = 0;

    configurar_senales_padre();
    deshabilitar_reporte_raton();
    imprimir_bienvenida();

    // Recibir el mensaje de bienvenida y el primer prompt del servidor
    while ((bytes_received = recv(client_sockfd, server_response, sizeof(server_response) - 1, MSG_DONTWAIT)) > 0) {
        server_response[bytes_received] = '\0';
        // Evita imprimir el prompt del servidor varias veces
        if (strstr(server_response, "\n\033[7;32mremote_shell@server:~$ ") == NULL) {
            printf("%s", server_response);
        } else {
            // Si es el prompt, podemos romper el bucle ya que no hay más datos iniciales.
            break;
        }
    }


    while (1) {
        prompt_actual = generar_prompt();
        linea_entrada = readline(prompt_actual);
        free(prompt_actual);

        if (linea_entrada == NULL) { // Ctrl+D
            printf("Saliendo del MiniShell.\n");
            send(client_sockfd, "[CLIENTE_MINISHELL_EVENTO]: MINISHELL_EOF\n", strlen("[CLIENTE_MINISHELL_EVENTO]: MINISHELL_EOF\n"), 0); // Notificar al servidor
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

        num_segmentos_and = dividir_cadena(linea_original, "&&", segmentos_and);
        ultimo_estado_salida = 0;

        for (int s = 0; s < num_segmentos_and; s++) {
            if (s > 0 && ultimo_estado_salida != 0) {
                continue;
            }

            char *comandos_str[MAX_COMANDOS];
            ComandoParseado comandos_parseados[MAX_COMANDOS];
            int num_comandos_tuberia;

            num_comandos_tuberia = dividir_cadena(segmentos_and[s], "|", comandos_str);

            if (num_comandos_tuberia < 1 || comandos_str[0] == NULL || strlen(comandos_str[0]) == 0) {
                char err_msg[BUFFER_SIZE];
                snprintf(err_msg, sizeof(err_msg), "[COMANDO]: (Error de parseo)\n[SALIDA]: Error: Comando inválido en segmento '&&'.\n");
                send(client_sockfd, err_msg, strlen(err_msg), 0);
                ultimo_estado_salida = 1;
                continue;
            }

            int error_parseo = 0;
            for (int i = 0; i < num_comandos_tuberia; i++) {
                comandos_parseados[i].argc = 0;
                comandos_parseados[i].archivo_entrada = NULL;
                comandos_parseados[i].archivo_salida = NULL;
                comandos_parseados[i].tipo_operacion = SIN_REDIR;

                if (parsear_argumentos_comando(comandos_str[i], &comandos_parseados[i]) != 0) {
                    char err_msg[BUFFER_SIZE];
                    snprintf(err_msg, sizeof(err_msg), "[COMANDO]: %s\n[SALIDA]: Error de sintaxis en el comando '%s'.\n", comandos_str[i], comandos_str[i]);
                    send(client_sockfd, err_msg, strlen(err_msg), 0);
                    error_parseo = 1;
                    for (int k = 0; k <= i; k++) {
                        liberar_comando_parseado(&comandos_parseados[k]);
                    }
                    ultimo_estado_salida = 1;
                    break;
                }
            }

            if (error_parseo) {
                continue;
            }

            // --- Manejo de comandos internos (built-ins) ---
            if (num_comandos_tuberia == 1 &&
                comandos_parseados[0].archivo_entrada == NULL &&
                comandos_parseados[0].archivo_salida == NULL) {
                if (ejecutar_comando_interno(&comandos_parseados[0], client_sockfd)) {
                    ultimo_estado_salida = 0;
                    // Si el comando interno fue 'exit' o 'quit', salir del bucle principal
                    if (strcmp(comandos_parseados[0].argv[0], "exit") == 0 ||
                        strcmp(comandos_parseados[0].argv[0], "quit") == 0) {
                        liberar_comando_parseado(&comandos_parseados[0]);
                        goto end_session;
                    }
                    liberar_comando_parseado(&comandos_parseados[0]);
                    continue;
                }
            }

            // --- Ejecución de tuberías (o comando único externo) ---
            ultimo_estado_salida = ejecutar_tuberia(comandos_parseados, num_comandos_tuberia, client_sockfd);

            for (int i = 0; i < num_comandos_tuberia; i++) {
                liberar_comando_parseado(&comandos_parseados[i]);
            }

            // Después de cada comando/tubería, verificar si el servidor envió un mensaje especial
            // o la salida del comando ejecutado en el servidor, o el prompt del servidor.
            // Loop para asegurar que leemos todo lo que el servidor envió
            int prompt_received = 0;
            while ((bytes_received = recv(client_sockfd, server_response, sizeof(server_response) - 1, MSG_DONTWAIT)) > 0) {
                server_response[bytes_received] = '\0';

                // Detectar el prompt del servidor para saber cuándo hemos terminado de leer la salida
                if (strstr(server_response, "\n\033[7;32mremote_shell@server:~$ ") != NULL) {
                    prompt_received = 1;
                    // Imprimir solo la parte antes del prompt si hay algo
                    char *prompt_start = strstr(server_response, "\n\033[7;32mremote_shell@server:~$ ");
                    if (prompt_start != server_response) {
                        *prompt_start = '\0'; // Cortar la cadena antes del prompt
                        printf("[Respuesta del servidor]: %s", server_response);
                    }
                    // No imprimimos el prompt del servidor, ya que el minishell imprime el suyo
                    break;
                }

                printf("[Respuesta del servidor]: %s", server_response); // Imprimir salida del servidor

                if (strstr(server_response, "HAZ SIDO HACKEADO") != NULL) {
                    printf("Servidor cerrando la conexión debido a 'passwd'.\n");
                    goto end_session;
                }
            }

            // Si el servidor no envió su prompt, podría ser un error o una desconexión.
            if (!prompt_received && bytes_received == 0) {
                printf("Servidor desconectado inesperadamente.\n");
                goto end_session;
            } else if (bytes_received == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
                perror("Error al recibir del servidor");
                goto end_session;
            }
        }
    }

end_session:
    close(client_sockfd);
    return 0;
}

// --- Implementación de funciones auxiliares ---

void imprimir_bienvenida() {
    printf("\033[1;36m");
    printf("┏┳┓╻┏┓╻╻┏━┓╻ ╻┏━╸╻  ╻      ┏━╸┏━┓╻ ╻╻┏━┓┏━┓      ┏━┓\n");
    printf("┃┃┃┃┃┗┫┃┗━┓┣━┫┣╸ ┃  ┃      ┣╸ ┃┓┃┃ ┃┃┣━┛┃ ┃      ┗━┫\n");
    printf("╹ ╹╹╹ ╹╹┗━┛╹ ╹┗━╸┗━╸┗━╸    ┗━╸┗┻┛┗━┛╹╹  ┗━┛     #┗━┛\n");
    printf("\033[0m");
    printf("\n");
    fflush(stdout);
}

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

void imprimir_error(const char *mensaje) {
    perror(mensaje);
    fflush(stderr);
}

int dividir_cadena(char *cadena, char *delimitador, char *tokens[]) {
    int contador = 0;
    char *token;
    char *saveptr;

    token = strtok_r(cadena, delimitador, &saveptr);
    while (token != NULL && contador < MAX_COMANDOS) {
        while (*token == ' ' || *token == '\t' || *token == '\n') token++;
        char *fin = token + strlen(token) - 1;
        while (fin > token && (*fin == ' ' || *fin == '\t' || *fin == '\n')) fin--;
        *(fin + 1) = '\0';

        if (strlen(token) > 0) {
            tokens[contador++] = token;
        }
        token = strtok_r(NULL, delimitador, &saveptr);
    }
    tokens[contador] = NULL;
    return contador;
}

void deshabilitar_reporte_raton() {
    printf("\033[?1000l");
    printf("\033[?1002l");
    printf("\033[?1003l");
    fflush(stdout);
}

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

int parsear_argumentos_comando(char *cadena_comando, ComandoParseado *comando_parseado) {
    comando_parseado->argc = 0;
    comando_parseado->archivo_entrada = NULL;
    comando_parseado->archivo_salida = NULL;
    comando_parseado->tipo_operacion = SIN_REDIR;

    char *current_char = cadena_comando;
    char *arg_start = NULL;
    char quote_char = '\0';
    int i = 0;
    char temp_arg_buffer[MAX_LONGITUD_ENTRADA];

    while (*current_char != '\0') {
        if (quote_char == '\0' && (*current_char == ' ' || *current_char == '\t' || *current_char == '\n')) {
            if (arg_start != NULL) {
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
        }
        else if (*current_char == '\'' || *current_char == '"') {
            if (quote_char == '\0') {
                quote_char = *current_char;
                if (arg_start == NULL) {
                    arg_start = current_char;
                }
            } else if (quote_char == *current_char) {
                quote_char = '\0';
            } else {
                if (arg_start == NULL) {
                    arg_start = current_char;
                }
                temp_arg_buffer[i++] = *current_char;
            }
        }
        else if (*current_char == '\\' && (current_char[1] == '\'' || current_char[1] == '"' || current_char[1] == ' ' || current_char[1] == '\t')) {
            if (arg_start == NULL) {
                arg_start = current_char;
            }
            current_char++;
            temp_arg_buffer[i++] = *current_char;
        }
        else if (*current_char == '<' && quote_char == '\0') {
            if (arg_start != NULL) {
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
            current_char++;
            while (*current_char == ' ' || *current_char == '\t') current_char++;
            char *file_start = current_char;
            while (*current_char != '\0' && *current_char != ' ' && *current_char != '\t' && *current_char != '>' && *current_char != '<') {
                current_char++;
            }
            if (file_start == current_char) {
                fprintf(stderr, "Error de sintaxis: se esperaba nombre de archivo después de '<'.\n");
                return -1;
            }
            char temp_file_name[PATH_MAX];
            strncpy(temp_file_name, file_start, current_char - file_start);
            temp_file_name[current_char - file_start] = '\0';
            if (comando_parseado->archivo_entrada != NULL) {
                fprintf(stderr, "Error de sintaxis: múltiples redirecciones de entrada.\n");
                return -1;
            }
            comando_parseado->archivo_entrada = strdup(temp_file_name);
            if (comando_parseado->archivo_entrada == NULL) {
                imprimir_error("strdup");
                return -1;
            }
            current_char--;
        }
        else if (*current_char == '>' && quote_char == '\0') {
            if (arg_start != NULL) {
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
            current_char++;
            TipoOperacion op = REDIR_SALIDA_TRUNCAR;
            if (*current_char == '>') {
                op = REDIR_SALIDA_ANEXAR;
                current_char++;
            }
            while (*current_char == ' ' || *current_char == '\t') current_char++;
            char *file_start = current_char;
            while (*current_char != '\0' && *current_char != ' ' && *current_char != '\t' && *current_char != '>' && *current_char != '<') {
                current_char++;
            }
            if (file_start == current_char) {
                fprintf(stderr, "Error de sintaxis: se esperaba nombre de archivo después de redirección de salida.\n");
                return -1;
            }
            char temp_file_name[PATH_MAX];
            strncpy(temp_file_name, file_start, current_char - file_start);
            temp_file_name[current_char - file_start] = '\0';
            if (comando_parseado->archivo_salida != NULL) {
                fprintf(stderr, "Error de sintaxis: múltiples redirecciones de salida.\n");
                return -1;
            }
            comando_parseado->archivo_salida = strdup(temp_file_name);
            if (comando_parseado->archivo_salida == NULL) {
                imprimir_error("strdup");
                return -1;
            }
            comando_parseado->tipo_operacion = op;
            current_char--;
        }
        else {
            if (arg_start == NULL) {
                arg_start = current_char;
            }
            temp_arg_buffer[i++] = *current_char;
            if (i >= MAX_LONGITUD_ENTRADA) {
                fprintf(stderr, "Argumento demasiado largo.\n");
                return -1;
            }
        }
        current_char++;
    }

    if (arg_start != NULL && i > 0) {
        if (quote_char != '\0') {
            fprintf(stderr, "Error de sintaxis: comilla sin cerrar.\n");
            return -1;
        }
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
    } else if (quote_char != '\0') {
        fprintf(stderr, "Error de sintaxis: comilla sin cerrar al final del comando.\n");
        return -1;
    }

    comando_parseado->argv[comando_parseado->argc] = NULL;

    if (comando_parseado->argc == 0 && comando_parseado->archivo_entrada == NULL && comando_parseado->archivo_salida == NULL) {
        fprintf(stderr, "Error de sintaxis: comando vacío o solo con operadores.\n");
        return -1;
    }

    return 0;
}

// Nueva función auxiliar para construir la cadena del comando
void build_command_string(char *dest, size_t dest_size, ComandoParseado *comando) {
    dest[0] = '\0'; // Asegurarse de que esté vacío al inicio
    for (int i = 0; i < comando->argc; i++) {
        strncat(dest, comando->argv[i], dest_size - strlen(dest) - 1);
        if (i < comando->argc - 1) {
            strncat(dest, " ", dest_size - strlen(dest) - 1);
        }
    }
    if (comando->archivo_entrada) {
        strncat(dest, " < ", dest_size - strlen(dest) - 1);
        strncat(dest, comando->archivo_entrada, dest_size - strlen(dest) - 1);
    }
    if (comando->archivo_salida) {
        if (comando->tipo_operacion == REDIR_SALIDA_ANEXAR) {
            strncat(dest, " >> ", dest_size - strlen(dest) - 1);
        } else {
            strncat(dest, " > ", dest_size - strlen(dest) - 1);
        }
        strncat(dest, comando->archivo_salida, dest_size - strlen(dest) - 1);
    }
}


int ejecutar_comando_interno(ComandoParseado *comando, int client_sockfd) {
    if (comando->argc == 0) return 0;

    char output_buffer[BUFFER_SIZE];
    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);
    int pipefd[2];

    if (pipe(pipefd) == -1) {
        perror("pipe for built-in output");
        return 1;
    }

    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    int handled = 0;
    char command_str_full[MAX_LONGITUD_ENTRADA];
    build_command_string(command_str_full, sizeof(command_str_full), comando);

    if (strcmp(comando->argv[0], "exit") == 0 || strcmp(comando->argv[0], "quit") == 0) {
        printf("Saliendo del MiniShell.\n");
        fflush(stdout);
        char message_to_server[BUFFER_SIZE];
        snprintf(message_to_server, sizeof(message_to_server), "[COMANDO]: %s\n[SALIDA]: Saliendo del MiniShell.\n[CLIENTE_MINISHELL_EVENTO]: MINISHELL_QUIT\n", command_str_full);
        send(client_sockfd, message_to_server, strlen(message_to_server), 0);
        handled = 1;
    }
    else if (strcmp(comando->argv[0], "history") == 0) {
        printf("[COMANDO]: %s\n", command_str_full); // Imprimir el comando localmente antes de enviar la salida
        history_set_pos(0);
        HIST_ENTRY *h_entry;
        int i = 0;
        char history_output[BUFFER_SIZE];
        history_output[0] = '\0'; // Reiniciar buffer

        while ((h_entry = history_get(i++)) != NULL) {
            char line_buf[256];
            snprintf(line_buf, sizeof(line_buf), "%d: %s\n", i, h_entry->line);
            strncat(history_output, line_buf, sizeof(history_output) - strlen(history_output) - 1);
        }
        printf("%s", history_output); // Imprimir localmente
        fflush(stdout);

        char message_to_server[BUFFER_SIZE + MAX_LONGITUD_ENTRADA];
        snprintf(message_to_server, sizeof(message_to_server), "[COMANDO]: %s\n[SALIDA]: %s", command_str_full, history_output);
        send(client_sockfd, message_to_server, strlen(message_to_server), 0);
        handled = 1;
    }
    else if (strcmp(comando->argv[0], "cd") == 0) {
        printf("[COMANDO]: %s\n", command_str_full); // Imprimir el comando localmente
        if (comando->argv[1] == NULL) {
            fprintf(stderr, "Uso: cd <directorio>\n");
        } else {
            if (chdir(comando->argv[1]) == -1) {
                perror("Error al cambiar de directorio");
            }
        }
        fflush(stdout);
        fflush(stderr); // Asegura que los errores/mensajes de cd se capturen

        // Capturar la salida/error y enviarla al servidor
        ssize_t bytes_read = read(pipefd[0], output_buffer, sizeof(output_buffer) - 1);
        if (bytes_read > 0) {
            output_buffer[bytes_read] = '\0';
            char message_to_server[BUFFER_SIZE + MAX_LONGITUD_ENTRADA];
            snprintf(message_to_server, sizeof(message_to_server), "[COMANDO]: %s\n[SALIDA]: %s", command_str_full, output_buffer);
            send(client_sockfd, message_to_server, strlen(message_to_server), 0);
        } else {
            // Si no hay salida, enviar solo el comando
            char message_to_server[BUFFER_SIZE + MAX_LONGITUD_ENTRADA];
            snprintf(message_to_server, sizeof(message_to_server), "[COMANDO]: %s\n[SALIDA]: (Sin salida visible)\n", command_str_full);
            send(client_sockfd, message_to_server, strlen(message_to_server), 0);
        }
        handled = 1;
    }

    // Restaurar stdout y stderr
    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);
    close(original_stdout);
    close(original_stderr);
    close(pipefd[0]);

    return handled;
}

int ejecutar_tuberia(ComandoParseado comandos_parseados[], int num_comandos_tuberia, int client_sockfd) {
    int tuberias[MAX_COMANDOS - 1][2];
    pid_t pids[MAX_COMANDOS];
    int estado_salida_final = 1;

    // Construir la cadena de comando completa para la tubería
    char full_command_line[MAX_LONGITUD_ENTRADA * MAX_COMANDOS]; // Suficientemente grande
    full_command_line[0] = '\0';
    for(int i = 0; i < num_comandos_tuberia; i++) {
        char cmd_part[MAX_LONGITUD_ENTRADA];
        build_command_string(cmd_part, sizeof(cmd_part), &comandos_parseados[i]);
        strncat(full_command_line, cmd_part, sizeof(full_command_line) - strlen(full_command_line) - 1);
        if (i < num_comandos_tuberia - 1) {
            strncat(full_command_line, " | ", sizeof(full_command_line) - strlen(full_command_line) - 1);
        }
    }

    // Preparar mensaje inicial del comando
    char initial_command_message[BUFFER_SIZE + MAX_LONGITUD_ENTRADA];
    snprintf(initial_command_message, sizeof(initial_command_message), "[COMANDO]: %s\n", full_command_line);
    // No enviar la salida aquí, se capturará después de la ejecución.

    for (int i = 0; i < num_comandos_tuberia - 1; i++) {
        if (pipe(tuberias[i]) == -1) {
            imprimir_error("Error al crear la tubería");
            for (int k = 0; k < i; k++) {
                close(tuberias[k][0]);
                close(tuberias[k][1]);
            }
            char err_msg[BUFFER_SIZE];
            snprintf(err_msg, sizeof(err_msg), "[SALIDA]: Error: No se pudo crear la tubería para el comando '%s'.\n", full_command_line);
            send(client_sockfd, initial_command_message, strlen(initial_command_message), 0); // Enviar el comando incluso si falló el pipe
            send(client_sockfd, err_msg, strlen(err_msg), 0);
            return 1;
        }
    }

    // Pipe para capturar la salida final de la tubería
    int output_pipe[2];
    if (pipe(output_pipe) == -1) {
        perror("Error al crear pipe para salida final");
        for (int j = 0; j < num_comandos_tuberia - 1; j++) {
            close(tuberias[j][0]);
            close(tuberias[j][1]);
        }
        char err_msg[BUFFER_SIZE];
        snprintf(err_msg, sizeof(err_msg), "[SALIDA]: Error: No se pudo crear el pipe de salida final para el comando '%s'.\n", full_command_line);
        send(client_sockfd, initial_command_message, strlen(initial_command_message), 0); // Enviar el comando incluso si falló el pipe
        send(client_sockfd, err_msg, strlen(err_msg), 0);
        return 1;
    }

    for (int i = 0; i < num_comandos_tuberia; i++) {
        pids[i] = fork();
        if (pids[i] == -1) {
            imprimir_error("Error al crear el proceso hijo");
            for (int j = 0; j < num_comandos_tuberia - 1; j++) {
                close(tuberias[j][0]);
                close(tuberias[j][1]);
            }
            close(output_pipe[0]);
            close(output_pipe[1]);
            for (int k = 0; k < i; k++) {
                if (pids[k] > 0) kill(pids[k], SIGKILL);
            }
            char err_msg[BUFFER_SIZE];
            snprintf(err_msg, sizeof(err_msg), "[SALIDA]: Error: No se pudo forkear para el comando '%s'.\n", full_command_line);
            send(client_sockfd, initial_command_message, strlen(initial_command_message), 0); // Enviar el comando incluso si falló el fork
            send(client_sockfd, err_msg, strlen(err_msg), 0);
            return 1;
        }

        if (pids[i] == 0) { // CÓDIGO DEL PROCESO HIJO
            restaurar_senales_hijo();

            if (comandos_parseados[i].archivo_entrada != NULL) {
                int fd_in = open(comandos_parseados[i].archivo_entrada, O_RDONLY);
                if (fd_in == -1) {
                    fprintf(stderr, "minishell: no such file or directory: %s\n", comandos_parseados[i].archivo_entrada);
                    exit(1); // Usar exit(1) para indicar un error específico
                }
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            } else if (i > 0) {
                dup2(tuberias[i - 1][0], STDIN_FILENO);
            }

            if (comandos_parseados[i].archivo_salida != NULL) {
                int flags = O_WRONLY | O_CREAT;
                if (comandos_parseados[i].tipo_operacion == REDIR_SALIDA_ANEXAR) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }
                int fd_out = open(comandos_parseados[i].archivo_salida, flags, 0644);
                if (fd_out == -1) {
                    perror("minishell: Error al abrir archivo de salida");
                    exit(1);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            } else if (i < num_comandos_tuberia - 1) {
                dup2(tuberias[i][1], STDOUT_FILENO);
            } else { // Si es el último comando y no hay redirección a archivo, redirige a output_pipe
                dup2(output_pipe[1], STDOUT_FILENO);
                dup2(output_pipe[1], STDERR_FILENO); // También redirigir stderr
            }

            for (int j = 0; j < num_comandos_tuberia - 1; j++) {
                close(tuberias[j][0]);
                close(tuberias[j][1]);
            }
            close(output_pipe[0]);
            close(output_pipe[1]);

            execvp(comandos_parseados[i].argv[0], comandos_parseados[i].argv);
            // Si execvp falla, imprimir error y salir
            perror("minishell"); // Imprime el error real de execvp (ej. "command not found")
            exit(127); // Convención para comando no encontrado
        }
    }

    // CÓDIGO DEL PROCESO PADRE
    for (int i = 0; i < num_comandos_tuberia - 1; i++) {
        close(tuberias[i][0]);
        close(tuberias[i][1]);
    }
    close(output_pipe[1]);

    pid_proceso_en_primer_plano = pids[num_comandos_tuberia - 1];

    signal(SIGINT, manejador_sigint_quit);
    signal(SIGQUIT, manejador_sigint_quit);

    int status;
    for (int i = 0; i < num_comandos_tuberia; i++) {
        waitpid(pids[i], &status, 0);
        if (i == num_comandos_tuberia - 1) { // Capturar el estado de salida del último comando
            if (WIFEXITED(status)) {
                estado_salida_final = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                estado_salida_final = 128 + WTERMSIG(status);
            }
        }
    }
    pid_proceso_en_primer_plano = 0;

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    // Leer la salida capturada y enviarla al servidor
    char command_output_buffer[BUFFER_SIZE * 2]; // Suficientemente grande para toda la salida
    ssize_t bytes_read;
    command_output_buffer[0] = '\0';
    
    // Leer toda la salida disponible del pipe
    while ((bytes_read = read(output_pipe[0], command_output_buffer + strlen(command_output_buffer), sizeof(command_output_buffer) - strlen(command_output_buffer) - 1)) > 0) {
        command_output_buffer[strlen(command_output_buffer)] = '\0';
    }
    close(output_pipe[0]);

    // Enviar el comando y la salida al servidor en un solo mensaje si es posible
    char full_message_to_server[BUFFER_SIZE * 3]; // Puede ser más grande
    snprintf(full_message_to_server, sizeof(full_message_to_server), "[COMANDO]: %s\n[SALIDA]: %s%s",
             full_command_line,
             strlen(command_output_buffer) > 0 ? command_output_buffer : "(Sin salida visible)\n",
             (strlen(command_output_buffer) > 0 && command_output_buffer[strlen(command_output_buffer)-1] != '\n') ? "\n" : "");
    send(client_sockfd, full_message_to_server, strlen(full_message_to_server), 0);

    return estado_salida_final;
}

void configurar_senales_padre() {
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = manejador_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        imprimir_error("Error al configurar el manejador de SIGCHLD");
        exit(EXIT_FAILURE);
    }
}

void restaurar_senales_hijo() {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

void manejador_sigint_quit(int signo) {
    if (pid_proceso_en_primer_plano > 0) {
        if (kill(pid_proceso_en_primer_plano, signo) == -1) {
            if (errno != ESRCH) {
                imprimir_error("Error al enviar señal al proceso hijo");
            }
        }
        if (signo == SIGINT) {
            printf("\nPrograma cancelado (Ctrl+C).\n");
            fflush(stdout);
        } else if (signo == SIGQUIT) {
            printf("\nPrograma terminado (Ctrl+\\).\n");
            fflush(stdout);
        }
    } else {
        if (signo == SIGINT) {
            printf("\n");
            fflush(stdout);
        } else if (signo == SIGQUIT) {
            printf("\n");
            fflush(stdout);
        }
    }
}

void manejador_sigchld(int signo) {
    (void)signo;
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        // No imprimir nada para evitar interferir con la salida del shell o del servidor
    }
}

// Función para obtener el nombre de la distribución (igual que en el servidor)
void get_os_name(char *os_name, size_t size) {
    FILE *fp;
    char buffer[256];
    os_name[0] = '\0';

    fp = fopen("/etc/os-release", "r");
    if (fp == NULL) {
        strncpy(os_name, "Desconocido", size - 1);
        os_name[size - 1] = '\0';
        return;
    }

    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        if (strncmp(buffer, "PRETTY_NAME=", 12) == 0) {
            char *start = strchr(buffer, '"');
            if (start != NULL) {
                start++;
                char *end = strchr(start, '"');
                if (end != NULL) {
                    size_t len = end - start;
                    if (len < size) {
                        strncpy(os_name, start, len);
                        os_name[len] = '\0';
                    } else {
                        strncpy(os_name, start, size - 1);
                        os_name[size - 1] = '\0';
                    }
                }
            }
            break;
        }
    }
    fclose(fp);
    if (os_name[0] == '\0') {
        strncpy(os_name, "Desconocido", size - 1);
        os_name[size - 1] = '\0';
    }
}
