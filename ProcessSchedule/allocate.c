#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <arpa/inet.h>

#define INIT_SIZE 10
#define MEMORY 2048
#define IMPLEMENTS_REAL_PROCESS

typedef struct {
    int arrival_time;     // arrival time
    int name;             // process name
    int cpu_time;         // CPU time needed, dynamic
    int memory_size;      // memory size needed
    int turnaround_time;  // update turnaround time
    int service_time;     // initial service time needed, static
    int memory_address;   // assigned memory location
    pid_t pid;
    int fd1[2];            // parent write to child
    int fd2[2];            // child write to parent
    char output[65];      // 64-byte output string
} Process;


typedef struct {
    Process** processes;
    int size;
    int capacity;
} Batch;

typedef struct memory_t {
    int start_address;
    int end_address;
    int size;
    struct memory_t* next;
} Memory;

typedef struct {
    Memory* head;
    Memory* tail;
} Memory_list;

Batch* read_input(char *filename);
void sjf_scheduler(char *filename, char *memory_strategy, int quantum);
void rr_scheduler(char *filename, char *memory_strategy, int quantum);
Batch* create_batch(int size);
void free_batch(Batch* batch);
void add_process(Batch* batch, Process* process);
void remove_process(Batch* batch, int index);
int compare_processes(const void *p1, const void *p2, const char *field);
void sort_processes(Batch *batch, const char *field);
int round_up(double num);
void memory_management(Memory_list *memory, Batch *batch, Batch *input, Batch *ready, char *memory_strategy, int simulation_time);
void print_stats(Batch *finish, int simulation_time);
Memory_list* init_memory_list();
void remove_memory(Memory_list* memory_list, int start_address);
int best_fit(Memory_list* memory_list, Process* process);
void free_memory(Memory_list* memory_list, Process* process);
void free_memory_list(Memory_list* memory_list);
void create_process(Process* process, int simulation_time);
void suspend_process(Process* process, int simulation_time);
void continue_process(Process* process, int simulation_time);
void terminate_process(Process* process, int simulation_time);

int main(int argc, char *argv[]) {
    char *filename = NULL;
    char *scheduler = NULL;
    char *memory_strategy = NULL;
    int quantum = 0;
    int opt;

    // Parsing command line arguments
    while ((opt = getopt(argc, argv, "f:s:m:q:")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            case 's':
                scheduler = optarg;
                break;
            case 'm':
                memory_strategy = optarg;
                break;
            case 'q':
                quantum = atoi(optarg);
                break;
            case '?':
                printf("Unknown option\n");
                return 1;
            default:
                abort();
        }
    }

    if (strcmp(scheduler, "SJF") == 0) {
        sjf_scheduler(filename, memory_strategy, quantum);
    } else if (strcmp(scheduler, "RR") == 0) {
        rr_scheduler(filename, memory_strategy, quantum);
    } else {
        printf("Unknown scheduler\n");
        return 1;
    }

    return 0;
}

