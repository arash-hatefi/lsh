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
#include "parse.h"



enum RETURN_CODES
{
  SUCCESS_EXIT_CODE = 0,
  EXECUTION_ERROR_EXIT_CODE,
  FORK_ERROR_EXIT_CODE,
  PIPE_CREATION_ERROR_EXIT_CODE,
  UNKNOWN_COMMAND_EXIT_CODE,
  TERMINATION_SIGNAL_EXIT_CODE,
  STDIN_ERROR_CODE,
  STDOUT_ERROR_CODE,
  SIGINT_EXIT_CODE,
  MEMORY_ALLOCATION_ERROR_CODE
};

#define TRUE 1
#define FALSE 0

#define STDIN_ERROR_MESSAGE "%s: Cannot open file\n"
#define STDOUT_ERROR_MESSAGE "%s: Cannot open file\n"
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
  struct PgidList* next;  

} PgidList;

PgidList* backgroundPgidList = NULL;
pid_t foregroundPgid = 0;



int RunCommand(Command *);//
int RunCommandInForeground(Pgm *, int , int );//
int RunCommandInBackground(Pgm *, int , int );//
int RunCommandRecursively(Pgm *, int , int );//
int RunSingleCommand(char **, int , int );//
int HandleBuiltins(char **, int , int );//
int RunCdCommand(char **);//
void RunExitCommad();//
void EndAllProcess();//
void KillProcessInBackgroundPgidList();//

void SigchldHandler(int );//
void SigintHandler(int );//
void SigtermHandler(int );//

void DebugPrintCommand(int, Command *);
void PrintPgm(Pgm *);

void stripwhite(char *); //
bool IsEqual(char *, char *);//
bool FileExists(char *);//
bool FileExistsInDir(char *, char *);//
void GetExternalCommandFullPath(char *, char *);//
void AddPaths(char *, char *, char *);//

int AddBackgroundPgid(pid_t pgid);
int RemoveBackgroundPgid(pid_t pgid);
void ResetBackgroundPgidList();





int main(int argc, char *argv[])
{

  signal(SIGCHLD, SigchldHandler);
  signal(SIGINT, SigintHandler);
  signal(SIGTERM, SigtermHandler);
  signal(SIGTTOU, SIG_IGN);


  bool debug = FALSE;
  if (argc==2 && IsEqual(argv[1], DEBUG_COMMAND_LINE_ARG)) {debug = TRUE;}

  Command cmd;
  int parse_result;

  while (TRUE)
  {
    char *line;
    line = readline("> ");

    /* If EOF encountered, exit shell */
    if (!line)
    {
      break;
    }
    /* Remove leading and trailing whitespace from the line */
    stripwhite(line);
    /* If stripped line not blank */
    if (*line)
    {
      add_history(line);
      parse_result = parse(line, &cmd);
      if(debug) {DebugPrintCommand(parse_result, &cmd);}
      if (parse_result != -1) {RunCommand(&cmd);}
    }

    /* Clear memory */
    free(line);
  }

  EndAllProcess();
  return 0;
}


int RunCommand(Command *cmd)
{  
  int inFileDescriptor = (cmd->rstdin) ? open(cmd->rstdin, O_RDONLY) : STDIN_FILENO;
  if (inFileDescriptor<0) 
  {
    fprintf(stderr, STDIN_ERROR_MESSAGE, cmd->rstdin);
    return STDIN_ERROR_CODE;
  }
  
  int outFileDescriptor = (cmd->rstdout) ? open(cmd->rstdout, O_CREAT | O_WRONLY, S_IRWXU) : STDOUT_FILENO;
  if (outFileDescriptor<0)
  {
    if (inFileDescriptor!=STDIN_FILENO) {close(inFileDescriptor);}
    fprintf(stderr, STDOUT_ERROR_MESSAGE, cmd->rstdout);
    return STDOUT_ERROR_CODE;
  }

  int status = (cmd->background) ? RunCommandInBackground(cmd->pgm, inFileDescriptor, outFileDescriptor) : RunCommandInForeground(cmd->pgm, inFileDescriptor, outFileDescriptor);  

  if (inFileDescriptor!=STDIN_FILENO) {close(inFileDescriptor);}
  if (outFileDescriptor!=STDOUT_FILENO) {close(outFileDescriptor);}

  return status;
}


