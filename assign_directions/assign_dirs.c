/*
 * assign_dirs.c: assign direction to edges so resulting graph remains acyclic
 *
 * Run as "./assign_dirs FILE"
 *
 * FILE consists of lines. First line specifies size of adjacency matrix
 * (number of vertices). Adjacency matrix elements follows. Set of undirected
 * edges follows. (see example in 'test.input')
 *
 * Output specifies resulting graph as adjacency matrix.
 *
 * Example:
 *
 * $ ./assign_dirs test.input
 *   ...
 * 1 => 4
 * 1 => 3
 * 6 => 5
 * 0 1 1 1 0 1
 * 0 0 1 0 1 0
 * 0 0 0 1 1 0
 * 0 0 0 0 1 0
 * 0 0 0 0 0 0
 * 0 1 1 0 1 0
 *
 * Copyright (c) 2019 Alexey Mikhailov. All rights reserved.
 *
 * This work is licensed under the terms of the MIT license.
 * For a copy, see <https://opensource.org/licenses/MIT>.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PROG "assign_dirs"

typedef int bool;

/*
 * topological_sort: given adjancency matrix, perform topological sorting
 */

int topological_sort (bool *adj_mat, int dim, int *res)
{
        int *in_deg, *queue;
        int n_queue_f = 0;
        int n_queue_l = 0;
        int i = 0, j = 0;

        in_deg = malloc(sizeof(int) * dim);
        queue = malloc(sizeof(int) * dim * dim);

        if (!in_deg || !queue) {
                fprintf(stderr, "%s: malloc() failed: %s", __func__,
                        strerror(errno));
                goto err;
        }


        for (i = 0; i < dim; i++) {
                for (j = 0; j < dim; j++) {
                        in_deg[i] += adj_mat[i + (dim * j)];
                }
#ifdef DEBUG
                printf("in_deg[%d] = %d\n", i, in_deg[i]);
#endif

        }

        for (i = 0; i < dim; i++) {
                if (in_deg[i] == 0) {
                        queue[n_queue_l] = i;
                        n_queue_l++;
                }
        }

        i = 0;
        j = 0;

        while (n_queue_f != n_queue_l) {
                int n = queue[n_queue_f];
                n_queue_f++;

                res[j] = n;

                for (i = 0; i < dim; i++) {
                        in_deg[i]--;

                        if (in_deg[i] == 0) {
                                queue[n_queue_l] = i;
                                n_queue_l++;
                        }
                }

                j++;
        }

        if (j != dim) {
                fprintf(stderr, "%s: wrong input? j = %d, n = %d\n", PROG, j, dim);
                goto err;
        }

        return 0;

err:
        if (in_deg)
                free(in_deg);

        if (queue)
                free(queue);

        return -1;
}


int parse_input (FILE *fin, bool **adj_mat, int *dim)
{
        bool *m, *p;
        int d, i, v;

        if (fscanf(fin, "%d", &d) != 1) {
                fprintf(stderr, "%s: fscanf failed()\n", __func__);
                return -1;
        }

        if (d <= 0) {
                fprintf(stderr, "%s: non-positive dimensions\n", __func__);
                return -1;
        }

        m = malloc(d*d*sizeof(bool));

        if (m == NULL) {
                fprintf(stderr, "%s: malloc() failed: %s\n",
                        __func__, strerror(errno));
                return -1;
        }

        *dim = d;
        *adj_mat = m;
        p = m;

        for (i = 0; i < d*d; i++) {
                if (fscanf(fin, "%d", &v) != 1) {
                        fprintf(stderr, "%s: fscanf failed()\n", __func__);
                        goto err;
                }

                if (v != 0 && v != 1) {
                        fprintf(stderr, "%s: wrong value in adjacency matrix\n",
                                __func__);
                        goto err;
                }

                *p = v;
                p++;
        }

        return 0;

err:
        free(m);
        return -1;
}

void print_matrix (bool *adj_mat, int dim)
{
        int i,j;

        for (i = 0; i < dim; i++) {
                for (j = 0; j < dim; j++) {
                        printf("%d ", adj_mat[j + i*dim]);
                }
                printf("\n");
        }

}

int main (int argc, char **argv) {
        FILE *fin;
        bool *adj_mat, *p;
        int *s_edges;
        int dim, v1, v2, i;

        if (argc != 2) {
                fprintf(stderr, "%s: missing filename\n", PROG);
                exit(EXIT_FAILURE);
        }

        fin = fopen(argv[1], "r");

        if (fin == NULL) {
                fprintf(stderr, "%s: failed to open %s: %s\n",
                        PROG, argv[1], strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (parse_input(fin, &adj_mat, &dim) < 0) {
                exit(EXIT_FAILURE);
        }

        print_matrix (adj_mat, dim);

        s_edges = malloc(dim * sizeof(int));

        if (s_edges == NULL) {
                fprintf(stderr, "%s: malloc() failed: %s\n",
                        PROG, strerror(errno));
                exit(EXIT_FAILURE);
        }

        topological_sort (adj_mat, dim, s_edges);

        while (!feof(fin)) {
                if (fscanf(fin, "%d %d", &v1, &v2) != 2) {
                        break;
                }

                if (v1 < 1 || v2 < 1 || v1 > dim || v2 > dim) {
                        fprintf(stderr, "%s: wrong edge specification (%d, %d)\n",
                                __func__, v1, v2);
                        exit(EXIT_FAILURE);
                }

                p = s_edges;

                for (i = 0; i < dim; i++) {
                        if ((v1-1) == *p) {

#ifdef DEBUG
                                printf("%d => %d\n", v1, v2);
#endif
                                adj_mat[(v2-1) + dim*(v1-1)] = 1;
                                break;
                        }

                        if ((v2-1) == *p) {
#ifdef DEBUG
                                printf("%d => %d\n", v2, v1);
#endif
                                adj_mat[(v1-1) + dim*(v2-1)] = 1;
                                break;
                        }

                        p++;
                }
        }

        print_matrix (adj_mat, dim);

        return 0;
}
