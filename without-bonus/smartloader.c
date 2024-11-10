#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <elf.h>
#include <errno.h>

Elf32_Ehdr *elf_header;
Elf32_Phdr *prog_header;
int file_desc;
int prog_header_size;
Elf32_Phdr **prog_header_array;

int total_prog_headers, header_offset, start_address;
int page_fault_count = 0;
int page_allocation_count = 0;
int unused_memory = 0;
int MEM_PAGE_SIZE = 4096;

void* entry_address;

typedef struct mapped_header {
    Elf32_Phdr* map_header;
    int region_size;
} mapped_header;

mapped_header **free_array;

void cleanup_loader() {
    free(elf_header);
    for (int i = 0; i < total_prog_headers; i++) {
        free(prog_header_array[i]);
    }
    free(prog_header_array);

    for (int i = 0; i < total_prog_headers; i++) {
        if (free_array[i] != NULL) {
            if (munmap(free_array[i]->map_header, free_array[i]->region_size) == -1) {
                perror("Error: munmap");
                exit(0);
            }
            free(free_array[i]);
        }
    }
    free(free_array);
}

void verify_elf(char** executable) {
    file_desc = open(*executable, O_RDONLY);
    if (file_desc == -1) {
        perror("Error opening file");
        exit(0);
    }

    int header_size = sizeof(Elf32_Ehdr);
    elf_header = (Elf32_Ehdr*)malloc(header_size);
    if (elf_header == NULL) {
        perror("Error: malloc");
        exit(0);
    }

    lseek(file_desc, 0, SEEK_SET);
    read(file_desc, elf_header, header_size);

    if (elf_header->e_ident[EI_MAG0] != ELFMAG0 || elf_header->e_ident[EI_MAG1] != ELFMAG1 ||
        elf_header->e_ident[EI_MAG2] != ELFMAG2 || elf_header->e_ident[EI_MAG3] != ELFMAG3) {
        printf("Invalid ELF file.\n");
        free(elf_header);
        exit(0);
    }

    free(elf_header);
    if (close(file_desc) == -1) {
        perror("Error closing file");
        exit(0);
    }
}

void execute_elf(char** executable) {
    file_desc = open(*executable, O_RDONLY);
    if (file_desc == -1) {
        perror("Error opening file");
        exit(0);
    }

    int elf_header_size = sizeof(Elf32_Ehdr);
    elf_header = (Elf32_Ehdr*) malloc(elf_header_size);
    if (elf_header == NULL) {
        perror("Error: malloc");
        exit(0);
    }

    if (lseek(file_desc, 0, SEEK_SET) == -1) {
        perror("Error with lseek");
        exit(0);
    }
    if (read(file_desc, elf_header, elf_header_size) == -1) {
        perror("Error reading file");
        exit(0);
    }

    total_prog_headers = elf_header->e_phnum;
    header_offset = elf_header->e_phoff;
    prog_header_size = elf_header->e_phentsize;
    start_address = elf_header->e_entry;

    prog_header_array = (Elf32_Phdr**)malloc(sizeof(Elf32_Phdr*) * total_prog_headers);
    if (prog_header_array == NULL) {
        perror("Error: malloc");
        exit(0);
    }

    free_array = (mapped_header**)malloc(sizeof(mapped_header*) * total_prog_headers);
    if (free_array == NULL) {
        perror("Error: malloc");
        exit(0);
    }

    for (int i = 0; i < total_prog_headers; i++) {
        Elf32_Phdr* temp_header = (Elf32_Phdr*)malloc(prog_header_size);
        if (temp_header == NULL) {
            perror("Error: malloc");
            exit(0);
        }
        if (lseek(file_desc, header_offset + i * prog_header_size, SEEK_SET) == -1) {
            perror("Error with lseek");
            exit(0);
        }
        if (read(file_desc, temp_header, prog_header_size) == -1) {
            perror("Error reading file");
            exit(0);
        }
        prog_header_array[i] = temp_header;
    }

    entry_address = (void*)start_address;
    int (*_start_function)() = (int(*)())(entry_address);
    _start_function();
    
    printf("Total page faults = %d\n", page_fault_count);
    printf("Total page allocations = %d\n", page_allocation_count);
    printf("Internal fragmentation in KB = %.2f\n", (double)unused_memory / 1024);

    if (close(file_desc) == -1) {
        perror("Error closing file");
        exit(0);
    }
}

static void signal_handler(int signal_num, siginfo_t *info, void *context) {
    if (signal_num == SIGSEGV) {
        void* fault_addr = info->si_addr;
        page_fault_count++;

        for (int i = 0; i < total_prog_headers; i++) {
            int virtual_addr = prog_header_array[i]->p_vaddr;

            if (virtual_addr <= (int)fault_addr &&
                (virtual_addr + prog_header_array[i]->p_memsz) > (int)fault_addr) {
                
                int page_offset = ((uintptr_t)fault_addr - prog_header_array[i]->p_vaddr) / MEM_PAGE_SIZE;
                void *page_addr = (void *)(prog_header_array[i]->p_vaddr + page_offset * MEM_PAGE_SIZE);

                if (mmap(page_addr, MEM_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0) == MAP_FAILED) {
                    perror("Error: mmap");
                    exit(0);
                }

                if (page_addr + MEM_PAGE_SIZE >= (void *)(prog_header_array[i]->p_vaddr + prog_header_array[i]->p_memsz)) {
                    int segment_size = prog_header_array[i]->p_memsz;
                    unused_memory += MEM_PAGE_SIZE - (segment_size % MEM_PAGE_SIZE);
                }

                page_allocation_count++;

                if (lseek(file_desc, prog_header_array[i]->p_offset + page_offset * MEM_PAGE_SIZE, SEEK_SET) == -1) {
                    perror("Error with lseek");
                    exit(0);
                }
                if (read(file_desc, page_addr, MEM_PAGE_SIZE) == -1) {
                    perror("Error reading file");
                    exit(0);
                }

                mapped_header* temp = (mapped_header*)malloc(sizeof(mapped_header));
                if (temp == NULL) {
                    perror("Error: malloc");
                    exit(0);
                }
                temp->map_header = page_addr;
                temp->region_size = MEM_PAGE_SIZE;
                free_array[i] = temp;
                break;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <ELF Executable>\n", argv[0]);
        exit(1);
    }

    struct sigaction saction;
    memset(&saction, 0, sizeof(saction));
    saction.sa_sigaction = signal_handler;
    saction.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &saction, NULL) == -1) {
        perror("Error: sigaction");
        exit(0);
    }

    verify_elf(&argv[1]);
    execute_elf(&argv[1]);
    cleanup_loader();
    return 0;
}