int RunCommandInForeground(Pgm* pgm, int inFileDescriptor, int outFileDescriptor)
{
  if (pgm->next==NULL)
  {
    int status = HandleBuiltins(pgm->pgmlist, inFileDescriptor, outFileDescriptor);
    if (status!=UNKNOWN_COMMAND_EXIT_CODE) {return status;}
  }

  pid_t id = fork();
  if (id<0)
  {
    fprintf(stderr, FORK_ERROR_MESSAGE);
    return FORK_ERROR_EXIT_CODE;
  }
  else if (id==0) 
  {
    setpgid(0, 0);
    tcsetpgrp(STDIN_FILENO, getpgid(0));
    ResetBackgroundPgidList();
    int status = RunCommandRecursively(pgm, inFileDescriptor, outFileDescriptor);
    exit(status);
  }
  else
  {
    foregroundPgid = id;
    int status;
    waitpid(id, &status, 0);
    tcsetpgrp(STDIN_FILENO, getpgid(0));
    foregroundPgid = 0;
    return status;
  }
}


int RunCommandInBackground(Pgm* pgm, int inFileDescriptor, int outFileDescriptor)
{
  pid_t id = fork();
  if (id<0)
  {
    fprintf(stderr, FORK_ERROR_MESSAGE);
    return FORK_ERROR_EXIT_CODE;
  }
  else if (id==0) 
  {
    setpgid(0, 0);
    ResetBackgroundPgidList();
    int status = RunCommandRecursively(pgm, inFileDescriptor, outFileDescriptor);
    exit(status);
  }
  else
  {
    AddBackgroundPgid(id);
    return SUCCESS_EXIT_CODE;
  }
}


int RunCommandRecursively(Pgm* pgm, int inFileDescriptor, int outFileDescriptor)
{
  if (pgm->next!=NULL)
  {
    int fd[2];
    if (pipe(fd)==-1)
    {
      fprintf(stderr, PIPE_CREATION_ERROR_MESSAGE);
      return PIPE_CREATION_ERROR_EXIT_CODE;
    }

    pid_t id = fork();
    if (id<0)
    {
      fprintf(stderr, FORK_ERROR_MESSAGE);
      return FORK_ERROR_EXIT_CODE;
    }
    else if (id==0)
    {
      close(fd[0]);
      int status = RunCommandRecursively(pgm->next, inFileDescriptor, fd[1]);
      close(fd[1]);
      exit(status);
    }
    else
    {
      close(fd[1]);
      int status;
      waitpid(id, &status, 0);
      int newStatus = RunSingleCommand(pgm->pgmlist, fd[0], outFileDescriptor);
      close(fd[0]);
      return (status==SUCCESS_EXIT_CODE) ? newStatus : status;
    }
  }
  else {return RunSingleCommand(pgm->pgmlist, inFileDescriptor, outFileDescriptor);}
}


int RunSingleCommand(char **pgmlist, int inFileDescriptor, int outFileDescriptor)
{
  int status = HandleBuiltins(pgmlist, inFileDescriptor, outFileDescriptor);
  if(status!=UNKNOWN_COMMAND_EXIT_CODE) {return status;}

  char* cmd = *pgmlist;
  char** args = pgmlist+1;
  char externalCommandFullPath[strlen(PATH_DIRS)+strlen(cmd)+2];
  GetExternalCommandFullPath(cmd, externalCommandFullPath);
  if (*externalCommandFullPath)
  {
    if (outFileDescriptor!=STDOUT_FILENO) {dup2(outFileDescriptor, STDOUT_FILENO);} 
    if (inFileDescriptor!=STDIN_FILENO) {dup2(inFileDescriptor, STDIN_FILENO);}
    if(execvp(externalCommandFullPath, pgmlist)==-1) {return EXECUTION_ERROR_EXIT_CODE;}
  }
  else 
  {
    fprintf(stderr, UNKNOWN_COMMAND_ERROR_MESSAGE, cmd);
    return UNKNOWN_COMMAND_EXIT_CODE;
  }
}


int HandleBuiltins(char **pgmlist, int inFileDescriptor, int outFileDescriptor)
{
  char* cmd = *pgmlist;
  char** args = pgmlist+1;
  if(IsEqual(cmd, CD_COMMAND)) {return RunCdCommand(args);}
  else if(IsEqual(cmd, EXIT_COMMAND)) {RunExitCommad();}
  return UNKNOWN_COMMAND_EXIT_CODE;
}


