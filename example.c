#include <assert.h>
#include <elf.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

/*
 * Fork a child process and set it up for tracing.
 * Replaces the child's image with the target program.
 */
pid_t run_target(char* const argv[])
{
    pid_t pid = fork();
    if (pid > 0) {
        return pid;
    } else if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            perror("ptrace");
            exit(1);
        }
        execv(argv[0], argv);
        exit(1);
    } else {
        perror("fork");
        exit(1);
    }
    return 0;
}

void* get_elf_content(const char* sym_name, const char* file_name)
{
    // Open ELF file for reading
    int elf_fd = open(file_name, O_RDONLY);
    if (elf_fd < 0) {
        perror("failed to open file");
        return NULL;
    }

    // Get file size
    struct stat elf_stats;
    int ret = fstat(elf_fd, &elf_stats);
    if (ret < 0) {
        perror("fstat failed");
        close(elf_fd);
        return NULL;
    }
    long file_size = elf_stats.st_size;

    // Allocate memory for the file content
    void* file_content = malloc(file_size);
    if (!file_content) {
        perror("malloc failed");
        close(elf_fd);
        return NULL;
    }

    // Read ELF file content into memory
    ret = read(elf_fd, file_content, file_size);
    if (ret < file_size) {
        perror("read failed or incomplete");
        free(file_content);
        close(elf_fd);
        return NULL;
    }
    close(elf_fd);

    return file_content;
}


int get_section_index_by_name(const char* section_name, Elf64_Ehdr* header, Elf64_Shdr* shtbl){
    char* strtbl = (char*)header + shtbl[header->e_shstrndx].sh_offset;
    for (int i = 0; i < header->e_shnum; i++){
        if(strcmp(section_name, strtbl + shtbl[i].sh_name) == 0){
            return i;
        }


    }
    fprintf(stderr, "could not find the section: %s\n", section_name);
    return -1;
}


unsigned long parse_elf(const char* target_sym, void* file_contents)
{
    // TODO: Look up target_sym address in the elf binary and return its virtual
    // address. Return 0 if not found.
    Elf64_Ehdr* header = (Elf64_Ehdr*) file_contents;
    Elf64_Shdr* shtbl = (Elf64_Shdr*)((char*)header + header->e_shoff);

    int symtab_idx = get_section_index_by_name(".symtab", header, shtbl);
    if (symtab_idx == -1) return 0;
    
    int strtbl_idx = get_section_index_by_name(".strtab", header, shtbl);
    if (strtbl_idx == -1) return 0;

    Elf64_Shdr symtab_header = shtbl[symtab_idx];
    Elf64_Shdr strtab_header = shtbl[strtbl_idx];

    int num_symbols = symtab_header.sh_size/symtab_header.sh_entsize;
    Elf64_Sym* symbol_arr = (Elf64_Sym*)((char*)header + symtab_header.sh_offset);

    char* strings_start = (char*)header + strtab_header.sh_offset;

    for(int i = 0; i < num_symbols; i++){
        char* symbol_name = strings_start + symbol_arr[i].st_name;
        if(strcmp(target_sym, symbol_name) == 0){
            return (unsigned long) symbol_arr[i].st_value;
        }
    }
    return 0;
}

/* 
    input: child proccess id, address to put char at, the char to put there.
    side effect: puts ch at the address
    returns: original byte that got replaced with a ch.
 */

unsigned char put_char(pid_t child_pid, void* addr, unsigned char ch){
    unsigned long original_instr = ptrace(PTRACE_PEEKTEXT, child_pid, addr, NULL);
    unsigned char original_byte = (unsigned char)(original_instr & 0XFF); 
    unsigned long trap = (original_instr & ~(0xFF)) | ch; // epic sifratiot trick :)
    ptrace(PTRACE_POKETEXT, child_pid, addr, (void*)trap);
    return original_byte;
}


// TODO: implement func that prints all params.
void print_params(struct user_regs_struct regs, int nr_params) {
    printf("(");

    if (nr_params > 0) {
        printf("%d", (int)regs.rdi);
    }
    if (nr_params > 1) {
        printf(", %d", (int)regs.rsi);
    }
    if (nr_params > 2) {
        printf(", %d", (int)regs.rdx);
    }
    if (nr_params > 3) {
        printf(", %d", (int)regs.rcx);
    }
    if (nr_params > 4) {
        printf(", %d", (int)regs.r8);
    }
    if (nr_params > 5) {
        printf(", %d", (int)regs.r9);
    }

    printf(")");
}

