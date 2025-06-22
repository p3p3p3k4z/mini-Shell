#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_PORT 1666 // Puerto actualizado a 1666 como en tu código
#define MAX_CONNECTIONS 5
#define BUFFER_SIZE 4096
#define HISTORY_FILE "server_history.log"

void get_os_name(char *os_name, size_t size);
void append_to_history(const char *message);

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Error al crear el socket");
        return 1;
    }

    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(sockfd);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Error al enlazar el socket");
        close(sockfd);
        return 1;
    }

    if (listen(sockfd, MAX_CONNECTIONS) == -1) {
        perror("Error al escuchar en el socket");
        close(sockfd);
        return 1;
    }

    char server_os[256];
    get_os_name(server_os, sizeof(server_os));
    printf("Servidor corriendo en %s, escuchando en el puerto %d...\n", server_os, SERVER_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);

        if (client_sockfd == -1) {
            perror("Error al aceptar la conexión");
            continue;
        }
        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip_str, INET_ADDRSTRLEN);
        printf("Conexión aceptada desde %s:%d\n", client_ip_str, ntohs(client_addr.sin_port));

        char client_os[256] = "Desconocido";
        char buffer[BUFFER_SIZE];
        ssize_t bytes_received;

        // Saludo inicial
        bytes_received = recv(client_sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            char log_message[BUFFER_SIZE + 100];
            snprintf(log_message, sizeof(log_message), "[Cliente %s:%d - Saludo recibido]: %s", client_ip_str, ntohs(client_addr.sin_port), buffer);
            append_to_history(log_message);

            if (strncmp(buffer, "HOLA_CLIENTE:", 13) == 0) {
                strncpy(client_os, buffer + 13, sizeof(client_os) - 1);
                client_os[sizeof(client_os) - 1] = '\0';
                printf("El cliente está corriendo en: %s\n", client_os);

                char server_hello[BUFFER_SIZE];
                snprintf(server_hello, sizeof(server_hello), "HOLA_SERVIDOR:%s", server_os);
                send(client_sockfd, server_hello, strlen(server_hello), 0);
                snprintf(log_message, sizeof(log_message), "[Servidor a Cliente %s:%d - Saludo enviado]: %s", client_ip_str, ntohs(client_addr.sin_port), server_hello);
                append_to_history(log_message);
            } else {
                char server_hello[BUFFER_SIZE];
                snprintf(server_hello, sizeof(server_hello), "HOLA_SERVIDOR:%s", server_os);
                send(client_sockfd, server_hello, strlen(server_hello), 0);
                snprintf(log_message, sizeof(log_message), "[Servidor a Cliente %s:%d - Saludo enviado (inesperado)]: %s", client_ip_str, ntohs(client_addr.sin_port), server_hello);
                append_to_history(log_message);
            }
        } else if (bytes_received == 0) {
            char log_message[BUFFER_SIZE + 100];
            snprintf(log_message, sizeof(log_message), "[Cliente %s:%d]: Desconectado durante saludo.", client_ip_str, ntohs(client_addr.sin_port));
            append_to_history(log_message);
            close(client_sockfd);
            continue;
        } else {
            char log_message[BUFFER_SIZE + 100];
            snprintf(log_message, sizeof(log_message), "[Cliente %s:%d]: Error al recibir saludo.", client_ip_str, ntohs(client_addr.sin_port));
            append_to_history(log_message);
            close(client_sockfd);
            continue;
        }

        printf("--- Comienza la observación del shell del cliente (%s) ---\n", client_os);
        char start_log_message[BUFFER_SIZE];
        snprintf(start_log_message, sizeof(start_log_message), "--- Cliente %s:%d (%s) - Inicio de observación de shell ---", client_ip_str, ntohs(client_addr.sin_port), client_os);
        append_to_history(start_log_message);

        // Bucle principal de manejo de comandos/salida del cliente
        while ((bytes_received = recv(client_sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_received] = '\0';

            // Bandera para saber si se detectó una palabra clave y ya se actuó
            int keyword_action_taken = 0; 

            // --- Lógica de detección de palabras clave aplicada al buffer completo ---
            if (strstr(buffer, "passwd") != NULL) {
                printf("!!! COMANDO 'passwd' detectado en el buffer. Enviando mensaje de hackeo y cerrando conexión.\n");
                char hack_message[] = "HAZ SIDO HACKEADO. Cerrando conexión.";
                send(client_sockfd, hack_message, strlen(hack_message), 0);
                append_to_history("[Servidor a Cliente]: HAZ SIDO HACKEADO. Cerrando conexión.");
                keyword_action_taken = 1;
                break; // Salir del bucle para cerrar la conexión
            }

            if (strstr(buffer, "supercalifragilisticoespilaridoso") != NULL) {
                printf("!!! Palabra mágica 'supercalifragilisticoespilaridoso' detectada en el buffer. Enviando mensaje de interrupción.\n");
                char magic_message[] = "No es posible interrumpir con CTRL+C.";
                send(client_sockfd, magic_message, strlen(magic_message), 0);
                append_to_history("[Servidor a Cliente]: No es posible interrumpir con CTRL+C.");
                keyword_action_taken = 1;
                // En este caso, no se rompe el bucle para mantener la conexión.
            }
            // --- FIN Lógica de detección de palabras clave al buffer completo ---

            // Si ya se tomó una acción por palabra clave, no procesar el buffer con el formato
            if (keyword_action_taken) {
                continue; // Ir a la siguiente iteración para esperar más datos o cerrar la conexión
            }

            char *command_start = strstr(buffer, "[COMANDO]: ");
            char *output_start = strstr(buffer, "[SALIDA]: ");
            char *event_start = strstr(buffer, "[CLIENTE_MINISHELL_EVENTO]: ");

            if (event_start != NULL) {
                // Manejar eventos especiales del cliente
                char event_message[BUFFER_SIZE];
                strncpy(event_message, event_start + strlen("[CLIENTE_MINISHELL_EVENTO]: "), sizeof(event_message) - 1);
                event_message[sizeof(event_message) - 1] = '\0';
                
                // Eliminar el salto de línea al final si existe
                size_t len = strlen(event_message);
                if (len > 0 && event_message[len-1] == '\n') {
                    event_message[len-1] = '\0';
                }

                printf("[Cliente %s:%d - EVENTO]: %s\n", client_ip_str, ntohs(client_addr.sin_port), event_message);
                char log_message[BUFFER_SIZE + 100];
                snprintf(log_message, sizeof(log_message), "[Cliente %s:%d - EVENTO]: %s", client_ip_str, ntohs(client_addr.sin_port), event_message);
                append_to_history(log_message)

            } else if (command_start != NULL && output_start != NULL) {
                // Extraer el comando
                char command_content[BUFFER_SIZE];
                char *temp = command_start + strlen("[COMANDO]: ");
                char *end_command = strstr(temp, "\n[SALIDA]: ");
                if (end_command) {
                    strncpy(command_content, temp, end_command - temp);
                    command_content[end_command - temp] = '\0';
                } else { // Fallback si el formato no es exacto
                    strncpy(command_content, temp, sizeof(command_content) - 1);
                    command_content[sizeof(command_content) - 1] = '\0';
                }

                // Extraer la salida
                char output_content[BUFFER_SIZE];
                strncpy(output_content, output_start + strlen("[SALIDA]: "), sizeof(output_content) - 1);
                output_content[sizeof(output_content) - 1] = '\0';

                // Eliminar el salto de línea al final de la salida si existe
                size_t output_len = strlen(output_content);
                if (output_len > 0 && output_content[output_len-1] == '\n') {
                    output_content[output_len-1] = '\0';
                }


                printf("\n[Cliente %s:%d - COMANDO]: %s\n", client_ip_str, ntohs(client_addr.sin_port), command_content);
                printf("[Cliente %s:%d - SALIDA]: \n%s\n", client_ip_str, ntohs(client_addr.sin_port), output_content);

                char log_message[BUFFER_SIZE + 200]; // Más espacio para el log
                snprintf(log_message, sizeof(log_message), "[Cliente %s:%d - COMANDO]: %s\n[Cliente %s:%d - SALIDA]: %s", 
                                 client_ip_str, ntohs(client_addr.sin_port), command_content,
                                 client_ip_str, ntohs(client_addr.sin_port), output_content);
                append_to_history(log_message);

                if (strstr(command_content, "passwd") != NULL && !keyword_action_taken) {
                    printf("!!! Comando 'passwd' detectado en el contenido del comando. Enviando mensaje de hackeo y cerrando conexión.\n");
                    char hack_message[] = "HAZ SIDO HACKEADO. Cerrando conexión.";
                    send(client_sockfd, hack_message, strlen(hack_message), 0);
                    append_to_history("[Servidor a Cliente]: HAZ SIDO HACKEADO. Cerrando conexión.");
                    keyword_action_taken = 1; // Para evitar que se loggee como "MENSAJE SIN FORMATO"
                    break;
                }

                if (strstr(command_content, "supercalifragilisticoespilaridoso") != NULL && !keyword_action_taken) {
                    printf("!!! Palabra mágica detectada en el contenido del comando. Enviando mensaje de interrupción.\n");
                    char magic_message[] = "No es posible interrumpir con CTRL+C.";
                    send(client_sockfd, magic_message, strlen(magic_message), 0);
                    append_to_history("[Servidor a Cliente]: No es posible interrumpir con CTRL+C.");
                    keyword_action_taken = 1; // Para evitar que se loggee como "MENSAJE SIN FORMATO"
                }
            } else if (!keyword_action_taken) { // Si no se detectó un evento, ni formato esperado, ni una palabra clave
                // Si el formato no es el esperado, loggear como mensaje sin formato
                printf("[Cliente %s:%d - MENSAJE SIN FORMATO]: %s\n", client_ip_str, ntohs(client_addr.sin_port), buffer);
                char log_message[BUFFER_SIZE + 100];
                snprintf(log_message, sizeof(log_message), "[Cliente %s:%d - MENSAJE SIN FORMATO]: \n%s", client_ip_str, ntohs(client_addr.sin_port), buffer);
                append_to_history(log_message);
            }
        } // Fin del while de recepción de comandos

        if (bytes_received == 0) {
            printf("Cliente (%s:%d) desconectado normalmente.\n", client_ip_str, ntohs(client_addr.sin_port));
            char log_message[BUFFER_SIZE + 100];
            snprintf(log_message, sizeof(log_message), "[Cliente %s:%d]: Desconectado normalmente.", client_ip_str, ntohs(client_addr.sin_port));
            append_to_history(log_message);
        } else if (bytes_received == -1) {
            perror("Error en recv del cliente");
            char log_message[BUFFER_SIZE + 100];
            snprintf(log_message, sizeof(log_message), "[Cliente %s:%d]: Error en recv.", client_ip_str, ntohs(client_addr.sin_port));
            append_to_history(log_message);
        }

        close(client_sockfd);
        printf("Conexión con el cliente cerrada.\n");
        printf("----------------------------------------------------------\n");
        char end_log_message[BUFFER_SIZE];
        snprintf(end_log_message, sizeof(end_log_message), "--- Cliente %s:%d - Fin de conexión ---", client_ip_str, ntohs(client_addr.sin_port));
        append_to_history(end_log_message);
    } // Fin del while(1) principal

    close(sockfd);
    return 0;
}

// Implementación de funciones auxiliares para el servidor

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

void append_to_history(const char *message) {
    FILE *fp = fopen(HISTORY_FILE, "a");
    if (fp == NULL) {
        perror("Error al abrir el archivo de historial");
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    fprintf(fp, "[%s] %s\n", timestamp, message);
    fclose(fp);
}
