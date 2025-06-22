# Glosario de Minishell

Este glosario proporciona una explicación concisa de las funciones de sistema, bibliotecas, y conceptos clave utilizados en la implementación del minishell. Es una referencia rápida para entender la funcionalidad detrás de cada elemento.

---

## Funciones de Sistema (Llamadas al Sistema y Funciones de Biblioteca C)

### `fork()`
* **Definición:** Crea un nuevo proceso duplicando el proceso llamador (el padre).
* **Retorno:**
    * En el proceso padre, devuelve el ID del proceso hijo (PID).
    * En el proceso hijo, devuelve 0.
    * En caso de error, devuelve -1 en el padre y no crea el hijo.
* **Uso en Shell:** Fundamental para crear nuevos procesos para ejecutar comandos externos. El padre puede seguir ejecutando el shell, mientras el hijo ejecuta el comando.

### `execvp(const char *file, char *const argv[])`
* **Definición:** Reemplaza la imagen del proceso actual con la imagen de un nuevo programa. No crea un nuevo proceso; el PID del proceso no cambia. La 'v' indica que toma un array de argumentos (`argv`), y la 'p' indica que busca el ejecutable en el `PATH` del sistema.
* **Argumentos:**
    * `file`: El nombre del ejecutable a cargar.
    * `argv`: Un array de cadenas de caracteres, donde `argv[0]` es el nombre del programa y los siguientes son sus argumentos. El array debe terminar con `NULL`.
* **Retorno:** No retorna en caso de éxito, ya que el proceso actual es reemplazado. En caso de error, devuelve -1.
* **Uso en Shell:** Se utiliza en el proceso hijo después de un `fork()` para ejecutar el comando tecleado por el usuario (ej. `ls`, `grep`, `cat`).

### `pipe(int pipefd[2])`
* **Definición:** Crea una tubería (pipe) unidireccional para comunicación entre procesos.
* **Argumentos:** `pipefd` es un array de dos enteros que se llenará con descriptores de archivo:
    * `pipefd[0]`: Descriptor de archivo para el extremo de lectura de la tubería.
    * `pipefd[1]`: Descriptor de archivo para el extremo de escritura de la tubería.
* **Retorno:** 0 en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Esencial para implementar el operador `|` (tubería). Permite que la salida estándar de un comando se convierta en la entrada estándar del siguiente comando en la cadena de tuberías.

### `dup2(int oldfd, int newfd)`
* **Definición:** Duplica un descriptor de archivo (`oldfd`) a un descriptor de archivo específico (`newfd`). Si `newfd` ya está abierto, se cierra primero.
* **Argumentos:**
    * `oldfd`: El descriptor de archivo existente que se quiere duplicar.
    * `newfd`: El número de descriptor de archivo al que se quiere asignar la duplicación (ej. `STDIN_FILENO`, `STDOUT_FILENO`).
* **Retorno:** El nuevo descriptor de archivo en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Crucial para las redirecciones de I/O y las tuberías:
    * **Redirección de entrada (`<`):** `dup2(fd_archivo, STDIN_FILENO)` para que la entrada del comando venga del archivo.
    * **Redirección de salida (`>` o `>>`):** `dup2(fd_archivo, STDOUT_FILENO)` para que la salida del comando vaya al archivo.
    * **Tuberías (`|`):**
        * `dup2(pipefd[1], STDOUT_FILENO)` en el proceso que escribe a la tubería.
        * `dup2(pipefd[0], STDIN_FILENO)` en el proceso que lee de la tubería.

### `waitpid(pid_t pid, int *status, int options)`
* **Definición:** Espera un cambio de estado en un proceso hijo específico (o cualquier hijo).
* **Argumentos:**
    * `pid`: El PID del hijo a esperar, o `-1` para cualquier hijo, `0` para cualquier hijo en el mismo grupo de procesos, etc.
    * `status`: Un puntero a un entero donde se almacenará el estado de salida del hijo. Se deben usar macros como `WIFEXITED()`, `WEXITSTATUS()`, etc., para interpretar este valor.
    * `options`: Opciones adicionales, como `WNOHANG` (no bloqueante), `WUNTRACED` (reportar hijos detenidos), `WCONTINUED` (reportar hijos reanudados).
