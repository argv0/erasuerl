// -------------------------------------------------------------------
//
// Copyright (c) 2007-2012 Basho Technologies, Inc. All Rights Reserved.
//
// This file is provided to you under the Apache License,
// Version 2.0 (the "License"); you may not use this file
// except in compliance with the License.  You may obtain
// a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// -------------------------------------------------------------------
#include "erasuerl.h"

extern "C" { 
#include "Jerasure/include/cauchy.h"
#include "Jerasure/include/jerasure.h"
}
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

static ErlNifResourceType* erasuerl_RESOURCE;

struct erasuerl_handle {
    int *matrix;
    int *bitmatrix;
    int **schedule;  
    int k; // = 9;
    int m; // = 4;
    int w;  // = 4;
  
};

struct decode_options {
    int size;
    int packetsize;
    int buffersize;
};

// Atoms (initialized in on_load)
static ERL_NIF_TERM ATOM_TRUE;
static ERL_NIF_TERM ATOM_FALSE;
static ERL_NIF_TERM ATOM_OK;
static ERL_NIF_TERM ATOM_ERROR;
static ERL_NIF_TERM ATOM_EMPTY;
static ERL_NIF_TERM ATOM_VALUE;
static ERL_NIF_TERM ATOM_K;
static ERL_NIF_TERM ATOM_M;
static ERL_NIF_TERM ATOM_W;
static ERL_NIF_TERM ATOM_SIZE;
static ERL_NIF_TERM ATOM_PACKETSIZE;
static ERL_NIF_TERM ATOM_BUFFERSIZE;

static ErlNifFunc nif_funcs[] =
{
    {"new", 0, erasuerl_new},
    {"encode", 2, erasuerl_encode},
    {"decode", 3, erasuerl_decode}
};

#define ATOM(Id, Value) { Id = enif_make_atom(env, Value); }

template <typename Acc> ERL_NIF_TERM fold(ErlNifEnv* env, ERL_NIF_TERM list,
                                          ERL_NIF_TERM(*fun)(ErlNifEnv*, ERL_NIF_TERM, Acc&),
                                          Acc& acc)
{
    ERL_NIF_TERM head, tail = list;
    while (enif_get_list_cell(env, tail, &head, &tail))
    {
        ERL_NIF_TERM result = fun(env, head, acc);
        if (result != ATOM_OK)
        {
            return result;
        }
    }

    return ATOM_OK;
}

ERL_NIF_TERM parse_decode_option(ErlNifEnv* env, ERL_NIF_TERM item, decode_options& opts)
{
    int arity;
    const ERL_NIF_TERM* option;
    if (enif_get_tuple(env, item, &arity, &option))
    {
        if (option[0] == ATOM_SIZE)
        {
            enif_get_int(env, option[1], &opts.size);
        }
        else if (option[0] == ATOM_PACKETSIZE)
        {
            enif_get_int(env, option[1], &opts.packetsize);
        }
        else if (option[0] == ATOM_BUFFERSIZE)
        {
            enif_get_int(env, option[1], &opts.buffersize);
        }
    }
    return ATOM_OK;
}

ERL_NIF_TERM erasuerl_new(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    erasuerl_handle *handle = 
        (erasuerl_handle *)enif_alloc_resource_compat(env, erasuerl_RESOURCE,
                                                   sizeof(erasuerl_handle));
    memset(handle, '\0', sizeof(erasuerl_handle));
    handle->k = 2;
    handle->m = 5;
    handle->w = 4;
    handle->matrix = cauchy_good_general_coding_matrix(handle->k, handle->m, handle->w);
    handle->bitmatrix = jerasure_matrix_to_bitmatrix(handle->k, handle->m, handle->w, 
                                                     handle->matrix);
    handle->schedule = jerasure_smart_bitmatrix_to_schedule(handle->k, handle->m, 
                                                            handle->w, handle->bitmatrix);
    ERL_NIF_TERM result = enif_make_resource(env, handle);
    enif_release_resource_compat(env, handle);
    return enif_make_tuple2(env, ATOM_OK, result);
}

