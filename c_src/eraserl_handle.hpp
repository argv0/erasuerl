// -------------------------------------------------------------------
//
// Copyright (c) 2007-2013 Basho Technologies, Inc. All Rights Reserved.
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
#ifndef __ERASERL_HANDLE_HPP
#define __ERASERL_HANDLE_HPP

#include <cstring>

extern "C" {
#include "Jerasure/include/cauchy.h"
#include "Jerasure/include/jerasure.h"
}

struct decode_options {
    int orig_size = 0;
    int blocksize = 0;
    int packetsize = 0;
    int k = 0;
    int m = 0;
    int w = 0;
};

struct size_info {
    std::size_t original_size = 0;
    std::size_t coded_size = 0;
    std::size_t block_size = 0;
};

class eraserl_handle {
  public:
    /* ec params */
    int k; // = 9;
    int m; // = 4;
    int w; // = 4;
    int packetsize;
    size_t num_blocks;

    eraserl_handle(int k, int m, int w, int packetsize)
        : k(k), m(m), w(w), packetsize(packetsize), num_blocks(k + m),
          matrix(cauchy_good_general_coding_matrix(k, m, w)),
          bitmatrix(jerasure_matrix_to_bitmatrix(k, m, w, matrix)),
          schedule(jerasure_smart_bitmatrix_to_schedule(k, m, w, bitmatrix)) {}

    eraserl_handle(const decode_options &opts)
        : eraserl_handle(opts.k, opts.m, opts.w, opts.packetsize) {}

    ~eraserl_handle() {
        if (matrix)
            free(matrix);
        if (bitmatrix)
            free(bitmatrix);
        if (schedule)
            free(schedule);
    }

  public:
    void encode(size_t blocksize, char **data, char **coding) {
        jerasure_schedule_encode(k, m, w, schedule, data, coding, blocksize,
                                 packetsize);
    }

    int decode(size_t blocksize, char **data, char **coding, int *erasures) {
        return jerasure_schedule_decode_lazy(k, m, w, bitmatrix, erasures, data,
                                             coding, blocksize, packetsize, 1);
    }

  private:
    int *matrix = nullptr;
    int *bitmatrix = nullptr;
    int **schedule = nullptr;
};

#endif // include guard
