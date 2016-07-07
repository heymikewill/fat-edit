/***
 * Author: Mike Williams
 * Class: COP4610
 * Project: 4
 * File: fat-edit.c
 ***/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#define BUFFER_SIZE 128
#define LCD_SSIZE 512
#define READ_ONLY 0x01
#define LONG_DIRECTORY 0x0F
#define SUB_DIRECTORY 0x10
#define EoC 0x0FFFFFF8
#define EMPTY 0x00000000
#define FREE 0xFFFFFFE5


/*** PROTOTYPES ***/
void init_env(char* file);
void prompt();
void read_input();
void clear_buffer();
void execute();
void usage_error(char *cmd);

void fat_info();
int fat_open(char *file_name, char *mode);
int fat_close(char *file_name);
int fat_create(char *file_name);
int fat_read(char *file_name, unsigned int start_pos, unsigned int num_bytes);
int fat_write(char *file_name, unsigned int start_pos, char *quoted_data);
int fat_rm(char *file_name, int clear);
int fat_cd(char *dir_name);
int fat_ls(char *dir_name);
int fat_mkdir(char *dir_name);
int fat_rmdir(char *dir_name);
int fat_size(char *file_name);

int seekTo(int location);
unsigned int firstSectorOfCluster(int n);
unsigned int getNextCluster(int entryIndex);
unsigned int combineShorts(unsigned short high, unsigned short low);
unsigned int newCluster(unsigned int linkedCluster);
unsigned int newDirectoryCluster();
void clearClusterChain(unsigned int startCluster);
void convertFilename(char *filename);
void removeTailWhitespace(char *filename);

/*** GLOBALS ***/
int imageid;
int sizeFAT, rootLoc, rootCluster, firstDataSector, numTotalSectors,
    currentCluster, bytesPerCluster, nextFreeLocation, numFreeSectors;
unsigned short bytesPerSector, reservedSectorCount, fsinfo;
char sectorsPerCluster, numFATs;
char psector[LCD_SSIZE];
char name[8];

int stay_alive;
char *username;
char *imagename;
char *buffer;
char *command;
char **command_args;
int num_command_args;

// open file table
typedef struct {
  char name[11];
  char attr;
  int mode;
  unsigned int firstCluster;
} open_file;
open_file *openFT;
int openFT_count;

/*** MAIN FUNCTION ***/
int main(int argc, char **argv) {
  // check for proper argument syntax
  if (argc != 2) {
    printf("Bad argument syntax.\n");
    printf("Usage: fat-edit <fs_image.img>\n");
    return 0;
  }

  // initialize environment
  init_env(argv[1]);

  while (stay_alive) {
    clear_buffer();
    prompt();
    read_input();
    if (strlen(buffer) != 0) {
      execute();
    }
  }

  return 0;
}

/** init_env - initializes the working environment for the FAT32 utility
 **/
void init_env(char* file) {
  char temp[4];

  username = getenv("USER");
  imagename = file;

  buffer = NULL;
  stay_alive = 1;

  // open file image
  imageid = open(imagename, O_RDWR);
  // seek to boot sector
  seekTo(0);
  // read in boot sector bytes
  read(imageid, psector, LCD_SSIZE);

  // copy over information from the appropriate offsets
  memcpy(name,&psector[3],8);      
  memcpy(&bytesPerSector, &psector[11], 2);
  memcpy(&sectorsPerCluster, &psector[13], 1);
  memcpy(&reservedSectorCount, &psector[14], 2);
  memcpy(&numFATs, &psector[16], 1);
  memcpy(&numTotalSectors, &psector[32], 4);
  memcpy(&sizeFAT, &psector[36], 4);
  memcpy(&rootCluster, &psector[44], 4);
  memcpy(&fsinfo, &psector[48], 2);

  // minor calculations
  bytesPerCluster = sectorsPerCluster*bytesPerSector;
  nextFreeLocation = 0;

  // calculate the location of the root directory
  firstDataSector = reservedSectorCount + ((int)numFATs * sizeFAT);
  rootLoc = firstSectorOfCluster(rootCluster);
  currentCluster = rootCluster;

  // free cluster information
  seekTo(fsinfo*bytesPerSector + 488);
  read(imageid, temp, 4);
  memcpy(&numFreeSectors, &temp, 4);

  // seek to the root directory
  seekTo(rootLoc*bytesPerSector);
  // read in the root directory
  read(imageid, psector, LCD_SSIZE);

  // initialize open file table
  openFT = NULL;
  openFT_count = 0;
}

/** prompt - prints out an informative prompt for the user
 **/
void prompt() {
  printf("%s(%s)> ",username,imagename);
}

/** read_input - reads input from the user and parses it accordingly
 **/
void read_input() {
  buffer = (char*)malloc((BUFFER_SIZE+1)*sizeof(char));
  fgets(buffer,BUFFER_SIZE,stdin);

  // remove tail newline/return
  int i;
  for (i = 0; i < BUFFER_SIZE; i++) {
    if (buffer[i] == '\n' || buffer[i] == '\r')
      buffer[i] = 0;
  }

  // parse command and arguments
  char *token, *temp;
  temp = NULL;
  for (i = 0, token = strtok(strdup(buffer)," ");
       token != NULL;
       token = strtok(NULL," "), i++) {
    // first token is command
    if (i == 0) {
      command = strdup(token);
    }
    // check for invalid character
    else if (i == 1 && strchr(token,'/') != NULL) {
      printf("fat-edit: Invalid character \'/\' detected.\n");
      free(buffer);
      buffer = (char*)malloc(sizeof(char));
      buffer[0] = 0;
      return;
    }
    // other tokens are arguments
    else {
      // check for quoted data
      if (token[0] == '"') {
        temp = (char*)malloc(strlen(token)*sizeof(char));
        strcpy(temp,token+1);
        if (temp[strlen(temp)-1] != '"') {
          temp[strlen(token+1)] = ' ';
          token = strtok(NULL,"\"");
          temp = (char*)realloc(temp,(strlen(temp)+strlen(token)+1)*sizeof(char));
          strcat(temp,token);
        }
        else
          temp[strlen(temp)-1] = 0;
        token = temp;
        strtok(NULL," ");
      }

      // make room for argument
      command_args = (char**)realloc(command_args,++num_command_args*sizeof(char*));
      command_args[i-1] = strdup(token);
    }
  }

  // free temp string if used
  if (temp != NULL)
    free(temp);
}