ERL_NIF_TERM erasuerl_encode(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    erasuerl_handle *handle;
    ErlNifBinary item;
    if (enif_get_resource(env,argv[0],erasuerl_RESOURCE,(void**)&handle) &&
        enif_inspect_binary(env, argv[1], &item))
    {
        int packetsize = 64;
        int size = item.size;
        
        int newsize = size;
        if (size%(handle->k*handle->w*sizeof(int)) != 0) 
            while (newsize%(handle->k*handle->w*sizeof(int)) != 0) 
                newsize++;

        int blocksize = newsize/handle->k;
        printf("newsize: %d\n", newsize);
        printf("size: %d\n", size);
        printf("blocksize: %d\n", blocksize);

        char **data = (char **)malloc(sizeof(char*)*handle->k);
        char **coding = (char **)malloc(sizeof(char*)*handle->m);
        char *block = (char *)malloc(sizeof(char)*newsize);

        for (int i = 0; i < handle->m; i++) {
            coding[i] = (char *)malloc(sizeof(char)*blocksize);
            if (coding[i] == NULL) { perror("malloc"); exit(1); }
        }

        memset(block, '0', newsize);
        memcpy(block, item.data, item.size);

        for (int i=0; i < handle->k; i++)  
            data[i] = block+(i*blocksize);

        jerasure_schedule_encode(handle->k, handle->m, handle->w, handle->schedule, 
                                 data, coding, blocksize, packetsize);

        ERL_NIF_TERM coded[handle->m];

        for (int i=0; i < handle->m; i++)
        {
            ErlNifBinary b;
            enif_alloc_binary(blocksize, &b);
            memcpy(b.data, coding[i], blocksize);
            coded[i] = enif_make_binary(env, &b);
        }
        return enif_make_tuple2(env, 
                                enif_make_list6(env,
                                                enif_make_tuple2(env, ATOM_SIZE, enif_make_int(env, size)),
                                                enif_make_tuple2(env, ATOM_K, enif_make_int(env, handle->k)),
                                                enif_make_tuple2(env, ATOM_M, enif_make_int(env, handle->m)),
                                                enif_make_tuple2(env, ATOM_W, enif_make_int(env, handle->w)),
                                                enif_make_tuple2(env, ATOM_PACKETSIZE, enif_make_int(env, packetsize)),
                                                enif_make_tuple2(env, ATOM_BUFFERSIZE, enif_make_int(env, newsize))),
                                enif_make_list_from_array(env, coded, handle->m));
    }
    else
    {
        return enif_make_badarg(env);
            
    }
}


