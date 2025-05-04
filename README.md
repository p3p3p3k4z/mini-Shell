# mini-shell


### arquitectura
```bash
mini_shell/
├── include/
│   ├── parser.h            # Header del parser (Integrante 1)
│   ├── process.h           # Header del manejador de procesos (TÚ - Integrante 2)
│   └── utils.h             # Utilidades comunes (opcional)
│
├── src/
│   ├── main.c              # Punto de entrada, se encarga del flujo general
│   ├── parser.c            # Parser del input del usuario (Integrante 1)
│   ├── process.c           # Código del ejecutor de comandos (TÚ)
│   └── utils.c             # Funciones auxiliares (limpieza, debug, etc.)
│
├── Makefile                # Compilación modular del proyecto
└── README.md               # Documentación del proyecto
``` 

### como ejecutar
```bash
m4r10@debian:~/Documentos/so/mini_shell$ make
gcc -Wall -Iinclude -c -o src/main.o src/main.c
gcc src/main.o src/process.o -o bin/shell
m4r10@debian:~/Documentos/so/mini_shell$ ./bin/shell
mini-shell> ls
bin  include  Makefile	src
mini-shell> ls -l | grep ".c" | sort | head -n 5
total 16
drwxr-xr-x 2 m4r10 m4r10 4096 may  3 16:57 bin
drwxr-xr-x 2 m4r10 m4r10 4096 may  3 16:50 include
-rw-r--r-- 1 m4r10 m4r10  302 may  3 16:54 Makefile
drwxr-xr-x 2 m4r10 m4r10 4096 may  3 16:57 src
mini-shell> ^C
```

### ejemplos
```bash
ls -l | grep ".c" | sort | head -n 5
echo "hola mundo"
ps aux | grep bash
```
