#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include "parse.h"

enum RETURN_CODES
{
  SUCCESS_EXIT_CODE = 0, /*Same in Linux */
    EXECUTION_ERROR_EXIT_CODE = 3,
    FORK_ERROR_EXIT_CODE = 4,
    PIPE_CREATION_ERROR_EXIT_CODE = 5,
    UNKNOWN_COMMAND_EXIT_CODE = -1, /*Same in Linux */
    SIGTERM_EXIT_CODE = 9, /*Same in Linux */
    OEPN_FILE_ERROR_CODE = 1, /*Same in Linux */
    SIGINT_EXIT_CODE = 2, /*Same in Linux */
    MEMORY_ALLOCATION_ERROR_CODE = 12 /*Same in Linux */
};

#define TRUE 1
#define FALSE 0
#define OPEN_FILE_ERROR_MESSAGE "%s: Cannot open file\n"
#define UNKNOWN_COMMAND_ERROR_MESSAGE "%s: Command not found\n"
#define CD_ERROR_MESSAGE "%s: No such file or directory\n"
#define FORK_ERROR_MESSAGE "Fork failed\n"
#define PIPE_CREATION_ERROR_MESSAGE "Pipe failed\n"
#define CD_COMMAND "cd"
#define EXIT_COMMAND "exit"
#define HOME_KEYWORD "HOME"
#define PATH_ENV_VAR "PATH"
#define DEBUG_COMMAND_LINE_ARG "-debug"
#define PATH_DIRS getenv(PATH_ENV_VAR)
#define TRUE 1
#define FALSE 0

const char PATH_SEPERATOR = ':';

typedef struct PgidList
{
  pid_t pgid;
  struct PgidList * next;

}
PgidList;

PgidList *backgroundPgidList = NULL;

int RunCommand(Command*);
int OpenIOs(Command *, int *);
void CloseIOs(const int[3]);
int ExecuteCommandsRecursively(Pgm *pgm, const int[3], pid_t *, bool);
int ExecuteSingleCommandInChildProcess(char **, const int[3], pid_t *, bool);
int ExecuteSingleCommandInProcess(char **, const int[3]);
int HandleBuiltins(char **, const int[3]);
int RunCdCommand(char **);
void RunExitCommand();
void EndAllProcesses();
void KillProcessesInBackgroundPgidList();
void SigchldHandler(int);
void SigintHandler(int);
void SigtermHandler(int);
void DebugPrintCommand(int, Command *);
void PrintPgm(Pgm*);
void stripwhite(char*);
bool IsEqual(char *, char *);
bool FileExists(char*);
bool FileExistsInDir(char *, char *);
void GetExternalCommandFullPath(char *, char *);
void AddPaths(char *, char *, char *);
int AddBackgroundPgid(pid_t);
int RemoveBackgroundPgid(pid_t);
void ResetBackgroundPgidList();

int main(int argc, char *argv[])
{
  /*Handlers are only used for the main process as all child processes replace their images after running exec */
  signal(SIGCHLD, SigchldHandler);
  signal(SIGINT, SigintHandler);
  signal(SIGTERM, SigtermHandler);
  signal(SIGTTOU, SIG_IGN); /*Needed as tcsetpgrp is used. More on tcsetpgrp man page */

  bool debug = FALSE;
  if (argc == 2 && IsEqual(argv[1], DEBUG_COMMAND_LINE_ARG))
  {
    debug = TRUE;
  }

  Command cmd;
  int parse_result;
  int status;

  while (TRUE)
  {
    char *line;
    line = readline("> ");

    /*If EOF encountered, exit shell */
    if (!line)
    {
      break;
    }
    /*Remove leading and trailing white space from the line */
    stripwhite(line);
    /*If stripped line not blank */
    if (*line)
    {
      add_history(line);
      parse_result = parse(line, &cmd);
      if (debug)
      {
        DebugPrintCommand(parse_result, &cmd);
      }
      if (parse_result != -1)
      {
        status = RunCommand(&cmd);
        if (status == SIGINT_EXIT_CODE)
        {
          printf("\n");
        }
      }
    }

    /*Clear memory */
    free(line);
  }

  EndAllProcesses();
  return 0;
}

