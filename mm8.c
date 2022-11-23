
// MM8

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <semaphore.h>

// gcc -g -Wall -o mm8 mm8.c -pthread -lrt

#ifndef MAX_DIM
#define MAX_DIM 4000
#endif // MAX_DIM

#ifndef DEF_DIM
#define DEF_DIM 1000
#endif // DEF_DIM



#define MICROSECONDS_PER_SECOND 1000000.0

#define ELEMENT(_mat, _x, _y, _cols) _mat[(_x * _cols) + _y]

#define MATRIX_SIZE (dim * dim * sizeof(float))

#define SHM_SIZE ((sizeof(int) + sizeof(sem_t)) + MATRIX_SIZE)



static float *matrix1 = NULL;
static float *matrix2 = NULL;
static float *result = NULL;
static int dim = DEF_DIM;
static int num_procs = 1;
static int shmfd = -1;
static void* shm_mem = NULL;

static sem_t *lock = NULL;
static int *next_row = NULL;
static int static_work = 0;

void init(float *, float *, float *);
void op_mat(float *);
void mult(void);
double elapse_time(struct timeval *, struct timeval *);
int get_next_row(void);


void
init(float *mat1, float *mat2, float *res)
{
    int i = -1;
    int j = -1;

    for(i = 0; i < dim; i++)
    {
        for(j = 0; j < dim; j++)
        {
            ELEMENT(mat1, i, j, dim) = (i + j) * 2.0;
            ELEMENT(mat2, i, j, dim) = (i + j) * 3.0;
            ELEMENT(res, i, j, dim) = 0.0;
        }
    }
}


void
op_mat(float *mat1)
{
    FILE *op = NULL;
    int i = -1;
    int j = -1;

#define FILE_NAME "mm8.txt"
    op = fopen(FILE_NAME, "w");
    if(op == NULL)
    {
        perror("could not open file: " FILE_NAME);
        exit(17);
    }
    for(i = 0; i < dim; i++)
    {
        for(j = 0; j < dim; j++)
        {
            fprintf(op, "%8.2f ", ELEMENT(mat1, i, j, dim));
        }
        fprintf(op, "\n");
    }
    fclose(op);
}


double
elapse_time(struct timeval *t0, struct timeval *t1)
{
    double et = (((double) (t1->tv_usec - t0->tv_usec))
            / MICROSECONDS_PER_SECOND)
        + ((double) (t1->tv_sec - t0->tv_sec));
    
    return et;
}


void
mult(void)
{
    int proc = 0;
    pid_t cpid = -1;

    for(proc = 0; proc < num_procs; proc++)
    {
        cpid = fork();   
    
        if(cpid == 0)
        {
            int i = -1;
            int j = -1;
            int k = -1;

            if(static_work != 0)
            {
                for(i = proc; i < dim; i += num_procs)
                {
                    for(j = 0; j < dim; j++)
                    {
                        for(k = 0; k < dim; k++)
                        {
                            ELEMENT(result,i, j, dim) += ELEMENT(matrix1, i, k, dim) * ELEMENT(matrix2, k, j, dim);
                        }
                    }
                }
            }
            else
            { 
                for(i = get_next_row(); i < dim; i = get_next_row())
                {
                    for(j = 0; j < dim; j++)
                    {
                        for(k = 0; k < dim; k++)
                        {
                            ELEMENT(result,i, j, dim) += ELEMENT(matrix1, i, k, dim) * ELEMENT(matrix2, k, j, dim);
                        }
                    }
                }
            }
            munmap(shm_mem, SHM_SIZE);
            close(shmfd);
            fflush(stderr);
            _exit(EXIT_SUCCESS);
        }
    }
    fprintf(stderr, "all child process created\n");
    while ((cpid = wait(NULL)) > 0)
    {
        fprintf(stderr, "\tchild process %d cleaned up\n", cpid);
    }
    fprintf(stderr, "all child process cleaned up\n");
}


int
get_next_row(void)
{
    int cur_row = 0;

    sem_wait(lock);
    cur_row = *next_row;
    *next_row += 1;
    sem_post(lock);

    return cur_row;
}




