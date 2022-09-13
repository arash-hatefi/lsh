#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include <stdbool.h>



#define PATH_ENV_VAR "PATH"
#define PATH_DIRS getenv(PATH_ENV_VAR)

#define TRUE 1
#define FALSE 0

const char PATH_SEPERATOR = ':';


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


bool CmdExists(char* cmd)
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
    if (FileExistsInDir(cmd, dir)) {return TRUE;}

    dirStartIdx = nextDirStartIdx+1;
  }

  return FALSE;
}


int main()
{
  char *line;

  while(line=readline("> "))
  {
    printf("cmd: %s\texists: %d\n", line, (int) CmdExists(line));
  }
  return 0;
}