int RunCommand(Command *cmd)
{
  int fileDescriptors[3];
  int status = OpenIOs(cmd, fileDescriptors);
  if (status != SUCCESS_EXIT_CODE)
  {
    return status;
  }

  if (cmd->pgm->next == NULL) /*Try to handle builtin commands that are not part of a pipe */
  {
    int status = HandleBuiltins(cmd->pgm->pgmlist, fileDescriptors);
    if (status != UNKNOWN_COMMAND_EXIT_CODE)
    {
      return status;
    }
  }

  pid_t pgid = 0; /*ExecuteCommandsRecursively requires the initial value of PGID to be 0. After running it the value is changed to the new process group id */
  int nDecendants = ExecuteCommandsRecursively(cmd->pgm, fileDescriptors, &pgid, cmd->background);
  status = errno; /*Errors in ExecuteCommandsRecursively are handled with errno*/
  if (cmd->background)
  {
    AddBackgroundPgid(pgid);
    return status;
  }
  else
  {
    for (int counter = 0; counter < nDecendants; counter++)
    {
      int newStatus;
      waitpid(-pgid, &newStatus, 0);
      status = (newStatus == SUCCESS_EXIT_CODE) ? status : EXECUTION_ERROR_EXIT_CODE;
    }
    tcsetpgrp(STDIN_FILENO, getpgid(0)); /*While running foreground commands, terminal controls the foreground group processes. After finishing the execution, terminal controls the main process again */
  }

  CloseIOs(fileDescriptors);

  return status;
}

void CloseIOs(const int fileDescriptors[3])
{
  /*STDIN_FILENO, STDOUT_FILENO, and STDERR_FILENO should not be closed */
  if (fileDescriptors[0] != STDIN_FILENO)
  {
    close(fileDescriptors[0]);
  }
  if (fileDescriptors[1] != STDOUT_FILENO)
  {
    close(fileDescriptors[1]);
  }
  if (fileDescriptors[2] != STDERR_FILENO)
  {
    close(fileDescriptors[2]);
  }
}

int OpenIOs(Command *cmd, int *fileDescriptors)
{
  fileDescriptors[0] = (cmd->rstdin) ? open(cmd->rstdin, O_RDONLY) : STDIN_FILENO;
  if (fileDescriptors[0] < 0)
  {
    fprintf(stderr, OPEN_FILE_ERROR_MESSAGE, cmd->rstdin);
    return OEPN_FILE_ERROR_CODE;
  }

  fileDescriptors[1] = (cmd->rstdout) ? open(cmd->rstdout, O_CREAT | O_WRONLY, S_IRWXU) : STDOUT_FILENO;
  if (fileDescriptors[1] < 0)
  {
    if (fileDescriptors[0] != STDIN_FILENO)
    {
      close(fileDescriptors[0]);
    }
    fprintf(stderr, OPEN_FILE_ERROR_MESSAGE, cmd->rstdout);
    return OEPN_FILE_ERROR_CODE;
  }

  fileDescriptors[2] = (cmd->rstderr) ? open(cmd->rstderr, O_CREAT | O_WRONLY, S_IRWXU) : STDERR_FILENO;
  if (fileDescriptors[2] < 0)
  {
    if (fileDescriptors[0] != STDIN_FILENO)
    {
      close(fileDescriptors[0]);
    }
    if (fileDescriptors[1] != STDOUT_FILENO)
    {
      close(fileDescriptors[1]);
    }
    fprintf(stderr, OPEN_FILE_ERROR_MESSAGE, cmd->rstderr);
    return OEPN_FILE_ERROR_CODE;
  }
  return SUCCESS_EXIT_CODE;
}

int ExecuteCommandsRecursively(Pgm *pgm, const int fileDescriptors[3], pid_t *pgid, bool background)
{
  /*The function returns the number of successfully created processes */

  int inFileDescriptor = fileDescriptors[0];
  int outFileDescriptor = fileDescriptors[1];
  int errFileDescriptor = fileDescriptors[2];

  if (pgm->next != NULL)
  {
    int pipeFileDescriptors[2];
    if (pipe(pipeFileDescriptors) == -1)
    {
      fprintf(stderr, PIPE_CREATION_ERROR_MESSAGE);
      errno = PIPE_CREATION_ERROR_EXIT_CODE;
      return 0;
    }

    pid_t id = fork();
    if (id < 0)
    {
      fprintf(stderr, FORK_ERROR_MESSAGE);
      errno = FORK_ERROR_EXIT_CODE;
      return 0;
    }
    else if (id == 0)
    {
      setpgid(0, *pgid);
      close(pipeFileDescriptors[1]);
      int newFileDescriptors[3] = { pipeFileDescriptors[0], outFileDescriptor, errFileDescriptor
      };
      int status = ExecuteSingleCommandInProcess(pgm->pgmlist, newFileDescriptors);
      close(pipeFileDescriptors[0]);
      exit(status);
    }
    else
    {
      close(pipeFileDescriptors[0]);
      if (*pgid == 0)
      {
        *pgid = id;
      }
      int newFileDescriptors[3] = { inFileDescriptor, pipeFileDescriptors[1], errFileDescriptor };
      int nProcesses = ExecuteCommandsRecursively(pgm->next, newFileDescriptors, pgid, background);
      close(pipeFileDescriptors[1]);
      return 1 + nProcesses;
    }
  }
  else
  {
    int status = ExecuteSingleCommandInChildProcess(pgm->pgmlist, fileDescriptors, pgid, background);
    errno = status;
    return (int)(status == SUCCESS_EXIT_CODE); /*process is created only if the exit code is SUCCESS_EXIT_CODE */
  }
}

