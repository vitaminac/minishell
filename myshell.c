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

char * strdup(const char * str);

/*#define DEBUG*/
#define BUFFER_SIZE 4096
#define ERR_FILE(FILE) "fichero %s: Error %s. Descripcion del error\n", FILE, strerror(errno)
#define ERR_COMMAND "mandato: No se encuentra el mandato %s %s\n"
#define JOBINFO "[%d]+ Running \t %s &\n"
#define DEFAULT_FILE_CREATE_MODE 0666

/* almacenamos las informaciones asociados a los procesos creado por sistema  */
typedef struct JobInfo {
	pid_t pgid;
	char * command;
	struct JobInfo * next;
} JobInfo;

/* variables globales*/
pid_t shell_pgid;
JobInfo * job_list = NULL;
pid_t last_bg_job;

/* esperar al proceso pid y imprimir command de debug */
pid_t debug_wait(pid_t pid, int options) {
	pid_t result;
	int status;
	options |= WUNTRACED;
#ifdef DEBUG
	fprintf(stdout, "waiting pid %d\n", pid);
#endif
	while (true) {
		result = waitpid(pid, &status, options);
		if (result == 0) {
#ifdef DEBUG
			fprintf(stdout, "child process %d's state haven't changed yet\n", pid);
#endif
			return result;
		}
		else {
			if (result > 0) {
				if (WIFEXITED(status)) {
#ifdef DEBUG
					fprintf(stdout, "task %d exited with status code %d\n", result, WEXITSTATUS(status));
#endif
				}
				else if (WIFSIGNALED(status)) {
#ifdef DEBUG
					fprintf(stdout, "task %d was terminated by signal %d \n", result, WTERMSIG(status));
#endif
				}
				else if (WIFSTOPPED(status)) {
#ifdef DEBUG
					fprintf(stdout, "task %d was stopped by signal %d\n", result, WSTOPSIG(status));
#endif
				}
			}
			else {
#ifdef DEBUG
				fprintf(stderr, "wait pid %d failed\n", pid);
				if (errno == ECHILD) {
					fprintf(stderr, "Child does not exist: %s\n", strerror(errno));
				}
				else if (errno == EINVAL) {
					fprintf(stderr, "Bad argument passed to waitpid: %s\n", strerror(errno));
				}
				else if (errno == ECHILD) {
					fprintf(stderr, "child process %d doesn't exist: %s\n", pid, strerror(errno));
				}
				else {
					fprintf(stderr, "Unknown error\n");
				}
#endif 
				return result;
			}
		}
#ifdef DEBUG
		fprintf(stdout, "waiting pid %d again\n", pid);
#endif
	}
}

#pragma region Job
void insert_job(JobInfo ** job_list_ptr, JobInfo * new_job) {
	int i = 1;
	JobInfo * current = *job_list_ptr;
	if (current == NULL) {
		*job_list_ptr = new_job;
		current = new_job;
	}
	else {
		current = *job_list_ptr;
		i += 1;
		while (current->next != NULL) {
			current = current->next;
			i += 1;
		}
		current->next = new_job;
	}
	printf(JOBINFO, i, current->command);
}

JobInfo * new_job(pid_t pid, char * command) {
	JobInfo * job = (JobInfo *)malloc(sizeof(JobInfo *));
	job->pgid = pid;
	job->command = command;
	job->next = NULL;
	return job;
}

void bg(pid_t pid, JobInfo * job_list) {
	while (job_list != NULL) {
		if (job_list->pgid == pid) {
			debug_wait(pid, 0);
			return;
		}
		job_list = job_list->next;
	}
}

void fg(int id, JobInfo ** job_list_ptr) {
	int i = 1;
	pid_t pgid;
	JobInfo * current;
	if (job_list_ptr != NULL) {
		current = *job_list_ptr;
		/* comprueba que esta ejecutando en backgroud */
		while (i < id && current != NULL) {
			current = current->next;
			i++;
		}
		if (i == id && current != NULL) {
			pgid = current->pgid;
			free(current->command);
			free(current);
			tcsetpgrp(STDIN_FILENO, pgid);
			debug_wait(pgid, 0);
			tcsetpgrp(STDIN_FILENO, shell_pgid);
		}
	}
}

/* comprobar que si hay tarea terminada en el segundo plano y muestra los que no han terminado*/
void jobs(JobInfo ** job_list_ptr) {
	int i = 0;
	JobInfo * current = NULL;
	JobInfo * next = NULL;
	if (job_list_ptr != NULL) {
		while (*job_list_ptr != NULL && current == NULL) {
			if (debug_wait((*job_list_ptr)->pgid, WNOHANG) != 0) {
				current = (*job_list_ptr)->next;
				free((*job_list_ptr)->command);
				free(*job_list_ptr);
				*job_list_ptr = current;
				current = NULL;
			}
			else {
				current = *job_list_ptr;
			}
		}
		if (*job_list_ptr != NULL) {
			current = *job_list_ptr;
			while (current != NULL) {
				next = current->next;
				if (debug_wait(current->pgid, WNOHANG) != 0) {
					free((current)->command);
					free(current);
				}
				else {
					i += 1;
					printf(JOBINFO, i, current->command);
				}
				current = next;
			}
		}
	}
}
#pragma endregion