* **Retorno:** El PID del hijo cuyo estado cambió, `0` si `WNOHANG` y ningún hijo ha cambiado de estado, o `-1` en caso de error.
* **Uso en Shell:**
    * En el proceso padre, para esperar a que los comandos en primer plano terminen (`waitpid(pid_hijo, &status, 0)`).
    * En el manejador de `SIGCHLD`, con `WNOHANG`, para recolectar procesos "zombie" en segundo plano de forma no bloqueante.

### `signal(int signum, sighandler_t handler)`
* **Definición:** Establece cómo se maneja una señal específica (`signum`).
* **Argumentos:**
    * `signum`: El número de la señal (ej. `SIGINT`, `SIGCHLD`).
    * `handler`: La función manejadora de señales (un puntero a una función que toma un `int` y devuelve `void`), o `SIG_IGN` (ignorar la señal), o `SIG_DFL` (comportamiento por defecto).
* **Retorno:** El manejador anterior de la señal en caso de éxito, `SIG_ERR` en caso de error.
* **Uso en Shell:** Utilizado para configurar el comportamiento del shell ante señales como `Ctrl+C` (`SIGINT`), `Ctrl+\` (`SIGQUIT`), `Ctrl+Z` (`SIGTSTP`), y para manejar la terminación de procesos hijos (`SIGCHLD`).
* **Nota:** `sigaction()` es una alternativa más robusta y preferida para manejo de señales complejas, ya que ofrece más control (ej. bloqueo de señales durante el manejador, `SA_RESTART`).

### `sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)`
* **Definición:** Inspecciona y/o cambia la acción asociada a una señal específica. Es la forma preferida y más potente de manejar señales en POSIX.
* **Argumentos:**
    * `signum`: El número de la señal.
    * `act`: Puntero a una estructura `sigaction` que describe la nueva acción para la señal.
    * `oldact`: (Opcional) Puntero a una estructura `sigaction` donde se guarda la acción anterior de la señal.
* **`struct sigaction`:** Contiene el puntero a la función manejadora (`sa_handler` o `sa_sigaction`), un conjunto de señales a bloquear durante la ejecución del manejador (`sa_mask`), y varias banderas (`sa_flags`) que modifican el comportamiento (ej. `SA_RESTART`, `SA_NOCLDSTOP`).
* **Retorno:** 0 en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Se utiliza en `configurar_senales_padre()` para establecer el manejador de `SIGCHLD` de forma robusta, permitiendo el uso de banderas como `SA_RESTART` para evitar que las llamadas al sistema se interrumpan por la señal.

### `kill(pid_t pid, int sig)`
* **Definición:** Envía una señal a un proceso o grupo de procesos.
* **Argumentos:**
    * `pid`: El ID del proceso o grupo de procesos al que se enviará la señal.
    * `sig`: El número de la señal a enviar (ej. `SIGINT`, `SIGKILL`).
* **Retorno:** 0 en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Utilizado en el manejador de `SIGINT`/`SIGQUIT` para reenviar la señal al proceso hijo en primer plano, permitiendo que el comando se termine correctamente.

### `chdir(const char *path)`
* **Definición:** Cambia el directorio de trabajo actual del proceso.
* **Argumentos:** `path`: La ruta del nuevo directorio de trabajo.
* **Retorno:** 0 en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Implementa el comando interno `cd`.

### `getcwd(char *buf, size_t size)`
* **Definición:** Obtiene la ruta absoluta del directorio de trabajo actual.
* **Argumentos:**
    * `buf`: Un buffer donde se almacenará la ruta.
    * `size`: El tamaño del buffer.
* **Retorno:** Un puntero a `buf` en caso de éxito, `NULL` en caso de error.
* **Uso en Shell:** Se utiliza en `generar_prompt()` para mostrar la ruta actual en el prompt del shell.

### `gethostname(char *name, size_t len)`
* **Definición:** Obtiene el nombre del host del sistema.
* **Argumentos:**
    * `name`: Un buffer donde se almacenará el nombre del host.
    * `len`: El tamaño del buffer.
* **Retorno:** 0 en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Se utiliza en `generar_prompt()` para mostrar el nombre del host en el prompt.

### `getpwuid(uid_t uid)`
* **Definición:** Busca una entrada en la base de datos de usuarios (ej. `/etc/passwd`) para un ID de usuario (UID) dado.
* **Argumentos:** `uid`: El ID de usuario.
* **Retorno:** Un puntero a una estructura `passwd` (que contiene información del usuario como nombre de usuario y directorio de casa) en caso de éxito, `NULL` si no se encuentra o hay un error.
* **Uso en Shell:** Se utiliza en `generar_prompt()` para obtener el nombre de usuario y el directorio de casa, que se usan para construir el prompt y para la funcionalidad de `cd ~`.

### `geteuid()`
* **Definición:** Devuelve el ID de usuario efectivo del proceso invocador.
* **Retorno:** El ID de usuario efectivo.
* **Uso en Shell:** Se utiliza con `getpwuid()` para obtener la información del usuario actual.

### `snprintf(char *str, size_t size, const char *format, ...)`
* **Definición:** Escribe una cadena formateada en un buffer, de forma segura, evitando desbordamientos de buffer. Es similar a `sprintf`, pero con una protección adicional.
* **Argumentos:**
    * `str`: El buffer de destino donde se escribirá la cadena resultante.
    * `size`: El tamaño máximo del buffer `str`, incluyendo el carácter nulo de terminación (`\0`). `snprintf` garantiza que no escribirá más de `size-1` caracteres en `str` antes de añadir el `\0`.
    * `format`: La cadena de formato, similar a `printf`, que especifica cómo se formatearán los argumentos adicionales.
    * `...`: Argumentos adicionales que se formatearán según la cadena `format`.
* **Retorno:** El número de caracteres que *habrían sido escritos* si el buffer fuera lo suficientemente grande (excluyendo el `\0`). Si el valor de retorno es mayor o igual a `size`, significa que el buffer no era lo suficientemente grande. En caso de error, devuelve un valor negativo.
* **Uso en Shell:** Se utiliza en la función `generar_prompt()` para construir la cadena del prompt (`usuario@host:ruta$ `) de manera segura, asegurando que el prompt no exceda el tamaño del buffer asignado, lo que previene vulnerabilidades de seguridad y fallos del programa por desbordamiento de buffer.

### `open(const char *pathname, int flags, mode_t mode)`
* **Definición:** Abre y posiblemente crea un archivo o dispositivo.
* **Argumentos:**
    * `pathname`: La ruta del archivo.
    * `flags`: Opciones para abrir el archivo (ej. `O_RDONLY` para lectura, `O_WRONLY` para escritura, `O_CREAT` para crear si no existe, `O_APPEND` para añadir, `O_TRUNC` para truncar/borrar contenido existente).
    * `mode`: Permisos del archivo si se crea (`0644` es común para rw-r--r--).
* **Retorno:** El descriptor de archivo en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Utilizado para implementar las redirecciones de entrada (`<`) y salida (`>`, `>>`) a archivos.

### `close(int fd)`
* **Definición:** Cierra un descriptor de archivo, liberando el recurso asociado.
* **Argumentos:** `fd`: El descriptor de archivo a cerrar.
* **Retorno:** 0 en caso de éxito, -1 en caso de error.
* **Uso en Shell:** Se utiliza para cerrar los descriptores de archivo de las tuberías y los archivos de redirección después de que ya no son necesarios, evitando fugas de descriptores de archivo.

### `readline(const char *prompt)` (de `libreadline`)
* **Definición:** Lee una línea de la entrada estándar, proporcionando funcionalidades avanzadas como edición de línea (usando teclas de flecha, etc.) e historial de comandos.
* **Argumentos:** `prompt`: La cadena de caracteres que se muestra como prompt al usuario.
* **Retorno:** Un puntero a una cadena de caracteres que contiene la línea leída. Debe ser liberada con `free()` por el llamador cuando ya no se necesite. Devuelve `NULL` en caso de EOF (ej. Ctrl+D).
* **Uso en Shell:** La función principal para obtener la entrada del usuario en el shell.

### `add_history(const char *string)` (de `libhistory`)
* **Definición:** Añade una línea de comando al historial de readline.
* **Argumentos:** `string`: La cadena de caracteres a añadir al historial.
* **Retorno:** `void`.
* **Uso en Shell:** Permite que los comandos ingresados por el usuario sean accesibles a través de las teclas de flecha hacia arriba/abajo.

### `perror(const char *s)`
* **Definición:** Imprime un mensaje de error descriptivo en la salida de error estándar (`stderr`), prefiriendo el mensaje proporcionado por el usuario y concatenando el mensaje de error del sistema (`strerror(errno)`).
* **Argumentos:** `s`: Una cadena de caracteres que se imprimirá antes del mensaje de error del sistema.
* **Retorno:** `void`.
* **Uso en Shell:** Utilizado en la función `imprimir_error()` para reportar errores del sistema de forma estandarizada y legible.

### `strtok_r(char *str, const char *delim, char **saveptr)`
* **Definición:** Una versión reentrante y segura para hilos de `strtok()`. Divide una cadena en tokens basándose en un delimitador.
* **Argumentos:**
    * `str`: La cadena a tokenizar (la primera vez; `NULL` para llamadas subsecuentes).
    * `delim`: La cadena de delimitadores.
    * `saveptr`: Un puntero a un `char*` que `strtok_r` utiliza internamente para mantener su estado.
* **Retorno:** Un puntero al siguiente token, o `NULL` si no hay más tokens.
* **Uso en Shell:** Se utiliza en `dividir_cadena()` y `parsear_argumentos_comando()` para dividir la línea de entrada por '|' o por espacios y operadores de redirección. La versión `_r` es importante para la seguridad en entornos donde se puedan manejar múltiples hilos (aunque en este shell es menos crítico, es una buena práctica).

---

## Conceptos y Operadores del Shell

### `&` (Proceso en Segundo Plano / Background)
* **Definición:** Un operador que, cuando se coloca al final de un comando o una tubería de comandos, le indica al shell que ejecute ese(s) comando(s) en **segundo plano**. Esto significa que el shell no espera a que el comando termine; en su lugar, regresa inmediatamente con un nuevo prompt, permitiendo al usuario ingresar otro comando.
* **Comportamiento de Señales:** Los procesos en segundo plano generalmente ignoran `SIGINT` (Ctrl+C) y `SIGQUIT` (Ctrl+\) por defecto, para no ser terminados por estas señales destinadas a los procesos en primer plano.
* **Uso en Shell:** Permite la multitarea simple, ejecutando tareas largas sin bloquear la interactividad del shell.

### `&&` (Operador Lógico AND / Y Condicional)
* **Definición:** Un operador de control de flujo que ejecuta el siguiente comando solo si el comando anterior finalizó con un **código de salida de éxito (0)**. Si el comando anterior falla (código de salida distinto de 0), el comando o los comandos posteriores en la misma línea después del `&&` no se ejecutarán.
* **Códigos de Salida:**
    * `0`: Indica éxito.
    * Cualquier valor distinto de `0`: Indica un error o fallo.
* **Uso en Shell:** Permite crear secuencias de comandos donde la ejecución de pasos posteriores depende del éxito de los pasos anteriores. Por ejemplo, `compilar && ejecutar` (ejecuta el programa solo si la compilación fue exitosa).

### `|` (Tubería / Pipe)
* **Definición:** Un operador que toma la **salida estándar** del comando a su izquierda y la redirige como la **entrada estándar** del comando a su derecha. Esto permite encadenar comandos, donde la salida de uno es procesada por el siguiente.
* **Uso en Shell:** Permite la composición de programas, donde utilidades simples se combinan para realizar tareas más complejas (ej. `ls -l | grep .txt | wc -l`).

### `<` (Redirección de Entrada / Input Redirection)
* **Definición:** Redirige la **entrada estándar** de un comando para que provenga de un archivo en lugar del teclado.
* **Sintaxis:** `comando < archivo_entrada`
* **Uso en Shell:** Útil cuando un comando espera entrada de un archivo en lugar de que el usuario la teclee.

### `>` (Redirección de Salida / Output Redirection - Truncar/Crear)
* **Definición:** Redirige la **salida estándar** de un comando a un archivo. Si el archivo no existe, lo crea. Si ya existe, su contenido es **truncado (borrado)** antes de que se escriba la nueva salida.
* **Sintaxis:** `comando > archivo_salida`
* **Uso en Shell:** Almacenar la salida de un comando en un archivo nuevo o sobrescribir uno existente.

### `>>` (Redirección de Salida / Output Redirection - Anexar)
* **Definición:** Redirige la **salida estándar** de un comando a un archivo. Si el archivo no existe, lo crea. Si ya existe, la nueva salida es **añadida (anexada)** al final del contenido existente del archivo.
* **Sintaxis:** `comando >> archivo_salida`
* **Uso en Shell:** Añadir la salida de un comando a un archivo existente sin borrar su contenido previo, útil para logs o acumulación de datos.

### PID (Process ID)
* **Definición:** Un número único que el sistema operativo asigna a cada proceso en ejecución.
* **Uso en Shell:** Utilizado por el shell para identificar y controlar sus procesos hijos (ej. con `waitpid`, `kill`).

### TTY (Teletypewriter) / Terminal
* **Definición:** Históricamente, un dispositivo de comunicación para interactuar con una computadora. En la actualidad, se refiere al dispositivo de terminal virtual o emulador de terminal a través del cual el usuario interactúa con el shell (teclado como entrada, pantalla como salida).
* **Contexto en Shell:** El shell interactúa directamente con el TTY para leer comandos y mostrar la salida. El manejo de señales es crucial para la interacción entre el shell y el TTY.

### Foreground (Primer Plano) vs. Background (Segundo Plano)
* **Foreground:** Un proceso o grupo de procesos que está directamente asociado con la terminal de control. Recibe la entrada del teclado y envía su salida a la terminal. Solo puede haber un proceso o grupo de procesos en primer plano en una terminal a la vez. Responde directamente a señales de la terminal (ej. Ctrl+C, Ctrl+Z).
* **Background:** Un proceso o grupo de procesos que se ejecuta independientemente de la terminal de control. No recibe la entrada del teclado y su salida puede ser redirigida o aparecer en la terminal, pero no interrumpe la interacción del usuario con el shell. Se lanza con `&`.
* **Uso en Shell:** El shell principal es un proceso en primer plano, y lanza otros comandos en primer o segundo plano según la directiva del usuario. El shell debe gestionar qué proceso está en primer plano para un correcto manejo de señales.

### Zombie Process (Proceso Zombie)
* **Definición:** Un proceso hijo que ha terminado su ejecución, pero su entrada en la tabla de procesos del sistema aún existe porque el proceso padre no ha llamado a `wait()` o `waitpid()` para recolectar su estado de salida. Ocupan recursos mínimos (solo la entrada en la tabla de procesos) pero no liberan completamente el PID.
* **Uso en Shell:** El manejador de `SIGCHLD` (`manejador_sigchld`) en el shell padre es fundamental para "recolectar" estos procesos y evitar la acumulación de zombies, llamando a `waitpid` con `WNOHANG`.
