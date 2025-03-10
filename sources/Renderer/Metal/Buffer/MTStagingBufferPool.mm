/*
 * MTStagingBufferPool.mm
 * 
 * Copyright (c) 2015 Lukas Hermanns. All rights reserved.
 * Licensed under the terms of the BSD 3-Clause license (see LICENSE.txt).
 */

#include "MTStagingBufferPool.h"
#include <algorithm>


namespace LLGL
{


MTStagingBufferPool::MTStagingBufferPool(id<MTLDevice> device, NSUInteger chunkSize) :
    device_    { device    },
    chunkSize_ { chunkSize }
{
}

void MTStagingBufferPool::Reset()
{
    if (chunkIdx_ < chunks_.size())
        chunks_[chunkIdx_].Reset();
    chunkIdx_ = 0;
}

void MTStagingBufferPool::Write(
    const void*     data,
    NSUInteger      dataSize,
    id<MTLBuffer>&  srcBuffer,
    NSUInteger&     srcOffset)
{
    /* Find a chunk that fits the requested data size or allocate a new chunk */
    while (chunkIdx_ < chunks_.size() && !chunks_[chunkIdx_].Capacity(dataSize))
    {
        chunks_[chunkIdx_].Reset();
        ++chunkIdx_;
    }

    if (chunkIdx_ == chunks_.size())
        AllocChunk(dataSize);

    /* Write data to current chunk */
    auto& chunk = chunks_[chunkIdx_];
    srcOffset = chunk.GetOffset();
    srcBuffer = chunk.GetNative();
    chunk.Write(data, dataSize);
}


/*
 * ======= Private: =======
 */

void MTStagingBufferPool::AllocChunk(NSUInteger minChunkSize)
{
    chunks_.emplace_back(device_, std::max(chunkSize_, minChunkSize));
    chunkIdx_ = chunks_.size() - 1;
}


} // /namespace LLGL



// ================================================================================