ERL_NIF_TERM erasuerl_decode(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    erasuerl_handle *h;
    ErlNifBinary item;
    
    if (enif_get_resource(env,argv[0],erasuerl_RESOURCE,(void**)&h) &&
        enif_is_list(env, argv[1]) &&
        enif_is_list(env, argv[2]))
    {
        h->matrix = cauchy_good_general_coding_matrix(h->k, h->m, h->w);
        h->bitmatrix = jerasure_matrix_to_bitmatrix(h->k, h->m, h->w, 
                                                         h->matrix);
        h->schedule = jerasure_smart_bitmatrix_to_schedule(h->k, h->m, 
                                                            h->w, h->bitmatrix);
        decode_options opts;
        fold(env, argv[1], parse_decode_option, opts);
        printf("%d %d\n", opts.size, opts.buffersize);
        int *erased = (int *)malloc(sizeof(int)*(h->k+h->m));
        for (int i=0; i< h->k+h->m; i++)
            erased[i] = 0;
        int *erasures = (int *)malloc(sizeof(int)*(h->k+h->m));
	char **data = (char **)malloc(sizeof(char *)*h->k);
	char **coding = (char **)malloc(sizeof(char *)*h->m);
        int blocksize;
        if (opts.buffersize != opts.size) 
        {
            for (int i=0; i < h->k; i++)
            {
                data[i] = (char *)malloc(opts.buffersize/h->k);
                bzero(data[i], opts.buffersize/h->k);
            }
            for (int i=0; i<h->m; i++) 
            {
                coding[i] = (char *)malloc(sizeof(char)*(opts.buffersize/h->k));
            }
            blocksize = opts.buffersize / h->k;
        }
        int numerased = 0;
        for (int i=1; i <= h->k; i++) 
        {
            erased[i-1] = 1;
            erasures[numerased] = i-1;
            numerased++;

        }
        ERL_NIF_TERM head, tail, list = argv[2];
        int nelems = 0;
        while(enif_get_list_cell(env, list, &head, &tail)) {
            ErlNifBinary bin;
            enif_inspect_binary(env, head, &bin);
            if (opts.buffersize == opts.size) 
            {
                blocksize = bin.size;
                printf("%d: %d\n", nelems, blocksize);
                coding[nelems] = (char *)malloc(sizeof(char)*blocksize);
                memcpy(coding[nelems], bin.data, blocksize);
            }
            else
            {
                printf("%d: %d\n", nelems, blocksize);
                memcpy(coding[nelems], bin.data, blocksize);
            }
            nelems++;
            list = tail;
        }
        for (int i=0; i < numerased; i++) 
        {
            if (erasures[i] < h->k) {
                data[erasures[i]] = (char *)malloc(blocksize);
                bzero(data[erasures[i]], blocksize);
            }
            else {
                coding[erasures[i]-h->k] = (char *)malloc(blocksize);
            }
        }
        erasures[numerased] = -1;
        int i = jerasure_schedule_decode_lazy(h->k, h->m, h->w, h->bitmatrix, erasures,
                                              data, coding, blocksize, opts.packetsize, 1);
        printf("%d\n", nelems);
        printf("%d\n", i);
        ERL_NIF_TERM decoded[h->k];
        int total = 0;
        int foo = 0;
        for (int i=0; i < h->k; i++)
        {
            ErlNifBinary b;
            if (total+blocksize <= opts.size) {
                enif_alloc_binary(blocksize, &b);
                printf("blocksize %d\n", blocksize);
                memcpy(b.data, data[i], blocksize);
                total += blocksize;
            }
            else 
            {
                enif_alloc_binary(opts.size - total, &b);
                for (int j=0; j < blocksize; j++)
                {
                    if (total < opts.size) 
                    { 
                        b.data[foo] = data[i][j];
                        total++;
                        foo++;
                    }
                    else {
                        break;
                    }

                }
            }
            decoded[i] = enif_make_binary(env, &b);
            foo = 0;
        }
        return enif_make_list_from_array(env, decoded, h->k);
    }
}
   
static void erasuerl_resource_cleanup(ErlNifEnv* env, void* arg)
{
    //erasuerl_h* h = (erasuerl_h*)arg;
    //delete h->q;
}

static int on_load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    ErlNifResourceFlags flags = (ErlNifResourceFlags)
        (ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER);
    erasuerl_RESOURCE = enif_open_resource_type_compat(env, 
                                                    "erasuerl",
                                                    &erasuerl_resource_cleanup,
                                                    flags, 
                                                    NULL);
    if (!erasuerl_RESOURCE)
        return 0;
    // Initialize common atoms
    ATOM(ATOM_OK, "ok");
    ATOM(ATOM_ERROR, "error");
    ATOM(ATOM_TRUE, "true");
    ATOM(ATOM_FALSE, "false");
    ATOM(ATOM_EMPTY, "empty");
    ATOM(ATOM_VALUE, "value");
    ATOM(ATOM_SIZE, "size");
    ATOM(ATOM_K, "k");
    ATOM(ATOM_M, "m");
    ATOM(ATOM_W, "w");
    ATOM(ATOM_PACKETSIZE, "packetsize");
    ATOM(ATOM_BUFFERSIZE, "buffersize");
    return 0;
}

extern "C" 
{
    ERL_NIF_INIT(erasuerl, nif_funcs, &on_load, NULL, NULL, NULL);
}