int ExecuteSingleCommandInChildProcess(char **pgmlist, const int fileDescriptors[3], pid_t *pgid, bool background)
{
  pid_t id = fork();
  if (id < 0)
  {
    fprintf(stderr, FORK_ERROR_MESSAGE);
    return FORK_ERROR_EXIT_CODE;
  }
  else if (id == 0)
  {
    setpgid(0, *pgid); /*Change the PGID of child from child */
    if (!background)
    {
      tcsetpgrp(STDIN_FILENO, getpgid(0));
    }
    int status = ExecuteSingleCommandInProcess(pgmlist, fileDescriptors);
    exit(status);
  }
  else
  {
    setpgid(id, *pgid); /*Change the PGID of child from the main process. If this is not here, the parent may continue while the PGID of child is still not changed*/
    if (*pgid == 0)
    {
      *pgid = id;
    }
    return SUCCESS_EXIT_CODE;
  }
}

int ExecuteSingleCommandInProcess(char **pgmlist, const int fileDescriptors[3])
{
  int inFileDescriptor = fileDescriptors[0];
  int outFileDescriptor = fileDescriptors[1];
  int errFileDescriptor = fileDescriptors[1];

  int status = HandleBuiltins(pgmlist, fileDescriptors);
  if (status != UNKNOWN_COMMAND_EXIT_CODE)
  {
    return status;
  }

  char *cmd = *pgmlist;
  char **args = pgmlist + 1;
  char externalCommandFullPath[strlen(PATH_DIRS) + strlen(cmd) + 2];
  GetExternalCommandFullPath(cmd, externalCommandFullPath); /*sets *externalCommandFullPath to NULL if no command is found */
  if (*externalCommandFullPath)
  {
    if (outFileDescriptor != STDOUT_FILENO)
    {
      dup2(outFileDescriptor, STDOUT_FILENO);
    }
    if (inFileDescriptor != STDIN_FILENO)
    {
      dup2(inFileDescriptor, STDIN_FILENO);
    }
    if (errFileDescriptor != STDERR_FILENO)
    {
      dup2(errFileDescriptor, STDERR_FILENO);
    }
    if (execvp(externalCommandFullPath, pgmlist) == -1)
    {
      return EXECUTION_ERROR_EXIT_CODE;
    }
  }
  else
  {
    fprintf(stderr, UNKNOWN_COMMAND_ERROR_MESSAGE, cmd);
    return UNKNOWN_COMMAND_EXIT_CODE;
  }
}

int HandleBuiltins(char **pgmlist, const int fileDescriptors[3])
{
  char *cmd = *pgmlist;
  char **args = pgmlist + 1;
  if (IsEqual(cmd, CD_COMMAND))
  {
    return RunCdCommand(args);
  }
  else if (IsEqual(cmd, EXIT_COMMAND))
  {
    RunExitCommand();
  }
  return UNKNOWN_COMMAND_EXIT_CODE;
}

int RunCdCommand(char **args)
{
  char *newDir = (*args == NULL) ? getenv(HOME_KEYWORD) : *args;
  if (chdir(newDir) == -1) /*Successfully changed directory */
  {
    fprintf(stderr, CD_ERROR_MESSAGE, newDir);
    return EXECUTION_ERROR_EXIT_CODE;
  }
  return SUCCESS_EXIT_CODE;
}

void RunExitCommand()
{
  EndAllProcesses(); /*Avoid orphan generation */
  exit(SUCCESS_EXIT_CODE);
}

void EndAllProcesses()
{
  KillProcessesInBackgroundPgidList(); /*Avoid orphan generation */
  ResetBackgroundPgidList(); /*Free the linked list */
}

void KillProcessesInBackgroundPgidList()
{
  PgidList *elementPointer = backgroundPgidList;
  while (elementPointer != NULL)
  {
    kill(-elementPointer->pgid, SIGTERM);
    elementPointer = elementPointer->next;
  }
  printf("\n");
}

void SigchldHandler(int signum)
{
  int status;
  int terminatedPgid = waitpid(-1, &status, WNOHANG);
  RemoveBackgroundPgid(terminatedPgid);
}

void SigintHandler(int signum)
{
  printf("\n> ");
}

void SigtermHandler(int signum)
{
  exit(SIGTERM_EXIT_CODE);
}

