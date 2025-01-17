#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define MAXVARS (250)   /* max # of variables */
#define EPSMIN (1E-6)  /* ending value of stepsize */

/* prototype of local optimization routine, code available in torczon.c */
extern void mds(double *startpoint, double *endpoint, int n, double *val, double eps, int maxfevals, int maxiter,
                double mu, double theta, double delta, int *ni, int *nf, double *xl, double *xr, int *term);



extern void write_results_to_json(const char* filename, double elapsed_time, int ntrials, unsigned long funevals, 
                           int best_trial, int best_nt, int best_nf, double* best_pt, int nvars, double best_fx);

                           
/* global variables */
unsigned long local_funevals = 0;


/* Rosenbrock classic parabolic valley ("banana") function */
double f(double *x, int n) {
    double fv;
    int i;

    local_funevals++;
    fv = 0.0;
    for (i = 0; i < n - 1; i++)   /* rosenbrock */
        fv = fv + 100.0 * pow((x[i + 1] - x[i] * x[i]), 2) + pow((x[i] - 1.0), 2);
    usleep(10);  /* do not remove, introduces some artificial work */

    return fv;
}

double get_wtime(void) {
    return MPI_Wtime();
}

int main(int argc, char *argv[]) {

    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* problem parameters */
    int nvars = 4;      /* number of variables (problem dimension) */
    int ntrials = 64;   /* number of trials */
    double lower[MAXVARS], upper[MAXVARS]; /* lower and upper bounds */

    /* mds parameters */
    double eps = EPSMIN;
    int maxfevals = 10000;
    int maxiter = 10000;
    double mu = 1.0;
    double theta = 0.25;
    double delta = 0.25;

    double startpt[MAXVARS], endpt[MAXVARS];    /* initial and final point of mds */
    double fx;  /* function value at the final point of mds */
    int nt, nf; /* number of iterations and function evaluations used by mds */

    /* information about the best point found by each process */

    double local_best_pt[MAXVARS];
    double local_best_fx = 1e10;
    int local_best_trial = -1;
    int local_best_nt = -1;
    int local_best_nf = -1;

    /* global best information */

    double global_best_pt[MAXVARS];
    double global_best_fx = 1e10;
    int global_best_trial = -1;
    int global_best_nt = -1;
    int global_best_nf = -1;

    /* local variables */
    int trial, i;
    double t0, t1;

    /* initialization of lower and upper bounds of search space */
    for (i = 0; i < MAXVARS; i++) lower[i] = -2.0; /* lower bound: -2.0 */
    for (i = 0; i < MAXVARS; i++) upper[i] = +2.0; /* upper bound: +2.0 */

    t0 = get_wtime();
    long tseed = 1;

    unsigned short randBuffer[3];
    randBuffer[0] = 0;
    randBuffer[1] = 0;
    randBuffer[2] = tseed + rank + ntrials;

    /* Calculate work distribution */
    double const step = (double)ntrials / size;
    unsigned long start = rank * step;
    unsigned long end = (rank + 1) * step;
    if (rank == size - 1) end = ntrials;

    for (trial = start; trial < end; trial++) {
        /* starting guess for rosenbrock test function, search space in [-2, 2) */
        for (i = 0; i < nvars; i++) {
            startpt[i] = lower[i] + (upper[i] - lower[i]) * erand48(randBuffer);
        }

        int term = -1;
        mds(startpt, endpt, nvars, &fx, eps, maxfevals, maxiter, mu, theta, delta, &nt, &nf, lower, upper, &term);

        if (fx < local_best_fx) {
            local_best_trial = trial;
            local_best_nt = nt;
            local_best_nf = nf;
            local_best_fx = fx;
            for (i = 0; i < nvars; i++)
                local_best_pt[i] = endpt[i];
        }
    }

    /* Rank 0 also considers its own results */
    if (rank == 0) {

        global_best_fx = local_best_fx;
        global_best_trial = local_best_trial;
        global_best_nt = local_best_nt;
        global_best_nf = local_best_nf;

        for (i = 0; i < nvars; i++)
            global_best_pt[i] = local_best_pt[i];

        for (int p = 1; p < size; p++) {
            double recv_fx;
            int recv_trial, recv_nt, recv_nf;
            double recv_pt[MAXVARS];

            MPI_Status status;

            MPI_Recv(&recv_fx, 1, MPI_DOUBLE, p, 100, MPI_COMM_WORLD, &status);
            MPI_Recv(&recv_trial, 1, MPI_INT, p, 101, MPI_COMM_WORLD, &status);
            MPI_Recv(&recv_nt, 1, MPI_INT, p, 102, MPI_COMM_WORLD, &status);
            MPI_Recv(&recv_nf, 1, MPI_INT, p, 103, MPI_COMM_WORLD, &status);
            MPI_Recv(recv_pt, MAXVARS, MPI_DOUBLE, p, 104, MPI_COMM_WORLD, &status);

            if (recv_fx < global_best_fx) {
                global_best_fx = recv_fx;
                global_best_trial = recv_trial;
                global_best_nt = recv_nt;
                global_best_nf = recv_nf;
                for (i = 0; i < nvars; i++)
                    global_best_pt[i] = recv_pt[i];
            }
        }

    } else {
        MPI_Send(&local_best_fx, 1, MPI_DOUBLE, 0, 100, MPI_COMM_WORLD);
        MPI_Send(&local_best_trial, 1, MPI_INT, 0, 101, MPI_COMM_WORLD);
        MPI_Send(&local_best_nt, 1, MPI_INT, 0, 102, MPI_COMM_WORLD);
        MPI_Send(&local_best_nf, 1, MPI_INT, 0, 103, MPI_COMM_WORLD);
        MPI_Send(local_best_pt, MAXVARS, MPI_DOUBLE, 0, 104, MPI_COMM_WORLD);
    }

    /* Reduce the function evaluations across all processes */
    unsigned long global_funevals;
    MPI_Reduce(&local_funevals, &global_funevals, 1, MPI_UNSIGNED_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    t1 = get_wtime();

    if (rank == 0) {
        printf("\n\nFINAL RESULTS (MPI):\n");
        printf("Elapsed time = %.3lf s\n", t1 - t0);
        printf("Total number of trials = %d\n", ntrials);
        printf("Total number of function evaluations = %ld\n", global_funevals);
        printf("Best result at trial %d used %d iterations, %d function calls and returned\n", global_best_trial, global_best_nt, global_best_nf);
        for (i = 0; i < nvars; i++) {
            printf("x[%3d] = %15.7le \n", i, global_best_pt[i]);
        }
        printf("f(x) = %15.7le\n", global_best_fx);

        write_results_to_json("results_mpi.json", t1 - t0, ntrials, global_funevals, global_best_trial, global_best_nt, global_best_nf, global_best_pt, nvars, global_best_fx);

    }

    MPI_Finalize();
    

    return 0;
}
