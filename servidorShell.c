#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_PORT 8080 // Usa el mismo puerto que en el cliente
#define MAX_CONNECTIONS 5
#define BUFFER_SIZE 4096

void get_os_name(char *os_name, size_t size);

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Error al crear el socket");
        return 1;
    }

    // Configurar la opción SO_REUSEADDR para reusar el puerto inmediatamente
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
    addr.sin_addr.s_addr = INADDR_ANY; // Escuchar en todas las interfaces

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
        printf("Conexión aceptada desde %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        char client_os[256] = "Desconocido";
        char buffer[BUFFER_SIZE];
        ssize_t bytes_received, bytes_sent;
        char response[BUFFER_SIZE];

        // Recibir el saludo del cliente (que contiene su nombre)
        bytes_received = recv(client_sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            if (strncmp(buffer, "HOLA_CLIENTE:", 13) == 0) {
                strncpy(client_os, buffer + 13, sizeof(client_os) - 1);
                client_os[sizeof(client_os) - 1] = '\0';
                printf("El cliente está corriendo en: %s\n", client_os);

                // Enviar el nombre del servidor como saludo
                char server_hello[BUFFER_SIZE];
                snprintf(server_hello, sizeof(server_hello), "HOLA_SERVIDOR:%s", server_os);
                bytes_sent = send(client_sockfd, server_hello, strlen(server_hello), 0);
                if (bytes_sent == -1) {
                    perror("Error al enviar el saludo del servidor");
                    close(client_sockfd);
                    continue;
                }
            } else {
                printf("Recibido del cliente (saludo inesperado): %s\n", buffer);
                // Si no es el saludo esperado, aún así enviamos nuestro saludo para continuar
                char server_hello[BUFFER_SIZE];
                snprintf(server_hello, sizeof(server_hello), "HOLA_SERVIDOR:%s", server_os);
                send(client_sockfd, server_hello, strlen(server_hello), 0);
            }
        } else if (bytes_received == 0) {
            printf("Cliente desconectado al recibir saludo.\n");
            close(client_sockfd);
            continue;
        } else {
            perror("Error al recibir el saludo del cliente");
            close(client_sockfd);
            continue;
        }

        printf("--- Comienza la observación del shell del cliente (%s) ---\n", client_os);
        // Bucle principal de recepción de datos del cliente (minishell)
        while ((bytes_received = recv(client_sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_received] = '\0';
            // Aquí el servidor simplemente imprime todo lo que recibe del minishell.
            // Si el minishell envía el prompt, la salida de comandos, etc., el servidor lo mostrará.
            printf("[Cliente %s]: %s", client_os, buffer); // Imprimir el mensaje del cliente

            // Si el minishell envió una señal de QUIT, el servidor puede responder o solo registrarlo
            if (strcmp(buffer, "MINISHELL_QUIT") == 0 || strcmp(buffer, "MINISHELL_EOF") == 0) {
                printf("El cliente (%s) ha solicitado desconectarse o ha terminado (EOF).\n", client_os);
                break; // Salir del bucle de comunicación con este cliente
            }

            // Aquí el servidor podría procesar los comandos si fueran mensajes estructurados,
            // pero para un "observador", simplemente mostrarlos es suficiente.
            // No hay necesidad de enviar una respuesta a cada mensaje de salida del shell.
            // Si el cliente necesita una respuesta, es un protocolo más complejo.

            // Ejemplo para una respuesta simple si el cliente espera algo:
            // strcpy(response, "ACK"); // Podrías enviar un ACK o un mensaje simple
            // send(client_sockfd, response, strlen(response), 0);
        }

        if (bytes_received == 0) {
            printf("Cliente (%s) desconectado.\n", client_os);
        } else if (bytes_received == -1) {
            perror("Error en recv del cliente");
        }

        close(client_sockfd);
        printf("Conexión con el cliente cerrada.\n");
        printf("----------------------------------------------------------\n");
    }

    close(sockfd);
    return 0;
}

// Función para obtener el nombre de la distribución (igual que en el cliente)
void get_os_name(char *os_name, size_t size) {
    FILE *fp;
    char buffer[256];
    os_name[0] = '\0'; // Inicializar la cadena

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
