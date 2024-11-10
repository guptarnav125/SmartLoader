
#include "loader.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

typedef int (*startf)(void);

int fd=-1;
void* _virtual_mem = NULL;

/*
 * release memory and other cleanups
 */
void loader_cleanup() {
  if (_virtual_mem != NULL) {
    munmap(_virtual_mem, sizeof(_virtual_mem));
  }

  if(fd!=-1){
    if(close(fd)==-1){
      perror("Error closing fd");
    }
    fd=-1;
  }

  //no additional cleaunp for ehdr, phdr necessary because we didnt allocate them dynamically,
  //they were locally declared inside the function and are automatically cleaned
  //when function returns

}

/*
 * Load and run the ELF executable file
 */
void load_and_run_elf(char** argv) {
  fd = open(argv[1], O_RDONLY);
  if (fd== -1){
    printf("Error");
    exit(1);
  }

  // printf("%d", fd);
  //Reading ELF header from file argv[1] into the structure & handling errors
  Elf32_Ehdr ehdr;
  if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        printf("Error");
        exit(1);
    }

  //Extracting info from elf header
  Elf32_Addr entrypoint = ehdr.e_entry;
  off_t e_phoff = ehdr.e_phoff;
  size_t e_phentsize = ehdr.e_phentsize;
  uint16_t e_phnum = ehdr.e_phnum;

  //set fd to start of program header table
  lseek(fd, e_phoff, SEEK_SET);
  if(lseek(fd,e_phoff,SEEK_SET)==(off_t)-1){
    printf("Error");
    exit(1);
  }
  void* entryadd =NULL;

  //iterating through program headers
  for(int i=0;i<e_phnum; i++){
    Elf32_Phdr phdr;
    if (read(fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
        printf("Error");
        exit(1);
    }
    //checking if segment is of PT_LOAD type
    if (phdr.p_type == PT_LOAD){
      uint32_t start = phdr.p_vaddr;
      uint32_t end =phdr.p_vaddr + phdr.p_memsz;
      
      if(entrypoint>= start && entrypoint<end){
        //allocating memory:
        _virtual_mem = mmap(NULL, phdr.p_memsz, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANONYMOUS|MAP_PRIVATE, 0, 0);
        if(_virtual_mem==MAP_FAILED){
          printf("Error");
          exit(1);
        }
        
        //setting fd to start of segment table
        lseek(fd, phdr.p_offset, SEEK_SET);
        if(read(fd, _virtual_mem, phdr.p_filesz)!=phdr.p_filesz){
          printf("Error");
          exit(1);
        }

        uint32_t offset = entrypoint - start;
        entryadd  = (void*)((char*)_virtual_mem + offset);
        break;
      }
    }
  }

  startf _start = (startf)entryadd;
  // 1. Load entire binary content into the memory from the ELF file.
  // 2. Iterate through the PHDR table and find the section of PT_LOAD 
  //    type that contains the address of the entrypoint method in fib.c
  // 3. Allocate memory of the size "p_memsz" using mmap function 
  //    and then copy the segment content
  // 4. Navigate to the entrypoint address into the segment loaded in the memory in above step
  // 5. Typecast the address to that of function pointer matching "_start" method in fib.c.
  // 6. Call the "_start" method and print the value returned from the "_start"
  int result = _start();
  printf("User _start return value = %d\n",result);

}

int main(int argc, char *argv[]){
    if (argc!=2) //only one argument
    {
      //error handle
      fprintf(stderr, "Usage: %s <ELF file>\n", argv[0]);
      exit(1);
    }

    load_and_run_elf(argv);
    loader_cleanup();
    return 0;    
}
