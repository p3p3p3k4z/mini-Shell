#ifndef PROCESS_H
#define PROCESS_H

/**
 * @brief Ejecuta una tubería de comandos.
 *
 * @param commands Un arreglo de arreglos de cadenas con los comandos y argumentos.
 * @param num_commands El número de comandos en la tubería.
 */
void execute_pipeline(char*** commands, int num_commands);

#endif // PROCESS_H
