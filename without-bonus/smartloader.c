#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <elf.h>
#include <errno.h>

// Constants
#define PAGE_SIZE 4096

// Global counters for tracking page faults and allocations
int page_fault_count = 0;
int page_alloc_count = 0;
int internal_fragmentation_kb = 0;

// Memory segment details
typedef struct {
    Elf32_Addr vaddr;
    size_t memsz;
    void *mapped_addr;
} SegmentInfo;

// Array to store segments and count
SegmentInfo segments[10];
int segment_count = 0;

// Function to handle segmentation fault and lazy load pages
void segfault_handler(int sig, siginfo_t *si, void *unused) {
    void *fault_addr = si->si_addr;
    int page_found = 0;

    for (int i = 0; i < segment_count; ++i) {
        Elf32_Addr segment_start = segments[i].vaddr;
        Elf32_Addr segment_end = segment_start + segments[i].memsz;

        // Check if faulting address falls within the segment range
        if ((Elf32_Addr)fault_addr >= segment_start && (Elf32_Addr)fault_addr < segment_end) {
            page_found = 1;
            void *page_addr = (void *)((uintptr_t)fault_addr & ~(PAGE_SIZE - 1));

            // Map the memory page
            if (mmap(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
                perror("mmap");
                exit(EXIT_FAILURE);
            }

            // Track page allocations and internal fragmentation
            page_alloc_count++;
            internal_fragmentation_kb += (PAGE_SIZE - (segments[i].memsz % PAGE_SIZE)) / 1024;

            // Load page contents if needed (you may copy data if available)
            memcpy(page_addr, (void *)(segments[i].mapped_addr + (page_addr - (void *)segment_start)),
                   PAGE_SIZE);

            break;
        }
    }

    if (!page_found) {
        fprintf(stderr, "Segmentation fault at address %p, not in any mapped segment\n", fault_addr);
        exit(EXIT_FAILURE);
    }

    // Increase the page fault count
    page_fault_count++;
}

// Initialize signal handler for segmentation faults
void setup_segfault_handler() {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

// Validate ELF header
int validate_elf_header(const Elf32_Ehdr *ehdr) {
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "File is not in ELF format (incorrect magic number)\n");
        return 0;
    }
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "Only ELF 32-bit files are supported\n");
        return 0;
    }
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "File is not in little-endian format\n");
        return 0;
    }
    if (ehdr->e_type != ET_EXEC) {
        fprintf(stderr, "Only ELF executables are supported\n");
        return 0;
    }
    if (ehdr->e_machine != EM_386) {
        fprintf(stderr, "Unsupported machine architecture. Only x86 is supported\n");
        return 0;
    }
    return 1;
}

// Load ELF headers and set up lazy loading
void load_elf(const char *filename) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    // Read ELF header
    Elf32_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        perror("read ELF header");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Validate ELF header
    if (!validate_elf_header(&ehdr)) {
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Read program headers
    lseek(fd, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        Elf32_Phdr phdr;
        if (read(fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            perror("read program header");
            close(fd);
            exit(EXIT_FAILURE);
        }

        if (phdr.p_type == PT_LOAD) {
            // Set up segment details for lazy loading
            segments[segment_count].vaddr = phdr.p_vaddr;
            segments[segment_count].memsz = phdr.p_memsz;
            segments[segment_count].mapped_addr = mmap(NULL, phdr.p_memsz,
                                                       PROT_READ | PROT_WRITE | PROT_EXEC,
                                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

            if (segments[segment_count].mapped_addr == MAP_FAILED) {
                perror("mmap for segment");
                close(fd);
                exit(EXIT_FAILURE);
            }

            // Copy segment data for lazy loading on page fault
            pread(fd, segments[segment_count].mapped_addr, phdr.p_filesz, phdr.p_offset);

            segment_count++;
        }
    }

    close(fd);

    // Call the entry point (_start)
    void (*entry)() = (void (*)())ehdr.e_entry;
    entry();

    // Print statistics
    printf("Total page faults: %d\n", page_fault_count);
    printf("Total page allocations: %d\n", page_alloc_count);
    printf("Total internal fragmentation: %d KB\n", internal_fragmentation_kb);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ELF file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Setup segmentation fault handler
    setup_segfault_handler();

    // Load and execute the ELF
    load_elf(argv[1]);

    return 0;
}
