/* 
 * Main source code file for lsh shell program
 *
 * You are free to add functions to this file.
 * If you want to add functions in a separate file 
 * you will need to modify Makefile to compile
 * your additional functions.
 *
 * Add appropriate comments in your code to make it
 * easier for us while grading your assignment.
 *
 * Submit the entire lab1 folder as a tar archive (.tgz).
 * Command to create submission archive: 
      $> tar cvf lab1.tgz lab1/
 *
 * All the best 
 */

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


#define TRUE 1
#define FALSE 0

#define STDIN_ERROR_MESSAGE "%s: Cannot open file\n"
#define STDOUT_ERROR_MESSAGE "%s: Cannot open file\n"
#define UNKNOWN_COMMAND_ERROR_MESSAGE "%s: Command not found\n"
#define CD_ERROR_MESSAGE "%s: No such file or directory\n"

#define CD_COMMAND "cd"
#define EXIT_COMMAND "exit"

#define HOME_KEYWORD "HOME"
#define PATH_ENV_VAR "PATH"

#define DEBUG_COMMAND_LINE_ARG "-debug"

#define PATH_DIRS getenv(PATH_ENV_VAR)

#define TRUE 1
#define FALSE 0

const char PATH_SEPERATOR = ':';

pid_t MAIN_PROCESS_PID;


void RunCommand(int, Command *);
void RunCommandRecursively(Pgm *, int , int );
void RunCommandInForeground(Pgm *, int , int );
void RunCommandInBackground(Pgm *, int , int );
void RunSingleCommand(char **, int , int );
void DebugPrintCommand(int, Command *);
void PrintPgm(Pgm *);
void stripwhite(char *);
bool IsEqual(char *, char *);
void RunCdCommand(char **);
void RunExitCommand();
bool FileExists(char *);
bool FileExistsInDir(char *, char *);
void GetExternalLinuxCommandFullPath(char *, char *);
void AddPaths(char *, char *, char *);
void SigchldHandler(int );
void SigintHandlerWhileRunningForgraoundProcess(int );
void SigintHandlerWhileNotRunningForgraoundProcess(int );



int main(int argc, char *argv[])
{

  setpgid(0,0);
  MAIN_PROCESS_PID = getpid();

  bool debug = FALSE;
  if (argc==2 && IsEqual(argv[1], DEBUG_COMMAND_LINE_ARG)) {debug = TRUE;}

  signal(SIGCHLD, SigchldHandler);
  signal(SIGINT, SigintHandlerWhileNotRunningForgraoundProcess);

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
      signal(SIGINT, SigintHandlerWhileRunningForgraoundProcess);
      RunCommand(parse_result, &cmd);
      signal(SIGINT, SigintHandlerWhileNotRunningForgraoundProcess);
    }

    /* Clear memory */
    free(line);
  }
  return 0;
}


/* Execute the given command(s).

 * Note: The function currently only prints the command(s).
 * 
 * TODO: 
 * 1. Implement this function so that it executes the given command(s).
 * 2. Remove the debug printing before the final submission.
 */
void RunCommand(int parse_result, Command *cmd)
{  
  int inFileDescriptor = (cmd->rstdin) ? open(cmd->rstdin, O_RDONLY) : STDIN_FILENO;
  if (inFileDescriptor<0)
  {
    fprintf(stderr, STDIN_ERROR_MESSAGE, cmd->rstdin);
    return;
  }
  int outFileDescriptor = (cmd->rstdout) ? open(cmd->rstdout, O_CREAT | O_APPEND | O_WRONLY, S_IRWXU) : STDOUT_FILENO;
  if (outFileDescriptor<0)
  {
    fprintf(stderr, STDOUT_ERROR_MESSAGE, cmd->rstdout);
    return;
  }
  (cmd->background) ? RunCommandInBackground(cmd->pgm, inFileDescriptor, outFileDescriptor) : RunCommandInForeground(cmd->pgm, inFileDescriptor, outFileDescriptor);

  if (inFileDescriptor!=STDIN_FILENO) {close(inFileDescriptor);}
  if (outFileDescriptor!=STDOUT_FILENO) {close(outFileDescriptor);}
}


