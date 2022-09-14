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

#define STDIN_ERROR_MESSAGE "STDIN_ERROR_MESSAGE"
#define STDOUT_ERROR_MESSAGE "STDOUT_ERROR_MESSAGE"

#define CD_COMMAND "cd"

#define HOME_KEYWORD "HOME"
#define PATH_ENV_VAR "PATH"

#define PATH_DIRS getenv(PATH_ENV_VAR)

#define TRUE 1
#define FALSE 0

const char PATH_SEPERATOR = ':';



void RunCommand(int, Command *);
void RunCommandRecursively(Pgm * , int , int , int );
void RunSingleCommand(char **, int , int , int );
void DebugPrintCommand(int, Command *);
void PrintPgm(Pgm *);
void stripwhite(char *);
bool IsEqual(char *, char *);
void RunCdCommand(char **);
bool FileExists(char* );
bool FileExistsInDir(char *, char *);
void GetExternalLinuxCommandFullPath(char *, char *);
void AddPaths(char *, char *, char *);



int main(void)
{
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
      RunCommand(parse_result, &cmd);
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
  DebugPrintCommand(parse_result, cmd);
  
  int in = (cmd->rstdin) ? open(cmd->rstdin, O_RDONLY | O_CREAT) : STDIN_FILENO;
  if (in<0)
  {
    fprintf(stderr, STDIN_ERROR_MESSAGE);
    return;
  }
  int out = (cmd->rstdout) ? open(cmd->rstdout, O_CREAT | O_APPEND | O_WRONLY, S_IRWXU) : STDOUT_FILENO;
  if (out<0)
  {
    fprintf(stderr, STDOUT_ERROR_MESSAGE);
    return;
  }

  RunCommandRecursively(cmd->pgm, in, out, cmd->background);

  if (in!=STDIN_FILENO) {close(in);}
  if (out!=STDOUT_FILENO) {close(out);}
}


void RunCommandRecursively(Pgm* pgm, int inFileDescriptor, int outFileDescriptor, int runInBackground)
{
  if (pgm->next!=NULL)
  {
    int fd[2];
    if (pipe(fd)==-1) {return;}

    int id = fork();
    if (id==0)
    {
      close(fd[0]);
      RunCommandRecursively(pgm->next, inFileDescriptor, fd[1], runInBackground);
      close(fd[1]);
      exit(0);
    }
    else
    {
      close(fd[1]);
      waitpid(id, NULL, 0);
      RunSingleCommand(pgm->pgmlist, fd[0], outFileDescriptor, runInBackground);
      close(fd[0]);
    }
  }
  else
  {   
      RunSingleCommand(pgm->pgmlist, inFileDescriptor, outFileDescriptor, runInBackground);
  }
}

void RunSingleCommand(char **pgmlist, int inFileDescriptor, int outFileDescriptor, int runInBackground)
{
  char* cmd = *pgmlist;
  char** args = pgmlist+1;
  
  if(IsEqual(cmd, CD_COMMAND)) {RunCdCommand(args);}
  else
  {
    char externalLinuxCommandFullPath[strlen(PATH_DIRS)+strlen(cmd)+2];
    GetExternalLinuxCommandFullPath(cmd, externalLinuxCommandFullPath);
    if (externalLinuxCommandFullPath!=NULL)
    {
      int id = fork();
      if (id==0)
      {  
        if (outFileDescriptor!=STDOUT_FILENO) {dup2(outFileDescriptor, STDOUT_FILENO);} 
        if (inFileDescriptor!=STDIN_FILENO) {dup2(inFileDescriptor, STDIN_FILENO);}
        if(execvp(externalLinuxCommandFullPath, pgmlist) == -1) {return;}
      }
      else {waitpid(id, NULL, 0);}
      // free(externalLinuxCommandFullPath); //////
    }
  }
  return;
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
  if(chdir(newDir)==-1) { printf("failed to change dir to %s\n", newDir);}
  return;
}

    
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
    if (FileExistsInDir(cmd, dir)) {AddPaths(dir, cmd, externalLinuxCommandFullPath);}

    dirStartIdx = nextDirStartIdx+1;
  }

  externalLinuxCommandFullPath = NULL;
  return;
}


void RunExternalCommand(char* cmd, char** args)
{

}


void AddPaths(char* dir, char* filename, char* result)
{
    strcpy(result, dir);
    strcat(result, "/");
    strcat(result, filename);
    return;
}