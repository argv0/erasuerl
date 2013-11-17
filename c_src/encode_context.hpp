#ifndef __ENCODE_CONTEXT_HPP_
#define __ENCODE_CONTEXT_HPP_

#include <cstdint>
#include <cstring>
#include "erasuerl_handle.hpp"
#include "erasuerl_blocks.hpp"
#include "erasuerl.h"

size_t round_up_size(size_t origsize, erasuerl_handle *handle) 
{
    size_t newsize = origsize;
    if (origsize % (handle->k * handle->w * handle->packetsize * sizeof(size_t)) != 0)
        while (newsize%((handle->k)*(handle->w)*handle->packetsize*sizeof(size_t)) != 0) 
            newsize++;
    return newsize;
}

class encode_context {
public:
    encode_context(ErlNifEnv *env, ErlNifBinary* bin, erasuerl_handle *handle) :
        env(env),
        item(bin), 
        h(handle), 
        size(item->size), 
        newsize(round_up_size(item->size, handle)),
        blocksize(newsize/handle->k), 
        coding(h->m, blocksize),
        data(h->k)
    {
        enif_realloc_binary(item, newsize);
        data.set_data(reinterpret_cast<char *>(item->data), blocksize);
    }

public:
    void encode() 
    {
        h->encode(blocksize, data, coding);
    }
    
    ERL_NIF_TERM 
    metadata() const 
    { 
        return enif_make_list5(env,
                   enif_make_tuple2(env, ATOM_SIZE, enif_make_int(env, size)),
                   enif_make_tuple2(env, ATOM_K, enif_make_int(env, h->k)),
                   enif_make_tuple2(env, ATOM_M, enif_make_int(env, h->m)),
                   enif_make_tuple2(env, ATOM_W, enif_make_int(env, h->w)),
                   enif_make_tuple2(env, ATOM_PACKETSIZE, enif_make_int(env, 
                                                                        h->packetsize)));
    }

    ERL_NIF_TERM 
    get_data_blocks() const 
    {
        ERL_NIF_TERM ret[h->k];
        for (int i=0; i < h->k; i++)
        {
            ErlNifBinary b;
            enif_alloc_binary(blocksize, &b);
            memcpy(b.data, data[i], blocksize);
            ret[i] = enif_make_binary(env, &b);
        }
        return enif_make_list_from_array(env, ret, h->k);
    }

    ERL_NIF_TERM 
    code_blocks() const 
    { 
        ERL_NIF_TERM ret[h->m];
        for (int i=0; i < h->m; i++)
        {
            ErlNifBinary b;
            enif_alloc_binary(blocksize, &b);
            memcpy(b.data, coding[i], blocksize);
            ret[i] = enif_make_binary(env, &b);
        }
        return enif_make_list_from_array(env, ret, h->m);
    }
private:
    ErlNifEnv *env = nullptr;
    ErlNifBinary *item = nullptr;
    erasuerl_handle* h = nullptr;
    std::size_t size;
    std::size_t newsize;
    std::size_t blocksize;
    coding_block coding;
    data_blocks data;
};

#endif // include guard
