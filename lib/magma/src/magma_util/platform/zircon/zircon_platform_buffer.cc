// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "fdio/io.h"
#include "platform_buffer.h"
#include "platform_object.h"
#include "platform_trace.h"
#include <ddk/driver.h>
#include <limits.h> // PAGE_SIZE
#include <map>
#include <zx/vmar.h>
#include <zx/vmo.h>
#include <vector>

namespace magma {

class PinCountArray {
public:
    using pin_count_t = uint8_t;

    static constexpr uint32_t kPinCounts = PAGE_SIZE / sizeof(pin_count_t);

    PinCountArray() : count_(kPinCounts) {}

    uint32_t pin_count(uint32_t index)
    {
        DASSERT(index < count_.size());
        return count_[index];
    }

    void incr(uint32_t index)
    {
        DASSERT(index < count_.size());
        total_count_++;
        DASSERT(count_[index] < static_cast<pin_count_t>(~0));
        ++count_[index];
    }

    // If pin count is positive, decrements the pin count and returns the new pin count.
    // Otherwise returns -1.
    int32_t decr(uint32_t index)
    {
        DASSERT(index < count_.size());
        if (count_[index] == 0)
            return -1;
        DASSERT(total_count_ > 0);
        --total_count_;
        return --count_[index];
    }

    uint32_t total_count() { return total_count_; }

private:
    uint32_t total_count_{};
    std::vector<pin_count_t> count_;
};

class PinCountSparseArray {
public:
    static std::unique_ptr<PinCountSparseArray> Create(uint32_t page_count)
    {
        uint32_t array_size = page_count / PinCountArray::kPinCounts;
        if (page_count % PinCountArray::kPinCounts)
            array_size++;
        return std::unique_ptr<PinCountSparseArray>(new PinCountSparseArray(array_size));
    }

    uint32_t total_pin_count() { return total_pin_count_; }

    uint32_t pin_count(uint32_t page_index)
    {
        uint32_t array_index = page_index / PinCountArray::kPinCounts;
        uint32_t array_offset = page_index - PinCountArray::kPinCounts * array_index;

        auto& array = sparse_array_[array_index];
        if (!array)
            return 0;

        return array->pin_count(array_offset);
    }

    void incr(uint32_t page_index)
    {
        uint32_t array_index = page_index / PinCountArray::kPinCounts;
        uint32_t array_offset = page_index - PinCountArray::kPinCounts * array_index;

        if (!sparse_array_[array_index]) {
            sparse_array_[array_index] = std::unique_ptr<PinCountArray>(new PinCountArray());
        }

        sparse_array_[array_index]->incr(array_offset);

        ++total_pin_count_;
    }

    int32_t decr(uint32_t page_index)
    {
        uint32_t array_index = page_index / PinCountArray::kPinCounts;
        uint32_t array_offset = page_index - PinCountArray::kPinCounts * array_index;

        auto& array = sparse_array_[array_index];
        if (!array)
            return DRETF(false, "page not pinned");

        int32_t ret = array->decr(array_offset);
        if (ret < 0)
            return DRET_MSG(ret, "page not pinned");

        --total_pin_count_;

        if (array->total_count() == 0)
            array.reset();

        return ret;
    }

private:
    PinCountSparseArray(uint32_t array_size) : sparse_array_(array_size) {}

    std::vector<std::unique_ptr<PinCountArray>> sparse_array_;
    uint32_t total_pin_count_{};
};

class ZirconPlatformBuffer : public PlatformBuffer {
public:
    ZirconPlatformBuffer(zx::vmo vmo, uint64_t size) : vmo_(std::move(vmo)), size_(size)
    {
        DLOG("ZirconPlatformBuffer ctor size %ld vmo 0x%x", size, vmo_.get());

        DASSERT(magma::is_page_aligned(size));
        pin_count_array_ = PinCountSparseArray::Create(size / PAGE_SIZE);

        bool success = PlatformObject::IdFromHandle(vmo_.get(), &koid_);
        DASSERT(success);
    }

