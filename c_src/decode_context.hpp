#ifndef __DECODE_CONTEXT_HPP_
#define __DECODE_CONTEXT_HPP_

#include <cstdint>
#include "erasuerl.h"
#include "erasuerl_handle.hpp"

struct decode_options 
{
    int size;
    int packetsize;
};

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
    }
    return ATOM_OK;
}

struct decode_context
{
public:    
    decode_context(ErlNifEnv *env, erasuerl_handle* handle, ERL_NIF_TERM metadata)
        : env_(env),
          handle_(handle),
          erasures_(handle->k+handle->m),
          data_(handle->k+handle->m)
    {
        fold(env, metadata, parse_decode_option, opts_);
    }
    
    decode_options options() const { return opts_; }

    std::size_t    blocksize() const { return blocksize_; }
    
    void find_erasures(ERL_NIF_TERM data_blocks, ERL_NIF_TERM coding_blocks)
    {
        ERL_NIF_TERM head, tail = data_blocks;        
        while (enif_get_list_cell(env_, tail, &head, &tail)) {
            if (is_data_erasure(head))
                erasures_[num_erased_++] = idx_;
            idx_++;
       }
        tail = coding_blocks;
        while (enif_get_list_cell(env_, tail, &head, &tail)) { 
            if (is_data_erasure(head))
                erasures_[num_erased_++] = idx_;
            idx_++;
        }
        for (int i=0; i < num_erased_; i++) {
            data_[erasures_[i]] = new char[blocksize_];
        }
        erasures_[num_erased_] = -1;
    }

    bool is_data_erasure(ERL_NIF_TERM item)
    {
        ErlNifBinary bin;
        if (!enif_inspect_binary(env_, item, &bin)) 
            return true;
        blocksize_ = bin.size;
        data_[idx_] = reinterpret_cast<char *>(bin.data);
        return false;
    }

    int decode() 
    {
        return handle_->decode(blocksize_, data_.data(), &(data_[(handle_->k)]), erasures_.data());
    }

    ERL_NIF_TERM get_blocks() 
    {
        unique_array<ERL_NIF_TERM> result(handle_->k);
        std::size_t total = 0;

        for (int i=0; i < handle_->k; i++) 
        {
            ErlNifBinary b;
            if (total + blocksize_ <= opts_.size)
            {
                enif_alloc_binary(blocksize_, &b);
                memcpy(b.data, data_[i], blocksize_);
                total += blocksize_;
            }
            else 
            {
                enif_alloc_binary(opts_.size - total, &b);
                for (int j=0; j < blocksize_; j++)
                {
                    if (total++ < opts_.size) 
                    {
                        b.data[j] = data_[i][j];
                    }
                    else 
                    {
                        break;
                    }
                }
            }
            result[i] = enif_make_binary(env_, &b);
        }
        return enif_make_list_from_array(env_, result.data(), handle_->k);
    }
private:
    ErlNifEnv *env_ = nullptr;
    erasuerl_handle* handle_ = nullptr;
    unique_array<int> erasures_;
    unique_array<char *> data_;
    std::size_t num_erased_ = 0;
    int idx_ = 0;
    std::size_t blocksize_ = 0;
    decode_options opts_;
};

#endif // include guard
