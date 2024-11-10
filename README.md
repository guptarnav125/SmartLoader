# OS_Assignment_4
# SmartLoader
This is a basic lazy loader implementation for executable ELFs (Executable and Linkable Format). It reads an ELF file, verifies its format, loads the program's headers into memory, and executes the program. It allocates memory by generating and handling segmentation faults during execution, using paging. A single page has size 4 kb.

Segmentation Fault Handling:
When a segmentation fault signal is received, the loader retrieves the address where the fault occurred (segfault address) from the siginfo_t structure. It then increments the total page faults count, tracking each segmentation fault encountered. Next, the loader iterates through phdrarray to determine if the faulting address falls within any segment’s memory range as specified by the program headers. Upon finding the corresponding segment, it calculates the required memory size using a method to round up to the nearest multiple of the page size. The segment is then loaded into memory with mmap, setting the segment's address as the target and specifying the calculated size. After this, the total page allocation count is incremented. The segment data is read into memory, and a mapped_phdr structure is allocated to keep track of this mapping. This structure is stored in a global array for cleanup, allowing it to be safely unmapped with munmap at program exit.

Use the smartloader: download this repo in your unix system and run make command in terminal, enter command: './smartloader executable_name', this generates the  Total page faults, Total page allocations, and Internal fragmentation in KB.

# Contributions
Arnav Gupta, 2023125 - Smartloader and Error Handling
Divyanshi, 2023209 - Smartloader and Readme