    ~ZirconPlatformBuffer() override
    {
        if (map_count_ > 0)
            vmar_unmap();
        ReleasePages();
    }

    // PlatformBuffer implementation
    uint64_t size() const override { return size_; }

    uint64_t id() const override { return koid_; }

    bool duplicate_handle(uint32_t* handle_out) const override
    {
        zx::vmo duplicate;
        zx_status_t status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate);
        if (status < 0)
            return DRETF(false, "zx_handle_duplicate failed");
        *handle_out = duplicate.release();
        return true;
    }

    bool GetFd(int* fd_out) const override;

    // PlatformBuffer implementation
    bool CommitPages(uint32_t start_page_index, uint32_t page_count) const override;
    bool MapCpu(void** addr_out, uintptr_t alignment) override;
    bool UnmapCpu() override;

    bool PinPages(uint32_t start_page_index, uint32_t page_count) override;
    bool UnpinPages(uint32_t start_page_index, uint32_t page_count) override;

    bool MapPageRangeBus(uint32_t start_page_index, uint32_t page_count,
                         uint64_t addr_out[]) override;
    bool UnmapPageRangeBus(uint32_t start_page_index, uint32_t page_count) override;
    bool CleanCache(uint64_t offset, uint64_t size, bool invalidate) override;

    uint32_t num_pages() { return size_ / PAGE_SIZE; }

private:
    void ReleasePages();

    zx_status_t vmar_unmap()
    {
        zx_status_t status = vmar_.destroy();
        vmar_.reset();

        if (status == ZX_OK)
            virt_addr_ = nullptr;
        return status;
    }

    zx::vmo vmo_;
    zx::vmar vmar_;
    uint64_t size_;
    uint64_t koid_;
    void* virt_addr_{};
    uint32_t map_count_ = 0;
    std::unique_ptr<PinCountSparseArray> pin_count_array_;
};

bool ZirconPlatformBuffer::GetFd(int* fd_out) const
{
    zx::vmo duplicate;
    zx_status_t status = vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate);
    if (status < 0)
        return DRETF(false, "zx_handle_duplicate failed");

    *fd_out = fdio_vmo_fd(duplicate.release(), 0, size());
    if (!*fd_out)
        return DRETF(false, "fdio_vmo_fd failed");
    return true;
}

bool ZirconPlatformBuffer::MapCpu(void** addr_out, uint64_t alignment)
{
    if (!magma::is_page_aligned(alignment))
        return DRETF(false, "alignment 0x%lx isn't page aligned", alignment);
    if (alignment && !magma::is_pow2(alignment))
        return DRETF(false, "alignment 0x%lx isn't power of 2", alignment);
    if (map_count_ == 0) {
        DASSERT(!virt_addr_);
        uintptr_t ptr;
        uintptr_t child_addr;
        // If alignment is needed, allocate a vmar that's large enough so that
        // the buffer will fit at an aligned address inside it.
        uintptr_t vmar_size = alignment ? size() + alignment : size();
        zx_status_t status = zx::vmar::root_self().allocate(
            0, vmar_size,
            ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_WRITE | ZX_VM_FLAG_CAN_MAP_SPECIFIC,
            &vmar_, &child_addr);
        if (status != ZX_OK)
            return DRETF(false, "failed to make vmar");
        uintptr_t offset = alignment ? magma::round_up(child_addr, alignment) - child_addr : 0;
        status =
            vmar_.map(offset, vmo_, 0, size(),
                      ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC, &ptr);
        if (status != ZX_OK)
            return DRETF(false, "failed to map vmo");

        virt_addr_ = reinterpret_cast<void*>(ptr);
    }

    DASSERT(!alignment || (reinterpret_cast<uintptr_t>(virt_addr_) & (alignment - 1)) == 0);

    *addr_out = virt_addr_;
    map_count_++;

    DLOG("mapped vmo %p got %p, map_count_ = %u", this, virt_addr_, map_count_);

    return true;
}