/* Mostrar en pantalla un prompt (los símbolos msh> seguidos de un espacio). */
void prompt() {
#ifdef DEBUG
	static char cwd[BUFFER_SIZE];
	printf("msh:>%s$", getcwd(cwd, BUFFER_SIZE));
#else
	printf("msh:>");
#endif
}

#pragma region Execute
/* Ejecuta el commando por separado y redirecciona la entrada y salida si es necesario */
void execute(const tcommand * command, int pgid,
	int input, int output, int error,
	int foreground) {
	pid_t pid = getpid();
	if (pgid == 0) pgid = pid;
	setpgid(pid, pgid);
	if (foreground) {
#ifdef DEBUG
		fprintf(stdout, "run %d in foreground\n", pid);
#endif
		tcsetpgrp(STDIN_FILENO, pgid);
	}

	/* recuperar los signal por defector  */
	if (signal(SIGINT, SIG_DFL) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "recuperar signal SIGINT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGQUIT, SIG_DFL) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "recuperar signal SIGQUIT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTSTP, SIG_DFL) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "recuperar signal SIGTSTP failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "recuperar signal SIGCHLD failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTTIN, SIG_DFL) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "recuperar signal SIGTTIN failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTTOU, SIG_DFL) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "recuperar signal SIGTTOU failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	/*if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "recuperar signal SIGCHILD handler failed\n");
#endif
		exit(EXIT_FAILURE);
	}*/

	/* connectar a los pipes */
	if (input != STDIN_FILENO) {
		dup2(input, STDIN_FILENO);
		close(input);
	}
	if (output != STDOUT_FILENO) {
		dup2(output, STDOUT_FILENO);
		close(output);
	}
	if (error != STDERR_FILENO) {
		dup2(error, STDERR_FILENO);
		close(error);
	}

	execvp(command->argv[0], command->argv);
	/* Si alguno de los mandatos a ejecutar no existe,
	   el programa debe mostrar el error
	   “mandato: No se encuentra el mandato”.
	*/
	fprintf(stdout, ERR_COMMAND, command->argv[0], strerror(errno));
	/* para que en el caso de error no vuelva al programa principal */
	exit(EXIT_FAILURE);
}

/* ejecutar mandato interno de shell */
bool inlinecommand(tline * line) {
	if (line->ncommands == 1) {
		if (strcmp("cd", line->commands[0].argv[0]) == 0) {
			if (line->commands->argc > 1) {
				chdir(line->commands[0].argv[1]);
				return true;
			}
			else {
				chdir(getenv("HOME"));
				return true;
			}
		}
		else if (strcmp("exit", line->commands[0].argv[0]) == 0) {
			exit(EXIT_SUCCESS);
		}
		else if (strcmp("jobs", line->commands[0].argv[0]) == 0) {
#ifdef DEBUG
			fprintf(stdout, "list the jobs\n");
#endif
			jobs(&job_list);
			return true;
		}
		else if (strcmp("fg", line->commands[0].argv[0]) == 0) {
			if (line->commands->argc > 1) {
				fg(atoi(line->commands[0].argv[1]), &job_list);
			}
			else {
				fg(last_bg_job, &job_list);
			}
			return true;
		}
	}
	return false;
}

/* Ejecutar todos los mandatos de la línea a la vez creando varios procesos hijo
   y comunicando unos con otros con las tuberías que sean necesarias,
   y realizando las redirecciones que sean necesarias.
   En caso de que no se ejecute en background,
   se espera a que todos los mandatos hayan finalizado
   para volver a mostrar el prompt y repetir el proceso. */
