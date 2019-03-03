/*
 * assign_dirs.c: assign direction to edges so resulting graph remains acyclic
 *
 * Run as "./assign_dirs FILE"
 *
 * FILE consists of lines. First line specifies size of adjacency matrix
 * (number of vertices). Adjacency matrix elements follows. Undirected
 * edges are treated as bidirectional ones.
 *
 * Output specifies resulting graph as adjacency matrix.
 *
 * Example:
 *
 * $ cat test.input
 * 6
 * 0 1 1 1 0 1
 * 0 0 1 0 1 0
 * 1 0 0 1 1 0
 * 1 0 0 0 1 0
 * 0 0 0 0 0 1
 * 0 1 1 0 1 0
 *
 * $ ./assign_dirs test.input
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

#define FALSE 0
#define TRUE 1

typedef int bool;

/*
 * topological_sort: given adjancency matrix, perform topological sorting
 */

int topological_sort (bool *adj_mat, int dim, int *res)
{
        int in_deg[dim];
        int queue[dim*dim];
        int n_queue_f = 0;
        int n_queue_l = 0;
        int i = 0, j = 0;

        for (i = 0; i < dim; i++) {
                if (adj_mat[i*(dim+1)] == 1) {
                        fprintf(stderr, "%s: failed to sort, no loops allowed\n",
                                __func__);
                        return -1;
                }

                in_deg[i] = 0;
                for (j = 0; j < dim; j++) {
                        queue[j+i*dim] = 0;
                }
        }

        for (i = 0; i < dim; i++) {
                for (j = 0; j < dim; j++) {
                        if (adj_mat[i + j*dim] == 1 && adj_mat[j + i*dim] == 0) {
                                in_deg[i]++;
                        }
                }
        }

        for (i = 0; i < dim; i++) {
                if (in_deg[i] == 0) {
                        queue[n_queue_l] = i;
                        n_queue_l++;
                }
        }

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
                fprintf(stderr, "%s: failed to sort, wrong input (DAG required)\n",
                        __func__);
                return -1;
        }

        return 0;
}

bool solve (bool *in, bool *out, size_t d)
{
        int i,j,k;
        int s_edges[d];

        if (topological_sort (in, d, s_edges) < 0) {
                fprintf(stderr, "%s: topological_sort() failed\n", __func__);
                return FALSE;
        }

        memcpy(out, in, d*d*sizeof(bool));

        for (i = 0; i < d; i++) {
                for (j = 0; j < d; j++) {
                        if (i !=j && in[i+j*d] && in[j+i*d]) {
                                for (k = 0; k < d; k++) {
                                        if (s_edges[k] == i) {
                                                out[j + i*d] = TRUE;
                                                out[i + j*d] = FALSE;
                                                break;
                                        }

                                        if (s_edges[k] == j) {
                                                out[i + j*d] = TRUE;
                                                out[j + i*d] = FALSE;
                                                break;
                                        }

                                }
                        }

                }
        }

        return TRUE;
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
        bool *adj_mat = NULL, *out_mat = NULL;
        int dim, ret = 0;

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
                ret = -1;
                goto out;
        }

        out_mat = malloc(dim*dim*sizeof(bool));

        if (out_mat == NULL) {
                fprintf(stderr, "%s: malloc() failed for outmat: %s\n",
                        PROG, strerror(errno));
                ret = -1;
                goto out;
        }

        if (solve(adj_mat, out_mat, dim) != TRUE) {
                fprintf(stderr, "%s: failed to solve\n", PROG);
                ret = -1;
                goto out;
        }

        print_matrix (out_mat, dim);

out:
        if (adj_mat)
                free(adj_mat);

        if (out_mat)
                free(out_mat);

        return ret;
}