//                      MAIN HERE                           //
int
main(int argc, char *argv[])
{
    struct timeval et0;
    struct timeval et1;
    struct timeval et2;
    struct timeval et3;
    struct timeval et4;
    struct timeval et5;

    char shm_name[256];
    int ret_val = 0;

    {
        int opt = -1;

        while((opt = getopt(argc, argv, "sp:d:h")) != -1)
        {
            switch (opt)
            {
                case 'p':
                    num_procs = atoi(optarg);
                    break;

                case 'd':
                    dim = atoi(optarg); 
                    if(dim < DEF_DIM)
                    {
                        dim = DEF_DIM;
                    }
                    if(dim > MAX_DIM)
                    {
                        dim = MAX_DIM;
                    }
                    break;
                
                case 's':
                    static_work = !static_work;
                    break;

                case 'h':
                    printf("%s: -t # -d #\n", argv[0]);
                    printf("\t-p #: number of procs\n");
                    printf("\t-d #: size of matrix\n");
                    exit(0);
                    break;

                default: /* '?' */
                    exit(EXIT_FAILURE);
                    break;
            }
        }
    }
    if(static_work)
    {
        fprintf(stderr, "static work allocation\n");
    }
    else
    {
        fprintf(stderr, "dynamic work allocation\n");
    }

    gettimeofday(&et0, NULL);

    matrix1 = malloc(MATRIX_SIZE);
    matrix2 = malloc(MATRIX_SIZE);

    sprintf(shm_name, "%s.%s", "/SharedMem_MatrixMult", getenv("LOGNAME"));
    if(shm_unlink(shm_name) != 0)
    {
        //Nada
    }
    shmfd = shm_open(shm_name
            , (O_CREAT | O_RDWR | O_EXCL)
            , (S_IRUSR | S_IWUSR)  
        );
    if(shmfd < 0)
    {
        fprintf(stderr, "Failed to open/create shared memory segment >%s<\n", shm_name);
        exit(3);
    }
    ret_val = ftruncate(shmfd, SHM_SIZE);
    if(ret_val != 0)
    {
        fprintf(stderr, "Failed to size shared memory\n");
        exit(4);
    }

    shm_mem = mmap(NULL
                , SHM_SIZE
                , PROT_READ | PROT_WRITE
                , MAP_SHARED
                , shmfd
                , 0 //offset
            );
#define SHARED_THREADS 0 //NOT GOING TO USE THIS
#define SHARED_PROCS 1

    next_row = (int *) shm_mem;
    *next_row = 0;
    lock = (sem_t *) (shm_mem + sizeof(int));
    sem_init(lock, SHARED_PROCS, 1);


    result = (float *) (shm_mem + sizeof(int) + sizeof(sem_t));

    gettimeofday(&et1, NULL);
    init(matrix1, matrix2, result);

    gettimeofday(&et2, NULL);
    mult();

    gettimeofday(&et3, NULL);
    op_mat(result);

    gettimeofday(&et4, NULL);
    free(matrix1);
    free(matrix2);
    munmap(shm_mem, SHM_SIZE);
    close(shmfd);
    shm_unlink(shm_name);

    matrix1 = matrix2 = result = NULL;
    
    gettimeofday(&et5, NULL);
    {
        double total_time = elapse_time(&et0, &et5);
        double alloc_time = elapse_time(&et0, &et1);
        double init_time = elapse_time(&et1, &et2);
        double comp_time = elapse_time(&et2, &et3);
        double op_time = elapse_time(&et3, &et4);
        double td_time = elapse_time(&et4, &et5);

        printf("Total time: %8.2lf\n", total_time);
        printf("  Alloc time: %8.2lf\n", alloc_time);
        printf("  Init  time: %8.2lf\n", init_time);
        printf("  Comp  time: %8.2lf\n", comp_time);
        printf("  O/P   time: %8.2lf\n", op_time);
        printf("  T/D   time: %8.2lf\n", td_time);
    }

    return EXIT_SUCCESS;
}