/*
 * Main debugger tracing loop.
 */
void run_tracer(pid_t child_pid, unsigned long addr, int nr_params)
{
    int wait_status;
    struct user_regs_struct regs;

    wait(&wait_status);

    //part B - print if first command in push rbp
    unsigned long first_instruction = ptrace(PTRACE_PEEKTEXT, child_pid, (void*)addr, NULL);

    // push rbp opcode = 0x55.
    if ((first_instruction & 0xFF) == 0x55) {
        printf("PRF:: This function starts by pushing rbp\n");
    }

    static unsigned char breakpoint = 0xCC;
    // insert breakpoint
    unsigned char original_byte = put_char(child_pid, (void*)addr, breakpoint);

    unsigned long return_address = 0;
    unsigned char return_original_byte = 0;

    //continue running
    ptrace(PTRACE_CONT, child_pid, NULL, NULL);

    int run_counter = 0;
    int recursive_count = 0;

    while(wait(&wait_status) > 0){
        //stop when process ends.
        if (WIFEXITED(wait_status)) {
            break; // The child program finished naturally
        }
        ptrace(PTRACE_SETREGS, child_pid, NULL, &regs);

        // hit breakpoint at the start of the function
        if (regs.rip - 1 == addr) {
            if (recursive_count == 0) {
                run_counter++;
                printf("PRF:: run #%d called with ", run_counter);
                print_params(regs, nr_params);
                printf(":\n");

                //for phase D - place breakpoint at the return address of the
                return_address = ptrace(PTRACE_PEEKTEXT, child_pid, (void*)regs.rsp, NULL);
                return_original_byte = put_char(child_pid, (void*)return_address, breakpoint);
            } else {
                // This is recursive
                printf("PRF::     entered recursive call with ");
                print_params(regs, nr_params);
                printf("\n");
            }

            // Will be used if the function will be called recursively
            recursive_count++;

            // place the original command, execute it and then return the breakpoint
            put_char(child_pid, (void*)addr, original_byte);
            regs.rip -= 1;                                      //move rip to before the breakboint and override the register
            ptrace(PTRACE_SETREGS, child_pid, NULL, &regs);
            ptrace(PTRACE_SINGLESTEP, child_pid, NULL, NULL);   //do 1 command
            wait(&wait_status);
            put_char(child_pid, (void*)addr, breakpoint);       //put breakpoint back

            //continue the function
            ptrace(PTRACE_CONT, child_pid, NULL, NULL);
        }
        // hit return breakpoint
        else if (return_address != 0 && (regs.rip - 1 == return_address)) {
            recursive_count--;

            if (recursive_count == 0) {
                // return value at rax after a function ended
                printf("PRF::  call to function returned with %d\n", (int)regs.rax);
            }

            // place the original command, execute it and NOT return the breakpoint
            put_char(child_pid, (void*)return_address, return_original_byte);
            regs.rip -= 1;
            ptrace(PTRACE_SETREGS, child_pid, NULL, &regs);
            ptrace(PTRACE_SINGLESTEP, child_pid, NULL, NULL);
            wait(&wait_status);

            return_address = 0;
            ptrace(PTRACE_CONT, child_pid, NULL, NULL);
        }
        // stopped not because of the breakpoint - continue.
        else {
            ptrace(PTRACE_CONT, child_pid, NULL, NULL);
        }
    }
}

int main(int argc, char* const argv[])
{
    if (argc < 4) {
        printf("usage: <sym_name> <number of input params> <elf_path> "
               "[optional input params for elf file]\n");
        return 0;
    }

    const char* sym_name  = argv[1];
    int nr_params         = strtol(argv[2], NULL, 10);
    const char* file_name = argv[3];

    // Put file content into memory for parsing
    void* file_content = get_elf_content(sym_name, file_name);

    // Find the symbol address
    unsigned long addr = parse_elf(sym_name, file_content);

    // Free the allocated memory
    free(file_content);

    // TODO: check if symbol was found
    if(addr == 0){
        printf("PRF:: symbol not found\n");
        return 1;
    }else{
        printf("PRF:: symbol address is 0x%lx\n", addr);
    }

    // Launch the target program

    pid_t child_pid = run_target(argv + 3);

    // Run the tracer
    run_tracer(child_pid, addr, nr_params);

    return 0;
}