void sjf_scheduler(char *filename, char *memory_strategy, int quantum) {
    // Read input
    Batch *batch = read_input(filename);

    // Three queues
    Batch *input = create_batch(batch->size);
    Batch *ready = create_batch(batch->size);
    Batch *finish = create_batch(batch->size);

    // Start scheduling
    int simulation_time = 0;
    int is_running = 0;
    int proc_remaining = input->size + ready->size;
    Process *run_process;
    Memory_list *memory = init_memory_list();
    for (int i = 0; (input->size + ready->size + is_running + batch -> size) != 0; i++) {
        simulation_time = i * quantum;
        // Move to input queue and ready queue
        memory_management(memory, batch, input, ready, memory_strategy, simulation_time);

        // Shortest job first
        if (!is_running) {
            // Determine next process to run
            sort_processes(ready, "cpu");
            if ((ready->processes != NULL)) {
                run_process = ready->processes[0];
            } else {
                run_process = NULL;
            }
            if (run_process != NULL) {
                is_running = 1;
                if (run_process->cpu_time > quantum) {
                    create_process(run_process, simulation_time);
                    printf("%d,RUNNING,process_name=P%d,remaining_time=%d\n", simulation_time, run_process->name, run_process->cpu_time);
                    run_process->cpu_time -= quantum;
                } else {
                    int finish_time = simulation_time + quantum;
                    run_process->cpu_time = 0;
                    remove_process(ready, 0);
                    add_process(finish, run_process);
                    is_running = 0;
                    proc_remaining = input->size + ready->size;
                    run_process->turnaround_time = finish_time - run_process->arrival_time;
                    if (strcmp(memory_strategy, "best-fit") == 0) {
                        free_memory(memory, run_process);
                    }
                    printf("%d,FINISHED,process_name=P%d,proc_remaining=%d\n", finish_time, run_process->name, proc_remaining);
                    continue_process(run_process, simulation_time);
                    terminate_process(run_process, finish_time);
                }
            }
        } else {
            if (run_process->cpu_time > quantum) {
                continue_process(run_process, simulation_time);
                run_process->cpu_time -= quantum;
            } else {
                int finish_time = simulation_time + quantum;
                run_process->cpu_time = 0;
                remove_process(ready, 0);
                add_process(finish, run_process);
                is_running = 0;
                proc_remaining = input->size + ready->size;
                run_process->turnaround_time = finish_time - run_process->arrival_time;
                if (strcmp(memory_strategy, "best-fit") == 0) {
                    free_memory(memory, run_process);
                }
                printf("%d,FINISHED,process_name=P%d,proc_remaining=%d\n", finish_time, run_process->name, proc_remaining);
                continue_process(run_process, simulation_time);
                terminate_process(run_process, finish_time);
            }
        }
    }
    print_stats(finish, simulation_time + quantum);
    free_memory_list(memory);
    free_batch(input);
    free_batch(ready);
    free_batch(finish);
    free_batch(batch);
}

void rr_scheduler(char *filename, char *memory_strategy, int quantum) {
    // Read input
    Batch *batch = read_input(filename);
    sort_processes(batch,"arrival");

    // Three queues
    Batch *input = create_batch(batch->size);
    Batch *ready = create_batch(batch->size);
    Batch *finish = create_batch(batch->size);

    // Start scheduling
    int simulation_time = 0;
    int proc_remaining = input->size + ready->size;
    Process *run_process = NULL;
    Process *prev_process = NULL;
    Memory_list* memory = init_memory_list();
    for (int i = 0; (input->size + ready->size + batch -> size) != 0; i++) {
        simulation_time = i * quantum;
        // Move to input queue and ready queue
        memory_management(memory, batch, input, ready, memory_strategy, simulation_time);

        // Round-robin
        if (ready != NULL) {
            run_process = ready->processes[0];
        }
        if (run_process == NULL) {
            continue;
        }

        // Run process
        if (run_process->cpu_time > quantum) {
            if (run_process != prev_process) {
                printf("%d,RUNNING,process_name=P%d,remaining_time=%d\n", simulation_time, run_process->name, run_process->cpu_time);
                if (run_process->cpu_time==run_process->service_time) {
                    create_process(run_process, simulation_time);
                } else  {
                    continue_process(run_process, simulation_time);
                }
            } else {
                continue_process(run_process, simulation_time);
            }
            run_process->cpu_time -= quantum;
            remove_process(ready,0);
            memory_management(memory, batch, input, ready, memory_strategy, simulation_time + quantum);
            add_process(ready,run_process);
            if (ready->size > 1) {
                suspend_process(run_process, simulation_time + quantum);
            }
        } else {
            int finish_time = simulation_time + quantum;
            if (run_process != prev_process) {
                printf("%d,RUNNING,process_name=P%d,remaining_time=%d\n", simulation_time, run_process->name, run_process->cpu_time);
            }
            continue_process(run_process, simulation_time);
            run_process->cpu_time = 0;
            run_process->turnaround_time = finish_time - run_process->arrival_time;
            remove_process(ready, 0);
            add_process(finish, run_process);
            proc_remaining = input->size + ready->size;
            if (strcmp(memory_strategy, "best-fit") == 0) {
                free_memory(memory, run_process);
            }
            printf("%d,FINISHED,process_name=P%d,proc_remaining=%d\n", finish_time, run_process->name, proc_remaining);
            terminate_process(run_process, finish_time);
        }
        prev_process = run_process;
    }
    print_stats(finish, simulation_time + quantum);
    free_memory_list(memory);
    free_batch(input);
    free_batch(ready);
    free_batch(finish);
    free_batch(batch);
}