bool ZirconPlatformBuffer::UnmapCpu()
{
    DLOG("UnmapCpu vmo %p, map_count_ %u", this, map_count_);
    if (map_count_) {
        map_count_--;
        if (map_count_ == 0) {
            DLOG("map_count 0 unmapping vmo %p", this);
            zx_status_t status = vmar_unmap();
            if (status != ZX_OK)
                DRETF(false, "failed to unmap vmo: %d", status);
        }
        return true;
    }
    return DRETF(false, "attempting to unmap buffer that isnt mapped");
}

bool ZirconPlatformBuffer::CommitPages(uint32_t start_page_index, uint32_t page_count) const
{
    TRACE_DURATION("magma", "CommitPages");
    if (!page_count)
        return true;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRETF(false, "offset + length greater than buffer size");

    zx_status_t status = vmo_.op_range(ZX_VMO_OP_COMMIT, start_page_index * PAGE_SIZE,
                                       page_count * PAGE_SIZE, nullptr, 0);

    if (status == ZX_ERR_NO_MEMORY)
        return DRETF(false,
                     "Kernel returned ZX_ERR_NO_MEMORY when attempting to commit %u vmo "
                     "pages (%u bytes).\nThis means the system has run out of physical memory and "
                     "things will now start going very badly.\nPlease stop using so much "
                     "physical memory or download more RAM at www.downloadmoreram.com :)",
                     page_count, PAGE_SIZE * page_count);
    else if (status != ZX_OK)
        return DRETF(false, "failed to commit vmo pages: %d", status);

    return true;
}

bool ZirconPlatformBuffer::PinPages(uint32_t start_page_index, uint32_t page_count)
{
    zx_status_t status;
    if (!page_count)
        return true;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRETF(false, "offset + length greater than buffer size");

    if (!CommitPages(start_page_index, page_count))
        return DRETF(false, "failed to commit pages");

    status = vmo_.op_range(ZX_VMO_OP_LOCK, start_page_index * PAGE_SIZE, page_count * PAGE_SIZE,
                           nullptr, 0);
    if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED)
        return DRETF(false, "failed to lock vmo pages: %d", status);

    for (uint32_t i = 0; i < page_count; i++) {
        pin_count_array_->incr(start_page_index + i);
    }

    return true;
}

bool ZirconPlatformBuffer::UnpinPages(uint32_t start_page_index, uint32_t page_count)
{
    TRACE_DURATION("magma", "UnPinPages");
    if (!page_count)
        return true;

    if ((start_page_index + page_count) * PAGE_SIZE > size())
        return DRETF(false, "offset + length greater than buffer size");

    uint32_t pages_to_unpin = 0;

    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t pin_count = pin_count_array_->pin_count(start_page_index + i);

        if (pin_count == 0)
            return DRETF(false, "page not pinned");

        if (pin_count == 1)
            pages_to_unpin++;
    }

    DLOG("pages_to_unpin %u page_count %u", pages_to_unpin, page_count);

    if (pages_to_unpin == page_count) {
        for (uint32_t i = 0; i < page_count; i++) {
            pin_count_array_->decr(start_page_index + i);
        }

        // Unlock the entire range.
        zx_status_t status = vmo_.op_range(ZX_VMO_OP_UNLOCK, start_page_index * PAGE_SIZE,
                                           page_count * PAGE_SIZE, nullptr, 0);
        if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED) {
            return DRETF(false, "failed to unlock full range: %d", status);
        }

    } else {
        // Unlock page by page
        for (uint32_t page_index = start_page_index; page_index < start_page_index + page_count;
             page_index++) {
            if (pin_count_array_->decr(page_index) == 0) {
                zx_status_t status =
                    vmo_.op_range(ZX_VMO_OP_UNLOCK, page_index * PAGE_SIZE, PAGE_SIZE, nullptr, 0);
                if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED) {
                    return DRETF(false, "failed to unlock page_index %u: %u", page_index, status);
                }
            }
        }
    }

    return true;
}

