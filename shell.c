#include "shell.h"

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
			return result;
#endif
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
#ifdef DEBUG
			else {
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
				return result;
			}
#endif 
		}
#ifdef DEBUG
		fprintf(stdout, "waiting pid %d again\n", pid);
#endif
	}
}

#pragma region Job
JobInfo * job_list = NULL;
pid_t last_bg_job;
void insert_job(JobInfo ** job_list_ptr, JobInfo * new_job) {
	int i = 1;
	JobInfo * current = *job_list_ptr;
	if (current== NULL) {
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
	printf(JOBINFO, i, current->info);
}

char * new_job_info(tline * line) {
	size_t old_length = 0;
	size_t new_length;
	char * info = malloc(2 * sizeof(char));
	info[0] = ' ';
	info[1] = '\0';
	tcommand command;
	int i, j;
	for (i = 0; i < line->ncommands; i++) {
		command = line->commands[i];
		for (j = 0; j < command.argc; j++) {
			new_length = old_length + strlen(command.argv[j]) + 1;
			info = realloc(info, new_length * sizeof(char));
			strcpy(info + old_length + 1, command.argv[j]);
			if (j == 0) {
				if (old_length != 0) {
					info[old_length] = '|';
				}
			}
			else {
				info[old_length] = ' ';
			}
			old_length = new_length;
		}
	}
	return info;
}

JobInfo * new_job(pid_t pid, tline * line) {
	JobInfo * job = (JobInfo *)malloc(sizeof(JobInfo *));
	job->pid = pid;
	job->info = new_job_info(line);
	job->next = NULL;
	return job;
}

void bg(pid_t pid, JobInfo * job_list) {
	while (job_list != NULL) {
		if (job_list->pid == pid) {
			debug_wait(pid, 0);
			return;
		}
		job_list = job_list->next;
	}
}

void fg(int id, JobInfo * job_list) {
	int i = 0;
	pid_t current;
	/* comprueba que esta ejecutando en backgroud */
	while (i < id && job_list != NULL) {
		job_list = job_list->next;
		i++;
	}
	if (i == id && job_list != NULL) {
		current = job_list->pid;
		free(job_list->info);
		free(job_list);
		debug_wait(current, 0);
	}
}

/* comprobar que si hay tarea terminada en el segundo plano y muestra los que no han terminado*/
void jobs(JobInfo ** job_list_ptr) {
	int i = 0;
	JobInfo * current = NULL;
	JobInfo * next = NULL;
	if (job_list_ptr != NULL) {
		while (*job_list_ptr != NULL && current == NULL) {
			if (debug_wait((*job_list_ptr)->pid, WNOHANG) != 0) {
				current = (*job_list_ptr)->next;
				free((*job_list_ptr)->info);
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
				if (debug_wait(current->pid, WNOHANG) != 0) {
					free((current)->info);
					free(current);
				}
				else {
					i += 1;
					printf(JOBINFO, i, current->info);
				}
				current = next;
			}
		}
	}
}
#pragma endregion

void prompt() {
	static char cwd[BUFFER_SIZE];
	printf("msh:>%s$", getcwd(cwd, BUFFER_SIZE));
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
				fg(atoi(line->commands[0].argv[1]), job_list);
			}
			else {
				fg(last_bg_job, job_list);
			}
			return true;
		}
	}
	return false;
}

void execline(tline * line) {
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
				insert_job(&job_list, new_job(current, line));
			}
			else {
				/* esperar a que termine */
				debug_wait(-pgid, 0);
				/* vuelve a capturar el control de terminal*/
				tcsetpgrp(STDIN_FILENO, getpid());
			}
			current = 0;
		}
	}
}
#pragma endregion

#pragma region initialization and deinitialization
/* tarea de preparacion */
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
	pid_t shell_pgid = getpid();
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

/* Liberar la memoria de los contenidos de jobs */
void destroy() {
	JobInfo * next;
	while (job_list != NULL) {
		next = job_list->next;
#ifdef DEBUG
		fprintf(stdout, "liberando memoria asosiada a %d\n", job_list->pid);
#endif
		if (job_list->info != NULL) {
			free(job_list->info);
		}
		free(job_list);
		job_list = next;
	}

}
#pragma endregion