void memory_management(Memory_list *memory, Batch *batch, Batch *input, Batch *ready, char *memory_strategy, int simulation_time) {
    Process *curr_process;
    // Move process to input queue
    for (int j = 0; j < batch->size; j++) {
        curr_process = batch->processes[j];
        if (curr_process == NULL) {
            break;
        }
        if (curr_process->arrival_time <= simulation_time) {
            add_process(input, curr_process);
            remove_process(batch, j);
            j--;
        }
    }

    // Move process to ready queue among successful memory allocation
    for (int k = 0; k < input->size; k++) {
        curr_process = input->processes[k];
        if (curr_process == NULL) {
            break;
        }
        if (strcmp(memory_strategy, "infinite") == 0) {
            add_process(ready, curr_process);
            remove_process(input, k);
            k--;
        } else if (strcmp(memory_strategy, "best-fit") == 0) {
            if (best_fit(memory, curr_process)) {
                add_process(ready, curr_process);
                remove_process(input, k);
                k--;
                printf("%d,READY,process_name=P%d,assigned_at=%d\n", simulation_time, curr_process->name, curr_process->memory_address);
            }
        }
    }
}

void create_process(Process* process, int simulation_time) {
    int fd1[2];
    int fd2[2];
    pipe(fd1);
    pipe(fd2);
    pid_t childpid = fork();
    if (childpid == 0) {
        close(fd1[1]);
        close(fd2[0]);
        dup2(fd1[0], STDIN_FILENO);
        dup2(fd2[1], STDOUT_FILENO);
        char str[20];
        sprintf(str, "P%d", process->name);
        char *args[] = {"process", str, NULL};
        execvp("./process", args);
    } else {
        close(fd1[0]);
        close(fd2[1]);
        uint32_t big_order_time = ntohl(simulation_time);
        write(fd1[1], & big_order_time, 4);
        process->pid = childpid;
        process->fd1[0] = fd1[0];
        process->fd1[1] = fd1[1];
        process->fd2[0] = fd2[0];
        process->fd2[1] = fd2[1];

        // Read 1 byte from the standard output of process
        uint8_t read_byte;
        read(process->fd2[0], &read_byte, 1);
        // Verify that it's the same as the least significant byte (last byte) that was sent
        uint8_t least_significant_byte = (uint8_t)simulation_time;
        if (read_byte == least_significant_byte) {
            // printf("Verification successful.\n");
        } else {
            // printf("Verification failed.\n");
        }
    }
}

void terminate_process(Process *process, int simulation_time) {
    uint32_t big_order_time = ntohl(simulation_time);
    write(process->fd1[1], &big_order_time, 4);
    kill(process->pid, SIGTERM);
    char output[65];
    read(process->fd2[0], output, 64);
    output[64] = '\0';
    strncpy(process->output, output, 65);
    printf("%d,FINISHED-PROCESS,process_name=P%d,sha=%s\n", simulation_time, process->name, output);
}

void continue_process(Process *process, int simulation_time) {
    uint32_t big_order_time = ntohl(simulation_time);
    write(process->fd1[1], &big_order_time, 4);
    kill(process->pid, SIGCONT);
    // Read 1 byte from the standard output of process
    uint8_t read_byte;
    read(process->fd2[0], &read_byte, 1);
    // Verify that it's the same as the least significant byte (last byte) that was sent
    uint8_t least_significant_byte = (uint8_t)simulation_time;
    if (read_byte == least_significant_byte) {
        // printf("Verification successful.\n");
    } else {
        // printf("Verification failed.\n");
    }
}


void suspend_process(Process *process, int simulation_time) {
    uint32_t big_order_time = htonl(simulation_time);
    write(process->fd1[1], &big_order_time, 4);
    kill(process->pid, SIGTSTP);
    int wstatus = 1;
    while (!WIFSTOPPED(wstatus)) {
        waitpid(process->pid, &wstatus, WUNTRACED);
    }
}