void ZirconPlatformBuffer::ReleasePages()
{
    TRACE_DURATION("magma", "ReleasePages");
    if (pin_count_array_->total_pin_count()) {
        // Still have some pinned pages, unlock.
        zx_status_t status = vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, size(), nullptr, 0);
        if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED)
            DLOG("failed to unlock pages: %d", status);
    }
}

bool ZirconPlatformBuffer::MapPageRangeBus(uint32_t start_page_index, uint32_t page_count,
                                            uint64_t addr_out[])
{
    TRACE_DURATION("magma", "MapPageRangeBus");
    static_assert(sizeof(zx_paddr_t) == sizeof(uint64_t), "unexpected sizeof(zx_paddr_t)");

    for (uint32_t i = start_page_index; i < start_page_index + page_count; i++) {
        if (pin_count_array_->pin_count(i) == 0)
            return DRETF(false, "zero pin_count for page %u", i);
    }

    zx_status_t status;
    {
        TRACE_DURATION("magma", "vmo lookup");
        status = vmo_.op_range(ZX_VMO_OP_LOOKUP, start_page_index * PAGE_SIZE,
                               page_count * PAGE_SIZE, addr_out, page_count * sizeof(addr_out[0]));
    }
    if (status != ZX_OK)
        return DRETF(false, "failed to lookup vmo");

    return true;
}

bool ZirconPlatformBuffer::UnmapPageRangeBus(uint32_t start_page_index, uint32_t page_count)
{
    return true;
}

bool ZirconPlatformBuffer::CleanCache(uint64_t offset, uint64_t length, bool invalidate)
{
#if defined(__aarch64__)
    if (map_count_) {
        uint32_t op = ZX_CACHE_FLUSH_DATA;
        if (invalidate)
            op |= ZX_CACHE_FLUSH_INVALIDATE;
        if (offset + length > size())
            return DRETF(false, "size too large for buffer");
        zx_status_t status = zx_cache_flush(static_cast<uint8_t*>(virt_addr_) + offset, length, op);
        if (status != ZX_OK)
            return DRETF(false, "failed to clean cache: %d", status);
        return true;
    }
#endif

    uint32_t op = invalidate ? ZX_VMO_OP_CACHE_CLEAN_INVALIDATE : ZX_VMO_OP_CACHE_CLEAN;
    zx_status_t status = vmo_.op_range(op, offset, length, nullptr, 0);
    if (status != ZX_OK)
        return DRETF(false, "failed to clean cache: %d", status);
    return true;
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Create(uint64_t size, const char* name)
{
    size = magma::round_up(size, PAGE_SIZE);
    if (size == 0)
        return DRETP(nullptr, "attempting to allocate 0 sized buffer");

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    if (status != ZX_OK)
        return DRETP(nullptr, "failed to allocate vmo size %" PRId64 ": %d", size, status);
    vmo.set_property(ZX_PROP_NAME, name, strlen(name));

    DLOG("allocated vmo size %ld handle 0x%x", size, vmo.get());
    return std::unique_ptr<PlatformBuffer>(new ZirconPlatformBuffer(std::move(vmo), size));
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::Import(uint32_t handle)
{
    uint64_t size;
    // presumably this will fail if handle is invalid or not a vmo handle, so we perform no
    // additional error checking
    zx::vmo vmo(handle);
    auto status = vmo.get_size(&size);

    if (status != ZX_OK)
        return DRETP(nullptr, "zx_vmo_get_size failed");

    if (!magma::is_page_aligned(size))
        return DRETP(nullptr, "attempting to import vmo with invalid size");

    return std::unique_ptr<PlatformBuffer>(new ZirconPlatformBuffer(std::move(vmo), size));
}

std::unique_ptr<PlatformBuffer> PlatformBuffer::ImportFromFd(int fd)
{
    zx_handle_t handle;
    zx_status_t status = fdio_get_exact_vmo(fd, &handle);
    if (status != ZX_OK)
        return DRETP(nullptr, "fdio_get_exact_vmo failed");
    return Import(handle);
}

} // namespace magma
