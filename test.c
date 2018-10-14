#include "shell.h"

int main(void) {
	char buf[1024];
	tline * line;

	prompt();
	while (fgets(buf, 1024, stdin)) {
		
		// Leer una linea del taclado
		line = tokenize(buf);
		if (line==NULL) {
			continue;
		}

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
		execline(line);
		prompt();
	}
	return 0;
}
