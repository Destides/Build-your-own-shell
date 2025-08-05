#include <stdio.h>      // For standard input/output functions like printf, stderr
#include <stdlib.h>     // For memory allocation (malloc, free) and exit
#include <string.h>     // For string manipulation functions like strcmp, strtok
#include <unistd.h>     // For POSIX operating system API, including fork, exec, pipe, chdir
#include <sys/wait.h>   // For waitpid and related macros
#include <sys/types.h>  // For data types like pid_t
#include <fcntl.h>      // For file control options used with open()
#include <signal.h>     // For signal handling

#define MAX_CMD_LEN 1024   // Maximum length of a command string
#define MAX_ARGS 64        // Maximum number of arguments for a command

// --- Function Declarations ---

// Built-in commands
int shell_cd(char **args);
int shell_exit(char **args);
int shell_help(char **args);

// Core shell functions
void shell_loop(void);
int execute_command(char **args);
void handle_signal(int signo);

// --- Built-in Commands Setup ---

// Array of built-in command names
const char *builtin_str[] = {
    "cd",
    "exit",
    "help"
};

// Array of function pointers to the built-in command implementations
int (*builtin_func[]) (char **) = {
    &shell_cd,
    &shell_exit,
    &shell_help
};

// Function to get the number of built-in commands
int num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}

// --- Signal Handler ---

// This function will be called when a signal is received
void handle_signal(int signo) {
    // If Ctrl+C (SIGINT) is pressed, we want to ignore it and just print a new prompt
    if (signo == SIGINT) {
        printf("\n"); // Move to a new line
        // We could add more complex behavior here, like killing a foreground process
    }
}

// --- Main Function: Entry point of the shell ---
int main(int argc, char **argv) {
    // Set up signal handlers.
    // SIGINT is sent when you press Ctrl+C. We want our shell to ignore this.
    signal(SIGINT, handle_signal);
    // SIGCHLD is sent when a child process terminates. We could use this to clean up zombies.
    // For this implementation, we handle zombies with waitpid in the execution logic.
    signal(SIGCHLD, SIG_IGN); // A simpler way to prevent zombies is to ignore the signal.

    // Run the main shell loop.
    shell_loop();

    // The shell_loop only exits on the 'exit' command.
    // Perform any shutdown cleanup here.
    return EXIT_SUCCESS; // Exit the shell successfully.
}


// --- Core Shell Loop ---
void shell_loop(void) {
    char cmd[MAX_CMD_LEN]; // Buffer to hold the command line input
    char *args[MAX_ARGS];  // Array of pointers to arguments
    int status;            // Status of the loop (1 to continue, 0 to stop)

    do {
        // --- Display Prompt ---
        char cwd[1024]; // Buffer for the current working directory
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            // If we can get the current directory, display it in the prompt
            printf("🚀 %s > ", cwd);
        } else {
            // If there's an error, display a generic prompt
            perror("getcwd() error");
            printf("🚀 > ");
        }

        // --- Read Input ---
        if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL) {
            // If fgets returns NULL, it's an EOF (Ctrl+D). We can exit cleanly.
            printf("exit\n");
            break; // Exit the loop
        }
        
        // --- Parse the command string ---
        char *token;             // Pointer to hold each token
        int i = 0;               // Index for the args array
        cmd[strcspn(cmd, "\n")] = 0; // Remove the trailing newline character from fgets
        token = strtok(cmd, " \t\r\n\a"); // Get the first token, splitting by whitespace

        while (token != NULL && i < MAX_ARGS - 1) {
            args[i] = token; // Store the token in the args array
            i++;             // Move to the next position
            token = strtok(NULL, " \t\r\n\a"); // Get the next token
        }
        args[i] = NULL; // The last element of args must be NULL for execvp

        // --- Execute the command ---
        status = execute_command(args); // Call the execution logic

    } while (status); // Continue the loop as long as status is non-zero
}


