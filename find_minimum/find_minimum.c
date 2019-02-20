/**
 * find_minimum.c: Find local minimum in given array, perform some
 * extremely simple benchmarks
 *
 * Copyright (c) 2019 Alexey Mikhailov. All rights reserved.
 *
 * This work is licensed under the terms of the MIT license.
 * For a copy, see <https://opensource.org/licenses/MIT>.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/**
 * find_minimum: find local minimum in given array (binary search)
 *
 * XXX: elements are guaranteed to be distinct (or can't be done in O(log n))
 *
 */

int find_minimum (const unsigned int *a, size_t s, unsigned int *n_iter)
{
        size_t from = 0;
        size_t to = s-1;

        if (n_iter)
                *n_iter = 0;

        if (s == 0)
                return -1;

        while (1) {
                size_t m = (from + to) / 2;

                if (n_iter)
                        (*n_iter)++;

                if (to == from) {
                        return a[from];
                } else if ((to - from) == 1) {
                        return ((a[to] < a[from]) ? a[to] : a[from]);
                } else if (a[m] < a[m-1] && a[m] < a[m+1]) {
                        /* current element is local minimum */
                        return a[m];
                } else if (a[m-1] < a[m]) {
                         /* shrink to left partition" */
                        to = m-1;
                } else {
                        /* shrink to right partition otherwise */
                        from = m+1;
                }
        }
}

/**
 * shuffle_array: shuffles array
 */

void shuffle_array (unsigned int *a, size_t s)
{
        for (int i = s-1; i > 0; i--) {
                int r = rand() % i;
                int t = a[i];
                a[i] = a[r];
                a[r] = t;
        }
}

#ifdef BENCH
#define N_STEPS 200
#define N_DIFF (100*1000)
#define N_SWAPS 100
#define N_RUNS 15
#endif

int main (int argc, char **argv)
{
        /* simple tests */

        const unsigned int data [] = {6,5,4,3,2,1};
        assert(find_minimum(data, 0, NULL) == -1);
        assert(find_minimum(data, 1, NULL) == 6);
        assert(find_minimum(data, 2, NULL) == 5);
        assert(find_minimum(data, 3, NULL) == 4);
        assert(find_minimum(data, 4, NULL) == 3);
        assert(find_minimum(data, 5, NULL) == 2);
        assert(find_minimum(data, 6, NULL) == 1);

        const unsigned int data1 [] = {6,5,1,2,3,4};
        assert(find_minimum(data1, 0, NULL) == -1);
        assert(find_minimum(data1, 1, NULL) == 6);
        assert(find_minimum(data1, 2, NULL) == 5);
        assert(find_minimum(data1, 3, NULL) == 1);
        assert(find_minimum(data1, 4, NULL) == 1);
        assert(find_minimum(data1, 5, NULL) == 1);
        assert(find_minimum(data1, 6, NULL) == 1);

#ifdef BENCH
        /* poor man's benchmark */
        size_t sz = N_STEPS*N_DIFF*sizeof(unsigned int);
        unsigned int *m = malloc (sz);
        unsigned int i, j, k;

        if (m == NULL) {
                fprintf(stderr, "malloc() failed\n");
                exit(EXIT_FAILURE);
        }


        fprintf(stdout, "# N_STEPS = %u N_DIFF = %u, N_RUNS = %u\n",
                N_STEPS, N_DIFF, N_RUNS);

        fprintf(stdout, "#\n# N TIME\n");

        fprintf(stdout, "set title \"find\\\\_minimum() perf\"\n");
        fprintf(stdout, "set xlabel \"size\"\n");
        fprintf(stdout, "set ylabel \"time\"\n");
        fprintf(stdout, "set grid\n");
        fprintf(stdout, "plot \"-\" u 1:2 smooth bezier \n");

        for (i = 1; i <= N_STEPS; i++) {
                unsigned int res[N_RUNS];
                unsigned total = 0;
                float avg;


                for (j = 0; j < N_DIFF * i; j++) {
                        m[j] = j;
                }

                for (j = 0; j < N_RUNS; j++) {
                        for (k = 0; k < N_SWAPS; k++) {
                                unsigned int f = rand() % (N_DIFF * i);
                                unsigned int s = rand() % (N_DIFF * i);
                                unsigned int t;

                                t = m[f];
                                m[f] = m[s];
                                m[s] = t;
                        }

                        find_minimum (m, N_DIFF * i, &res[j]);
                        total += res[j];
                }

                avg = total/N_RUNS;

                fprintf(stdout, "%u %f\n", N_DIFF * i, avg);
                fflush(stdout);
        }

        fprintf(stdout, "e\n");
        fprintf(stdout, "pause -1\n");
#endif

        return 0;
}