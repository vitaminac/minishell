#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "parser.h"

#define DEBUG
#define BUFFER_SIZE 4096
#define ERR_FILE(FILE) "fichero %s: Error %s. Descripcion del error\n", FILE, strerror(errno)
#define ERR_COMMAND "mandato: No se encuentra el mandato %s %s\n"
#define JOBINFO "[%d]+ Running \t %s &\n"
#define DEFAULT_FILE_CREATE_MODE 0666

typedef struct JobInfo {
	pid_t pgid;
	char * info;
	struct JobInfo * next;
} JobInfo;

/* Mostrar en pantalla un prompt (los símbolos msh> seguidos de un espacio). */
void prompt();

/* Ejecutar todos los mandatos de la línea a la vez creando varios procesos hijo
   y comunicando unos con otros con las tuberías que sean necesarias,
   y realizando las redirecciones que sean necesarias.
   En caso de que no se ejecute en background,
   se espera a que todos los mandatos hayan finalizado
   para volver a mostrar el prompt y repetir el proceso. */
void execline(tline * line);

/* initialization */
void init();

/* deinitialization */
void destroy();