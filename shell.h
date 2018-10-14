#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>

#include "parser.h"

#define MAX_BGTASK 20
#define FD_STDIN 0
#define FD_STDOUT 1
#define FD_STDERR 2

// Mostrar en pantalla un prompt (los símbolos msh> seguidos de un espacio).
void prompt();

/* Ejecutar todos los mandatos de la línea a la vez creando varios procesos hijo 
   y comunicando unos con otros con las tuberías que sean necesarias, 
   y realizando las redirecciones que sean necesarias. 
   En caso de que no se ejecute en background, 
   se espera a que todos los mandatos hayan finalizado 
   para volver a mostrar el prompt y repetir el proceso. */
int execline(tline * line);