void execline(tline * line, const char * command) {
	static pid_t current;

	int i;
	int input, output, error;
	int pipeline[2];
	int pgid = 0;
	/* comprobar el argumentos */
	if (line != NULL) {
		if (inlinecommand(line)) {
			return;
		}
		if (line->ncommands > 0) {
			if (line->redirect_input == NULL) {
				input = STDIN_FILENO;
			}
			else {
				input = open(line->redirect_input, O_RDONLY);
				if (input < 0) {
#ifdef DEBUG
					fprintf(stderr, "fallo en redireccionar la entrada %s\n", strerror(errno));
#endif
					exit(EXIT_FAILURE);
				}
			}
			if (line->redirect_error == NULL) {
				error = STDERR_FILENO;
			}
			else {
				error = open(line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC);
				if (error < 0) {
#ifdef DEBUG
					fprintf(stderr, "fallo en redireccionar la salida de error %s\n", strerror(errno));
#endif
					exit(EXIT_FAILURE);
				}
			}
			for (i = 0; i < line->ncommands; i++) {
				if (i < line->ncommands - 1) {
					if (pipe(pipeline) < 0) {
#ifdef DEBUG
						fprintf(stderr, "fallo en establecer pipeline %s\n", strerror(errno));
#endif
						exit(EXIT_FAILURE);
					}
					output = pipeline[1];
				}
				else {
					if (line->redirect_output == NULL) {
						output = STDOUT_FILENO;
					}
					else {
						output = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, DEFAULT_FILE_CREATE_MODE);
						if (output < 0) {
#ifdef DEBUG
							fprintf(stderr, "fallo en redireccionar la salida de standard %s\n", strerror(errno));
#endif
							exit(EXIT_FAILURE);
						}
					}
				}
				current = fork();
				if (current == 0) {
					if (i < line->ncommands - 1) {
						close(pipeline[0]);
					}
					execute(&(line->commands[i]), pgid, input, output, error, !line->background);
				}
				else if (current > 0) {
					if (pgid == 0) pgid = current;
					setpgid(current, pgid);
				}
#ifdef DEBUG
				else {
					fprintf(stderr, "fallo al ejecutar el comando\n");
				}
#endif
				if (input != STDIN_FILENO) {
					close(input);
				}
				if (output != STDOUT_FILENO) {
					close(output);
				}
				input = pipeline[0];
			}

			if (error != STDERR_FILENO) {
				close(error);
			}

			if (line->background) {
#ifdef DEBUG
				fprintf(stdout, "Ejecutamos la tarea %d en el segundo plano\n", current);
#endif 
				insert_job(&job_list, new_job(current, strdup(command)));
			}
			else {
				/* esperar a que termine */
				debug_wait(-pgid, 0);
				/* vuelve a capturar el control de terminal*/
				tcsetpgrp(STDIN_FILENO, shell_pgid);
			}
			current = 0;
		}
	}
}
#pragma endregion

#pragma region initialization and deinitialization
/* initialization, tarea de preparacion */
void init() {
	if (signal(SIGINT, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGINT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGQUIT, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGQUIT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTSTP, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGTSTP failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTTIN, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGTTIN failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTTOU, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGTTOU failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGCHILD handler failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	shell_pgid = getpid();
	if (setpgid(shell_pgid, shell_pgid) < 0) {
#ifdef DEBUG
		fprintf(stderr, "no podemos crear nuestro propio grupo\n");
#endif
		exit(EXIT_FAILURE);
	}
#ifdef DEBUG
	fprintf(stdout, "capturamos el control de terminal\n");
#endif
	tcsetpgrp(STDIN_FILENO, shell_pgid);
}

/* deinitialization Liberar la memoria de los contenidos de jobs */
void destroy() {
	JobInfo * next;
	while (job_list != NULL) {
		next = job_list->next;
#ifdef DEBUG
		fprintf(stdout, "liberando memoria asosiada a %d\n", job_list->pgid);
#endif
		if (job_list->command != NULL) {
			free(job_list->command);
		}
		free(job_list);
		job_list = next;
	}

}
#pragma endregion

int main(int argc, char * argv[]) {
	/* echo "0" | sudo tee /proc/sys/kernel/yama/ptrace_scope > /dev/null */
	char buf[BUFFER_SIZE];
	tline * line;

	init();

	prompt();
	do {
		if (fgets(buf, BUFFER_SIZE, stdin) != NULL) {
			/* Leer una linea del taclado */
			line = tokenize(buf);
			if (line == NULL) {
				continue;
			}

#ifdef DEBUG
			if (line->redirect_input != NULL) {
				printf("redirección de entrada: %s\n", line->redirect_input);
			}
			if (line->redirect_output != NULL) {
				printf("redirección de salida: %s\n", line->redirect_output);
			}
			if (line->redirect_error != NULL) {
				printf("redirección de error: %s\n", line->redirect_error);
			}
			if (line->background) {
				printf("comando a ejecutarse en background\n");
			}
			for (i = 0; i < line->ncommands; i++) {
				printf("orden %d (%s):\n", i, line->commands[i].filename);
				for (j = 0; j < line->commands[i].argc; j++) {
					printf("  argumento %d: %s\n", j, line->commands[i].argv[j]);
				}
			}
#endif
			/* hacer algo con la linea de command parseado */
			execline(line, buf);
			prompt();
		}
	} while (true);

	/* liberar los recursos */
	destroy();
	return 0;
}