/** clear_buffer - empties the input buffer for the next input
 **/
void clear_buffer() {
  free(buffer);
  buffer = NULL;

  free(command);
  command = NULL;

  int i;
  for (i = num_command_args-1; i >= 0; i--) {
    free(command_args[i]);
    command_args[i] = NULL;
  }
  num_command_args = 0;

  command_args = (char**)malloc(sizeof(char*));
  command_args[0] = NULL;
}

/** execute - determines the proper command and prints out result output
 **/
void execute() {
  int result;
  char *open_mode;

  // exit
  if (strcmp(command,"exit") == 0) {
    if (num_command_args != 0)
      usage_error("exit");
    else
      stay_alive = 0;
  }
  // fsinfo
  else if (strcmp(command,"fsinfo") == 0) {
    if (num_command_args != 0)
      usage_error("fsinfo");
    else {
      fat_info();
    }
  }
  // open
  else if (strcmp(command,"open") == 0) {
    if (num_command_args != 2)
      usage_error("open");
    else {
      result = fat_open(command_args[0],command_args[1]);
      switch (result) {
        case 0: if (strcmp(command_args[1],"r") == 0) open_mode = "reading";
                else if (strcmp(command_args[1],"w") == 0) open_mode = "writing";
                else if (strcmp(command_args[1],"rw") == 0 ||
                         strcmp(command_args[1],"wr") == 0) open_mode = "reading and writing";
                printf("%s has been opened for %s.\n",command_args[0],open_mode);
                break;
        case 1: printf("fat-edit: open: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: open: %s is already open.\n",command_args[0]); break;
        case 3: printf("fat-edit: open: %s is not a file.\n",command_args[0]); break;
        case 4: printf("fat-edit: open: Invalid open mode.\n"); break;
        case 5: printf("fat-edit: open: Permission denied - file is read-only.\n"); break;
        default: break;
      }
    }
  }
  // close
  else if (strcmp(command,"close") == 0) {
    if (num_command_args != 1)
      usage_error("close");
    else {
      result = fat_close(command_args[0]);
      switch (result) {
        case 0: printf("%s has been closed.\n",command_args[0]); break;
        case 1: printf("fat-edit: close: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: close: %s isn't open.\n",command_args[0]); break;
        case 3: printf("fat-edit: close: %s is not a file.\n",command_args[0]); break;
        default: break;
      }
    }
  }
  // create
  else if (strcmp(command,"create") == 0) {
    if (num_command_args != 1)
      usage_error("create");
    else {
      result = fat_create(command_args[0]);
      switch (result) {
        case 1: printf("fat-edit: create: %s already exists.\n",command_args[0]); break;
        case 2: printf("fat-edit: create: %s is a directory.\n",command_args[0]); break;
        case 3: printf("fat-edit: create: FAT32 volume ran out of space.\n"); break;
        default: break;
      }
    }
  }
  // read
  else if (strcmp(command,"read") == 0) {
    if (num_command_args != 3)
      usage_error("read");
    else {
      result = fat_read(command_args[0],atoi(command_args[1]),atoi(command_args[2]));
      switch (result) {
        case 1: printf("fat-edit: read: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: read: %s isn't open.\n",command_args[0]); break;
        case 3: printf("fat-edit: read: %s doesn't have read permission.\n",command_args[0]); break;
        case 4: printf("fat-edit: read: %s is not a file.\n",command_args[0]); break;
        case 5: printf("fat-edit: read: Start position beyond EoF.\n"); break;
        default: break;
      }
    }
  }
  // write
  else if (strcmp(command,"write") == 0) {
    if (num_command_args != 3)
      usage_error("write");
    else {
      result = fat_write(command_args[0],atoi(command_args[1]),command_args[2]);
      switch (result) {
        case 0: printf("Wrote \"%s\" @ %d of length %d to %s\n",command_args[2],
                                                                atoi(command_args[1]),
                                                                (int)strlen(command_args[2]),
                                                                command_args[0]);
                                                                break;
        case 1: printf("fat-edit: write: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: write: %s isn't open.\n",command_args[0]); break;
        case 3: printf("fat-edit: write: %s doesn't have write permission.\n",command_args[0]); break;
        case 4: printf("fat-edit: write: %s is not a file.\n",command_args[0]); break;
        case 5: printf("fat-edit: write: Start position beyond EoF.\n"); break;
        case 6: printf("fat-edit: write: FAT32 volume ran out of space.\n"); break;
        default: break;
      }
    }
  }
  // rm
  else if (strcmp(command,"rm") == 0) {
    if (num_command_args != 1)
      usage_error("rm");
    else {
      result = fat_rm(command_args[0],0);
      switch (result) {
        case 1: printf("fat-edit: rm: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: rm: %s is a directory.\n",command_args[0]); break;
        default: break;
      }
    }
  }
  // srm
  else if (strcmp(command,"srm") == 0) {
    if (num_command_args != 1)
      usage_error("srm");
    else {
      result = fat_rm(command_args[0],1);
      switch (result) {
        case 1: printf("fat-edit: srm: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: srm: %s is a directory.\n",command_args[0]); break;
        default: break;
      }
    }
  }
  // cd
  else if (strcmp(command,"cd") == 0) {
    if (num_command_args != 1)
      usage_error("cd");
    // check if parent call is in root already
    else if (strcmp(command_args[0],"..") == 0 &&
             currentCluster == rootCluster)
      printf("fat-edit: cd: Root directory has no parent.\n");
    else {
      result = fat_cd(command_args[0]);
      switch (result) {
        case 1: printf("fat-edit: cd: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: cd: %s is not a directory.\n",command_args[0]); break;
        default: break;
      }
    }
  }
  // ls
  else if (strcmp(command,"ls") == 0) {
    if (num_command_args != 1)
      usage_error("ls");
    // check if parent call is in root already
    else if (strcmp(command_args[0],"..") == 0 &&
             currentCluster == rootCluster)
      printf("fat-edit: ls: Root directory has no parent.\n");
    else {
      result = fat_ls(command_args[0]);
      switch (result) {
        case 1: printf("fat-edit: ls: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: ls: %s is not a directory.\n",command_args[0]); break;
        default: break;
      }
    }
  }
  // mkdir
  else if (strcmp(command,"mkdir") == 0) {
    if (num_command_args != 1)
      usage_error("mkdir");
    else {
      result = fat_mkdir(command_args[0]);
      switch (result) {
        case 1: printf("fat-edit: mkdir: %s is already a file.\n",command_args[0]); break;
        case 2: printf("fat-edit: mkdir: %s already exists.\n",command_args[0]); break;
        case 3: printf("fat-edit: mkdir: FAT32 volume ran out of space.\n"); break;
        default: break;
      }
    }
  }
  // rmdir
  else if (strcmp(command,"rmdir") == 0) {
    if (num_command_args != 1)
      usage_error("rmdir");
    else if (strcmp(command_args[0],".") == 0)
      printf("fat-edit: rm: Cannot delete current working directory.\n");
    else if (strcmp(command_args[0],"..") == 0)
      printf("fat-edit: rm: Cannot delete parent directory.\n");
    else {
      result = fat_rmdir(command_args[0]);
      switch (result) {
        case 1: printf("fat-edit: rmdir: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: rmdir: %s is not a directory.\n",command_args[0]); break;
        case 3: printf("fat-edit: rmdir: %s is not empty.\n",command_args[0]); break;
        default: break;
      }
    }
  }
  // size
  else if (strcmp(command,"size") == 0) {
    if (num_command_args != 1)
      usage_error("size");
    else {
      result = fat_size(command_args[0]);
      switch (result) {
        case 1: printf("fat-edit: size: %s doesn't exist.\n",command_args[0]); break;
        case 2: printf("fat-edit: size: %s is not a file.\n",command_args[0]); break;
        default: break;
      }
    }
  }
  // unknown command
  else {
    printf("fat-edit: Command not found: %s\n",command);
  }
}