// --- Command Execution Logic ---
int execute_command(char **args) {
    // If no command was entered (just pressed Enter), do nothing and continue
    if (args[0] == NULL) {
        return 1; // Return 1 to continue the loop
    }

    // --- Check for Built-in Commands ---
    for (int i = 0; i < num_builtins(); i++) {
        // Compare the entered command with the list of built-ins
        if (strcmp(args[0], builtin_str[i]) == 0) {
            // If it matches, call the corresponding function and return its status
            return (*builtin_func[i])(args);
        }
    }

    // --- External Command Execution ---
    pid_t pid = fork(); // Create a new child process
    int background = 0; // Flag to check for background execution (&)
    int arg_count = 0;  // Counter for arguments

    // Count the arguments to find the last one
    while(args[arg_count] != NULL) {
        arg_count++;
    }

    // Check if the last argument is '&'
    if (arg_count > 0 && strcmp(args[arg_count - 1], "&") == 0) {
        background = 1;      // Set background flag
        args[arg_count - 1] = NULL; // Remove '&' from the arguments list so execvp doesn't see it
    }

    // Forking error check
    if (pid < 0) {
        perror("fork failed"); // Report error if fork fails
        return 1; // Return to loop
    }
    
    if (pid == 0) {
        // --- This is the CHILD process ---
        
        // Before executing, we must check for redirection and pipes
        char *inputFile = NULL;
        char *outputFile = NULL;
        int append = 0; // Flag for '>>'
        int pipe_pos = -1; // Position of the pipe '|' symbol

        // Scan arguments for redirection and pipe symbols
        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], "<") == 0) {      // Input redirection
                inputFile = args[i + 1];
                args[i] = NULL; // Remove '<' and filename from args
            } else if (strcmp(args[i], ">") == 0) { // Output redirection
                outputFile = args[i + 1];
                args[i] = NULL;
            } else if (strcmp(args[i], ">>") == 0) { // Append output redirection
                outputFile = args[i + 1];
                append = 1;
                args[i] = NULL;
            } else if (strcmp(args[i], "|") == 0) { // Pipe
                pipe_pos = i;
                args[i] = NULL; // Split the command at the pipe
                break; // Handle one pipe for simplicity. More complex logic needed for multiple pipes.
            }
        }

        // --- Handle I/O Redirection ---
        if (inputFile) {
            int fd_in = open(inputFile, O_RDONLY); // Open file for reading
            if (fd_in < 0) {
                perror("open input file failed");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO); // Duplicate file descriptor to standard input
            close(fd_in); // Close the original file descriptor
        }
        if (outputFile) {
            int flags = O_WRONLY | O_CREAT; // Write-only, create if not exists
            if (append) {
                flags |= O_APPEND; // Add append flag if '>>'
            } else {
                flags |= O_TRUNC; // Truncate (overwrite) if '>'
            }
            int fd_out = open(outputFile, flags, 0644); // 0644 are file permissions
            if (fd_out < 0) {
                perror("open output file failed");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO); // Duplicate file descriptor to standard output
            close(fd_out); // Close original
        }

        // --- Handle Piping ---
        if (pipe_pos != -1) {
            int pipefd[2]; // Array to hold the two pipe file descriptors
            if (pipe(pipefd) < 0) {
                perror("pipe failed");
                exit(EXIT_FAILURE);
            }

            pid_t pipe_pid = fork(); // Fork again for the second command in the pipe
            if (pipe_pid < 0) {
                perror("pipe fork failed");
                exit(EXIT_FAILURE);
            }

            if (pipe_pid == 0) {
                // --- CHILD of pipe (RIGHT side of | e.g., 'grep .c') ---
                close(pipefd[1]); // Close the write-end of the pipe, not needed
                dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to the read-end of the pipe
                close(pipefd[0]); // Close the original read-end
                // Execute the second command (the one after the '|')
                if (execvp(args[pipe_pos + 1], &args[pipe_pos + 1]) < 0) {
                    perror("execvp for piped command failed");
                    exit(EXIT_FAILURE);
                }
            } else {
                // --- PARENT of pipe (LEFT side of | e.g., 'ls -l') ---
                close(pipefd[0]); // Close the read-end, not needed
                dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to the write-end of the pipe
                close(pipefd[1]); // Close the original write-end
                // Wait for the right-side command to finish
                waitpid(pipe_pid, NULL, 0); 
                // Execute the first command (the one before the '|')
                if (execvp(args[0], args) < 0) {
                    perror("execvp for first command failed");
                    exit(EXIT_FAILURE);
                }
            }
        } else {
            // --- No pipe, just a simple command (possibly with redirection) ---
            if (execvp(args[0], args) < 0) {
                perror("execvp failed"); // If execvp returns, an error occurred
                exit(EXIT_FAILURE); // Exit the child process on error
            }
        }

    } else {
        // --- This is the PARENT process ---
        if (!background) {
            // If the command is NOT a background process, wait for it to finish
            int status;
            waitpid(pid, &status, 0); // Wait for the specific child process
        } else {
            // If it IS a background process, print its PID and don't wait
            printf("Started background job with PID: %d\n", pid);
        }
    }

    return 1; // Return 1 to signal the shell_loop to continue
}


// --- Built-in Command Implementations ---

/**
 * @brief Built-in command: change directory.
 * @param args List of args. args[1] is the directory.
 * @return 1 to continue running the shell.
 */
int shell_cd(char **args) {
    if (args[1] == NULL) {
        // If no directory is provided, report an error.
        fprintf(stderr, "shell: expected argument to \"cd\"\n");
    } else {
        // Attempt to change the directory.
        if (chdir(args[1]) != 0) {
            // If chdir returns non-zero, an error occurred.
            perror("shell");
        }
    }
    return 1; // Always return 1 to continue.
}

/**
 * @brief Built-in command: print help.
 * @param args List of args. Not examined.
 * @return 1 to continue running the shell.
 */
int shell_help(char **args) {
    printf("My Advanced C Shell\n");
    printf("Type program names and arguments, and hit enter.\n");
    printf("The following are built in:\n");

    for (int i = 0; i < num_builtins(); i++) {
        printf("  %s\n", builtin_str[i]);
    }

    printf("Use the man command for information on other programs.\n");
    printf("Supports piping ('|'), I/O redirection ('<', '>', '>>'), and background tasks ('&').\n");
    return 1;
}

/**
 * @brief Built-in command: exit.
 * @param args List of args. Not examined.
 * @return 0 to terminate the shell.
 */
int shell_exit(char **args) {
    return 0; // Returning 0 will terminate the shell_loop.
}