void DebugPrintCommand(int parse_result, Command *cmd)
{
  if (parse_result != 1)
  {
    printf("Parse ERROR\n");
    return;
  }
  printf("------------------------------\n");
  printf("Parse OK\n");
  printf("stdin:      %s\n", cmd->rstdin ? cmd->rstdin : "<none>");
  printf("stdout:     %s\n", cmd->rstdout ? cmd->rstdout : "<none>");
  printf("background: %s\n", cmd->background ? "true" : "false");
  printf("Pgms:\n");
  PrintPgm(cmd->pgm);
  printf("------------------------------\n");
}

void PrintPgm(Pgm *p)
{
  if (p == NULL)
  {
    return;
  }
  else
  {
    char **pl = p->pgmlist;

    /*The list is in reversed order so print
     *it reversed to get right
     */
    PrintPgm(p->next);
    printf("            * [ ");
    while (*pl)
    {
      printf("%s ", *pl++);
    }
    printf("]\n");
  }
}

void stripwhite(char *string)
{
  register int i = 0;

  while (isspace(string[i]))
  {
    i++;
  }

  if (i)
  {
    memmove(string, string + i, strlen(string + i) + 1);
  }

  i = strlen(string) - 1;
  while (i > 0 && isspace(string[i]))
  {
    i--;
  }

  string[++i] = '\0';
}

bool IsEqual(char *string1, char *string2)
{
  return (strcmp(string1, string2) == 0);
}

bool FileExists(char *filename)
{
  struct stat buffer;
  return (stat(filename, &buffer) == 0);
}

bool FileExistsInDir(char *filename, char *dir)
{
  char *fullPath = strdup(dir);
  strcat(fullPath, "/");
  strcat(fullPath, filename);
  bool cmdExists = FileExists(fullPath);
  free(fullPath);

  if (cmdExists)
  {
    return TRUE;
  }
  return FALSE;
}

void GetExternalCommandFullPath(char *cmd, char *externalCommandFullPath)
{
  char *dirStartIdx = PATH_DIRS;

  bool done = FALSE;
  while (!done)
  {
    char *nextDirStartIdx = strchr(dirStartIdx, PATH_SEPERATOR);
    if (nextDirStartIdx == NULL)
    {
      nextDirStartIdx = PATH_DIRS + strlen(PATH_DIRS);
      done = TRUE;
    }

    char dir[nextDirStartIdx - dirStartIdx + 1];
    strncpy(dir, dirStartIdx, nextDirStartIdx - dirStartIdx + 1);
    dir[nextDirStartIdx - dirStartIdx] = '\0';
    if (FileExistsInDir(cmd, dir))
    {
      AddPaths(dir, cmd, externalCommandFullPath);
      return;
    }

    dirStartIdx = nextDirStartIdx + 1;
  }

  *externalCommandFullPath = 0;
}

void AddPaths(char *dir, char *filename, char *result)
{
  strcpy(result, dir);
  strcat(result, "/");
  strcat(result, filename);
}

int AddBackgroundPgid(pid_t pgid)
{
  PgidList *new = (PgidList*) malloc(sizeof(PgidList));
  if (new == NULL)
  {
    return MEMORY_ALLOCATION_ERROR_CODE;
  }
  new->next = NULL;
  new->pgid = pgid;

  if (backgroundPgidList == NULL)
  {
    backgroundPgidList = new;
  }
  else
  {
    PgidList *elementPointer = backgroundPgidList;
    while (elementPointer->next != NULL)
    {
      elementPointer = elementPointer->next;
    }
    elementPointer->next = new;
  }

  return SUCCESS_EXIT_CODE;
}

int RemoveBackgroundPgid(pid_t pgid)
{
  PgidList *elementPointer = backgroundPgidList;

  if (elementPointer == NULL)
  {
    return 0;
  }
  if (elementPointer->next == NULL)
  {
    if (elementPointer->pgid == pgid)
    {
      free(elementPointer);
      backgroundPgidList = NULL;
      return pgid;
    }
    return 0;
  }
  while (elementPointer->next != NULL)
  {
    if (elementPointer->next->pgid == pgid)
    {
      PgidList *nextElementPointer = elementPointer->next->next;
      free(elementPointer->next);
      elementPointer->next = nextElementPointer;
      return pgid;
    }
    elementPointer = elementPointer->next;
  }
  return 0;
}

void ResetBackgroundPgidList()
{
  if (backgroundPgidList == NULL)
  {
    return;
  }
  else
  {
    PgidList *elementPointer = backgroundPgidList;
    do 
    {  
      PgidList *nextElementPointer = elementPointer->next;
      free(elementPointer);
      elementPointer = nextElementPointer;
    }
    while (elementPointer != NULL);
  }
}