/*
 * assign_dirs.c: assign direction to edges so resulting graph remains acyclic
 *
 * Run as "./assign_dirs FILE"
 *
 * FILE consists of lines. Each line defines either directed or undirected
 * edge for input graph. Directed edges are defined as "D VERTEX1 VERTEX2", same
 * way undirected egdges are fined as "U VERTEX1 VERTEX2"
 *
 * Output specifies resulting graph as set of directed edges (adjacency matrix)
 *
 * Example:
 *
 * $ ./assign_dirs test1.input
 *   ...
 * 0 => 2
 * 0 => 3
 * 5 => 4
 * 0 1 0 0 0 1
 * 0 0 1 1 1 0
 * 0 0 0 1 1 0
 * 0 0 0 0 1 0
 * 0 0 0 0 0 0
 * 0 0 1 0 0 0
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

#define N_VERTEX 50
#define PROG "assign_dirs"

typedef int bool;

/*
 * topological_sort: given adjancency matrix, perform topological sorting
 */

void topological_sort (bool *adj_mat, int n_vert, int *res, int *res_n)
{
        int in_deg[N_VERTEX] = {0};
        int queue[N_VERTEX*N_VERTEX];
        int n_queue_f = 0;
        int n_queue_l = 0;

        *res_n = 0;

        int i = 0, j = 0;

        for (i = 0; i < n_vert; i++) {
                for (j = 0; j < n_vert; j++) {
                        in_deg[i] += adj_mat[j + (N_VERTEX * i)];
                }
#ifdef DEBUG
                printf("in_deg[%d] = %d\n", i, in_deg[i]);
#endif

        }

        for (i = 0; i < n_vert; i++) {
                if (in_deg[i] == 0) {
                        queue[n_queue_l] = i;
                        n_queue_l++;
                }
        }

        j = 0;
        while (n_queue_f != n_queue_l) {
                int n = queue[n_queue_f];
                n_queue_f++;

                res[*res_n] = n;
                (*res_n)++;

                for (i = 0; i < n_vert; i++) {
                        in_deg[i]--;

                        if (in_deg[i] == 0) {
                                queue[n_queue_l] = i;
                                n_queue_l++;
                        }
                }
                j++;
        }

        if (j != n_vert) {
                fprintf(stderr, "%s: wrong input? j = %d, n = %d\n", PROG, j, n_vert);
                //exit(EXIT_FAILURE);
        }

#ifdef DEBUG
        for (i = 0; i < *res_n; i++) {
                printf("%d ", res[i]);
        }

        printf("\n");
#endif
}


int parse_input (FILE *fin, bool *adj_mat, int *n_vert, int *undir_edges, int *n_uedges)
{
        char buf[16];
        char *r;

        *n_vert = 0;
        *n_uedges = 0;

        while ((r = fgets(buf, sizeof(buf), fin)) != NULL) {
                char t;
                int v1;
                int v2;

                if (sscanf(r, "%c %d %d", &t, &v1, &v2) != 3) {
                        fprintf(stderr, "%s: failed to parse string: %s\n",
                                PROG, buf);
                        return -1;
                }

                assert(v1 >= 0 && v1 < N_VERTEX);
                assert(v2 >= 0 && v2 < N_VERTEX);
                assert(t == 'D' || t == 'U');

                if (v1 > (*n_vert)-1)
                        *(n_vert) = v1 + 1;

                if (v2 > (*n_vert)-1)
                        *(n_vert) = v2 + 1;

                if (t == 'D') {
                        adj_mat [v1 + (v2 * N_VERTEX)] = 1;
                } else if (t == 'U') {
                        undir_edges[*n_uedges] = (v1 | (v2 << 16));
                        (*n_uedges)++;
                }
        }

#ifdef DEBUG
        for (int i = 0; i < *n_vert; i++) {
                for (int j = 0; j < *n_vert; j++) {
                        printf("%d ", adj_mat[i + j*N_VERTEX]);
                }
                printf("\n");
        }
#endif

        return 0;
}

void solve (bool *adj_mat, int *undir_edges, int n_uedges, bool *out, int n_vert)
{
        int i, j;
        int res[N_VERTEX];
        int res_n;

        topological_sort (adj_mat, n_vert, res, &res_n);

        memcpy(out, adj_mat, N_VERTEX * N_VERTEX * sizeof(bool));

        for (i = 0; i < n_uedges; i++) {
                int e = undir_edges[i];
                int v1 = e & 0xFFFF;
                int v2 = (e >> 16) & 0xFFFF;

                for (j = 0; j < res_n; j++) {
                        /* if v1 comes first then v1 => v2 direction, v2 => v1 otherwise */
                        if (res[j] == v1) {

#ifdef DEBUG
                                printf("%d => %d\n", v1, v2);
#endif
                                out[v1 + N_VERTEX*v2] = 1;
                                break;
                        }

                        if (res[j] == v2) {
#ifdef DEBUG
                                printf("%d => %d\n", v2, v1);
#endif
                                out[v2 + N_VERTEX*v1] = 1;
                                break;
                        }

                }
        }

}

int main (int argc, char **argv) {
        FILE *fin;
        bool adj_mat [N_VERTEX * N_VERTEX];
        int n_vert = 0;

        int undir_edges [N_VERTEX];
        int n_uedges;

        bool out[N_VERTEX * N_VERTEX];

        if (argc != 2) {
                fprintf(stderr, "%s: missing filename\n", PROG);
                exit(EXIT_FAILURE);
        }

        bzero(adj_mat, sizeof(bool) * N_VERTEX * N_VERTEX);

        fin = fopen(argv[1], "r");

        if (fin == NULL) {
                fprintf(stderr, "%s: failed to open %s: %s\n",
                        PROG, argv[1], strerror(errno));
                exit(EXIT_FAILURE);
        }

        if (parse_input(fin, adj_mat, &n_vert, undir_edges, &n_uedges) < 0) {
                exit(EXIT_FAILURE);
        }

        solve (adj_mat, undir_edges, n_uedges, out, n_vert);

        for (int i = 0; i < n_vert; i++) {
                for (int j = 0; j < n_vert; j++) {
                        printf("%d ", adj_mat[i + j*N_VERTEX]);
                }
                printf("\n");
        }

}