Batch* read_input(char *filename) {
    FILE *fp = fopen(filename, "r");
    Batch* process_batch = create_batch(INIT_SIZE);
    // Read the process data from the file into the array
    int arrival_time_temp, cpu_time_temp, memory_size_temp, name_temp;
    while (fscanf(fp, "%d P%d %d %d", &arrival_time_temp, &name_temp, &cpu_time_temp, &memory_size_temp) == 4) {
        // Create a new process
        Process* new_process = (Process*) malloc(sizeof(Process));
        new_process->arrival_time = arrival_time_temp;
        new_process->name = name_temp;
        new_process->memory_size = memory_size_temp;
        new_process->cpu_time = cpu_time_temp;
        new_process->service_time = cpu_time_temp;
        // Add the new process to the batch
        add_process(process_batch, new_process);
    }
    fclose(fp);
    return process_batch;
}

void print_stats(Batch *finish, int simulation_time) {
    float turnaround = 0;
    float max_overhead = 0, avg_overhead = 0;
    float curr_overhead = 0;
    float overhead = 0;
    for (int i=0; i<finish->size; i++) {
        turnaround += finish->processes[i]->turnaround_time;
        curr_overhead = (float) finish->processes[i]->turnaround_time / (float) finish->processes[i]->service_time;
        overhead += curr_overhead;
        if (curr_overhead > max_overhead) {
            max_overhead = curr_overhead;
        }
    }
    int avg_turnaround = round_up(turnaround / finish->size);
    avg_overhead = overhead / finish->size;
    printf("Turnaround time %d\n", avg_turnaround);
    printf("Time overhead %.2f %.2f\n", max_overhead, avg_overhead);
    printf("Makespan %d\n", simulation_time);
}

Batch* create_batch(int size) {
    Batch* batch = (Batch*) malloc(sizeof(Batch));
    if (batch == NULL) {
        return NULL;  // allocation failed
    }
    batch->processes = (Process**) calloc(size, sizeof(Process*));
    batch->size = 0;
    batch->capacity = size;
    return batch;
}

Memory_list* init_memory_list() {
    Memory* head = (Memory*)malloc(sizeof(Memory));
    Memory_list* memory = (Memory_list*)malloc(sizeof(Memory_list));
    head->start_address = 0;
    head->end_address = MEMORY - 1;
    head->size = MEMORY;
    head->next = NULL;
    memory->head = head;
    memory->tail = head;
    return memory;
}

int best_fit(Memory_list* memory_list, Process* process) {
    int best_size = MEMORY + 1;
    int best_address = -1;
    Memory* curr = memory_list->head;
    Memory* best_block = memory_list->head;

    // find the best fit memory block
    while (curr != NULL) {
        if (curr->size >= process->memory_size && curr->size < best_size) {
            best_size = curr->size;
            best_address = curr->start_address;
            best_block = curr;
        }
        curr = curr->next;
    }
    // no found
    if (best_block == NULL) {
        return 0;
    }
    // update the process memory address and update the memory list
    process->memory_address = best_address;
    if (best_block->size == process->memory_size) {
        // remove the entire memory block if it exactly fits the process
        remove_memory(memory_list, best_address);
    } else {
        // update the start address and size of the memory block
        best_block->start_address = best_address + process->memory_size;
        best_block->size -= process->memory_size;
    }
    return 1;
}


void add_memory(Memory_list* memory_list, Memory* new_memory) {
    Memory* curr = memory_list->head;
    Memory* prev = NULL;
    while (curr != NULL && curr->start_address < new_memory->start_address) {
        prev = curr;
        curr = curr->next;
    }
    if (prev == NULL) {
        new_memory->next = curr;
        memory_list->head = new_memory;
    } else {
        prev->next = new_memory;
        new_memory->next = curr;
    }
    if (curr == NULL) {
        memory_list->tail = new_memory;
    }
}

void remove_memory(Memory_list* memory_list, int start_address) {
    Memory* curr = memory_list->head;
    Memory* prev = NULL;
    while (curr != NULL && curr->start_address != start_address) {
        prev = curr;
        curr = curr->next;
    }
    if (curr == NULL) {
        return;
    }
    if (prev == NULL) {
        // if the block is the head, update the head to the next block
        memory_list->head = curr->next;
    } else {
        prev->next = curr->next;
    }
    free(curr);
}