/** usage_error - prints out an error upon improper argument usage
                  for a given command
 **/
void usage_error(char *cmd) {
  printf("fat-edit: %s: Improper argument usage.\n",cmd);
}

/** fat_info - prints out important information relating to the FAT32 volume
 **/
void fat_info() {
  printf("Bytes per sector: %hd\n", bytesPerSector);
  printf("Sectors per cluster: %d\n", sectorsPerCluster);
  printf("Total number of sectors: %d\n", numTotalSectors);
  printf("Number of free sectors: %d\n", numFreeSectors);
  printf("Number of FATs: %d\n", numFATs);
  printf("Sectors per FAT: %d\n", sizeFAT);
}

/** fat_open - open a file with the given mode
 **/
int fat_open(char *file_name, char *mode) {
  char *filename = (char*)malloc(strlen(file_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, j, nextCluster, originalCluster;

  // invalid mode
  if (strcmp(mode,"r") != 0 &&
      strcmp(mode,"w") != 0 &&
      strcmp(mode,"rw") != 0 &&
      strcmp(mode,"wr") != 0) return 4;

  result = -1;
  strcpy(filename,file_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
             attrLongName != LONG_DIRECTORY &&
             strcmp(entry_filename,filename) == 0) {
      // file is a directory
      if (attrLongName == SUB_DIRECTORY)
	      result = 3;
      else if (attrLongName == READ_ONLY &&
               strcmp(mode,"r") != 0)
        result = 5;
      // file found
      else {
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);

        // check if file is open already
        for (j = 0; j < openFT_count && result == -1; j++)
          if (openFT[j].firstCluster == combineShorts(clusHigh,clusLow))
            result = 2;

        // open file if not already open
        if (result == -1) {
          openFT = (open_file*)realloc(openFT,++openFT_count*sizeof(open_file));
          memcpy(&openFT[openFT_count-1].name, &psector[32*i], 11);
          memcpy(&openFT[openFT_count-1].attr, &psector[11 + 32*i], 1);
          openFT[openFT_count-1].firstCluster = combineShorts(clusHigh,clusLow);
          if (strcmp(mode,"r") == 0)
            openFT[openFT_count-1].mode = O_RDONLY;
          else if (strcmp(mode,"w") == 0)
            openFT[openFT_count-1].mode = O_WRONLY;
          else
            openFT[openFT_count-1].mode = O_RDWR;

	        result = 0;
        }
      }
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }
  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_close - closes an open file
 **/
int fat_close(char *file_name) {
  char *filename = (char*)malloc(strlen(file_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, j, nextCluster, originalCluster;

  result = -1;
  strcpy(filename,file_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);

    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
             attrLongName != LONG_DIRECTORY &&
             strcmp(entry_filename,filename) == 0) {
      // file is a directory
      if (attrLongName == SUB_DIRECTORY)
	      result = 3;
      // file found
      else {
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);

        // check if file is open
        for (j = 0; j < openFT_count && result == -1; j++)
          if (openFT[j].firstCluster == combineShorts(clusHigh,clusLow)) {
            int remove;
            for (remove = j; remove < openFT_count; remove++)
              openFT[remove] = openFT[++j];
            openFT = (open_file*)realloc(openFT,--openFT_count*sizeof(open_file));
            result = 0;
          }

        if (result == -1)
          result = 2;
      }
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }
  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_create - creats a new empty file in the current directory tree
 **/
int fat_create(char *file_name) {
  char *filename = (char*)malloc(strlen(file_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  char attrLongName;
  int result, i, nextCluster, originalCluster, freeEntryIndex;
  unsigned int filesize;
  char filesize_raw[4];

  result = freeEntryIndex = -1;
  strcpy(filename,file_name);
  convertFilename(filename);
  originalCluster = currentCluster;
  filesize = 0;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // set first free entry index for creation
    if ((entry_filename[0] == FREE || entry_filename[0] == 0x00) &&
        freeEntryIndex == -1)
      freeEntryIndex = i;

    // ignore empty entries and long entry names
    if (entry_filename[0] != FREE &&
        entry_filename[0] != 0x00 &&
        attrLongName != LONG_DIRECTORY &&
        strcmp(entry_filename,filename) == 0) {

      // file is a directory
      if (attrLongName == SUB_DIRECTORY)
	      result = 2;
      // file exists already
      else
        result = 1;
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }

  // continue if no failures occured
  if (result == -1) {
    // check for no more room in current cluster
    if (freeEntryIndex == -1) {
      // allocate new cluster
      nextCluster = newCluster(currentCluster);
      // check for out of space
      if (nextCluster == 0) {
        seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
        currentCluster = originalCluster;
        read(imageid, psector, LCD_SSIZE);
        return 3;
      }

      // go to new cluster
      seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
      currentCluster = nextCluster;
      freeEntryIndex = 0;
    }
    
    // write filename
    seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector + freeEntryIndex*32);
    write(imageid, filename, 11);
    // write filesize
    seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector + (28 + freeEntryIndex*32));
    memcpy(&filesize_raw, &filesize, 4);
    write(imageid, filesize_raw, 4);

    result = 0;
  }

  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_read - reads a certain number of bytes of information from the
               given file starting at the requested location
 **/
int fat_read(char *file_name, unsigned int start_pos, unsigned int num_bytes) {
  char *filename = (char*)malloc(strlen(file_name)*sizeof(char));
  char data;
  int bytesToRead;
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, j, nextCluster, originalCluster, openFT_index;
  unsigned int filesize;

  result = -1;
  strcpy(filename,file_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
             attrLongName != LONG_DIRECTORY &&
             strcmp(entry_filename,filename) == 0) {
      // file is a directory
      if (attrLongName == SUB_DIRECTORY)
	      result = 4;
      // file found
      else {
        // get cluster values
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);

        // check if file is open already
        for (j = 0; j < openFT_count && result == -1; j++)
          // file is open
          if (openFT[j].firstCluster == combineShorts(clusHigh,clusLow)) {
            // check for read permissions
            if (openFT[j].mode != O_RDONLY &&
                openFT[j].mode != O_RDWR)
              result = 3;
            // file is allowed to read
            else {
              openFT_index = j;
              result = 0;
            }
          }

        // file is not open
        if (result == -1)
          result = 2;
        // file is open and allowed to read
        else if (result == 0) {
          // get file size
          memcpy(&filesize, &psector[28 + 32*i], 4);

          // check for start position beyond EoF
          if (start_pos >= filesize)
            result = 5;
          // ready to read
          else {
            // find first cluster
            nextCluster = openFT[openFT_index].firstCluster;
            // determine if more clusters need to be read until start position
            while (start_pos >= bytesPerCluster) {
              nextCluster = getNextCluster(nextCluster);
              start_pos -= bytesPerCluster;
              filesize -= bytesPerCluster;
            }

            // go to data section and begin reading from appropriate start position
            seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
            currentCluster = nextCluster;
            read(imageid, psector, LCD_SSIZE);
            bytesToRead = num_bytes;
            for (j = 0; j < start_pos+num_bytes && bytesToRead && filesize > 0; j++, filesize--) {
              if (j >= start_pos) {
                memcpy(&data, &psector[j], 1);
                printf("%c",data);
                bytesToRead--;
              }
              // check for end end of cluster before finished reading
              if (j == LCD_SSIZE-1 && bytesToRead) {
                nextCluster = getNextCluster(currentCluster);
                // check for EoF
                if (nextCluster >= EoC) {
                  printf("\nfat-edit: read: EoF reached.");
                  break;
                }
                // more valid data, seek to it
                else {
                  seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
                  currentCluster = nextCluster;
                  read(imageid, psector, LCD_SSIZE);
                  j = -1;
                  start_pos = 0;
                  num_bytes -= (num_bytes-bytesToRead);
                }
              }
            }
            printf("\n");
            if (filesize == 0)
              printf("fat-edit: read: EoF reached.\n");
          }
        }
      }
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }
  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_write - writes a certain number of bytes of information to the
                given file starting at the requested location
 **/
int fat_write(char *file_name, unsigned int start_pos, char *quoted_data) {
  char *filename = (char*)malloc(strlen(file_name)*sizeof(char));
  int bytesToWrite;
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, j, nextCluster, originalCluster, entryCluster, openFT_index, final_pos;
  unsigned int filesize;

  result = -1;
  strcpy(filename,file_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
             attrLongName != LONG_DIRECTORY &&
             strcmp(entry_filename,filename) == 0) {
      entryCluster = currentCluster;

      // file is a directory
      if (attrLongName == SUB_DIRECTORY)
	      result = 4;
      // file found
      else {
        // get cluster values
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);

        // check if file is open already
        for (j = 0; j < openFT_count && result == -1; j++)
          // file is open
          if (openFT[j].firstCluster == combineShorts(clusHigh,clusLow)) {
            // check for write permissions
            if (openFT[j].mode != O_WRONLY &&
                openFT[j].mode != O_RDWR)
              result = 3;
            // file is allowed to write
            else {
              openFT_index = j;
              result = 0;
            }
          }

        // file is not open
        if (result == -1)
          result = 2;
        // file is open and allowed to write
        else if (result == 0) {
          // get file size
          memcpy(&filesize, &psector[28 + 32*i], 4);
          // recompute filesize if necessary
          if ((start_pos+strlen(quoted_data)) > filesize)
            filesize = start_pos + strlen(quoted_data);

          // load first cluster from file table
          nextCluster = openFT[openFT_index].firstCluster;
          // determine if more clusters need to be read until start position
          while (start_pos >= bytesPerCluster) {
            nextCluster = getNextCluster(nextCluster);
            // check for new allocation
            if (nextCluster >= EoC) {
              nextCluster = newCluster(currentCluster);
              // check for out of space
              if (nextCluster == 0) {
                seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
                currentCluster = originalCluster;
                read(imageid, psector, LCD_SSIZE);
                return 6;
              }
            }
            start_pos -= bytesPerCluster;
          }

          // go to data section and begin writing from appropriate start position
          seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
          currentCluster = nextCluster;
          bytesToWrite = strlen(quoted_data);
          final_pos = start_pos + bytesToWrite;
          for (j = 0; j < final_pos; j++) {
            if (j >= start_pos) {
              write(imageid,&quoted_data[j-start_pos],1);
              bytesToWrite--;
            }
            else
              seekTo((firstSectorOfCluster(currentCluster)*bytesPerSector)+(j+1));

            // check for end end of cluster before finished writing
            if (j == LCD_SSIZE-1 && bytesToWrite) {
              nextCluster = getNextCluster(currentCluster);
              // check for EoC
              if (nextCluster >= EoC)
                nextCluster = newCluster(currentCluster);

              seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
              currentCluster = nextCluster;
              j = -1;
              start_pos = 0;
            }
          }
          // write new file information to directory entry
          memcpy(&psector[28 + 32*i], &filesize, 4);
          seekTo(firstSectorOfCluster(entryCluster)*bytesPerSector);
          write(imageid, psector, LCD_SSIZE);
        }
      }
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }
  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_rm - deletes a file in the current directory
 **/
int fat_rm(char *file_name, int clear) {
  char *filename = (char*)malloc(strlen(file_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  char zero[1];
  zero[0] = 0x00;
  int result, i, j, nextCluster, originalCluster, firstDataCluster, entryCluster;

  result = -1;
  strcpy(filename,file_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);

    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
        attrLongName != LONG_DIRECTORY &&
        strcmp(entry_filename,filename) == 0) {

      // file is a directory
      if (attrLongName == SUB_DIRECTORY)
	      result = 2;
      // file found
      else {
        // get cluster values
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);
        firstDataCluster = combineShorts(clusHigh,clusLow);

        // delete directory entry properly
        if (i != 64-1) {
          memcpy(&entry_filename, &psector[32*(i+1)], 11);
          if (entry_filename[0] != 0x00) {
            memcpy(&entry_filename, &psector[32*i], 11);
            entry_filename[0] = 0xE5;
          } else {
            memcpy(&entry_filename, &psector[32*i], 11);
            entry_filename[0] = 0x00;
          }
        }
        else
          entry_filename[0] = 0x00;

        // check for data removal
        if (clear) {
          entryCluster = currentCluster;
          seekTo(firstSectorOfCluster(firstDataCluster)*bytesPerSector);
          currentCluster = firstDataCluster;
          for (j = 0; j < bytesPerSector*sectorsPerCluster; j++) {
            write(imageid, zero, 1);
            if (j == (bytesPerSector*sectorsPerCluster)-1) {
              nextCluster = getNextCluster(currentCluster);
              if (nextCluster >= EoC)
                break;
              seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
              currentCluster = nextCluster;
              j = -1;
            }
          }
          seekTo(firstSectorOfCluster(entryCluster)*bytesPerSector);
          currentCluster = entryCluster;
        }

        // clear data cluster chain
        clearClusterChain(firstDataCluster);

        // set directory entry free
        memcpy(&psector[32*i], &entry_filename, 11);
        if (clear) {
          for (j = 1; j < 32; j++)
            memcpy(&psector[32*i+j], &zero, 1);
        }
        seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector);
        write(imageid, psector, LCD_SSIZE);

        result = 0;
      }
    }


    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster; 
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }

  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_cd - changes the current working directory to the specified one
 **/
int fat_cd(char *dir_name) {
  char *filename = (char*)malloc(strlen(dir_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, nextCluster, originalCluster;

  result = -1;
  strcpy(filename,dir_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
             attrLongName != LONG_DIRECTORY &&
             strcmp(entry_filename,filename) == 0) {
      // file is a directory
      if (attrLongName == SUB_DIRECTORY) {
        // get cluster values
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);

        // check for next cluster being root
        nextCluster = combineShorts(clusHigh,clusLow);
        if (nextCluster == 0)
          nextCluster = rootCluster;

        // seek to new directory
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);

	      result = 0;
      }
      // entry is a file
      else
        result = 2;
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }
  free(filename);

  // return to original directory only on error
  if (result != 0) {
    seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
    currentCluster = originalCluster;
    read(imageid, psector, LCD_SSIZE);
  }

  return result;
}

/** fat_ls - lists the contents of a given directory
 **/
int fat_ls(char *dir_name) {
  char *filename = (char*)malloc(strlen(dir_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, nextCluster, originalCluster;

  result = -1;
  strcpy(filename,dir_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // check for root directory
  if (strcmp(dir_name,".") == 0 &&
      currentCluster == rootCluster) {
    for (i = 0; i < 64; ++i) {
      memcpy(&entry_filename, &psector[32*i], 11);
      memcpy(&attrLongName, &psector[11 + 32*i], 1);

      // no more entries
      if (entry_filename[0] == 0x00)
        break;
      if (entry_filename[0] != FREE && attrLongName != LONG_DIRECTORY) {
        removeTailWhitespace(entry_filename);
	      printf("%s   ", entry_filename);
      }
    }
    printf("\n");
  }
  // navigate through current directory
  else for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
             attrLongName != LONG_DIRECTORY &&
             strcmp(entry_filename,filename) == 0) {
      // found directory
      if (attrLongName == SUB_DIRECTORY) {
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);

        // check for next cluster being root
        nextCluster = combineShorts(clusHigh,clusLow);
        if (nextCluster == 0)
          nextCluster = rootCluster;

        // seek to new directory
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);

        // print out entry names in new directory
        for (i = 0; i < 64; ++i) {
          memcpy(&entry_filename, &psector[32*i], 11);
          memcpy(&attrLongName, &psector[11 + 32*i], 1);

          // no more entries
          if (entry_filename[0] == 0x00)
            break;
          if (entry_filename[0] != FREE && attrLongName != LONG_DIRECTORY) {
            removeTailWhitespace(entry_filename);
	          printf("%s   ", entry_filename);
          }

          // check for more clusters
          if (i == 64-1) {
            nextCluster = getNextCluster(currentCluster);
            if (nextCluster < EoC) {
              seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
              currentCluster = nextCluster;
              read(imageid, psector, LCD_SSIZE);
              i = 0;
            }
          }
        }
        printf("\n");
	      result = 0;
      }
      // entry is a file
      else
        result = 2;
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster != EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }
  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_mkdir - creates a new directory in the current directory
 **/
int fat_mkdir(char *dir_name) {
  char *filename = (char*)malloc(strlen(dir_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, nextCluster, originalCluster, freeEntryIndex, allocatedCluster;
  unsigned int filesize;
  char filesize_raw[4];
  char directory_attribute[1];
  char short_buffer[2];

  result = freeEntryIndex = -1;
  strcpy(filename,dir_name);
  convertFilename(filename);
  originalCluster = currentCluster;
  filesize = 0;
  directory_attribute[0] = SUB_DIRECTORY;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // set first free entry index for creation
    if ((entry_filename[0] == FREE || entry_filename[0] == 0x00) &&
        freeEntryIndex == -1)
      freeEntryIndex = i;

    // ignore empty entries and long entry names
    if (entry_filename[0] != FREE &&
        entry_filename[0] != 0x00 &&
        attrLongName != LONG_DIRECTORY &&
        strcmp(entry_filename,filename) == 0) {

      // directory exists already
      if (attrLongName == SUB_DIRECTORY)
	      result = 2;
      // file exists already
      else
        result = 1;
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }

  // continue if no failures occured
  if (result == -1) {
    // check for no more room in current cluster
    if (freeEntryIndex == -1) {
      // allocate new cluster
      nextCluster = newCluster(currentCluster);
      // check for out of space
      if (nextCluster == 0) {
        seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
        currentCluster = originalCluster;
        read(imageid, psector, LCD_SSIZE);
        return 3;
      }

      // go to new cluster
      seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
      currentCluster = nextCluster;
      freeEntryIndex = 0;
    }

    // check for room for new directory entry cluster
    allocatedCluster = newDirectoryCluster();
    if (allocatedCluster == 0) {
      seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
      currentCluster = originalCluster;
      read(imageid, psector, LCD_SSIZE);
      return 3;
    }

    // NEW_DIRECTORY
    // write filename
    seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector + freeEntryIndex*32);
    write(imageid, filename, 11);
    write(imageid, directory_attribute, 1);
    // write cluster locations
    clusHigh = allocatedCluster >> 16;
    clusLow = allocatedCluster & 0xFF;
    seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector + (20 + freeEntryIndex*32));
    memcpy(&short_buffer, &clusHigh, 2);
    write(imageid, short_buffer, 2);
    seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector + (26 + freeEntryIndex*32));
    memcpy(&short_buffer, &clusLow, 2);
    write(imageid, short_buffer, 2);
    // write filesize
    memcpy(&filesize_raw, &filesize, 4);
    write(imageid, filesize_raw, 4);

    free(filename);
    // NEW_DIRECTORY/.
    // write filename
    filename = ".          ";
    seekTo(firstSectorOfCluster(allocatedCluster)*bytesPerSector);
    write(imageid, filename, 11);
    write(imageid, directory_attribute, 1);
    // write cluster locations
    clusHigh = allocatedCluster >> 16;
    clusLow = allocatedCluster & 0xFF;
    seekTo(firstSectorOfCluster(allocatedCluster)*bytesPerSector + 20);
    memcpy(&short_buffer, &clusHigh, 2);
    write(imageid, short_buffer, 2);
    seekTo(firstSectorOfCluster(allocatedCluster)*bytesPerSector + 26);
    memcpy(&short_buffer, &clusLow, 2);
    write(imageid, short_buffer, 2);
    // write filesize
    memcpy(&filesize_raw, &filesize, 4);
    write(imageid, filesize_raw, 4);

    // NEW_DIRECTORY/..
    // write filename
    filename = "..         ";
    seekTo(firstSectorOfCluster(allocatedCluster)*bytesPerSector + 32);
    write(imageid, filename, 11);
    write(imageid, directory_attribute, 1);
    // write cluster locations
    clusHigh = currentCluster >> 16;
    clusLow = currentCluster & 0xFF;
    seekTo(firstSectorOfCluster(allocatedCluster)*bytesPerSector + 20 + 32);
    memcpy(&short_buffer, &clusHigh, 2);
    write(imageid, short_buffer, 2);
    seekTo(firstSectorOfCluster(allocatedCluster)*bytesPerSector + 26 + 32);
    memcpy(&short_buffer, &clusLow, 2);
    write(imageid, short_buffer, 2);
    // write filesize
    memcpy(&filesize_raw, &filesize, 4);
    write(imageid, filesize_raw, 4);

    filename = NULL;
    result = 0;
  }

  if (filename != NULL)
    free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_rmdir - removes the given directory from the current directory
 **/
int fat_rmdir(char *dir_name) {
  char *filename = (char*)malloc(strlen(dir_name)*sizeof(char));
  char entry_filename[12];
  entry_filename[11] = 0;
  unsigned short clusHigh, clusLow;
  char attrLongName;
  int result, i, j, nextCluster, originalCluster, firstDataCluster;

  result = -1;
  strcpy(filename,dir_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);

    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
        attrLongName != LONG_DIRECTORY &&
        strcmp(entry_filename,filename) == 0) {

      // directory found
      if (attrLongName == SUB_DIRECTORY) {
	      // get cluster values
        memcpy(&clusHigh, &psector[20 + 32*i], 2);
        memcpy(&clusLow, &psector[26 + 32*i], 2);
        firstDataCluster = combineShorts(clusHigh,clusLow);

        // check for empty directory
        seekTo(firstSectorOfCluster(firstDataCluster)*bytesPerSector);
        read(imageid, psector, LCD_SSIZE);
        for (j = 2; j < 64 && result == -1; j++) {
          memcpy(&entry_filename, &psector[32*j], 11);
          memcpy(&attrLongName, &psector[11 + 32*j], 1);
          if (entry_filename[0] == 0x00)
            break;
          else if (entry_filename[0] != FREE && attrLongName != LONG_DIRECTORY)
            result = 3;
        }
        seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector);
        read(imageid, psector, LCD_SSIZE);
        if (result != -1)
          break;

        // delete directory entry properly
        if (i != 64-1) {
          memcpy(&entry_filename, &psector[32*(i+1)], 11);
          if (entry_filename[0] != 0x00) {
            memcpy(&entry_filename, &psector[32*i], 11);
            entry_filename[0] = 0xE5;
          } else {
            memcpy(&entry_filename, &psector[32*i], 11);
            entry_filename[0] = 0x00;
          }
        }
        else
          entry_filename[0] = 0x00;

        // clear data cluster chain
        clearClusterChain(firstDataCluster);

        // set directory entry free
        memcpy(&psector[32*i], &entry_filename, 11);
        seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector);
        write(imageid, psector, LCD_SSIZE);

        result = 0;
      }
      // file is not a directory
      else
        result = 2;
    }


    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster < EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster; 
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }

  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** fat_size - prints out the size of a file in bytes
 **/
int fat_size(char *file_name) {
  char *filename = (char*)malloc(strlen(file_name)*sizeof(char));
  char entry_filename[12];
  char attrLongName;
  int result, i, nextCluster, originalCluster;
  unsigned int filesize;

  result = -1;
  strcpy(filename,file_name);
  convertFilename(filename);
  originalCluster = currentCluster;

  // navigate through current directory
  for (i = 0; i < 64 && result == -1; ++i) {
    memcpy(&entry_filename, &psector[32*i], 11);
    memcpy(&attrLongName, &psector[11 + 32*i], 1);
    
    // no more entries
    if (entry_filename[0] == 0x00) {
      result = 1;
    }
    // ignore empty entries and long entry names
    else if (entry_filename[0] != FREE &&
             attrLongName != LONG_DIRECTORY &&
             strcmp(entry_filename,filename) == 0) {
      // file is a directory
      if (attrLongName == SUB_DIRECTORY)
	      result = 2;
      // file found
      else {
        memcpy(&filesize, &psector[28 + 32*i], 4);
        printf("%d\n",filesize);
        result = 0;
      }
    }

    // check for more clusters
    if (i == 64-1 && result == -1) {
      nextCluster = getNextCluster(currentCluster);
      if (nextCluster != EoC) {
        seekTo(firstSectorOfCluster(nextCluster)*bytesPerSector);
        currentCluster = nextCluster;
        read(imageid, psector, LCD_SSIZE);
        i = 0;
      }
    }
  }
  free(filename);

  seekTo(firstSectorOfCluster(originalCluster)*bytesPerSector);
  currentCluster = originalCluster;
  read(imageid, psector, LCD_SSIZE);

  return result;
}

/** seek_to - seeks to the given location
 **/
int seekTo(int location) {
  return lseek(imageid, location, SEEK_SET);
}

/** firstSectorOfCluster - returns the first sector number of
                           the given cluster index
 **/
unsigned int firstSectorOfCluster(int n) {
  return ((n-2) * sectorsPerCluster) + firstDataSector;
}

/** getNextCluster - returns the FAT value for the next cluster
                     based on the current cluster
 **/
unsigned int getNextCluster(int entryIndex) {
  int current_pos, FATLoc, indexLoc;
  unsigned int FATValue;
  char rawFATValue[4];

  // set FAT location
  FATLoc = reservedSectorCount * bytesPerSector;
  // determine requested index location
  indexLoc = FATLoc + (entryIndex * 4);
  // get current position
  current_pos = lseek(imageid,0,SEEK_CUR);

  // move to first FAT index
  seekTo(indexLoc);

  // read information
  read(imageid,rawFATValue,4);
  memcpy(&FATValue,&rawFATValue,4);

  // return to original position
  seekTo(current_pos);
  
  return FATValue;
}

/** combineShorts - combines two shorts into a long properly
 **/
unsigned int combineShorts(unsigned short high, unsigned short low) {
  return ((high<<16) | low);
}

/** newCluster - allocates a new cluster and updates the FATs accordingly
 **/
unsigned int newCluster(unsigned int linkedCluster) {
  int current_pos, FATLoc, i, j;
  unsigned int FATValue;
  unsigned int EoC_value = EoC;
  char rawFATValue[4];
  char blank_data[sectorsPerCluster*bytesPerSector];
  int freeLocation, linkedClusterIndex;

  freeLocation = linkedClusterIndex = 0;

  // set FAT location
  FATLoc = reservedSectorCount * bytesPerSector;
  // get current position
  current_pos = lseek(imageid,0,SEEK_CUR);

  // move to first FAT index
  seekTo(FATLoc);

  for (i = 0; i < (sizeFAT*bytesPerSector)/4; i++) {
    // read information
    read(imageid,rawFATValue,4);
    memcpy(&FATValue,&rawFATValue,4);

    // check for first free block
    if (FATValue == 0x00000000 && freeLocation == 0)
      freeLocation = i;
    // find next free location
    else if (FATValue == 0x00000000 && freeLocation != 0)
      nextFreeLocation = i;
    // check for linkedCluster indeex
    else if (FATValue == linkedCluster)
      linkedClusterIndex = i;
    // check for no free space
    else if (i == ((sizeFAT*bytesPerSector)/4)-1) {
      // return to original position
      seekTo(current_pos);
      return 0;
    }
  }

  // update all FAT tables
  for (j = 0; j < numFATs; j++) {
    // update new block to EoC value
    seekTo(FATLoc+(j*sizeFAT*bytesPerSector)+(freeLocation*((sizeFAT*bytesPerSector)/4)));
    memcpy(&rawFATValue,&EoC_value,4);
    write(imageid,&rawFATValue,4);
    // update linkedCluster FAT entry to new free block location
    seekTo(FATLoc+(linkedClusterIndex*4));
    memcpy(&rawFATValue,&freeLocation,4);
    write(imageid,&rawFATValue,4);
  }

  // clear out data in cluster
  seekTo(firstSectorOfCluster(freeLocation)*bytesPerSector);
  write(imageid, &blank_data, sectorsPerCluster*bytesPerSector);

  // return to original position
  seekTo(current_pos);
  
  return freeLocation;
}

/** newDirectoryCluster - allocates a new directory cluster and updates
                          the FATs accordingly
 **/
unsigned int newDirectoryCluster() {
  int current_pos, FATLoc, i, j;
  unsigned int FATValue;
  unsigned int EoC_value = EoC;
  char rawFATValue[4];
  int freeLocation;

  freeLocation = 0;

  // set FAT location
  FATLoc = reservedSectorCount * bytesPerSector;
  // get current position
  current_pos = lseek(imageid,0,SEEK_CUR);

  // move to first FAT index
  seekTo(FATLoc);

  for (i = nextFreeLocation; i < (sizeFAT*bytesPerSector)/4; i++) {
    // read information
    read(imageid,rawFATValue,4);
    memcpy(&FATValue,&rawFATValue,4);

    // check for first free block
    if (FATValue == 0x00000000 && freeLocation == 0)
      freeLocation = i;
    // find next free location
    else if (FATValue == 0x00000000 && freeLocation != 0) {
      nextFreeLocation = i;
      break;
    }
    // check for no free space
    else if (i == ((sizeFAT*bytesPerSector)/4)-1) {
      // return to original position
      seekTo(current_pos);
      return 0;
    }
  }

  // update all FAT tables
  for (j = 0; j < numFATs; j++) {
    // update new block to EoC value
    seekTo(FATLoc+(j*sizeFAT*bytesPerSector)+(freeLocation*((sizeFAT*bytesPerSector)/4)));
    memcpy(&rawFATValue,&EoC_value,4);
    write(imageid,&rawFATValue,4);
  }

  // return to original position
  seekTo(current_pos);
  
  return freeLocation;
}

/** clearClusterChain - clears out a cluster chain
 **/
void clearClusterChain(unsigned int startCluster) {
  int FATLoc, j;
  unsigned int EMPTY_value = EMPTY;
  unsigned int nextCluster;
  char rawFATValue[4];

  // end of cluster chain
  if (startCluster >= EoC)
    return;

  // call recursively
  nextCluster = getNextCluster(startCluster);
  clearClusterChain(nextCluster);

  // set FAT location
  FATLoc = reservedSectorCount * bytesPerSector;

  // update all FAT tables
  for (j = 0; j < numFATs; j++) {
    // update new block to empty value
    seekTo(FATLoc+(j*sizeFAT*bytesPerSector)+(startCluster*((sizeFAT*bytesPerSector)/4)));
    memcpy(&rawFATValue,&EMPTY_value,4);
    write(imageid,&rawFATValue,4);
  }

  // go back to original position
  seekTo(firstSectorOfCluster(currentCluster)*bytesPerSector);
}

/** convertFilename - converts a filename to a proper short filename
 **/
void convertFilename(char *filename) {
  // check for dot entries
  int i, j;
  char *temp;
  int dot_entry = 0;
  if (strcmp(filename,".") == 0) {
    temp = ".          ";
    dot_entry = 1;
  } else if (strcmp(filename,"..") == 0) {
    temp = "..         ";
    dot_entry = 1;
  }
  if (dot_entry) {
    strcpy(filename,temp);
    return;
  }

  // otherwise convert normally
  char *old_filename = (char*)malloc(strlen(filename)*sizeof(char));
  strcpy(old_filename,filename);

  free(filename);
  filename = (char*)malloc(12*sizeof(char));
  filename[11] = 0;

  for (i = 0; i < 11; i++) {
    if (old_filename[i] == '.') {
      for (j = i; j < 11; j++) {
        if (j < 8)
          filename[j] = ' ';
        else {
          i++;
          if (old_filename[i] == 0)
            filename[j] = ' ';
          else
            filename[j] = toupper(old_filename[i]);
        }
      }
      i = 12;
    } else if (old_filename[i] == 0)
      filename[i] = ' ';
    else
      filename[i] = toupper(old_filename[i]);
  }

  free(old_filename);
}

/** removeTailWhitespace - removes trailing whitespace in FAT32 short filenames
 **/
void removeTailWhitespace(char *filename) {
  int i;
  for (i = strlen(filename)-1; i >= 0 && filename[i] == ' '; i--) {
    filename[i] = 0;
  }
}