void RunCommandInForeground(Pgm* pgm, int inFileDescriptor, int outFileDescriptor)
{
  pid_t id = fork();
  if (id==0) 
  {
    RunCommandRecursively(pgm, inFileDescriptor, outFileDescriptor);
    exit(0);
  }
  else(waitpid(id, NULL, 0));
}


void RunCommandInBackground(Pgm* pgm, int inFileDescriptor, int outFileDescriptor)
{
  pid_t id = fork();
  if (id==0) 
  {
    setpgid(0,0);
    RunCommandRecursively(pgm, inFileDescriptor, outFileDescriptor);
    exit(0);
  }
}


void RunCommandRecursively(Pgm* pgm, int inFileDescriptor, int outFileDescriptor)
{
  if (pgm->next!=NULL)
  {
    int fd[2];
    if (pipe(fd)==-1) {return;}

    pid_t id = fork();
    if (id==0)
    {
      close(fd[0]);
      RunCommandRecursively(pgm->next, inFileDescriptor, fd[1]);
      close(fd[1]);
      exit(0);
    }
    else
    {
      close(fd[1]);
      waitpid(id, NULL, 0);
      RunSingleCommand(pgm->pgmlist, fd[0], outFileDescriptor);
      close(fd[0]);
    }
  }
  else
  { 
    RunSingleCommand(pgm->pgmlist, inFileDescriptor, outFileDescriptor);
  }
}


void RunSingleCommand(char **pgmlist, int inFileDescriptor, int outFileDescriptor)
{
  char* cmd = *pgmlist;
  char** args = pgmlist+1;
  if(IsEqual(cmd, CD_COMMAND)) {RunCdCommand(args);}
  else if(IsEqual(cmd, EXIT_COMMAND)) {RunExitCommand();}
  else
  {
    char externalLinuxCommandFullPath[strlen(PATH_DIRS)+strlen(cmd)+2];
    GetExternalLinuxCommandFullPath(cmd, externalLinuxCommandFullPath);
    if (*externalLinuxCommandFullPath)
    { 
      if (outFileDescriptor!=STDOUT_FILENO) {dup2(outFileDescriptor, STDOUT_FILENO);} 
      if (inFileDescriptor!=STDIN_FILENO) {dup2(inFileDescriptor, STDIN_FILENO);}
      if(execvp(externalLinuxCommandFullPath, pgmlist) == -1) {return;}
    }
    else {fprintf(stderr, UNKNOWN_COMMAND_ERROR_MESSAGE, cmd);}
  }
}


/* 
 * Print a Command structure as returned by parse on stdout. 
 * 
 * Helper function, no need to change. Might be useful to study as inpsiration.
 */
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


/* Print a (linked) list of Pgm:s.
 * 
 * Helper function, no need to change. Might be useful to study as inpsiration.
 */
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


/* Strip whitespace from the start and end of a string. 
 *
 * Helper function, no need to change.
 */
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


bool IsEqual(char* string1, char* string2)
{

  return (strcmp(string1, string2)==0); 
}


void RunCdCommand(char** args)
{
  char* newDir = (*args==NULL) ? getenv(HOME_KEYWORD) : *args; 
  if(chdir(newDir)==-1) { fprintf(stderr, CD_ERROR_MESSAGE, newDir);}  
}


void RunExitCommand() {exit(0);}

    
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


void GetExternalLinuxCommandFullPath(char* cmd, char* externalLinuxCommandFullPath)
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
      AddPaths(dir, cmd, externalLinuxCommandFullPath);
      return;
    }

    dirStartIdx = nextDirStartIdx+1;
  }

  *externalLinuxCommandFullPath = 0;
}


void AddPaths(char* dir, char* filename, char* result)
{
    strcpy(result, dir);
    strcat(result, "/");
    strcat(result, filename);
}


void SigchldHandler(int signum) {waitpid(-1, NULL, WNOHANG);}


void SigintHandlerWhileRunningForgraoundProcess(int signum)
{
  if (getpgid(0)==MAIN_PROCESS_PID && getpid()!=MAIN_PROCESS_PID) {exit(0);}
  if (getpid()==MAIN_PROCESS_PID) {printf("\n");}
}

void SigintHandlerWhileNotRunningForgraoundProcess(int signum)
{
  SigintHandlerWhileRunningForgraoundProcess(signum);
  printf("> ");
}