void free_memory(Memory_list* memory_list, Process* process) {
    Memory* freed_memory = (Memory*)malloc(sizeof(Memory));
    freed_memory->start_address = process->memory_address;
    freed_memory->end_address = process->memory_address + process->memory_size - 1;
    freed_memory->size = process->memory_size;

    Memory* curr = memory_list->head;
    Memory* prev = NULL;

    // find adjacent memory block
    while (curr != NULL && curr->end_address < freed_memory->start_address) {
        prev = curr;
        curr = curr->next;
    }

    // merge the freed memory with any adjacent holes
    // merge with prev
    if (prev != NULL && prev->end_address == freed_memory->start_address - 1) {
        prev->end_address = freed_memory->end_address;
        prev->size += freed_memory->size;
        free(freed_memory);
        freed_memory = prev;
    } else {
        add_memory(memory_list, freed_memory);
    }
    // merge with curr
    if (curr != NULL && curr->start_address == freed_memory->end_address + 1) {
        freed_memory->end_address = curr->end_address;
        freed_memory->size += curr->size;
        freed_memory->next = curr->next;
        if (curr == memory_list->tail) {
            memory_list->tail = freed_memory;
        }
        free(curr);
    }
}

void free_memory_list(Memory_list* memory_list) {
    if (memory_list == NULL) {
        return;
    }

    Memory* current = memory_list->head;
    Memory* temp;

    while (current != NULL) {
        temp = current;
        current = current->next;
        free(temp);
    }

    // Set head and tail to NULL after freeing all memory.
    memory_list->head = NULL;
    memory_list->tail = NULL;
    free(memory_list);
}

void free_batch(Batch* batch) {
    if (batch == NULL) {
        return;
    }
    for (int i = 0; i < batch->size; i++) {
        if (batch->processes[i] != NULL) {
            free(batch->processes[i]);
            batch->processes[i] = NULL;
        }
    }
    free(batch->processes);
    batch->processes = NULL;
    free(batch);
}

void add_process(Batch* batch, Process* process) {
    if (batch->size >= batch->capacity) {
        batch->capacity += INIT_SIZE;
        batch->processes = (Process**) realloc(batch->processes, batch->capacity * sizeof(Process*));
    }

    batch->processes[batch->size] = process;
    batch->size += 1;
}

void remove_process(Batch* batch, int index) {
    for (int i = index; i < batch->size; i++) {
        batch->processes[i] = batch->processes[i+1];
    }
    batch->size--;
}

int compare_processes(const void *p1, const void *p2, const char *field) {
    Process **process_a = (Process **) p1;
    Process **process_b = (Process **) p2;
    if (strcmp(field, "cpu") == 0) {
        if ((*process_a)->cpu_time < (*process_b)->cpu_time) {
            return -1;
        } else if ((*process_a)->cpu_time > (*process_b)->cpu_time) {
            return 1;
        } else {
            return 0;
        }
    } else if (strcmp(field, "arrival") == 0) {
        if ((*process_a)->arrival_time < (*process_b)->arrival_time) {
            return -1;
        } else if ((*process_a)->arrival_time > (*process_b)->arrival_time) {
            return 1;
        } else {
            return 0;
        }
    } else if (strcmp(field, "memory") == 0) {
        if ((*process_a)->memory_size < (*process_b)->memory_size) {
            return -1;
        } else if ((*process_a)->memory_size > (*process_b)->memory_size) {
            return 1;
        } else {
            return 0;
        }
    } else if (strcmp(field, "name") == 0) {
        if ((*process_a)->name < (*process_b)->name) {
            return -1;
        } else if ((*process_a)->name > (*process_b)->name) {
            return 1;
        } else {
            return 0;
        }
    } else {
        // Invalid field specified
        return 0;
    }
}


// In-place sorting algorithm
void sort_processes(Batch *batch, const char *field) {
    int i, j;
    if (batch->size == 0) {
        return;
    }
    for (i = 0; i < batch->size - 1; i++) {
        for (j = i + 1; j < batch->size; j++) {
            if (compare_processes(&batch->processes[i], &batch->processes[j], field) > 0) {
                Process *process_temp = batch->processes[i];
                batch->processes[i] = batch->processes[j];
                batch->processes[j] = process_temp;
            }
        }
    }
}

int round_up(double num) {
    int int_part = (int)num;
    double fractional_part = num - int_part;
    if (fractional_part > 0) {
        return int_part + 1;
    } else {
        return int_part;
    }
}