int RunCdCommand(char** args)
{
  char* newDir = (*args==NULL) ? getenv(HOME_KEYWORD) : *args; 
  if(chdir(newDir)==-1) 
  {
    fprintf(stderr, CD_ERROR_MESSAGE, newDir);
    return EXECUTION_ERROR_EXIT_CODE;
  }
  return SUCCESS_EXIT_CODE;  
}


void RunExitCommad()
{
  EndAllProcess();
  exit(SUCCESS_EXIT_CODE);
}


void EndAllProcess()
{
  KillProcessInBackgroundPgidList();
  ResetBackgroundPgidList();
}


void KillProcessInBackgroundPgidList()
{
  PgidList* elementPointer = backgroundPgidList;
  while (elementPointer!=NULL)
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
  printf("\n");
  if (foregroundPgid==0) {printf("\n> ");}
  else
  {
    kill(-foregroundPgid, SIGTERM);
    foregroundPgid = 0;
  }
}

void SigtermHandler(int signum) {exit(TERMINATION_SIGNAL_EXIT_CODE);}


void DebugPrintCommand(int parse_result, Command *cmd)
{
  if (parse_result != 1) {
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

    /* The list is in reversed order so print
     * it reversed to get right
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


bool IsEqual(char* string1, char* string2) {return (strcmp(string1, string2)==0); }


bool FileExists(char* filename)
{
  struct stat buffer;   
  return (stat(filename, &buffer)==0);
}


bool FileExistsInDir(char* filename, char* dir)
{
  char* fullPath = strdup(dir);
  strcat(fullPath, "/");
  strcat(fullPath, filename);
  bool cmdExists = FileExists(fullPath);
  free(fullPath);

  if (cmdExists) {return TRUE;}
  return FALSE;
}


void GetExternalCommandFullPath(char* cmd, char* externalCommandFullPath)
{
  char* dirStartIdx = PATH_DIRS;

  bool done = FALSE;
  while (!done)
  {
    char* nextDirStartIdx = strchr(dirStartIdx, PATH_SEPERATOR);
    if (nextDirStartIdx==NULL)
    {
      nextDirStartIdx = PATH_DIRS + strlen(PATH_DIRS);
      done = TRUE;
    }
    
    char dir[nextDirStartIdx-dirStartIdx+1];
    strncpy(dir,dirStartIdx,nextDirStartIdx-dirStartIdx+1);
    dir[nextDirStartIdx-dirStartIdx] = '\0';
    if (FileExistsInDir(cmd, dir))
    {
      AddPaths(dir, cmd, externalCommandFullPath);
      return;
    }

    dirStartIdx = nextDirStartIdx+1;
  }

  *externalCommandFullPath = 0;
}


void AddPaths(char* dir, char* filename, char* result)
{
    strcpy(result, dir);
    strcat(result, "/");
    strcat(result, filename);
}


int AddBackgroundPgid(pid_t pgid)
{ 
  PgidList* new = (PgidList*) malloc(sizeof(PgidList));//
  if (new==NULL) {return MEMORY_ALLOCATION_ERROR_CODE;}
  new->next = NULL;
  new->pgid = pgid;

  if (backgroundPgidList==NULL) {backgroundPgidList = new;}
  else
  {
    PgidList* elementPointer = backgroundPgidList;
    while (elementPointer->next!=NULL) {elementPointer = elementPointer->next;}
    elementPointer->next = new;
  }

  return SUCCESS_EXIT_CODE;
}


int RemoveBackgroundPgid(pid_t pgid)
{
  PgidList* elementPointer = backgroundPgidList;

  if (elementPointer==NULL) {return 0;}
  if (elementPointer->next==NULL)
  {
    if (elementPointer->pgid==pgid)
    {
      free(elementPointer);
      backgroundPgidList = NULL;
      return pgid;
    }
    return 0;
  }
  while (elementPointer->next!=NULL)
  {
    if (elementPointer->next->pgid==pgid)
    {
      PgidList* nextElementPointer = elementPointer->next->next;
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
  if (backgroundPgidList==NULL) {return;}
  else
  {
    PgidList* elementPointer = backgroundPgidList;
    do
    {
      PgidList* nextElementPointer = elementPointer->next;
      free(elementPointer);
      elementPointer = nextElementPointer;
    }
    while (elementPointer!=NULL);
  }
}


